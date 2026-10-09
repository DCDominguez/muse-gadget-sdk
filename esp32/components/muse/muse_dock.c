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
 * The dock around Muse on a screen bigger than Muse's own area (the Tab5):
 * a typed chat beside Muse's stage, and over the stage a status strip,
 * Muse's name and mood, hold-to-talk and the pocket of quick controls. Full
 * screen hides the chat and centres the stage. With CONFIG_MUSE_PIXEL_THEME
 * it's drawn in Muse's pixel-art style: night-sky colours, notched pixel
 * borders, pixel icons and pixel fonts.
 */

#include "muse_dock.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include "muse_audio.h"
#include "muse_board.h"
#include "muse_chat.h"
#include "muse_link.h"
#include "muse_pixel_style.h"
#include "muse_settings.h"
#include "muse_settings_ui.h"
#include "muse_state.h"
#include "muse_text.h"
#include "muse_ui.h"
#if CONFIG_MUSE_PIXEL_THEME
#include "muse_props.h"
#include "muse_scene.h"
#endif
#include "muse_voice.h"
#include "muse_wifi.h"

#define REFRESH_MS 250
#define STATUS_EVERY 4          /* status every 4 refreshes: 1 s */
#define MAX_BUBBLES 40
#define CHAT_RING 32
#define KEY_QUEUE 32
#define INPUT_MAX 1000
#define SPOKEN_OVERLAP_MIN 8    /* bytes a caption must repeat to count as the same text */
#define METER_CELLS 10

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
    CHAT_ACTIVITY,      /* what kind of work Muse is doing: "code/status label" */
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

static lv_obj_t *s_stage;           /* over Muse's area: status, name, talk, pocket button */
static lv_obj_t *s_chat;            /* the chat window */
static lv_obj_t *s_pocket;          /* the quick controls, over the chat window */
static lv_obj_t *s_setwin;          /* settings, over the chat window too */
static lv_obj_t *s_dot, *s_link, *s_bars[4], *s_cells[3], *s_batt_pct, *s_kb_icon;
static lv_obj_t *s_mood;
static lv_obj_t *s_log, *s_input, *s_osk, *s_talk, *s_talk_label;
#if CONFIG_MUSE_EMOJI_FONT
static lv_obj_t *s_emoji;           /* the picker grid */
#endif
static lv_obj_t *s_vol_cells[METER_CELLS], *s_bri_cells[METER_CELLS], *s_vol_value, *s_bri_value;
static lv_obj_t *s_pocket_info;
static lv_obj_t *s_camera;          /* the camera tile, on boards that have one */
static lv_obj_t *s_camera_notice;   /* "snap! photo", over Muse */
static lv_obj_t *s_full_btn;
static lv_obj_t *s_reply;           /* the Muse bubble being filled, or NULL */
static lv_obj_t *s_spoken;          /* this voice turn's bubble, or NULL */
static char s_spoken_last[MUSE_CAPTION_MAX];
static uint32_t s_caption_version;
static muse_mode_t s_last_mode = MUSE_MODE_BOOT;
static bool s_busy;
static bool s_talk_down;
static bool s_full;
static int s_ui_x, s_ui_y, s_ui_w, s_screen_w;
static char s_activity[96];         /* Muse's latest activity, "" when none */
static int64_t s_text_at;           /* when the last piece of a reply arrived */
static bool s_working;              /* Cosmo has a work prop out: it stays home */

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
    } else if (!strcmp(type, "activity")) {
        kind = CHAT_ACTIVITY;
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

#if CONFIG_MUSE_EMOJI_FONT
LV_FONT_DECLARE(muse_font_emoji_16)

/* The emoji the picker offers, a row of eight at a time. Each must be in the
 * font (tools/muse/gen_emoji_font.sh). */
static const char *const EMOJI_MAP[] = {
    "\U0001F600", "\U0001F602", "\U0001F60A", "\U0001F60D", "\U0001F609", "\U0001F60E", "\U0001F914", "\U0001F622", "\n",
    "\U0001F62E", "\U0001F634", "\U0001F973", "\U0001F64F", "\U0001F44D", "\U0001F44E", "\U0001F44B", "\U0001F44F", "\n",
    "❤",     "\U0001F525", "✨",     "\U0001F389", "\U0001F916", "\U0001F431", "☕",     "\U0001F4A1", "",
};

/* With the LVGL lock held: the font's glyph cache isn't shared safely. */
static bool emoji_drawable(uint32_t cp)
{
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(&muse_font_emoji_16, &dsc, cp, 0);
}
#endif

/* The chat font; with CONFIG_MUSE_EMOJI_FONT, a copy that falls back to Noto
 * Emoji for what it lacks. */
static const lv_font_t *chat_font(void)
{
#if CONFIG_MUSE_EMOJI_FONT
    static lv_font_t font;
    if (!font.get_glyph_dsc) {
        font = *F_BODY;
        font.fallback = &muse_font_emoji_16;
    }
    return &font;
#else
    return F_BODY;
#endif
}

/* A label's text as the chat font can draw it: emoji it has stay; other
 * characters it lacks get muse_text's ASCII stand-ins, and variation
 * selectors, joiners and skin tones go, so they don't show as boxes. */
static void set_chat_text(lv_obj_t *l, const char *str)
{
    size_t cap = strlen(str) * 3 / 2 + 4;   /* stand-ins: up to 3 ASCII for 2 bytes */
    char *shown = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!shown) {
        lv_label_set_text(l, str);
        return;
    }
    strlcpy(shown, str, cap);
#if CONFIG_MUSE_EMOJI_FONT
    muse_text_to_ascii_keeping(shown, cap, emoji_drawable);
#else
    muse_text_to_ascii(shown, cap);
#endif
    lv_label_set_text(l, shown);
    free(shown);
}

/* ---- Pixel pieces ----------------------------------------------------------- */

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = plain(parent);
    lv_obj_add_flag(o, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

/* A pixel icon: rectangles on a grid of `px` screen pixels a cell. */
typedef struct {
    uint8_t x, y, w, h;
} cell_t;
#define ICON(...) ((const cell_t[]){ __VA_ARGS__ }), (sizeof((const cell_t[]){ __VA_ARGS__ }) / sizeof(cell_t))

static void icon_cells(lv_obj_t *box, const cell_t *cells, size_t n, int px, uint32_t color)
{
    for (size_t i = 0; i < n; i++) {
        rect(box, cells[i].x * px, cells[i].y * px, cells[i].w * px, cells[i].h * px, color);
    }
}

static lv_obj_t *icon_box(lv_obj_t *parent, int w_cells, int h_cells, int px)
{
    lv_obj_t *box = plain(parent);
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(box, w_cells * px, h_cells * px);
    return box;
}

/*
 * A box with a pixel border: the corners notched out (the backdrop's colour
 * shows there), and an optional shade along the bottom and right for a raised
 * look. Children go inside the border.
 */
static lv_obj_t *pixel_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t fill, uint32_t edge,
                           uint32_t shade, uint32_t backdrop)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(edge), 0);
    lv_obj_set_style_border_width(o, PX, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    if (shade != fill) {
        rect(o, 0, h - 3 * PX, w - 2 * PX, PX, shade);
        rect(o, w - 3 * PX, 0, PX, h - 3 * PX, shade);
    }
    /* Notches: the border's corner cells in the backdrop's colour. */
    lv_obj_t *c[4];
    for (int i = 0; i < 4; i++) {
        c[i] = plain(o);
        lv_obj_add_flag(c[i], LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(c[i], PX, PX);
        lv_obj_set_style_bg_color(c[i], lv_color_hex(backdrop), 0);
        lv_obj_set_style_bg_opa(c[i], LV_OPA_COVER, 0);
    }
    lv_obj_align(c[0], LV_ALIGN_TOP_LEFT, -PX, -PX);
    lv_obj_align(c[1], LV_ALIGN_TOP_RIGHT, PX, -PX);
    lv_obj_align(c[2], LV_ALIGN_BOTTOM_LEFT, -PX, PX);
    lv_obj_align(c[3], LV_ALIGN_BOTTOM_RIGHT, PX, PX);
    return o;
}

/* A pressable pixel box: darker while held. */
static lv_obj_t *pixel_button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t fill, uint32_t edge,
                              uint32_t shade, uint32_t backdrop)
{
    lv_obj_t *b = pixel_box(parent, x, y, w, h, fill, edge, shade, backdrop);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, lv_color_hex(shade), LV_STATE_PRESSED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 12, 0);
    return b;
}

/* ---- Chat bubbles ------------------------------------------------------------ */

#define BUBBLE_PAD_H 14
#define BUBBLE_PAD_V 10

/* A bubble is as wide as its text up to 84% of the log, then wraps there and
 * grows taller. LVGL won't wrap a content-sized label, so measure the text and
 * pick a width that way. */
static void fit_bubble(lv_obj_t *b)
{
    int max_w = lv_obj_get_content_width(s_log) * 84 / 100;
    int max_text = max_w - 2 * BUBBLE_PAD_H - PX;
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(b), lv_obj_get_style_text_font(b, 0), 0,
                     lv_obj_get_style_text_line_space(b, 0), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(b, size.x <= max_text ? LV_SIZE_CONTENT : max_w);
}

static void scroll_to_end(void)
{
    lv_obj_update_layout(s_log);
    lv_obj_scroll_to_y(s_log, LV_COORD_MAX, LV_ANIM_OFF);
}

/* Muse's bubbles are cream on the left, the owner's lavender on the right; each
 * sits in a full-width row so it can be pushed to its side. Returns the label. */
