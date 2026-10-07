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
 * 5" 720x1280 MIPI-DSI LCD, run landscape (1280x720), in three revisions (ILI9881C + GT911, ST7123 or
 * ST7121 with built-in touch), which the BSP tells apart at start by probing
 * the touch controller. ES8388 speaker codec and ES7210 microphones. Two
 * PI4IOE5V6408 expanders (0x43, 0x44) switch the LCD, touch, speaker, USB and
 * the ESP32-C6 that carries Wi-Fi and BLE over SDIO (esp_hosted).
 * There is no user button: the touchscreen talks and runs the menus.
 */
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "muse_board.h"
#include "muse_audio.h"
#include "muse_mem.h"

/* M5Unified Power_Class.inl (board_M5Tab5): pulsing the second expander's
 * (0x44) pin 4, PWROFF_PULSE, ten times 50 ms apart turns the Tab5 off. */
#define TAB5_PWROFF_PIN    IO_EXPANDER_PIN_NUM_4
#define TAB5_PWROFF_PULSES 10

static const char *TAG = "board";
static esp_codec_dev_handle_t s_spk, s_mic;

static esp_err_t radio_init(void)
{
    /* The C6 is off until the second expander powers it (BSP_WIFI_EN), and
     * esp_hosted reaches it as soon as Home Link starts Wi-Fi. */
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c init");
    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_WIFI, true), TAG, "C6 power");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

static esp_err_t init(void)
{
    return bsp_i2c_init();
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
    /* 90 degrees, as Cartographer runs this Tab5; touch follows the display. */
    bsp_display_lock(0);
    bsp_display_rotate(disp, LV_DISPLAY_ROTATION_90);
    bsp_display_unlock();
    *touch = bsp_display_get_input_dev();
    return *touch ? disp : NULL;
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
    *spk = s_spk = bsp_audio_codec_speaker_init();
    *mic = s_mic = bsp_audio_codec_microphone_init();
    return *spk && *mic ? ESP_OK : ESP_FAIL;
}

static void set_mic_gain(esp_codec_dev_handle_t mic, int db)
{
    /* ES7210 PGA steps are 3 dB; snap so the UI shows what's applied. */
    db = (db / 3) * 3;
    /* esp_codec_dev rounds 33 dB down to 30; the next real step up is 34.5. */
    esp_codec_dev_set_in_gain(mic, db == 33 ? 34.5f : (float)db);
}

static unsigned poll_buttons(void)
{
    return 0;
}

static esp_err_t power_off(void)
{
    esp_io_expander_handle_t exp = bsp_io_expander1_init();
    ESP_RETURN_ON_FALSE(exp, ESP_ERR_INVALID_STATE, TAG, "no IO expander");
    bsp_display_backlight_off();
    if (s_spk) {
        esp_codec_dev_close(s_spk);
    }
    if (s_mic) {
        esp_codec_dev_close(s_mic);
    }
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(exp, TAB5_PWROFF_PIN, IO_EXPANDER_OUTPUT), TAG, "pwroff pin");
    for (int i = 0; i < TAB5_PWROFF_PULSES; i++) {
        esp_io_expander_set_level(exp, TAB5_PWROFF_PIN, i & 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    /* Still running on USB power. */
    return ESP_FAIL;
}

static const muse_board_t s_board = {
    .name = "M5Stack Tab5",
    .width = BSP_LCD_V_RES,     /* landscape: 1280 */
    .height = BSP_LCD_H_RES,    /* 720 */
    .avatar_px = 480,
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
    .power_off = power_off,
};

const muse_board_t *muse_board_get(void)
{
    return &s_board;
}
