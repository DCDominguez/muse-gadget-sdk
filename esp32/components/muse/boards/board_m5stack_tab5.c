/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * M5Stack Tab5 (ESP32-P4). Pin map, IO expanders, LCD, touch and codecs come
 * from Espressif's BSP: https://github.com/espressif/esp-bsp/tree/master/bsp/m5stack_tab5
 * 5" 720x1280 MIPI-DSI LCD, run landscape (1280x720), in three revisions
 * (ILI9881C + GT911, ST7123 or ST7121 with built-in touch), which the BSP
 * tells apart at start by probing the touch controller. ES8388 speaker codec
 * and ES7210 microphones. Two PI4IOE5V6408 expanders (0x43, 0x44) switch the
 * LCD, touch, speaker, USB and the ESP32-C6 that carries Wi-Fi and BLE over
 * SDIO (esp_hosted).
 *
 * Muse takes an 800x480 box in the top left and muse_dock fills the rest:
 * status and a typed chat to the right, hold-to-talk and quick controls below.
 * There is no user button: the touchscreen talks and runs the menus, and the
 * Tab5 Keyboard (M5Stack A164, on Ext.Port1) types into the chat, with Tab
 * held to talk.
 *
 * Battery: an INA226 at 0x41 on the internal bus across a 5 mOhm shunt, and
 * the charger's CHG_STAT on the second expander's pin 6 (M5Unified
 * Power_Class.inl and Power_Class.hpp, board_M5Tab5).
 */
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "muse_board.h"
#include "muse_audio.h"
#include "muse_dock.h"
#include "muse_mem.h"
#include "muse_settings.h"

/* M5Unified Power_Class.inl (board_M5Tab5): pulsing the second expander's
 * (0x44) pin 4, PWROFF_PULSE, ten times 50 ms apart turns the Tab5 off. */
#define TAB5_PWROFF_PIN    IO_EXPANDER_PIN_NUM_4
#define TAB5_PWROFF_PULSES 10
/* Same source: io1.gpio6 == CHG_STAT, high while charging. */
#define TAB5_CHG_STAT_PIN  IO_EXPANDER_PIN_NUM_6

#define UI_W 800
#define UI_H 480

/* INA226 (TI datasheet): config 0x00, shunt voltage 0x01 (2.5 uV/bit,
 * signed), bus voltage 0x02 (1.25 mV/bit). Config: 16-sample averaging,
 * 1.1 ms conversions, shunt and bus continuous, as M5Unified sets it. */
#define INA_ADDR      0x41
#define INA_REG_CFG   0x00
#define INA_REG_SHUNT 0x01
#define INA_REG_BUS   0x02
#define INA_CFG       0x4527
#define SHUNT_MOHM    5

/* Tab5 Keyboard (docs.m5stack.com/en/tab5/Tab5_Keyboard, M5Unit-KEYBOARD
 * unit_Tab5Keyboard.hpp): I2C 0x6D on Ext.Port1, SDA G0, SCL G1 (M5's own
 * Tab5 pin map, M5Tab5-Keyboard-UserDemo m5tab5_pinmap.h). The BSP has I2C
 * port 1 for the internal bus, so the keyboard takes port 0. In HID mode
 * each event is a modifier byte and a USB HID usage; a release reads as
 * usage 0 (M5Tab5-Keyboard-Internal-FW user_keyboard_handle.c). Ctrl, Alt,
 * Sym and Aa never arrive alone: the keyboard applies them itself. */
#define KB_PORT        I2C_NUM_0
#define KB_SDA         GPIO_NUM_0
#define KB_SCL         GPIO_NUM_1
#define KB_ADDR        0x6D
#define KB_HZ          100000
#define KB_REG_EVENTS  0x02
#define KB_REG_MODE    0x10
#define KB_REG_HID     0x30
#define KB_MODE_HID    1
#define KB_EMPTY       0xFF
#define KB_POLL_EVERY  2            /* poll_buttons runs every 10 ms: 20 ms */
#define KB_PROBE_EVERY 200          /* look for a keyboard every 2 s */
#define KB_PROBE_STUCK 3000         /* every 30 s while the bus is held low */
#define KB_MAX_EVENTS  8            /* per poll */

