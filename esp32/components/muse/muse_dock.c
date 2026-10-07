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

#include "muse_dock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_chat.h"
#include "muse_link.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_ui.h"
#include "muse_voice.h"
#include "muse_wifi.h"

#define COLOR_TEXT 0xf2efff
#define COLOR_DIM 0x8b84a8
#define COLOR_CARD 0x1a1530
#define COLOR_CARD_PRESSED 0x2e2552
#define COLOR_ACCENT 0xa77dff
#define COLOR_OK 0x6ff0bf
#define COLOR_WARN 0xffb45c
#define COLOR_DANGER 0xff5c5c
#define COLOR_USER 0x3b2a6e

#define GAP 12
#define REFRESH_MS 250
#define STATUS_EVERY 4          /* status lines every 4 refreshes: 1 s */
#define MAX_BUBBLES 40
#define CHAT_RING 32
#define KEY_QUEUE 32
#define INPUT_MAX 1000

static const char *TAG = "muse_dock";

typedef enum {
    CHAT_SENT,          /* our typed line reached Muse */
    CHAT_BUSY_ON,
    CHAT_BUSY_OFF,
    CHAT_TEXT,          /* a piece of Muse's reply */
    CHAT_FINAL,         /* the whole reply, replacing the pieces */
    CHAT_MESSAGE_DONE,
    CHAT_DONE,
    CHAT_ERROR,
} chat_kind_t;

typedef struct {
    chat_kind_t kind;
    char *text;         /* heap, or NULL */
} chat_item_t;

/* Filled by the console hook (the chat task), emptied by refresh() (LVGL). */
static chat_item_t s_ring[CHAT_RING];
static int s_ring_head, s_ring_len;
static SemaphoreHandle_t s_ring_lock;
static QueueHandle_t s_keys;

static lv_obj_t *s_link, *s_wifi, *s_power, *s_kb_line, *s_mode;
static lv_obj_t *s_log, *s_input, *s_osk, *s_talk, *s_talk_label;
static lv_obj_t *s_volume, *s_brightness;
static lv_obj_t *s_reply;           /* the Muse bubble being filled, or NULL */
static lv_obj_t *s_spoken;          /* this voice turn's bubble, or NULL */
static char s_spoken_last[MUSE_CAPTION_MAX];
static uint32_t s_caption_version;
static muse_mode_t s_last_mode = MUSE_MODE_BOOT;
static bool s_busy;
static bool s_talk_down;

/* ---- Chat events from the Muse client ------------------------------------- */

static void on_console(const char *type, const char *text, const char *fields)
{
    chat_kind_t kind;
    if (!strcmp(type, "sent")) {
        kind = CHAT_SENT;
    } else if (!strcmp(type, "busy")) {
        kind = strstr(fields, "\"on\":true") ? CHAT_BUSY_ON : CHAT_BUSY_OFF;
    } else if (!strcmp(type, "text")) {
        kind = CHAT_TEXT;
    } else if (!strcmp(type, "final")) {
        kind = CHAT_FINAL;
    } else if (!strcmp(type, "message_done")) {
        kind = CHAT_MESSAGE_DONE;
    } else if (!strcmp(type, "done")) {
        kind = CHAT_DONE;
    } else if (!strcmp(type, "error")) {
        kind = CHAT_ERROR;
    } else {
        return;     /* "ack" and anything new */
    }
    char *copy = NULL;
    if (text && text[0]) {
        size_t n = strlen(text);
        copy = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!copy) {
            return;
        }
        memcpy(copy, text, n + 1);
    }
    xSemaphoreTake(s_ring_lock, portMAX_DELAY);
    if (s_ring_len == CHAT_RING) {
        free(copy);     /* the screen is behind; the console still has it */
    } else {
        s_ring[(s_ring_head + s_ring_len++) % CHAT_RING] = (chat_item_t){ kind, copy };
    }
    xSemaphoreGive(s_ring_lock);
}

static bool chat_pop(chat_item_t *out)
{
    bool got = false;
    xSemaphoreTake(s_ring_lock, portMAX_DELAY);
    if (s_ring_len) {
        *out = s_ring[s_ring_head];
        s_ring_head = (s_ring_head + 1) % CHAT_RING;
        s_ring_len--;
        got = true;
    }
    xSemaphoreGive(s_ring_lock);
    return got;
}

/* ---- Widgets -------------------------------------------------------------- */

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_all(c, GAP, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *caption, int w, int h, uint32_t color)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_CARD_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = label(b, &lv_font_montserrat_20, COLOR_TEXT);
    lv_label_set_text(l, caption);
    lv_obj_center(l);
    return b;
}