static lv_obj_t *bubble(bool mine, uint32_t fill, uint32_t ink, uint32_t shade, const char *str)
{
    while (lv_obj_get_child_count(s_log) >= MAX_BUBBLES) {
        lv_obj_t *old = lv_obj_get_child(s_log, 0);
        lv_obj_t *l = lv_obj_get_child(old, 0);
        if (l == s_reply) {
            s_reply = NULL;
        }
        if (l == s_spoken) {
            s_spoken = NULL;
        }
        lv_obj_delete(old);
    }
    lv_obj_t *row = plain(s_log);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, mine ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_t *b = lv_label_create(row);
    lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(b, chat_font(), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(ink), 0);
    lv_obj_set_style_text_line_space(b, 4, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(b, BUBBLE_PAD_H, 0);
    lv_obj_set_style_pad_ver(b, BUBBLE_PAD_V, 0);
    /* The pixel bevel: a shade along the bottom and right. */
    lv_obj_set_style_border_color(b, lv_color_hex(shade), 0);
    lv_obj_set_style_border_width(b, PX, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
    set_chat_text(b, str);
    fit_bubble(b);
    scroll_to_end();
    return b;
}

static lv_obj_t *muse_bubble(const char *str)
{
    return bubble(false, C_CREAM, C_INK, C_CREAM_SHADE, str);
}

static lv_obj_t *my_bubble(const char *str)
{
    return bubble(true, C_USER, C_CREAM, C_USER_SHADE, str);
}

static lv_obj_t *error_bubble(const char *str)
{
    return bubble(false, C_RED_LO, C_CREAM, C_RED, str);
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
    set_chat_text(b, joined);
    fit_bubble(b);
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
    error_bubble("Typed chat needs PSRAM");
    return;
#endif
    size_t n = strlen(line);
    char *copy = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        error_bubble("Out of memory");
        return;
    }
    memcpy(copy, line, n + 1);
    my_bubble(line);
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

/* Focus opens the on-screen keyboard unless a keyboard is attached; a tap on
 * the line always opens it, so it's there with a keyboard plugged in too. */
static void on_input_focus(lv_event_t *e)
{
    bool keyboard = muse_board->keyboard_present && muse_board->keyboard_present();
    if (lv_event_get_code(e) == LV_EVENT_CLICKED || !keyboard) {
        lv_obj_remove_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_osk);
    }
}

#if CONFIG_MUSE_EMOJI_FONT
static void on_emoji_button(lv_event_t *e)
{
    (void)e;
    muse_state_poke();
    if (lv_obj_has_flag(s_emoji, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_emoji, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_emoji);
    } else {
        lv_obj_add_flag(s_emoji, LV_OBJ_FLAG_HIDDEN);
    }
}

/* A picked emoji goes in at the cursor; the grid closes. */
static void on_emoji_pick(lv_event_t *e)
{
    (void)e;
    muse_state_poke();
    uint32_t id = lv_buttonmatrix_get_selected_button(s_emoji);
    const char *emoji = lv_buttonmatrix_get_button_text(s_emoji, id);
    if (emoji && strlen(lv_textarea_get_text(s_input)) + strlen(emoji) <= INPUT_MAX) {
        lv_textarea_add_text(s_input, emoji);
    }
    lv_obj_add_flag(s_emoji, LV_OBJ_FLAG_HIDDEN);
}
#endif

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

static void show_meter(lv_obj_t *const *cells, lv_obj_t *value, int v, uint32_t on)
{
    int lit = (v + 5) / 10;
    for (int i = 0; i < METER_CELLS; i++) {
        lv_obj_set_style_bg_color(cells[i], lv_color_hex(i < lit ? on : C_METER_OFF), 0);
    }
    lv_label_set_text_fmt(value, "%d%%", v);
}

/* A tap on the volume meter's cell i sets 10 * (i + 1); the chirp lets it be heard. */
static void on_volume_cell(lv_event_t *e)
{
    int v = 10 * ((int)(intptr_t)lv_event_get_user_data(e) + 1);
    muse_audio_set_volume(v);
    muse_settings_set_volume(v);
    muse_voice_request_chirp();
    show_meter(s_vol_cells, s_vol_value, v, C_LAV);
}

static void on_brightness_cell(lv_event_t *e)
{
    int v = 10 * ((int)(intptr_t)lv_event_get_user_data(e) + 1);
    muse_ui_preview_brightness(v);
    muse_settings_set_brightness(v);
    show_meter(s_bri_cells, s_bri_value, v, C_SUN);
}

static void update_status(void);
static void settings_show(bool show);

static void pocket_show(bool show)
{
    if (show) {
        settings_show(false);
        show_meter(s_vol_cells, s_vol_value, muse_settings_volume(), C_LAV);
        show_meter(s_bri_cells, s_bri_value, muse_settings_brightness(), C_SUN);
        lv_obj_remove_flag(s_pocket, LV_OBJ_FLAG_HIDDEN);
        update_status();   /* its details, now rather than in a second */
        lv_obj_move_foreground(s_pocket);
    } else {
        lv_obj_add_flag(s_pocket, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_pocket(lv_event_t *e)
{
    (void)e;
    muse_state_poke();
    pocket_show(lv_obj_has_flag(s_pocket, LV_OBJ_FLAG_HIDDEN));
}

static void on_nap(lv_event_t *e)
{
    (void)e;
    pocket_show(false);
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

/* Settings open where the pocket was, over the chat; closing goes back to it. */
static void settings_show(bool show)
{
    if (!s_setwin || show != lv_obj_has_flag(s_setwin, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (show) {
        pocket_show(false);
        lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_setwin, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_setwin);
        muse_settings_ui_tick(true);
    } else {
        lv_obj_add_flag(s_setwin, LV_OBJ_FLAG_HIDDEN);
        muse_settings_ui_home();
        muse_settings_ui_tick(false);
    }
}

void muse_dock_show_settings(bool show)
{
    settings_show(show);
}

static void on_settings(lv_event_t *e)
{
    (void)e;
    settings_show(true);
}

static void on_settings_close(lv_event_t *e)
{
    (void)e;
    settings_show(false);
}

static void set_full(bool full);

static void on_full(lv_event_t *e)
{
    (void)e;
    muse_state_poke();
    set_full(!s_full);
}

static void tile_text(lv_obj_t *tile, const char *name, const char *sub, uint32_t color);

/* The owner's camera switch: lavender while Muse may take photos, red while it does. */
static void show_camera_state(void)
{
    bool on = muse_board->camera_enabled();
    bool in_use = muse_board->camera_in_use && muse_board->camera_in_use();
    uint32_t fill = in_use ? C_RED : on ? C_LAV : C_TILE;
    uint32_t ink = on || in_use ? C_INK : C_TEXT;
    lv_obj_set_style_bg_color(s_camera, lv_color_hex(fill), 0);
    tile_text(s_camera, "camera", in_use ? "in use" : on ? "on" : "off", ink);
}

/* Each refresh: the notice over Muse while a photo is being taken. */
static void follow_camera(void)
{
    if (!s_camera) {
        return;
    }
    bool in_use = muse_board->camera_in_use && muse_board->camera_in_use();
    if (in_use == !lv_obj_has_flag(s_camera_notice, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (in_use) {
        muse_state_poke();   /* awake, so the notice is seen */
        lv_obj_remove_flag(s_camera_notice, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_camera_notice);
    } else {
        lv_obj_add_flag(s_camera_notice, LV_OBJ_FLAG_HIDDEN);
    }
    show_camera_state();
}

static void on_camera(lv_event_t *e)
{
    (void)e;
    muse_state_poke();
    muse_board->set_camera_enabled(!muse_board->camera_enabled());
    show_camera_state();
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

/* The scenery behind see-through Muse, for where Muse now stands. */
static void paint_scene(void)
{
#if CONFIG_MUSE_PIXEL_THEME
    int fx, fy;
    if (muse_ui_feet(&fx, &fy)) {
        muse_scene_paint(lv_obj_get_x(s_stage), s_ui_w, fx, fy, s_full);
    }
#endif
}

static void wander_home(void);

/* Full screen: the chat and pocket go, Muse's stage moves to the middle and
 * the scenery spreads across the whole screen. */
static void set_full(bool full)
{
    wander_home();   /* the island is painted under Muse's feet at home */
    s_full = full;
    int x = full ? (s_screen_w - s_ui_w) / 2 : s_ui_x;
    muse_ui_set_origin(x, s_ui_y);
    lv_obj_set_x(s_stage, x);
    if (full) {
        lv_obj_add_flag(s_chat, LV_OBJ_FLAG_HIDDEN);
        pocket_show(false);
        settings_show(false);
        lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_chat, LV_OBJ_FLAG_HIDDEN);
    }
    paint_scene();
}

/* ---- Muse wandering its island ------------------------------------------- */

/*
 * While Muse is idle it doesn't just stand there: every 15-35 s it strolls
 * about its island (smaller as it goes further back, facing where it walks),
 * gazes up at the stars, looks around, or daydreams with a thought bubble; a
 * reply arriving makes it peek at the chat. Anything else (a talk, a reply,
 * a reaction such as dizzy or a tickle, the screen going dark) puts it back
 * home facing the front at once, so Muse's own animations play as always.
 */

#define WANDER_MS 80
#define WALK_PX 6               /* a step: one of Muse's pixels at 384 px */
#define TURN_RAD 0.35f          /* the most Muse turns in a step */
#define ISLAND_RX 150           /* where Muse may walk, around home */
#define ISLAND_BACK 52
#define ISLAND_FRONT 10
#define MAX_LEGS 4

typedef enum {
    W_HOME,         /* standing at home, facing front */
    W_WALK,         /* to s_w.legs[s_w.leg] */
    W_HOLD,         /* standing a while, facing s_w.face_to */
} wander_act_t;

typedef enum { THOUGHT_RAMEN, THOUGHT_STAR, THOUGHT_NOTE, THOUGHT_MOON, THOUGHT_HEART, THOUGHTS } thought_t;

static const char *const THOUGHT_MOOD[THOUGHTS] = {
    "~ thinking about ramen ~", "~ wishing on a star ~", "~ humming a tune ~", "~ moon-watching ~",
    "~ feeling cozy ~",
};

static struct {
    wander_act_t act;
    float x, y;                 /* feet, from home; y < 0 is further back */
    float face, face_to;
    struct { int16_t x, y; } legs[MAX_LEGS];
    int legs_n, leg;
    int hold;                   /* steps left standing */
    int next;                   /* steps until the next idle whim */
    const char *mood;           /* overrides "daydreaming" while set */
    bool peeking;
} s_w;

static lv_obj_t *s_thought;
static lv_obj_t *s_thought_art[THOUGHTS];

static int base_px(void)
{
    return s_full ? 448 : 384;
}

/* Further back, Muse is drawn smaller: whole grid steps, so the pixels stay crisp. */
static int depth_px(float y)
{
    int px = base_px();
    return y < -40 ? px - 128 : y < -14 ? px - 64 : px;
}

static float wrap_pi(float a)
{
    while (a > 3.14159f) {
        a -= 6.28318f;
    }
    while (a < -3.14159f) {
        a += 6.28318f;
    }
    return a;
}

static void thought_show(int which)
{
    if (!s_thought) {
        return;
    }
    if (which < 0) {
        lv_obj_add_flag(s_thought, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    for (int i = 0; i < THOUGHTS; i++) {
        lv_obj_set_flag(s_thought_art[i], LV_OBJ_FLAG_HIDDEN, i != which);
    }
    int fx, fy;
    if (muse_ui_feet(&fx, &fy)) {
        int px = depth_px(s_w.y);
        lv_obj_set_pos(s_thought, fx - lv_obj_get_x(s_stage) + px / 6, fy - px * 9 / 10 - 72);
    }
    lv_obj_remove_flag(s_thought, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_thought);
}

static void wander_apply(float walk)
{
    muse_ui_set_motion((int)s_w.x / WALK_PX * WALK_PX, (int)s_w.y / 2 * 2, depth_px(s_w.y), s_w.face, walk);
}

static void wander_home(void)
{
    s_w.act = W_HOME;
    s_w.x = s_w.y = 0;
    s_w.face = s_w.face_to = 0;
    s_w.mood = NULL;
    s_w.peeking = false;
    thought_show(-1);
    wander_apply(0);
}

static void hold(float face, int steps, const char *mood)
{
    s_w.act = W_HOLD;
    s_w.face_to = face;
    s_w.hold = steps;
    s_w.mood = mood;
}

/* A stroll: one to three spots on the island, then home. */
static void plan_walk(void)
{
    int n = 1 + (int)(esp_random() % 3);
    for (int i = 0; i < n; i++) {
        float a = (esp_random() % 628) / 100.0f, r = 0.35f + (esp_random() % 65) / 100.0f;
        float y = -ISLAND_BACK / 2 + sinf(a) * r * (ISLAND_BACK + ISLAND_FRONT) / 2;
        s_w.legs[i].x = (int16_t)(cosf(a) * r * ISLAND_RX);
        s_w.legs[i].y = (int16_t)(y < -ISLAND_BACK ? -ISLAND_BACK : y > ISLAND_FRONT ? ISLAND_FRONT : y);
    }
    s_w.legs[n].x = 0;
    s_w.legs[n].y = 0;
    s_w.legs_n = n + 1;
    s_w.leg = 0;
    s_w.act = W_WALK;
    s_w.mood = "~ out for a stroll ~";
}

static void whim(void)
{
    uint32_t r = esp_random() % 100;
    if (r < 45) {
        plan_walk();
    } else if (r < 60) {
        hold(2.5f, 50, "~ stargazing ~");       /* back to us, looking up */
        thought_show(THOUGHT_STAR);
    } else if (r < 75) {
        hold(0.9f, 18, "~ looking around ~");
    } else {
        int t = (int)(esp_random() % THOUGHTS);
        hold(0, 50, THOUGHT_MOOD[t]);
        thought_show(t);
    }
}

/* A reply landed: if Muse is standing about, it turns to the chat a moment. */
static void wander_peek(void)
{
    if (s_w.act == W_HOME && !s_full) {
        hold(1.1f, 30, NULL);
        s_w.peeking = true;
    }
}

static void wander_tick(lv_timer_t *t)
{
    (void)t;
    if (muse_ui_dark()) {
        return;
    }
    float secs;
    bool busy = muse_state_mode(&secs) != MUSE_MODE_IDLE || muse_state_dizzy() > 0 || muse_state_sleepy() > 0
                || muse_state_waking() > 0 || muse_state_tickle() > 0 || muse_state_happiness() > 0.05f
                || muse_link_state() != MUSE_LINK_ONLINE || s_working;
    if (busy) {
        if (s_w.act != W_HOME || s_w.x || s_w.y || s_w.face) {
            wander_home();
        }
        s_w.next = (8000 + (int)(esp_random() % 7000)) / WANDER_MS;
        return;
    }

    float walk = 0;
    switch (s_w.act) {
    case W_HOME:
        s_w.face_to = 0;
        if (--s_w.next <= 0) {
            whim();
            s_w.next = (15000 + (int)(esp_random() % 20000)) / WANDER_MS;
        }
        break;
    case W_WALK: {
        float dx = s_w.legs[s_w.leg].x - s_w.x, dy = s_w.legs[s_w.leg].y - s_w.y;
        /* Depth reads half as far on screen, so it's walked at half the pace. */
        float d = sqrtf(dx * dx + 4 * dy * dy);
        if (d < WALK_PX) {
            s_w.x = s_w.legs[s_w.leg].x;
            s_w.y = s_w.legs[s_w.leg].y;
            if (++s_w.leg >= s_w.legs_n) {
                hold(0, 6, NULL);   /* home: turn to the front */
            } else {
                s_w.hold = 6 + (int)(esp_random() % 12);   /* a pause at each spot */
            }
            break;
        }
        if (s_w.hold > 0) {
            s_w.hold--;
            break;
        }
        s_w.face_to = atan2f(dx, 2 * dy);
        if (fabsf(wrap_pi(s_w.face_to - s_w.face)) < 0.6f) {   /* turn first, then go */
            s_w.x += dx / d * WALK_PX;
            s_w.y += dy / d * WALK_PX / 2;
            walk = 1;
        }
        break;
    }
    case W_HOLD:
        if (--s_w.hold <= 0) {
            if (s_w.x || s_w.y) {
                s_w.act = W_WALK;           /* finishing a stroll */
            } else if (s_w.face_to != 0) {
                hold(0, 6, NULL);           /* turn back to the front */
                thought_show(-1);
            } else {
                s_w.act = W_HOME;
                s_w.mood = NULL;
                s_w.peeking = false;
                thought_show(-1);
            }
        }
        break;
    }
    float turn = wrap_pi(s_w.face_to - s_w.face);
    s_w.face = wrap_pi(s_w.face + (turn > TURN_RAD ? TURN_RAD : turn < -TURN_RAD ? -TURN_RAD : turn));
    if (fabsf(s_w.face) < 0.01f) {
        s_w.face = 0;
    }
    wander_apply(walk);
}

static void build_thought(lv_obj_t *parent)
{
    s_thought = pixel_box(parent, 0, 0, 84, 72, C_CREAM, C_CREAM_SHADE, C_CREAM_SHADE, C_NIGHT);
    lv_obj_set_flex_flow(s_thought, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_thought, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *a;
    a = s_thought_art[THOUGHT_RAMEN] = icon_box(s_thought, 11, 9, 4);
    icon_cells(a, ICON({ 7, 0, 1, 4 }, { 9, 0, 1, 4 }), 4, 0x6f4a2a);
    icon_cells(a, ICON({ 0, 4, 11, 1 }, { 1, 5, 9, 2 }, { 2, 7, 7, 1 }), 4, C_RED);
    icon_cells(a, ICON({ 3, 8, 5, 1 }), 4, 0xa8323f);
    icon_cells(a, ICON({ 2, 3, 4, 1 }, { 3, 2, 2, 1 }), 4, C_SUN);
    a = s_thought_art[THOUGHT_STAR] = icon_box(s_thought, 9, 9, 4);
    icon_cells(a, ICON({ 4, 0, 1, 2 }, { 3, 2, 3, 2 }, { 0, 3, 9, 2 }, { 1, 5, 7, 1 }, { 2, 6, 5, 1 }, { 1, 7, 2, 2 },
                       { 6, 7, 2, 2 }), 4, C_SUN);
    a = s_thought_art[THOUGHT_NOTE] = icon_box(s_thought, 8, 9, 4);
    icon_cells(a, ICON({ 3, 0, 5, 1 }, { 3, 1, 1, 6 }, { 7, 1, 1, 5 }, { 1, 6, 3, 2 }, { 5, 5, 3, 2 }), 4, C_LAV_LO);
    a = s_thought_art[THOUGHT_MOON] = icon_box(s_thought, 8, 9, 4);
    icon_cells(a, ICON({ 2, 0, 4, 1 }, { 1, 1, 3, 1 }, { 0, 2, 3, 5 }, { 1, 7, 3, 1 }, { 2, 8, 4, 1 }), 4, 0xd9a93a);
    a = s_thought_art[THOUGHT_HEART] = icon_box(s_thought, 9, 8, 4);
    icon_cells(a, ICON({ 1, 0, 3, 1 }, { 5, 0, 3, 1 }, { 0, 1, 9, 3 }, { 1, 4, 7, 1 }, { 2, 5, 5, 1 }, { 3, 6, 3, 1 },
                       { 4, 7, 1, 1 }), 4, 0xf2a7b0);
    lv_obj_add_flag(s_thought, LV_OBJ_FLAG_HIDDEN);
}

/* ---- Cosmo at work ---------------------------------------------------------- */

/*
 * While Muse works, Cosmo shows what kind of work it is with a prop in its own
 * pixels: a big book for research, a spinning globe for a web search, a
 * typewriter while a reply streams in, an easel for an image, a camera (and a
 * flash) for a photo. Listening, thinking and speaking are Muse's own
 * animations and need no prop. The kind comes from Muse's activity code, by
 * the words in it, so new codes that say what they are still find a prop.
 */

#define WP 6                    /* a prop's pixel: Muse's at 384 px */
#define TEXT_TYPING_US (1500 * 1000)

typedef enum { WORK_NONE, WORK_RESEARCH, WORK_SEARCH, WORK_WRITE, WORK_IMAGE, WORK_CAMERA, WORKS } work_t;

static const char *const WORK_MOOD[WORKS] = {
    NULL, "~ researching ~", "~ searching the web ~", "~ writing back ~", "~ painting ~", "~ taking a photo ~",
};

static lv_obj_t *s_work[WORKS];
static lv_obj_t *s_flash;
static work_t s_work_now;

static bool has_word(const char *s, const char *const *words)
{
    for (; *words; words++) {
        if (strcasestr(s, *words)) {
            return true;
        }
    }
    return false;
}

static int s_work_force = -1;   /* "dock=work=..." from the console, for a look; -1 off */

static work_t work_kind(muse_mode_t mode)
{
    if (s_work_force >= 0) {
        return (work_t)s_work_force;
    }
    if (muse_board->camera_in_use && muse_board->camera_in_use()) {
        return WORK_CAMERA;
    }
    if (mode == MUSE_MODE_LISTENING || mode == MUSE_MODE_SPEAKING) {
        return WORK_NONE;
    }
    static const char *const IMAGE[] = { "image", "draw", "paint", "picture", "illustrat", "render", NULL };
    static const char *const SEARCH[] = { "search", "web", "brows", "google", "lookup", "look_up", NULL };
    static const char *const RESEARCH[] = { "research", "read", "fetch", "document", "analy", "study", NULL };
    static const char *const WRITE[] = { "writ", "compos", "draft", "typ", NULL };
    static const char *const CAMERA[] = { "camera", "photo", "snap", NULL };
    if (s_activity[0] && (s_busy || mode == MUSE_MODE_THINKING)) {
        if (has_word(s_activity, IMAGE)) {
            return WORK_IMAGE;
        }
        if (has_word(s_activity, CAMERA)) {
            return WORK_CAMERA;
        }
        if (has_word(s_activity, SEARCH)) {
            return WORK_SEARCH;
        }
        if (has_word(s_activity, RESEARCH)) {
            return WORK_RESEARCH;
        }
        if (has_word(s_activity, WRITE)) {
            return WORK_WRITE;
        }
    }
    if (s_text_at && esp_timer_get_time() - s_text_at < TEXT_TYPING_US) {
        return WORK_WRITE;
    }
    return WORK_NONE;
}

static void anim_x(void *o, int32_t v) { lv_obj_set_x(o, v); }
static void anim_y(void *o, int32_t v) { lv_obj_set_y(o, v); }
static void anim_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }

static void loop_anim(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay,
                      bool back)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    if (back) {
        lv_anim_set_reverse_duration(&a, ms);
    }
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* Materials, in tones from Muse's own palette (avatar/muse_pixel.c): warm
 * paper like Muse's face, soft leather and wood, muted sea and sage, brass. */
static const muse_prop_mat_t MATS[] = {
    { 'p', 0xfdeed6, 0xf6dfbd, 0xe0c49e },   /* paper */
    { 'k', 0x8c7560, 0x8c7560, 0x8c7560 },   /* ink */
    { 's', 0x8a5643, 0x6e4333, 0x55322a },   /* spine */
    { 'c', 0xb07a5e, 0x93624a, 0x6e4636 },   /* leather cover */
    { 'b', 0xe0c27a, 0xc4a25c, 0x9a7a40 },   /* brass */
    { 'g', 0xa9c6e0, 0x86a8c9, 0x6688ab },   /* sea, glass */
    { 'l', 0xb9d3a4, 0x96b585, 0x74936a },   /* land */
    { 'w', 0xb0896a, 0x8f6c51, 0x6b5444 },   /* wood */
    { 'm', 0xa79eb2, 0x8a8096, 0x6c6378 },   /* metal */
    { 'd', 0x5a5266, 0x4a4356, 0x3c3647 },   /* dark metal */
    { 'L', 0x4a4560, 0x3a3550, 0x2c283e },   /* lens */
    { 'S', 0xdfe8f5, 0xdfe8f5, 0xdfe8f5 },   /* its shine */
    { 'v', 0xc9b8ff, 0xb9a7ff, 0x9d88e6 },   /* lavender paint */
    { 'y', 0xf2d48a, 0xe8c66a, 0xc9a650 },   /* sun paint */
    { 'n', 0xb5dcc4, 0x9fd1b4, 0x7fb396 },   /* mint paint */
    { 'r', 0xf0b8b0, 0xe8a0a0, 0xc98585 },   /* rose paint */
};
#define NMATS ((int)(sizeof(MATS) / sizeof(MATS[0])))

static lv_obj_t *mk(lv_obj_t *parent, const char *map, int w, int h, int x, int y, bool outline)
{
#if CONFIG_MUSE_PIXEL_THEME
    lv_obj_t *o = muse_prop_create(parent, map, w, h, MATS, NMATS, WP, outline);
#else
    lv_obj_t *o = NULL;   /* props are drawn in the pixel look only */
#endif
    if (o) {
        lv_obj_set_pos(o, x, y);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    return o;
}

/* A work's props live in one box, placed from Muse's feet when it shows. */
static lv_obj_t *work_box(lv_obj_t *stage, int w, int h)
{
    lv_obj_t *b = plain(stage);
    lv_obj_set_size(b, w, h);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

static const char BOOK[] =
    ".pppppppp..pppppppp."
    "pppppppppssppppppppp"
    "pkkkkkpppsspkkkkkkpp"
    "pppppppppssppppppppp"
    "pkkkkppppssppkkkkkpp"
    "pppppppppssppppppppp"
    "pkkkkkkppssppkkkpppp"
    "pppppppppssppppppppp"
    "cccccccccssccccccccc"
    ".cccccccccccccccccc.";
static const char PAGE[] =
    "ppppppp"
    "pkkkkpp"
    "ppppppp"
    "pkkkppp"
    "ppppppp"
    "pkkkkkp"
    "ppppppp";
static const char LENS[] =
    "..bbbb.."
    ".bggggb."
    "bggggggb"
    "bggggggb"
    ".bggggb."
    "..bbbbw."
    "......ww"
    ".......w";
static const char TYPEWRITER[] =
    "....pppppppp...."
    "....pkkkkkpp...."
    "....pppppppp...."
    "..dddddddddddd.."
    ".mmmmmmmmmmmmmm."
    "mmmmmmmmmmmmmmmm"
    "mkmkmkmkmkmkmkmm"
    "mmkmkmkmkmkmkmmm"
    "mmmmmmmmmmmmmmmm"
    "mmmkkkkkkkkkkmmm"
    ".mmmmmmmmmmmmmm.";
static const char NOTE[] = "pp" "pk";
static const char EASEL[] =
    ".......ww......."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    ".pppppppppppppp."
    "wwwwwwwwwwwwwwww"
    ".......ww......."
    "...w...ww...w..."
    "...w...ww...w..."
    "..w....ww....w.."
    "..w....ww....w.."
    "..w....ww....w.."
    ".w.....ww.....w."
    ".w.....ww.....w."
    ".w.....ww.....w."
    "w......ww......w"
    "w......ww......w";
static const char DAB_V[] = "vvv" "vv.";
static const char DAB_Y[] = ".yy" "yyy" "yy.";
static const char DAB_N[] = "nnnn" ".nn.";
static const char DAB_R[] = "rr" "rr" "r.";
static const char CAMERA[] =
    "..mmmm......."
    "..mmmm...bb.."
    "mmmmmmmmmmmmm"
    "mmmmLLLLLmmmm"
    "mmmLLSLLLLmmm"
    "mmmLLLLLLLmmm"
    "mmmLLLLLLLmmm"
    "mmmmLLLLLmmmm"
    "mmmmmmmmmmmmm";

#define GLOBE_W 14
#define GLOBE_H 18
#define GLOBE_FRAMES 6
static char s_globe[GLOBE_FRAMES][GLOBE_W * GLOBE_H];
static lv_obj_t *s_globe_img[GLOBE_FRAMES];

/* A globe on a stand, its land turned `turn` cells round. */
static void globe_map(char *m, int turn)
{
    static const char LAND[] =       /* 24 around, 13 down */
        "........................"
        "...ll.........lll......."
        "..llll.......lllll......"
        "..lllll.......lll....ll."
        "...llll..............lll"
        "....ll......ll.......ll."
        ".....l.....llll........."
        "...........lllll........"
        "..ll........lll....ll..."
        ".llll.........l...llll.."
        "..ll...............ll..."
        "........................"
        "........................";
    memset(m, '.', GLOBE_W * GLOBE_H);
    for (int y = 0; y < 13; y++) {
        for (int x = 0; x < GLOBE_W; x++) {
            float dx = x + 0.5f - 7.0f, dy = y + 0.5f - 6.5f;
            if (dx * dx + dy * dy <= 6.3f * 6.3f) {
                m[y * GLOBE_W + x] = LAND[y * 24 + (x + turn) % 24] == 'l' ? 'l' : 'g';
            }
        }
    }
    for (int y = 13; y < 16; y++) {
        m[y * GLOBE_W + 6] = m[y * GLOBE_W + 7] = 'b';
    }
    for (int x = 3; x < 11; x++) {
        m[16 * GLOBE_W + x] = 'w';
        m[17 * GLOBE_W + x] = 'w';
    }
}

static void globe_frame(void *o, int32_t v)
{
    (void)o;
    for (int i = 0; i < GLOBE_FRAMES; i++) {
        lv_obj_set_flag(s_globe_img[i], LV_OBJ_FLAG_HIDDEN, i != v);
    }
}

static void build_work(lv_obj_t *stage)
{
    lv_obj_t *b, *o;

    /* Research: a big open book held in front, a page turning, a magnifier. */
    b = s_work[WORK_RESEARCH] = work_box(stage, 34 * WP, 20 * WP);
    mk(b, BOOK, 20, 10, 0, 8 * WP, true);
    o = mk(b, PAGE, 7, 7, 11 * WP, 9 * WP, true);
    if (o) {
        loop_anim(o, anim_x, 12 * WP, 2 * WP, 700, 600, true);
    }
    o = mk(b, LENS, 8, 8, 23 * WP, 0, true);
    if (o) {
        loop_anim(o, anim_y, 2 * WP, 0, 900, 0, true);
    }

    /* A web search: a globe on a stand beside Cosmo, turning. */
    b = s_work[WORK_SEARCH] = work_box(stage, (GLOBE_W + 2) * WP, (GLOBE_H + 2) * WP);
    for (int i = 0; i < GLOBE_FRAMES; i++) {
        globe_map(s_globe[i], i * 24 / GLOBE_FRAMES);
        s_globe_img[i] = mk(b, s_globe[i], GLOBE_W, GLOBE_H, 0, 0, true);
    }
    if (s_globe_img[0]) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, b);
        lv_anim_set_exec_cb(&a, globe_frame);
        lv_anim_set_values(&a, 0, GLOBE_FRAMES - 1);
        lv_anim_set_duration(&a, 1800);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }

    /* Writing back: a little typewriter, notes floating up out of it. */
    b = s_work[WORK_WRITE] = work_box(stage, 18 * WP, 30 * WP);
    mk(b, TYPEWRITER, 16, 11, 0, 17 * WP, true);
    for (int i = 0; i < 3; i++) {
        o = mk(b, NOTE, 2, 2, (6 + 3 * i) * WP, 16 * WP, true);
        if (o) {
            loop_anim(o, anim_y, 16 * WP, 0, 1400, i * 460, false);
            loop_anim(o, anim_opa, LV_OPA_COVER, LV_OPA_TRANSP, 1400, i * 460, false);
        }
    }

    /* An image: an easel beside Cosmo, dabs of paint coming onto it. */
    b = s_work[WORK_IMAGE] = work_box(stage, 18 * WP, 26 * WP);
    mk(b, EASEL, 16, 24, 0, 0, true);
    static const struct { const char *map; int w, h, x, y; } DABS[] = {
        { DAB_V, 3, 2, 3, 3 }, { DAB_Y, 3, 3, 8, 4 }, { DAB_N, 4, 2, 4, 8 }, { DAB_R, 2, 3, 11, 8 },
    };
    for (int i = 0; i < 4; i++) {
        o = mk(b, DABS[i].map, DABS[i].w, DABS[i].h, DABS[i].x * WP, DABS[i].y * WP, false);
        if (o) {
            loop_anim(o, anim_opa, LV_OPA_TRANSP, LV_OPA_COVER, 500, i * 700, true);
        }
    }

    /* A photo: a camera held up to the face, and a soft flash over the stage. */
    b = s_work[WORK_CAMERA] = work_box(stage, 15 * WP, 11 * WP);
    mk(b, CAMERA, 13, 9, 0, 0, true);
    s_flash = rect(stage, 0, 0, lv_obj_get_width(stage), lv_obj_get_height(stage), C_CREAM);
    lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_opa(s_flash, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
}

/* Where each prop goes, from Muse's feet (at home: Muse is home while working).
 * Things that stand do so on the island; things held are at Muse's paws. */
static void place_work(work_t w, int fx, int fy)
{
    lv_obj_t *p = s_work[w];
    switch (w) {
    case WORK_RESEARCH: lv_obj_set_pos(p, fx - 11 * WP, fy - 28 * WP); break;
    case WORK_SEARCH:   lv_obj_set_pos(p, fx + 21 * WP, fy - (GLOBE_H + 1) * WP); break;
    case WORK_WRITE:    lv_obj_set_pos(p, fx + 10 * WP, fy - 28 * WP); break;
    case WORK_IMAGE:    lv_obj_set_pos(p, fx - 40 * WP, fy - 25 * WP); break;
    case WORK_CAMERA:   lv_obj_set_pos(p, fx - 7 * WP - WP / 2, fy - 40 * WP); break;
    default: break;
    }
}

static void flash_done(lv_anim_t *a)
{
    lv_obj_add_flag(a->var, LV_OBJ_FLAG_HIDDEN);
}

static void work_update(muse_mode_t mode)
{
    work_t w = s_flash ? work_kind(mode) : WORK_NONE;   /* no props built: none */
    if (w == s_work_now) {
        return;
    }
    if (s_work_now != WORK_NONE) {
        lv_obj_add_flag(s_work[s_work_now], LV_OBJ_FLAG_HIDDEN);
    }
    s_work_now = w;
    s_working = w != WORK_NONE;
    if (w == WORK_NONE) {
        return;
    }
    int fx, fy;
    if (muse_ui_feet(&fx, &fy)) {
        place_work(w, fx - lv_obj_get_x(s_stage), fy);
    }
    lv_obj_remove_flag(s_work[w], LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_work[w]);
    if (w == WORK_CAMERA) {
        /* One flash as the photo is taken. */
        lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_flash);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_flash);
        lv_anim_set_exec_cb(&a, anim_opa);
        lv_anim_set_values(&a, LV_OPA_80, LV_OPA_TRANSP);
        lv_anim_set_duration(&a, 450);
        lv_anim_set_completed_cb(&a, flash_done);
        lv_anim_start(&a);
    }
}

/* ---- Console: show the dock's states without touching the screen ---------- */

static const char *const DEMO[][2] = {
    { "m", "good evening! the stars are extra sparkly tonight." },
    { "u", "what should I make for dinner?" },
    { "m", "ooh. something warm and noodly? you have eggs and scallions, and a reply long enough to wrap over several lines to see where the bubble ends." },
    { "u", "ramen it is" },
};

/* "dock=pocket", "dock=full", "dock=demo" (sample bubbles), "dock=osk": in the
 * LVGL task, from muse_input's console. Prints @dock with what it did. */
void muse_dock_command(const char *what)
{
    if (!s_stage) {
        printf("@dock.error no dock\n");
    } else if (!strcmp(what, "pocket")) {
        pocket_show(lv_obj_has_flag(s_pocket, LV_OBJ_FLAG_HIDDEN));
        printf("@dock {\"pocket\":%s}\n", lv_obj_has_flag(s_pocket, LV_OBJ_FLAG_HIDDEN) ? "false" : "true");
    } else if (!strcmp(what, "full")) {
        set_full(!s_full);
        printf("@dock {\"full\":%s}\n", s_full ? "true" : "false");
    } else if (!strcmp(what, "demo")) {
        for (size_t i = 0; i < sizeof(DEMO) / sizeof(DEMO[0]); i++) {
            if (DEMO[i][0][0] == 'm') {
                muse_bubble(DEMO[i][1]);
            } else {
                my_bubble(DEMO[i][1]);
            }
        }
        printf("@dock {\"demo\":%u}\n", (unsigned)(sizeof(DEMO) / sizeof(DEMO[0])));
    } else if (!strcmp(what, "walk")) {
        plan_walk();
        printf("@dock {\"walk\":%d}\n", s_w.legs_n);
    } else if (!strcmp(what, "turn")) {
        hold(3.1416f, 40, "~ stargazing ~");   /* from behind */
        printf("@dock {\"turn\":true}\n");
    } else if (!strcmp(what, "think")) {
        hold(0, 60, THOUGHT_MOOD[THOUGHT_RAMEN]);
        thought_show(THOUGHT_RAMEN);
        printf("@dock {\"think\":true}\n");
    } else if (!strncmp(what, "work=", 5)) {
        static const char *const NAMES[WORKS] = { "none", "research", "search", "write", "image", "camera" };
        s_work_force = -1;
        for (int i = 0; i < WORKS; i++) {
            if (!strcmp(what + 5, NAMES[i])) {
                s_work_force = i == WORK_NONE ? -1 : i;
            }
        }
        if (s_work_force >= 0) {
            wander_home();
        }
        work_update(MUSE_MODE_IDLE);
        printf("@dock {\"work\":\"%s\"}\n", NAMES[s_work_now]);
    } else if (!strncmp(what, "settings", 8)) {
        /* "settings" toggles the window; "settings=wifi" opens it at a page. */
        bool open = what[8] == '=' || lv_obj_has_flag(s_setwin, LV_OBJ_FLAG_HIDDEN);
        settings_show(open);
        bool ok = !open || what[8] != '=' || muse_settings_ui_open(what + 9);
        printf("@dock {\"settings\":%s,\"page\":%s}\n", open ? "true" : "false", ok ? "true" : "false");
    } else if (!strcmp(what, "osk")) {
        lv_obj_set_flag(s_osk, LV_OBJ_FLAG_HIDDEN, !lv_obj_has_flag(s_osk, LV_OBJ_FLAG_HIDDEN));
        printf("@dock {\"osk\":%s}\n", lv_obj_has_flag(s_osk, LV_OBJ_FLAG_HIDDEN) ? "false" : "true");
    } else {
        printf("@dock.error want dock=pocket, full, demo, osk, walk, turn, think, work=... or settings[=page]\n");
    }
    fflush(stdout);
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
    /* Settings' text page (a Wi-Fi password) takes the keys while it's open. */
    if (muse_settings_ui_key(key)) {
        return;
    }
    if (s_full && key != LV_KEY_ESC) {
        set_full(false);     /* typing brings the chat back */
    }
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
    case LV_KEY_DEL:
        lv_textarea_delete_char_forward(s_input);
        break;
    case LV_KEY_ESC:
        lv_textarea_set_text(s_input, "");
        lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
        pocket_show(false);
        settings_show(false);
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
                    wander_peek();
                    s_reply = muse_bubble(it.text);
                } else {
                    bubble_append(s_reply, it.text);
                }
            }
            break;
        case CHAT_FINAL:
            if (it.text) {
                if (!s_reply) {
                    s_reply = muse_bubble(it.text);
                } else {
                    set_chat_text(s_reply, it.text);
                    fit_bubble(s_reply);
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
            error_bubble(it.text ? it.text : "Error");
            s_reply = NULL;
            s_busy = false;
            break;
        case CHAT_SENT:
            break;
        case CHAT_ACTIVITY:
            strlcpy(s_activity, it.text ? it.text : "", sizeof(s_activity));
            break;
        }
        if (it.kind == CHAT_TEXT || it.kind == CHAT_FINAL) {
            s_text_at = esp_timer_get_time();   /* a reply streaming in: Cosmo types */
        }
        if (it.kind == CHAT_DONE || it.kind == CHAT_ERROR) {
            s_activity[0] = '\0';
        }
        free(it.text);
    }
}

/* Voice turns: Muse's spoken reply, a caption page at a time. */
static void follow_voice(muse_mode_t mode)
{
    if (mode == MUSE_MODE_LISTENING && s_last_mode != MUSE_MODE_LISTENING) {
        lv_obj_t *b = my_bubble("voice note");
        lv_obj_set_style_text_color(b, lv_color_hex(C_DIM), 0);
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
                s_spoken = muse_bubble(cap);
            } else {
                /* A caption that grew or slid along the reply repeats what the
                 * bubble already ends with: add only what's new. */
                size_t k = muse_text_overlap(lv_label_get_text(s_spoken), cap, SPOKEN_OVERLAP_MIN);
                if (cap[k]) {
                    if (!k) {
                        bubble_append(s_spoken, " ");
                    }
                    bubble_append(s_spoken, cap + k);
                }
            }
        }
    }
    if (mode == MUSE_MODE_IDLE) {
        s_spoken = NULL;
    }
    s_last_mode = mode;
}

/* The talk button follows what Muse is doing. */
static void show_talk(muse_mode_t mode)
{
    const char *text;
    uint32_t fill, ink;
    switch (mode) {
    case MUSE_MODE_LISTENING: text = "LISTENING..."; fill = C_MINT; ink = C_INK; break;
    case MUSE_MODE_THINKING:  text = "THINKING...";  fill = C_TILE_DOWN; ink = C_LAV_SOFT; break;
    case MUSE_MODE_SPEAKING:  text = "SPEAKING...";  fill = C_TILE_DOWN; ink = C_LAV_SOFT; break;
    case MUSE_MODE_ERROR:     text = "OOPS";         fill = C_RED; ink = C_CREAM; break;
    default:                  text = "HOLD TO TALK"; fill = C_LAV; ink = C_INK; break;
    }
    if (strcmp(lv_label_get_text(s_talk_label), text)) {
        lv_label_set_text(s_talk_label, text);
        lv_obj_set_style_text_color(s_talk_label, lv_color_hex(ink), 0);
        lv_obj_set_style_bg_color(s_talk, lv_color_hex(fill), 0);
    }
}

/* Under Muse's name: what Muse is up to, in a few words. */
static void show_mood(muse_mode_t mode)
{
    muse_link_state_t link = muse_link_state();
    const char *mood;
    if (link == MUSE_LINK_UNPAIRED || link == MUSE_LINK_PAIRING || link == MUSE_LINK_CONFIRM) {
        mood = "~ waiting to meet you ~";
    } else if (link == MUSE_LINK_OFFLINE || link == MUSE_LINK_ERROR) {
        mood = "~ can't reach muse ~";
    } else if (s_work_now != WORK_NONE) {
        mood = WORK_MOOD[s_work_now];
    } else if (mode == MUSE_MODE_LISTENING) {
        mood = "~ all ears ~";
    } else if (mode == MUSE_MODE_THINKING || s_busy) {
        mood = "~ working on it ~";
    } else if (mode == MUSE_MODE_SPEAKING) {
        mood = "~ chatting ~";
    } else if (mode == MUSE_MODE_ERROR) {
        mood = "~ oops ~";
    } else if (link == MUSE_LINK_CONNECTING || link == MUSE_LINK_BOOT) {
        mood = "~ waking up ~";
    } else if (s_w.mood) {
        mood = s_w.mood;
    } else {
        mood = "~ daydreaming ~";
    }
    if (strcmp(lv_label_get_text(s_mood), mood)) {
        lv_label_set_text(s_mood, mood);
    }
}

static void update_status(void)
{
    muse_link_state_t link = muse_link_state();
    bool online = link == MUSE_LINK_ONLINE;
    bool bad = link == MUSE_LINK_ERROR || link == MUSE_LINK_OFFLINE;
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(online ? C_MINT : bad ? C_RED : C_SUN), 0);
    lv_label_set_text(s_link, online ? "online"
                    : bad ? "offline"
                    : link == MUSE_LINK_UNPAIRED || link == MUSE_LINK_PAIRING || link == MUSE_LINK_CONFIRM ? "pairing"
                    : "connecting");

    muse_wifi_status_t w;
    muse_wifi_status(&w);
    int lit = w.state != MUSE_WIFI_CONNECTED ? 0 : w.rssi >= -55 ? 4 : w.rssi >= -65 ? 3 : w.rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_bars[i], lv_color_hex(i < lit ? C_TEXT : C_EDGE_BTN), 0);
    }

    muse_power_t p = muse_state_power();
    if (p.battery_pct < 0) {
        lv_label_set_text(s_batt_pct, p.usb ? "usb" : "--");
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(s_cells[i], lv_color_hex(C_METER_OFF), 0);
        }
    } else {
        int cells = p.battery_pct > 66 ? 3 : p.battery_pct > 33 ? 2 : p.battery_pct > 5 ? 1 : 0;
        uint32_t c = p.charging ? C_SUN : cells <= 1 ? C_RED : C_MINT;
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(s_cells[i], lv_color_hex(i < cells ? c : C_METER_OFF), 0);
        }
        lv_label_set_text_fmt(s_batt_pct, "%d%%", p.battery_pct);
    }

    bool kb = muse_board->keyboard_present && muse_board->keyboard_present();
    if (kb == lv_obj_has_flag(s_kb_icon, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flag(s_kb_icon, LV_OBJ_FLAG_HIDDEN, !kb);
    }

    /* The pocket keeps the details the status strip leaves out. */
    if (!lv_obj_has_flag(s_pocket, LV_OBJ_FLAG_HIDDEN)) {
        char batt[32];
        if (p.battery_pct < 0) {
            strlcpy(batt, "no battery reading", sizeof(batt));
        } else {
            snprintf(batt, sizeof(batt), "battery %d%%%s", p.battery_pct, p.charging ? " charging" : "");
        }
        if (w.state == MUSE_WIFI_CONNECTED) {
            lv_label_set_text_fmt(s_pocket_info, "wi-fi %s  %d dBm\n%s%s", w.ssid, w.rssi, batt,
                                  p.usb ? "  usb" : "");
        } else {
            lv_label_set_text_fmt(s_pocket_info, "wi-fi %s\n%s%s", w.detail[0] ? w.detail : "not connected",
                                  batt, p.usb ? "  usb" : "");
        }
    }
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
    show_talk(mode);
    work_update(mode);
    show_mood(mode);
    follow_camera();
    if (s_setwin && !lv_obj_has_flag(s_setwin, LV_OBJ_FLAG_HIDDEN)) {
        muse_settings_ui_tick(true);
    }
    if (n++ % STATUS_EVERY == 0) {
        update_status();
        if (!lv_obj_has_flag(s_pocket, LV_OBJ_FLAG_HIDDEN)) {
            show_meter(s_vol_cells, s_vol_value, muse_settings_volume(), C_LAV);
            show_meter(s_bri_cells, s_bri_value, muse_settings_brightness(), C_SUN);
        }
    }
}

/* ---- Layout --------------------------------------------------------------- */

static void build_status(lv_obj_t *parent)
{
    lv_obj_t *hud = pixel_box(parent, 24, 20, 260, 48, C_PANEL, C_EDGE, C_PANEL, C_NIGHT);
    lv_obj_set_flex_flow(hud, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hud, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hud, 12, 0);
    lv_obj_set_style_pad_column(hud, 12, 0);

    s_dot = plain(hud);
    lv_obj_set_size(s_dot, 10, 10);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    s_link = label(hud, F_LABEL, C_TEXT);

    /* Wi-Fi: four bars, lit by signal. */
    lv_obj_t *wifi = icon_box(hud, 13, 10, 2);
    static const cell_t bars[4] = { { 0, 7, 2, 3 }, { 3, 5, 2, 5 }, { 6, 3, 2, 7 }, { 9, 0, 2, 10 } };
    for (int i = 0; i < 4; i++) {
        s_bars[i] = rect(wifi, bars[i].x * 2, bars[i].y * 2, bars[i].w * 2, bars[i].h * 2, C_EDGE_BTN);
    }

    /* Battery: an outline with three cells. */
    lv_obj_t *batt = icon_box(hud, 17, 9, 2);
    icon_cells(batt, ICON({ 0, 0, 15, 9 }, { 15, 3, 2, 3 }), 2, C_TEXT);
    icon_cells(batt, ICON({ 1, 1, 13, 7 }), 2, C_PANEL);
    for (int i = 0; i < 3; i++) {
        s_cells[i] = rect(batt, (2 + 4 * i) * 2, 2 * 2, 3 * 2, 5 * 2, C_METER_OFF);
    }
    s_batt_pct = label(hud, F_LABEL, C_TEXT);

    s_kb_icon = icon_box(hud, 11, 9, 2);
    icon_cells(s_kb_icon, ICON({ 0, 1, 11, 7 }), 2, C_TEXT);
    icon_cells(s_kb_icon, ICON({ 1, 2, 9, 5 }), 2, C_PANEL);
    icon_cells(s_kb_icon, ICON({ 2, 3, 1, 1 }, { 4, 3, 1, 1 }, { 6, 3, 1, 1 }, { 8, 3, 1, 1 }, { 3, 5, 5, 1 }), 2,
               C_TEXT);
    lv_obj_add_flag(s_kb_icon, LV_OBJ_FLAG_HIDDEN);
}

static void build_stage(lv_obj_t *scr, int ui_x, int ui_y, int ui_w, int ui_h)
{
    /* Over Muse's own area and the row of buttons under it, on the screen so
     * it's above Muse's layers; it lets touches through except on its buttons.
     * ui_h is the stage's whole height: Muse's area ends above the buttons. */
    s_stage = plain(scr);
    lv_obj_set_pos(s_stage, ui_x, ui_y);
    lv_obj_set_size(s_stage, ui_w, ui_h);

    build_status(s_stage);

    lv_obj_t *name = plain(s_stage);
    lv_obj_set_pos(name, 28, 82);
    lv_obj_set_size(name, ui_w - 56, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(name, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(name, 14, 0);
    lv_obj_t *title = label(name, F_TITLE, C_CREAM);
    lv_label_set_text(title, "COSMO");
    lv_obj_set_style_text_letter_space(title, 2, 0);
    s_mood = label(name, F_LABEL, C_DIM);
    lv_obj_set_style_pad_bottom(s_mood, 4, 0);

    /* Full screen, top right of the stage. */
    s_full_btn = pixel_button(s_stage, ui_w - 24 - 56, 20, 56, 56, C_TILE, C_EDGE_BTN, C_TILE_DOWN, C_NIGHT);
    lv_obj_t *expand = icon_box(s_full_btn, 7, 7, 4);
    icon_cells(expand, ICON({ 0, 0, 3, 1 }, { 0, 0, 1, 3 }, { 4, 0, 3, 1 }, { 6, 0, 1, 3 }, { 0, 6, 3, 1 },
                            { 0, 4, 1, 3 }, { 4, 6, 3, 1 }, { 6, 4, 1, 3 }), 4, C_CREAM);
    lv_obj_add_event_cb(s_full_btn, on_full, LV_EVENT_CLICKED, NULL);

    /* Hold to talk, under Muse. */
    s_talk = pixel_button(s_stage, (ui_w - 300) / 2, ui_h - 24 - 92, 300, 92, C_LAV, C_INK, C_LAV_LO, C_NIGHT);
    lv_obj_t *mic = icon_box(s_talk, 7, 9, 4);
    icon_cells(mic, ICON({ 2, 0, 3, 5 }, { 1, 3, 1, 2 }, { 5, 3, 1, 2 }, { 2, 5, 3, 1 }, { 3, 6, 1, 2 },
                         { 1, 8, 5, 1 }), 4, C_INK);
    s_talk_label = label(s_talk, F_TITLE, C_INK);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_talk, on_talk, LV_EVENT_PRESS_LOST, NULL);

    /* The pocket, bottom left. */
    lv_obj_t *pocket = pixel_button(s_stage, 28, ui_h - 24 - 80, 80, 80, C_TILE, C_EDGE_BTN, C_TILE_DOWN, C_NIGHT);
    lv_obj_t *star = icon_box(pocket, 9, 9, 4);
    icon_cells(star, ICON({ 4, 0, 1, 9 }, { 0, 4, 9, 1 }, { 3, 3, 3, 3 }), 4, C_CREAM);
    icon_cells(star, ICON({ 4, 4, 1, 1 }), 4, C_LAV);
    lv_obj_add_event_cb(pocket, on_pocket, LV_EVENT_CLICKED, NULL);

    if (muse_board->camera_enabled) {
        /* Top middle of the stage while a photo is being taken. */
        s_camera_notice = pixel_box(s_stage, 300, 20, 220, 48, C_RED, C_RED_LO, C_RED, C_NIGHT);
        lv_obj_set_flex_flow(s_camera_notice, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_camera_notice, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(s_camera_notice, 10, 0);
        lv_obj_t *eye = icon_box(s_camera_notice, 9, 6, 3);
        icon_cells(eye, ICON({ 2, 0, 5, 1 }, { 1, 1, 7, 4 }, { 0, 2, 9, 2 }, { 2, 5, 5, 1 }), 3, C_CREAM);
        icon_cells(eye, ICON({ 3, 2, 3, 2 }), 3, C_RED);
        icon_cells(eye, ICON({ 4, 2, 1, 1 }), 3, C_INK);
        lv_obj_t *l = label(s_camera_notice, F_LABEL, C_CREAM);
        lv_label_set_text(l, "snap! photo");
        lv_obj_add_flag(s_camera_notice, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_chat(lv_obj_t *scr, int x, int y, int w, int h)
{
    s_chat = pixel_box(scr, x, y, w, h, C_PANEL, C_EDGE, C_PANEL, C_NIGHT);

    lv_obj_t *bar = plain(s_chat);
    lv_obj_set_size(bar, lv_pct(100), 44);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_TITLE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(C_EDGE), 0);
    lv_obj_set_style_border_width(bar, PX, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_t *star_l = label(bar, F_LABEL, C_SUN);
    lv_label_set_text(star_l, "*");
    lv_obj_t *t = label(bar, F_LABEL, C_TEXT);
    lv_label_set_text(t, "chatting with cosmo");
    lv_obj_t *star_r = label(bar, F_LABEL, C_SUN);
    lv_label_set_text(star_r, "*");

    int inner_w = w - 2 * PX, inner_h = h - 2 * PX;
    int field_h = 56, row_y = inner_h - 16 - field_h;
    s_log = plain(s_chat);
    /* Touch must reach it for a drag to scroll it (bubbles let touches through). */
    lv_obj_add_flag(s_log, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_pos(s_log, 16, 44 + 16);
    lv_obj_set_size(s_log, inner_w - 32, row_y - 16 - (44 + 16));
    lv_obj_set_flex_flow(s_log, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_log, 14, 0);
    lv_obj_set_style_pad_right(s_log, 18, 0);
    lv_obj_set_scroll_dir(s_log, LV_DIR_VER);
    /* A chunky pixel scrollbar, shown whenever there's more to scroll to. */
    lv_obj_set_scrollbar_mode(s_log, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_bg_color(s_log, lv_color_hex(C_LAV), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_log, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_log, 2 * PX, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(s_log, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(s_log, 0, LV_PART_SCROLLBAR);

    int btn = 56, gap = 14;
#if CONFIG_MUSE_EMOJI_FONT
    int field_w = inner_w - 32 - 2 * (btn + gap);
#else
    int field_w = inner_w - 32 - (btn + gap);
#endif
    lv_obj_t *field = pixel_box(s_chat, 16, row_y, field_w, field_h, C_NIGHT, C_EDGE_BTN, C_NIGHT, C_PANEL);
    s_input = lv_textarea_create(field);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_placeholder_text(s_input, "say something nice...");
    lv_textarea_set_max_length(s_input, INPUT_MAX);
    lv_obj_set_size(s_input, lv_pct(100), lv_pct(100));
    lv_obj_set_style_text_font(s_input, chat_font(), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(C_CREAM), 0);
    lv_obj_set_style_text_color(s_input, lv_color_hex(C_FAINT), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_opa(s_input, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_input, 0, 0);
    lv_obj_set_style_border_width(s_input, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(s_input, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(s_input, 0, 0);
    lv_obj_set_style_pad_hor(s_input, 12, 0);
    lv_obj_set_style_pad_ver(s_input, 12, 0);
    /* A thick lavender bar of a cursor, before the character rather than over it. */
    lv_obj_set_style_bg_opa(s_input, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_border_color(s_input, lv_color_hex(C_LAV), LV_PART_CURSOR);
    lv_obj_set_style_border_width(s_input, 3, LV_PART_CURSOR);
    lv_obj_set_style_border_side(s_input, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
    lv_obj_add_event_cb(s_input, on_input_focus, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_input, on_input_focus, LV_EVENT_CLICKED, NULL);

    int bx = 16 + field_w + gap;
#if CONFIG_MUSE_EMOJI_FONT
    lv_obj_t *pick = pixel_button(s_chat, bx, row_y, btn, btn, C_TILE, C_EDGE_BTN, C_TILE_DOWN, C_PANEL);
    lv_obj_t *smile = icon_box(pick, 7, 7, 4);
    icon_cells(smile, ICON({ 1, 0, 5, 7 }, { 0, 1, 7, 5 }), 4, C_SUN);
    icon_cells(smile, ICON({ 2, 2, 1, 1 }, { 4, 2, 1, 1 }, { 2, 4, 3, 1 }, { 1, 3, 1, 1 }, { 5, 3, 1, 1 }), 4, C_INK);
    lv_obj_add_event_cb(pick, on_emoji_button, LV_EVENT_CLICKED, NULL);
    bx += btn + gap;
    s_emoji = lv_buttonmatrix_create(s_chat);
    lv_buttonmatrix_set_map(s_emoji, EMOJI_MAP);
    lv_obj_set_size(s_emoji, inner_w - 32, 132);
    lv_obj_set_pos(s_emoji, 16, row_y - 12 - 132);
    lv_obj_set_style_text_font(s_emoji, chat_font(), 0);
    lv_obj_set_style_radius(s_emoji, 0, 0);
    lv_obj_set_style_bg_color(s_emoji, lv_color_hex(C_NIGHT), 0);
    lv_obj_set_style_border_color(s_emoji, lv_color_hex(C_LAV), 0);
    lv_obj_set_style_border_width(s_emoji, PX, 0);
    lv_obj_set_style_radius(s_emoji, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_emoji, lv_color_hex(C_TILE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_emoji, lv_color_hex(C_CREAM), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(s_emoji, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_emoji, on_emoji_pick, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_flag(s_emoji, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_t *send = pixel_button(s_chat, bx, row_y, btn, btn, C_LAV, C_INK, C_LAV_LO, C_PANEL);
    lv_obj_t *arrow = icon_box(send, 7, 7, 4);
    icon_cells(arrow, ICON({ 0, 3, 5, 1 }, { 3, 1, 1, 5 }, { 4, 2, 1, 3 }, { 5, 3, 1, 1 }), 4, C_INK);
    lv_obj_add_event_cb(send, on_send, LV_EVENT_CLICKED, NULL);
}

static void meter(lv_obj_t *parent, int x, int y, int w, const char *name, lv_obj_t **cells, lv_obj_t **value,
                  lv_event_cb_t cb)
{
    lv_obj_t *l = label(parent, F_LABEL, C_TEXT);
    lv_label_set_text(l, name);
    lv_obj_set_pos(l, x, y);
    *value = label(parent, F_LABEL, C_TEXT);
    lv_obj_align(*value, LV_ALIGN_TOP_RIGHT, -(x), y);
    int gap = 6, cw = (w - (METER_CELLS - 1) * gap) / METER_CELLS;
    for (int i = 0; i < METER_CELLS; i++) {
        lv_obj_t *c = plain(parent);
        lv_obj_set_pos(c, x + i * (cw + gap), y + 28);
        lv_obj_set_size(c, cw, 40);
        lv_obj_set_style_bg_color(c, lv_color_hex(C_METER_OFF), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(c, gap / 2 + 1);
        lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        cells[i] = c;
    }
}

/* A tile's two lines live in the last two children: its name and a note. */
static void tile_text(lv_obj_t *tile, const char *name, const char *sub, uint32_t color)
{
    int n = (int)lv_obj_get_child_count(tile);
    lv_obj_t *a = lv_obj_get_child(tile, n - 2), *b = lv_obj_get_child(tile, n - 1);
    lv_label_set_text(a, name);
    lv_label_set_text(b, sub);
    lv_obj_set_style_text_color(a, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(color), 0);
}

static lv_obj_t *tile(lv_obj_t *parent, int x, int y, int w, int h, const char *name, const char *sub,
                      const cell_t *art, size_t n, uint32_t fill, uint32_t ink, uint32_t edge)
{
    lv_obj_t *t = pixel_button(parent, x, y, w, h, fill, edge, fill == C_TILE ? C_TILE_DOWN : C_RED_LO, C_PANEL);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(t, 6, 0);
    lv_obj_t *box = icon_box(t, 9, 9, 4);
    icon_cells(box, art, n, 4, ink);
    lv_obj_t *a = label(t, F_BODY, ink);
    lv_obj_t *b = label(t, F_LABEL, ink);
    (void)a;
    (void)b;
    tile_text(t, name, sub, ink);
    return t;
}

static void build_pocket(lv_obj_t *scr, int x, int y, int w, int h)
{
    s_pocket = pixel_box(scr, x, y, w, h, C_PANEL, C_LAV, C_PANEL, C_NIGHT);
    lv_obj_add_flag(s_pocket, LV_OBJ_FLAG_CLICKABLE);   /* touches stop here, not on the chat under it */
    int inner_w = w - 2 * PX;
    int pad = 26;

    lv_obj_t *title = label(s_pocket, F_TITLE, C_CREAM);
    lv_label_set_text(title, "cosmo's pocket");
    lv_obj_set_pos(title, pad, 20);
    lv_obj_t *close = pixel_button(s_pocket, inner_w - pad - 56, 16, 56, 56, C_TILE, C_EDGE_BTN, C_TILE_DOWN,
                                   C_PANEL);
    lv_obj_t *x_art = icon_box(close, 6, 6, 4);
    icon_cells(x_art, ICON({ 0, 0, 2, 2 }, { 4, 0, 2, 2 }, { 2, 2, 2, 2 }, { 0, 4, 2, 2 }, { 4, 4, 2, 2 }), 4,
               C_CREAM);
    lv_obj_add_event_cb(close, on_pocket, LV_EVENT_CLICKED, NULL);

    s_pocket_info = label(s_pocket, F_LABEL, C_DIM);
    lv_obj_set_pos(s_pocket_info, pad, 84);
    lv_obj_set_style_text_line_space(s_pocket_info, 6, 0);

    int mw = inner_w - 2 * pad;
    meter(s_pocket, pad, 150, mw, "volume", s_vol_cells, &s_vol_value, on_volume_cell);
    meter(s_pocket, pad, 240, mw, "brightness", s_bri_cells, &s_bri_value, on_brightness_cell);

    int gap = 18, tw = (mw - 2 * gap) / 3, th = 128, ty = 336;
    lv_obj_t *t;
    t = tile(s_pocket, pad, ty, tw, th, "nap", "screen off",
             ICON({ 3, 1, 3, 1 }, { 2, 2, 2, 1 }, { 1, 3, 2, 3 }, { 2, 6, 2, 1 }, { 3, 7, 4, 1 }, { 6, 6, 2, 1 }),
             C_TILE, C_TEXT, C_EDGE_BTN);
    lv_obj_add_event_cb(t, on_nap, LV_EVENT_CLICKED, NULL);
    t = tile(s_pocket, pad + tw + gap, ty, tw, th, "flip", "upside down",
             ICON({ 1, 1, 6, 1 }, { 6, 0, 1, 3 }, { 2, 7, 6, 1 }, { 2, 6, 1, 3 }, { 1, 2, 1, 3 }, { 7, 4, 1, 3 }),
             C_TILE, C_TEXT, C_EDGE_BTN);
    lv_obj_add_event_cb(t, on_flip, LV_EVENT_CLICKED, NULL);
    if (!muse_board->flip_display) {
        lv_obj_add_state(t, LV_STATE_DISABLED);
    }
    if (muse_board->camera_enabled) {
        s_camera = tile(s_pocket, pad + 2 * (tw + gap), ty, tw, th, "camera", "off",
                        ICON({ 2, 2, 5, 1 }, { 1, 3, 7, 3 }, { 2, 6, 5, 1 }, { 0, 4, 9, 1 }), C_TILE, C_TEXT,
                        C_EDGE_BTN);
        lv_obj_add_event_cb(s_camera, on_camera, LV_EVENT_CLICKED, NULL);
        show_camera_state();
    }
    ty += th + gap;
    t = tile(s_pocket, pad, ty, tw, th, "settings", "and pairing",
             ICON({ 3, 0, 3, 9 }, { 0, 3, 9, 3 }, { 1, 1, 7, 7 }), C_TILE, C_TEXT, C_EDGE_BTN);
    lv_obj_add_event_cb(t, on_settings, LV_EVENT_CLICKED, NULL);
    t = tile(s_pocket, pad + tw + gap, ty, tw, th, "full screen", "just cosmo",
             ICON({ 0, 0, 3, 1 }, { 0, 0, 1, 3 }, { 6, 0, 3, 1 }, { 8, 0, 1, 3 }, { 0, 8, 3, 1 }, { 0, 6, 1, 3 },
                  { 6, 8, 3, 1 }, { 8, 6, 1, 3 }), C_TILE, C_TEXT, C_EDGE_BTN);
    lv_obj_add_event_cb(t, on_full, LV_EVENT_CLICKED, NULL);
    t = tile(s_pocket, pad + 2 * (tw + gap), ty, tw, th, "power", "hold down",
             ICON({ 4, 0, 1, 4 }, { 1, 2, 1, 5 }, { 7, 2, 1, 5 }, { 2, 7, 5, 1 }), 0x3a1420, 0xff8a96, C_RED_LO);
    lv_obj_add_event_cb(t, on_power, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(t, on_power, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(t, on_power, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_add_flag(s_pocket, LV_OBJ_FLAG_HIDDEN);
}

/* The settings pages fill the window; the close button stays over them. */
static void build_settings(lv_obj_t *scr, int x, int y, int w, int h)
{
    s_setwin = pixel_box(scr, x, y, w, h, C_PANEL, C_LAV, C_PANEL, C_NIGHT);
    lv_obj_add_flag(s_setwin, LV_OBJ_FLAG_CLICKABLE);   /* touches stop here, not on the chat under it */
    int inner_w = w - 2 * PX;
    lv_obj_t *body = plain(s_setwin);
    lv_obj_set_size(body, inner_w, h - 2 * PX);
    muse_settings_ui_build(body);
    lv_obj_t *close = pixel_button(s_setwin, inner_w - 26 - 56, 16, 56, 56, C_TILE, C_EDGE_BTN, C_TILE_DOWN,
                                   C_PANEL);
    lv_obj_t *x_art = icon_box(close, 6, 6, 4);
    icon_cells(x_art, ICON({ 0, 0, 2, 2 }, { 4, 0, 2, 2 }, { 2, 2, 2, 2 }, { 0, 4, 2, 2 }, { 4, 4, 2, 2 }), 4,
               C_CREAM);
    lv_obj_add_event_cb(close, on_settings_close, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_setwin, LV_OBJ_FLAG_HIDDEN);
}

static void build_keyboard(lv_obj_t *scr, int x, int y, int w, int h)
{
    s_osk = lv_keyboard_create(scr);
    /* lv_keyboard_create() aligns it bottom-centre, which would make a
     * position an offset from there and put it off the screen. */
    lv_obj_align(s_osk, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_size(s_osk, w, h);
    lv_keyboard_set_textarea(s_osk, s_input);
    lv_obj_set_style_radius(s_osk, 0, 0);
    lv_obj_set_style_bg_color(s_osk, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_color(s_osk, lv_color_hex(C_LAV), 0);
    lv_obj_set_style_border_width(s_osk, PX, 0);
    lv_obj_set_style_radius(s_osk, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_osk, lv_color_hex(C_TILE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_osk, lv_color_hex(C_LAV), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(s_osk, lv_color_hex(C_CREAM), LV_PART_ITEMS);
    /* Its control keys (backspace, enter, arrows) are LVGL symbols, which only
     * Montserrat has: the key font falls back to it. */
    static lv_font_t key_font;
    key_font = *F_BODY;
    key_font.fallback = &lv_font_montserrat_16;
    lv_obj_set_style_text_font(s_osk, &key_font, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_osk, lv_color_hex(C_EDGE_BTN), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(s_osk, lv_color_hex(C_LAV_SOFT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(s_osk, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_osk, on_osk, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_osk, on_osk, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
}

void muse_dock_build(lv_obj_t *scr, int ui_x, int ui_y, int ui_w, int ui_h)
{
    lv_display_t *disp = lv_obj_get_display(scr);
    int sw = lv_display_get_horizontal_resolution(disp);
    int sh = lv_display_get_vertical_resolution(disp);
    s_ring_lock = xSemaphoreCreateMutex();
    s_keys = xQueueCreate(KEY_QUEUE, sizeof(uint32_t));
    s_ui_x = ui_x;
    s_ui_y = ui_y;
    s_ui_w = ui_w;
    s_screen_w = sw;

    /* The chat beside Muse's stage (the Tab5's is on the left). */
    int col_x = ui_x + ui_w + 24, col_w = sw - col_x - 28;
    if (col_w < 300) {
        ESP_LOGW(TAG, "no room for the dock (%dx%d beside %dx%d)", sw, sh, ui_w, ui_h);
        return;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_NIGHT), 0);
#if CONFIG_MUSE_PIXEL_THEME
    muse_scene_build(scr);
#endif

    build_stage(scr, ui_x, ui_y, ui_w, sh - ui_y);
    build_chat(scr, col_x, 24, col_w, sh - 48);
    build_pocket(scr, col_x, 24, col_w, sh - 48);
    build_settings(scr, col_x, 24, col_w, sh - 48);
    build_keyboard(scr, ui_x, sh - 300, ui_w, 300);
    build_thought(s_stage);
#if CONFIG_MUSE_PIXEL_THEME
    build_work(s_stage);
#endif

    paint_scene();
    muse_hatch_set_console_hook(on_console);
    update_status();
    show_talk(MUSE_MODE_IDLE);
    show_mood(MUSE_MODE_IDLE);
    lv_timer_create(refresh, REFRESH_MS, NULL);
#if CONFIG_MUSE_PIXEL_THEME
    /* Wandering needs the island to stand on. */
    s_w.next = 20000 / WANDER_MS;
    lv_timer_create(wander_tick, WANDER_MS, NULL);
#endif
    ESP_LOGI(TAG, "dock: stage %dx%d, chat %d px", ui_w, ui_h, col_w);
}