/* USB HID keyboard usages (HID Usage Tables, page 0x07). */
#define HID_A          0x04
#define HID_Z          0x1D
#define HID_1          0x1E
#define HID_0          0x27
#define HID_ENTER      0x28
#define HID_ESC        0x29
#define HID_BACKSPACE  0x2A
#define HID_TAB        0x2B
#define HID_SPACE      0x2C
#define HID_MINUS      0x2D
#define HID_SLASH      0x38
#define HID_RIGHT      0x4F
#define HID_LEFT       0x50
#define HID_DOWN       0x51
#define HID_UP         0x52
#define HID_SHIFT_BITS 0x22         /* left and right shift */

static const char *TAG = "board";
static lv_display_t *s_disp;
static bool s_flipped;              /* the other way up from the Kconfig default */

static i2c_master_dev_handle_t s_ina;
static esp_io_expander_handle_t s_exp1;

static i2c_master_bus_handle_t s_kb_bus;
static i2c_master_dev_handle_t s_kb;
static bool s_kb_present;
static bool s_tab_down;

/* ---- Radio and buses ------------------------------------------------------ */

static esp_err_t radio_init(void)
{
    /* The C6 is off until the second expander powers it (BSP_WIFI_EN), and
     * esp_hosted reaches it as soon as Home Link starts Wi-Fi. */
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_WIFI, true), TAG, "C6 power");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t ina_write(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, v >> 8, v & 0xff };
    return i2c_master_transmit(s_ina, b, sizeof(b), 50);
}

static esp_err_t ina_read(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    esp_err_t err = i2c_master_transmit_receive(s_ina, &reg, 1, b, sizeof(b), 50);
    *v = (uint16_t)(b[0] << 8 | b[1]);
    return err;
}

static void power_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA_ADDR,
        .scl_speed_hz = 400000,
    };
    if (!bus || i2c_master_probe(bus, INA_ADDR, 50) != ESP_OK
        || i2c_master_bus_add_device(bus, &cfg, &s_ina) != ESP_OK || ina_write(INA_REG_CFG, INA_CFG) != ESP_OK) {
        ESP_LOGW(TAG, "INA226 not answering at 0x%02x: no battery reading", INA_ADDR);
        s_ina = NULL;
    }
    s_exp1 = bsp_io_expander1_init();
    if (s_exp1) {
        /* An input; nothing else on the board drives it. */
        esp_io_expander_set_dir(s_exp1, TAB5_CHG_STAT_PIN, IO_EXPANDER_INPUT);
    }
}

static void keyboard_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = KB_PORT,
        .sda_io_num = KB_SDA,
        .scl_io_num = KB_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&cfg, &s_kb_bus) != ESP_OK) {
        ESP_LOGW(TAG, "Ext.Port1 I2C bus failed: no keyboard");
        s_kb_bus = NULL;
    }
}

static esp_err_t init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");
    power_init();
    keyboard_bus_init();
    return ESP_OK;
}

/* ---- Display -------------------------------------------------------------- */

static lv_display_rotation_t rotation(void)
{
#if CONFIG_MUSE_TAB5_ROTATE_270
    bool r270 = !s_flipped;
#else
    bool r270 = s_flipped;
#endif
    return r270 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90;
}

static void load_flip(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("muse_tab5", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "flip", &v);
        nvs_close(h);
    }
    s_flipped = v != 0;
}