static void scroll_to_end(void)
{
    lv_obj_update_layout(s_log);
    lv_obj_scroll_to_y(s_log, LV_COORD_MAX, LV_ANIM_OFF);
}

static lv_obj_t *bubble(bool mine, uint32_t color, const char *str)
{
    while (lv_obj_get_child_count(s_log) >= MAX_BUBBLES) {
        lv_obj_t *old = lv_obj_get_child(s_log, 0);
        if (old == s_reply) {
            s_reply = NULL;
        }
        if (old == s_spoken) {
            s_spoken = NULL;
        }
        lv_obj_delete(old);
    }
    lv_obj_t *b = lv_label_create(s_log);
    lv_obj_set_width(b, lv_pct(88));
    lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(mine ? COLOR_USER : 0x0e0b1c), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_all(b, 10, 0);
    lv_obj_set_style_align(b, mine ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT, 0);
    lv_label_set_text(b, str);
    scroll_to_end();
    return b;
}

static void bubble_append(lv_obj_t *b, const char *more)
{
    const char *was = lv_label_get_text(b);
    size_t a = strlen(was), n = strlen(more);
    char *joined = malloc(a + n + 1);
    if (!joined) {
        return;
    }
    memcpy(joined, was, a);
    memcpy(joined + a, more, n + 1);
    lv_label_set_text(b, joined);
    free(joined);
    scroll_to_end();
}

/* ---- Actions -------------------------------------------------------------- */

static void send_input(void)
{
    const char *line = lv_textarea_get_text(s_input);
    if (!line || !line[0]) {
        return;
    }
#if !CONFIG_MUSE_HATCH
    bubble(false, COLOR_DANGER, "Typed chat needs PSRAM");
    return;
#endif
    size_t n = strlen(line);
    char *copy = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        bubble(false, COLOR_DANGER, "Out of memory");
        return;
    }
    memcpy(copy, line, n + 1);
    bubble(true, COLOR_TEXT, line);
    lv_textarea_set_text(s_input, "");
    s_reply = NULL;
    muse_state_poke();
#if CONFIG_MUSE_HATCH
    muse_hatch_text_turn(copy);     /* frees it; refuses while a voice turn runs */
#else
    free(copy);
#endif
}

static void on_send(lv_event_t *e)
{
    (void)e;
    send_input();
}

static void on_osk(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        send_input();
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(s_input, LV_STATE_FOCUSED);
    }
}

static void on_input_focus(lv_event_t *e)
{
    (void)e;
    bool keyboard = muse_board->keyboard_present && muse_board->keyboard_present();
    if (!keyboard) {
        lv_obj_remove_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_osk);
    }
}

static void on_talk(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED && !s_talk_down) {
        s_talk_down = true;
        muse_input_post_buttons(MUSE_BTN_TALK_PRESS);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_talk_down) {
        s_talk_down = false;
        muse_input_post_buttons(MUSE_BTN_TALK_RELEASE);
    }
}

/* As in Settings: preview while dragging, save (to flash) on release. */
static void on_volume(lv_event_t *e)
{
    int v = lv_slider_get_value(s_volume);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        muse_settings_set_volume(v);
        muse_voice_request_chirp();
    } else {
        muse_audio_set_volume(v);
    }
}

static void on_brightness(lv_event_t *e)
{
    int v = lv_slider_get_value(s_brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        muse_settings_set_brightness(v);
    } else {
        muse_ui_preview_brightness(v);
    }
}

static void on_sleep(lv_event_t *e)
{
    (void)e;
    /* The aux button's short press: the screen sleeps; a touch wakes it. */
    muse_input_post_buttons(MUSE_BTN_AUX_PRESS | MUSE_BTN_AUX_RELEASE);
}

static void on_flip(lv_event_t *e)
{
    (void)e;
    if (muse_board->flip_display) {
        muse_board->flip_display();
    }
}

static void on_power(lv_event_t *e)
{
    /* Held like the aux button: muse_input warns, then powers off at 1.5 s. */
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        muse_input_post_buttons(MUSE_BTN_AUX_PRESS);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        muse_input_post_buttons(MUSE_BTN_AUX_RELEASE);
    }
}

/* ---- Keys from an external keyboard --------------------------------------- */

void muse_dock_key(uint32_t key)
{
    if (s_keys) {
        xQueueSend(s_keys, &key, 0);
    }
}