static lv_display_t *display_start(lv_indev_t **touch)
{
    /* The BSP picks the panel by probing the touch controller, which a cold
     * panel may not answer until LCD_EN is released; on a rev v1.3 P4 the
     * probe then asserts. Release it first and let the panel come up.
     * (Seen on this Tab5 with BSP 1.3.0: DCDominguez/ESP32-Projects,
     * projects/cartographer/firmware/main/main.c.) */
    if (bsp_feature_enable(BSP_FEATURE_LCD, true) != ESP_OK) {
        ESP_LOGE(TAG, "LCD enable failed");
        return NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* The panel is 720x1280 portrait; Muse runs it landscape for a keyboard.
     * esp_lvgl_port rotates each flush with the P4's PPA when sw_rotate is
     * set and CONFIG_LVGL_PORT_ENABLE_PPA is on, so it costs no CPU. Buffers
     * as the BSP's own bsp_display_start(): internal DMA RAM. */
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .flags = { .buff_dma = true, .buff_spiram = false, .sw_rotate = true },
    };
    cfg.lvgl_port_cfg.task_affinity = MUSE_UI_CORE;
    cfg.lvgl_port_cfg.task_priority = MUSE_UI_PRIORITY;
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        return NULL;
    }
    load_flip();
    /* LVGL turns touch points with the display, so taps follow the picture. */
    bsp_display_lock(0);
    bsp_display_rotate(disp, rotation());
    bsp_display_unlock();
    int32_t w = lv_display_get_horizontal_resolution(disp), h = lv_display_get_vertical_resolution(disp);
    if (w != BSP_LCD_V_RES || h != BSP_LCD_H_RES) {
        ESP_LOGE(TAG, "rotation left the display %ldx%ld, not landscape", (long)w, (long)h);
    }
    ESP_LOGI(TAG, "display %ldx%ld, rotated %d degrees%s", (long)w, (long)h,
             rotation() == LV_DISPLAY_ROTATION_270 ? 270 : 90, s_flipped ? " (flipped)" : "");
    s_disp = disp;
    *touch = bsp_display_get_input_dev();
    return *touch ? disp : NULL;
}

static void flip_display(void)
{
    /* From muse_dock's button, in the LVGL task. */
    s_flipped = !s_flipped;
    lv_display_set_rotation(s_disp, rotation());
    lv_obj_invalidate(lv_screen_active());
    nvs_handle_t h;
    if (nvs_open("muse_tab5", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "flip", s_flipped);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "display flipped: %d degrees", rotation() == LV_DISPLAY_ROTATION_270 ? 270 : 90);
}

static bool display_lock(int timeout_ms)
{
    /* Muse uses -1 for forever; esp_lvgl_port uses 0. */
    return bsp_display_lock(timeout_ms < 0 ? 0 : (uint32_t)timeout_ms);
}

static void set_brightness(int pct)
{
    bsp_display_brightness_set(pct);
}

/* ---- Audio ---------------------------------------------------------------- */

static esp_err_t audio_init(esp_codec_dev_handle_t *spk, esp_codec_dev_handle_t *mic)
{
    /* Muse records two slots; the BSP's default is 48 kHz. The speaker codec
     * init also switches on the amplifier (BSP_SPEAKER_EN). */
    const i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSE_AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    ESP_RETURN_ON_ERROR(bsp_audio_init(&cfg), TAG, "duplex audio init");
    *spk = bsp_audio_codec_speaker_init();
    *mic = bsp_audio_codec_microphone_init();
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    /* ES7210 PGA steps are 3 dB; snap so the UI shows what's applied. */
    db = (db / 3) * 3;
    /* esp_codec_dev rounds 33 dB down to 30; the next real step up is 34.5. */
    esp_codec_dev_set_in_gain(mic, db == 33 ? 34.5f : (float)db);
}

/* ---- Keyboard ------------------------------------------------------------- */

static esp_err_t kb_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_master_transmit(s_kb, b, sizeof(b), 20);
}

/* Returns how many polls until the next look: a bus held low (a bad cable,
 * something else on the port) times out and logs, so look less often. */
static unsigned kb_probe(void)
{
    esp_err_t err = s_kb_bus ? i2c_master_probe(s_kb_bus, KB_ADDR, 20) : ESP_FAIL;
    if (err == ESP_ERR_TIMEOUT) {
        return KB_PROBE_STUCK;
    }
    if (err != ESP_OK) {
        return KB_PROBE_EVERY;
    }
    if (!s_kb) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = KB_ADDR,
            .scl_speed_hz = KB_HZ,
        };
        if (i2c_master_bus_add_device(s_kb_bus, &cfg, &s_kb) != ESP_OK) {
            s_kb = NULL;
            return KB_PROBE_EVERY;
        }
    }
    /* HID mode, and drop anything queued from before. */
    if (kb_write(KB_REG_MODE, KB_MODE_HID) == ESP_OK && kb_write(KB_REG_EVENTS, 0) == ESP_OK) {
        s_kb_present = true;
        s_tab_down = false;
        ESP_LOGI(TAG, "Tab5 Keyboard connected");
    }
    return KB_PROBE_EVERY;
}

/* A printable HID usage as ASCII (US layout, as the keyboard maps it), or 0. */
static uint32_t hid_char(uint8_t usage, bool shift)
{
    static const char digits[] = "1234567890", shifted[] = "!@#$%^&*()";
    /* Usages 0x2D..0x38: - = [ ] \ # ; ' ` , . /  */
    static const char punct[] = "-=[]\\#;'`,./", punct_shift[] = "_+{}|~:\"~<>?";
    if (usage >= HID_A && usage <= HID_Z) {
        return (shift ? 'A' : 'a') + (usage - HID_A);
    }
    if (usage >= HID_1 && usage <= HID_0) {
        return (shift ? shifted : digits)[usage - HID_1];
    }
    if (usage == HID_SPACE) {
        return ' ';
    }
    if (usage >= HID_MINUS && usage <= HID_SLASH) {
        return (shift ? punct_shift : punct)[usage - HID_MINUS];
    }
    return 0;
}

static unsigned kb_event(uint8_t mod, uint8_t usage)
{
    if (usage == 0) {
        /* A release; it doesn't say which key, so Tab is held until any key lets go. */
        if (s_tab_down) {
            s_tab_down = false;
            return MUSE_BTN_TALK_RELEASE;
        }
        return 0;
    }
    switch (usage) {
    case HID_TAB:
        if (!s_tab_down) {
            s_tab_down = true;
            return MUSE_BTN_TALK_PRESS;
        }
        return 0;
    case HID_ENTER:     muse_dock_key(LV_KEY_ENTER); return 0;
    case HID_BACKSPACE: muse_dock_key(LV_KEY_BACKSPACE); return 0;
    case HID_ESC:       muse_dock_key(LV_KEY_ESC); return 0;
    case HID_LEFT:      muse_dock_key(LV_KEY_LEFT); return 0;
    case HID_RIGHT:     muse_dock_key(LV_KEY_RIGHT); return 0;
    case HID_UP:        muse_dock_key(LV_KEY_UP); return 0;
    case HID_DOWN:      muse_dock_key(LV_KEY_DOWN); return 0;
    default: {
        uint32_t c = hid_char(usage, mod & HID_SHIFT_BITS);
        if (c) {
            muse_dock_key(c);
        }
        return 0;
    }
    }
}

static unsigned kb_poll(void)
{
    static unsigned tick, probe_in = 1;
    tick++;
    if (!s_kb_present) {
        if (--probe_in == 0) {
            probe_in = kb_probe();
        }
        return 0;
    }
    if (tick % KB_POLL_EVERY) {
        return 0;
    }
    unsigned out = 0;
    for (int i = 0; i < KB_MAX_EVENTS; i++) {
        uint8_t reg = KB_REG_HID, b[2];
        if (i2c_master_transmit_receive(s_kb, &reg, 1, b, sizeof(b), 20) != ESP_OK) {
            ESP_LOGW(TAG, "Tab5 Keyboard disconnected");
            s_kb_present = false;
            if (s_tab_down) {
                s_tab_down = false;
                out |= MUSE_BTN_TALK_RELEASE;
            }
            break;
        }
        if (b[0] == KB_EMPTY && b[1] == KB_EMPTY) {
            break;
        }
        out |= kb_event(b[0], b[1]);
    }
    return out;
}