static void handle_key(uint32_t key)
{
    muse_state_poke();
    switch (key) {
    case LV_KEY_ENTER:
        if (lv_textarea_get_text(s_input)[0]) {
            send_input();
        } else if (muse_link_state() == MUSE_LINK_CONFIRM) {
            muse_input_post_buttons(MUSE_BTN_TALK_PRESS | MUSE_BTN_TALK_RELEASE);
        }
        break;
    case LV_KEY_BACKSPACE:
        lv_textarea_delete_char(s_input);
        break;
    case LV_KEY_ESC:
        lv_textarea_set_text(s_input, "");
        lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        break;
    case LV_KEY_LEFT:
        lv_textarea_cursor_left(s_input);
        break;
    case LV_KEY_RIGHT:
        lv_textarea_cursor_right(s_input);
        break;
    case LV_KEY_UP:
        lv_obj_scroll_by(s_log, 0, 80, LV_ANIM_ON);
        break;
    case LV_KEY_DOWN:
        lv_obj_scroll_by(s_log, 0, -80, LV_ANIM_ON);
        break;
    default:
        if (key >= 0x20 && strlen(lv_textarea_get_text(s_input)) < INPUT_MAX) {
            lv_textarea_add_char(s_input, key);
        }
        break;
    }
}

/* ---- Refresh -------------------------------------------------------------- */

static void drain_chat(void)
{
    chat_item_t it;
    while (chat_pop(&it)) {
        switch (it.kind) {
        case CHAT_BUSY_ON:
        case CHAT_BUSY_OFF:
            s_busy = it.kind == CHAT_BUSY_ON;
            break;
        case CHAT_TEXT:
            if (it.text) {
                if (!s_reply) {
                    s_reply = bubble(false, COLOR_TEXT, it.text);
                } else {
                    bubble_append(s_reply, it.text);
                }
            }
            break;
        case CHAT_FINAL:
            if (it.text) {
                if (!s_reply) {
                    s_reply = bubble(false, COLOR_TEXT, it.text);
                } else {
                    lv_label_set_text(s_reply, it.text);
                    scroll_to_end();
                }
            }
            break;
        case CHAT_MESSAGE_DONE:
            s_reply = NULL;
            break;
        case CHAT_DONE:
            s_reply = NULL;
            s_busy = false;
            break;
        case CHAT_ERROR:
            bubble(false, COLOR_DANGER, it.text ? it.text : "Error");
            s_reply = NULL;
            s_busy = false;
            break;
        case CHAT_SENT:
            break;
        }
        free(it.text);
    }
}

/* Voice turns: Muse's spoken reply, a caption page at a time. */
static void follow_voice(muse_mode_t mode)
{
    if (mode == MUSE_MODE_LISTENING && s_last_mode != MUSE_MODE_LISTENING) {
        bubble(true, COLOR_DIM, "(voice note)");
        s_spoken = NULL;
        s_spoken_last[0] = '\0';
    }
    char cap[MUSE_CAPTION_MAX];
    uint32_t version = s_caption_version;
    if (mode == MUSE_MODE_SPEAKING && muse_state_caption(cap, sizeof(cap), &version)
        && version != s_caption_version) {
        s_caption_version = version;
        if (cap[0] && strcmp(cap, s_spoken_last)) {
            strlcpy(s_spoken_last, cap, sizeof(s_spoken_last));
            if (!s_spoken) {
                s_spoken = bubble(false, COLOR_TEXT, cap);
            } else {
                bubble_append(s_spoken, " ");
                bubble_append(s_spoken, cap);
            }
        }
    }
    if (mode == MUSE_MODE_IDLE) {
        s_spoken = NULL;
    }
    s_last_mode = mode;
}

static const char *mode_name(muse_mode_t mode)
{
    switch (mode) {
    case MUSE_MODE_LISTENING: return "Listening... release to send";
    case MUSE_MODE_THINKING:  return "Thinking";
    case MUSE_MODE_SPEAKING:  return "Speaking";
    case MUSE_MODE_ERROR:     return "Error";
    default:                  return "Hold to talk";
    }
}