static bool keyboard_present(void)
{
    return s_kb_present;
}

static unsigned poll_buttons(void)
{
    return kb_poll();
}

/* ---- Power ---------------------------------------------------------------- */

static esp_err_t read_power(muse_power_t *out)
{
    if (!s_ina) {
        return ESP_ERR_INVALID_STATE;   /* said once, at init */
    }
    uint16_t bus, shunt;
    ESP_RETURN_ON_ERROR(ina_read(INA_REG_BUS, &bus), TAG, "bus voltage");
    ESP_RETURN_ON_ERROR(ina_read(INA_REG_SHUNT, &shunt), TAG, "shunt voltage");
    int pack_mv = bus * 5 / 4;                              /* 1.25 mV per bit */
    /* 2.5 uV per bit across the shunt; M5's wiring reads charging negative. */
    int charge_ma = -(int)(int16_t)shunt * 5 / 2 / SHUNT_MOHM;
    uint32_t chg = 0;
    bool charging = s_exp1 && esp_io_expander_get_level(s_exp1, TAB5_CHG_STAT_PIN, &chg) == ESP_OK
                    && (chg & TAB5_CHG_STAT_PIN);
    /* Two Li-ion cells in series (NP-F550); M5Unified's percentage, per cell. */
    int cell_mv = pack_mv / 2;
    int pct = (cell_mv - 3300) * 100 / (4150 - 3350);
    *out = (muse_power_t){
        .battery_pct = pack_mv < 3000 ? -1 : pct < 0 ? 0 : pct > 100 ? 100 : pct,
        .battery_mv = pack_mv,
        .charging = charging,
        /* No VBUS sense: USB is inferred from charge flowing in. */
        .usb = charging || charge_ma > 20,
    };
    return ESP_OK;
}

static esp_err_t power_off(void)
{
    esp_io_expander_handle_t exp = bsp_io_expander1_init();
    ESP_RETURN_ON_FALSE(exp, ESP_ERR_INVALID_STATE, TAG, "no IO expander");
    /* Dark and quiet first, so the cut doesn't pop. The codecs stay open:
     * on USB power the Tab5 carries on, and Muse with it. */
    bsp_display_backlight_off();
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);
    ESP_LOGI(TAG, "power off: pulsing PWROFF");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(exp, TAB5_PWROFF_PIN, IO_EXPANDER_OUTPUT), TAG, "pwroff pin");
    for (int i = 0; i < TAB5_PWROFF_PULSES; i++) {
        esp_io_expander_set_level(exp, TAB5_PWROFF_PIN, i & 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Still running: on USB power the Tab5 stays on. */
    ESP_LOGW(TAG, "still powered (USB?)");
    bsp_feature_enable(BSP_FEATURE_SPEAKER, true);
    bsp_display_brightness_set(muse_settings_brightness());
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "M5Stack Tab5",
    .width = UI_W,
    .height = UI_H,
    .ui_x = 0,
    .ui_y = 0,
    .avatar_px = 288,
    .round = false,
    .touch = true,
    .diagonal_in = 5.0f,
    .talk_button = "screen",
    .frame_ms = 40,
    .radio_init = radio_init,
    .init = init,
    .display_start = display_start,
    .display_lock = display_lock,
    .display_unlock = bsp_display_unlock,
    .set_brightness = set_brightness,
    .audio_init = audio_init,
    .mic_slot = -1,
    .set_mic_gain = set_mic_gain,
    .poll_buttons = poll_buttons,
    .read_power = read_power,
    .power_off = power_off,
    .flip_display = flip_display,
    .keyboard_present = keyboard_present,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