static void update_status(void)
{
    muse_link_state_t link = muse_link_state();
    uint32_t link_color = link == MUSE_LINK_ONLINE ? COLOR_OK
                        : link == MUSE_LINK_ERROR || link == MUSE_LINK_OFFLINE ? COLOR_DANGER
                        : COLOR_WARN;
    lv_label_set_text_fmt(s_link, "Muse: %s", muse_link_state_name(link));
    lv_obj_set_style_text_color(s_link, lv_color_hex(link_color), 0);

    muse_wifi_status_t w;
    muse_wifi_status(&w);
    if (w.state == MUSE_WIFI_CONNECTED) {
        lv_label_set_text_fmt(s_wifi, "Wi-Fi: %s  %d dBm  %s", w.ssid, w.rssi, w.ip);
    } else {
        lv_label_set_text_fmt(s_wifi, "Wi-Fi: %s", w.detail[0] ? w.detail
                              : w.state == MUSE_WIFI_OFF ? "off"
                              : w.state == MUSE_WIFI_NO_NETWORK ? "no network saved"
                              : w.state == MUSE_WIFI_CONNECTING ? "connecting"
                              : w.state == MUSE_WIFI_NOT_NEARBY ? "saved network not nearby" : "failed");
    }

    muse_power_t p = muse_state_power();
    if (p.battery_pct < 0 && !p.battery_mv) {
        lv_label_set_text(s_power, "Battery: not read");
    } else {
        lv_label_set_text_fmt(s_power, "Battery: %d%%  %d.%02d V%s%s", p.battery_pct < 0 ? 0 : p.battery_pct,
                              p.battery_mv / 1000, (p.battery_mv % 1000) / 10,
                              p.charging ? "  charging" : "", p.usb ? "  USB" : "");
    }

    bool kb = muse_board->keyboard_present && muse_board->keyboard_present();
    lv_label_set_text(s_kb_line, kb ? "Keyboard: connected (hold Tab to talk)" : "Keyboard: not connected");
}

static void refresh(lv_timer_t *t)
{
    static unsigned n;
    (void)t;
    uint32_t key;
    while (s_keys && xQueueReceive(s_keys, &key, 0) == pdTRUE) {
        handle_key(key);
    }
    drain_chat();
    float secs;
    muse_mode_t mode = muse_state_mode(&secs);
    follow_voice(mode);
    lv_label_set_text(s_talk_label, mode_name(mode));
    lv_obj_set_style_bg_color(s_talk, lv_color_hex(mode == MUSE_MODE_LISTENING ? COLOR_ACCENT : COLOR_CARD), 0);
    lv_label_set_text(s_mode, s_busy ? "Muse is working..." : "");
    if (n++ % STATUS_EVERY == 0) {
        update_status();
        if (!lv_slider_is_dragged(s_volume)) {
            lv_slider_set_value(s_volume, muse_settings_volume(), LV_ANIM_OFF);
        }
        if (!lv_slider_is_dragged(s_brightness)) {
            lv_slider_set_value(s_brightness, muse_settings_brightness(), LV_ANIM_OFF);
        }
    }
}

/* ---- Layout --------------------------------------------------------------- */

static lv_obj_t *slider_row(lv_obj_t *parent, const char *name, int x, int y, int w, int min, int max,
                            lv_event_cb_t cb)
{
    lv_obj_t *l = label(parent, &lv_font_montserrat_16, COLOR_DIM);
    lv_label_set_text(l, name);
    lv_obj_set_pos(l, x, y);
    lv_obj_t *s = lv_slider_create(parent);
    lv_obj_set_size(s, w, 14);
    lv_obj_set_pos(s, x, y + 30);
    lv_slider_set_range(s, min, max);
    lv_obj_set_style_bg_color(s, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(COLOR_TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);
    return s;
}

void muse_dock_build(lv_obj_t *scr, int ui_x, int ui_y, int ui_w, int ui_h)
{
    lv_display_t *disp = lv_obj_get_display(scr);
    int sw = lv_display_get_horizontal_resolution(disp);
    int sh = lv_display_get_vertical_resolution(disp);
    s_ring_lock = xSemaphoreCreateMutex();
    s_keys = xQueueCreate(KEY_QUEUE, sizeof(uint32_t));

    /* Beside Muse: status and chat. Below: talk and controls. The Tab5 has
     * the UI at 0,0, so the column is on the right and the strip at the
     * bottom; other placements keep the same two regions. */
    int col_x = ui_x + ui_w + GAP, col_w = sw - col_x - GAP;
    int strip_y = ui_y + ui_h + GAP, strip_h = sh - strip_y - GAP;
    if (col_w < 200 || strip_h < 120) {
        ESP_LOGW(TAG, "no room for the dock (%dx%d around %dx%d)", sw, sh, ui_w, ui_h);
        return;
    }

    lv_obj_t *status = card(scr, col_x, GAP, col_w, 150);
    s_link = label(status, &lv_font_montserrat_20, COLOR_WARN);
    lv_obj_set_pos(s_link, 0, 0);
    s_wifi = label(status, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_set_pos(s_wifi, 0, 34);
    s_power = label(status, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_set_pos(s_power, 0, 62);
    s_kb_line = label(status, &lv_font_montserrat_16, COLOR_DIM);
    lv_obj_set_pos(s_kb_line, 0, 90);

    int chat_y = GAP + 150 + GAP;
    lv_obj_t *chat = card(scr, col_x, chat_y, col_w, sh - chat_y - GAP);
    lv_obj_t *title = label(chat, &lv_font_montserrat_20, COLOR_TEXT);
    lv_label_set_text(title, "Chat with Muse");
    lv_obj_set_pos(title, 0, 0);
    s_mode = label(chat, &lv_font_montserrat_14, COLOR_ACCENT);
    lv_obj_align(s_mode, LV_ALIGN_TOP_RIGHT, 0, 4);

    int inner_w = col_w - 2 * GAP, inner_h = sh - chat_y - GAP - 2 * GAP;
    s_log = lv_obj_create(chat);
    lv_obj_remove_style_all(s_log);
    lv_obj_set_pos(s_log, 0, 36);
    lv_obj_set_size(s_log, inner_w, inner_h - 36 - 60);
    lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log, 8, 0);
    lv_obj_set_scroll_dir(s_log, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_AUTO);

    s_input = lv_textarea_create(chat);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_placeholder_text(s_input, "Type to Muse...");
    lv_textarea_set_max_length(s_input, INPUT_MAX);
    lv_obj_set_size(s_input, inner_w - 100, 48);
    lv_obj_set_pos(s_input, 0, inner_h - 48);
    lv_obj_set_style_text_font(s_input, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_input, lv_color_hex(0x0e0b1c), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(s_input, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(s_input, on_input_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_input, on_input_focus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *send = button(chat, "Send", 88, 48, COLOR_ACCENT);
    lv_obj_set_pos(send, inner_w - 88, inner_h - 48);
    lv_obj_add_event_cb(send, on_send, LV_EVENT_CLICKED, NULL);

    /* Below Muse: hold to talk, then the quick controls. */
    lv_obj_t *strip = card(scr, ui_x, strip_y, ui_w, strip_h);
    int sh_in = strip_h - 2 * GAP;
    s_talk = button(strip, "", 260, sh_in, COLOR_CARD);
    lv_obj_set_style_border_color(s_talk, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(s_talk, 3, 0);
    lv_obj_set_pos(s_talk, 0, 0);
    s_talk_label = lv_obj_get_child(s_talk, 0);
    lv_label_set_long_mode(s_talk_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_talk_label, 230);
    lv_obj_set_style_text_align(s_talk_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_PRESS_LOST, NULL);

    int cx = 260 + 2 * GAP, cw = ui_w - 2 * GAP - cx;
    s_volume = slider_row(strip, "Volume", cx, 0, cw, 0, 100, on_volume);
    s_brightness = slider_row(strip, "Brightness", cx, 56, cw, 10, 100, on_brightness);
    int bw = (cw - 2 * GAP) / 3, by = sh_in - 56;
    lv_obj_t *b = button(strip, "Sleep", bw, 56, COLOR_CARD_PRESSED);
    lv_obj_set_pos(b, cx, by);
    lv_obj_add_event_cb(b, on_sleep, LV_EVENT_CLICKED, NULL);
    b = button(strip, "Flip", bw, 56, COLOR_CARD_PRESSED);
    lv_obj_set_pos(b, cx + bw + GAP, by);
    lv_obj_add_event_cb(b, on_flip, LV_EVENT_CLICKED, NULL);
    if (!muse_board->flip_display) {
        lv_obj_add_state(b, LV_STATE_DISABLED);
    }
    b = button(strip, "Hold: off", bw, 56, 0x4a1d2a);
    lv_obj_set_pos(b, cx + 2 * (bw + GAP), by);
    lv_obj_add_event_cb(b, on_power, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(b, on_power, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(b, on_power, LV_EVENT_PRESS_LOST, NULL);

    /* On-screen keyboard over the strip, for typing without one attached. */
    s_osk = lv_keyboard_create(scr);
    lv_obj_set_pos(s_osk, ui_x, strip_y);
    lv_obj_set_size(s_osk, ui_w, strip_h);
    lv_keyboard_set_textarea(s_osk, s_input);
    lv_obj_add_event_cb(s_osk, on_osk, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_osk, on_osk, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);

    muse_hatch_set_console_hook(on_console);
    update_status();
    lv_label_set_text(s_talk_label, mode_name(MUSE_MODE_IDLE));
    lv_timer_create(refresh, REFRESH_MS, NULL);
    ESP_LOGI(TAG, "dock: column %d px, strip %d px", col_w, strip_h);
}
