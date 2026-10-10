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

#include "sim_board.h"
#include "sim_platform.h"

#include <stdio.h>
#include <string.h>

#include "src/drivers/sdl/lv_sdl_mouse.h"
#include "src/drivers/sdl/lv_sdl_window.h"

#define WATCHER_RESOLUTION 412
/* M5Stack Tab5: a 1280x720 landscape panel with Muse in an 800x480 box and
 * muse_dock around it (components/muse/boards/board_m5stack_tab5.c). */
#define TAB5_W 1280
#define TAB5_H 720

static lv_display_t *s_display;
static int s_window_w = WATCHER_RESOLUTION, s_window_h = WATCHER_RESOLUTION;
static bool s_keyboard;
static bool s_flipped;

/* Scripted touches (scenario "tap="), beside the SDL mouse. */
static lv_indev_t *s_tap;
static int32_t s_tap_x, s_tap_y;
static bool s_tap_down;

static void tap_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_tap_x;
    data->point.y = s_tap_y;
    data->state = s_tap_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* muse_ui.c reads the selected board through this production global. */
const muse_board_t *muse_board;

static esp_err_t sim_init(void)
{
    return ESP_OK;
}

static lv_display_t *sim_display_start(lv_indev_t **touch)
{
    s_display = lv_sdl_window_create(s_window_w, s_window_h);
    if (!s_display) {
        return NULL;
    }
    /* The SDL driver installs SDL_GetTicks. Use the simulator clock instead so
     * scripted runs can advance time without sleeping and render repeatably. */
    lv_tick_set_cb(sim_time_tick_ms);
    lv_sdl_window_set_title(s_display, "Muse Gadget Simulator");
    lv_sdl_window_set_resizeable(s_display, false);
    if (touch) {
        *touch = lv_sdl_mouse_create();
    }
    s_tap = lv_indev_create();
    lv_indev_set_type(s_tap, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_tap, tap_read);
    lv_indev_set_display(s_tap, s_display);
    return s_display;
}

static bool sim_display_lock(int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

static void sim_display_unlock(void)
{
}

static void sim_set_brightness(int pct)
{
    (void)pct;
}

static void sim_panel_sleep(bool sleep)
{
    (void)sleep;
}

static esp_err_t sim_power_off(void)
{
    return ESP_FAIL;
}

static void sim_flip_display(void)
{
    /* The SDL window doesn't turn; record it for scenarios and the log. */
    s_flipped = !s_flipped;
    printf("sim: display flipped: %s\n", s_flipped ? "yes" : "no");
}

static bool sim_keyboard_present(void)
{
    return s_keyboard;
}

static const muse_board_t s_tab5_board = {
    .name = "M5Stack Tab5 Simulator",
    .width = 800,
    .height = 480,
    .ui_x = 0,
    .ui_y = 0,
    .avatar_px = 288,
    .round = false,
    .touch = true,
    .diagonal_in = 5.0f,
    .talk_button = "screen",
    .frame_ms = 40,
    .init = sim_init,
    .display_start = sim_display_start,
    .display_lock = sim_display_lock,
    .display_unlock = sim_display_unlock,
    .set_brightness = sim_set_brightness,
    .panel_sleep = sim_panel_sleep,
    .power_off = sim_power_off,
    .flip_display = sim_flip_display,
    .keyboard_present = sim_keyboard_present,
};

static const muse_board_t s_sim_board = {
    .name = "SenseCAP Watcher Simulator",
    .width = WATCHER_RESOLUTION,
    .height = WATCHER_RESOLUTION,
    .round = true,
    .touch = true,
    .diagonal_in = 1.45f,
    .talk_button = "wheel",
    .aux_button = "scroll",
    .talk_hint = { LV_ALIGN_CENTER, 100, -143 },
    .frame_ms = 40,
    .init = sim_init,
    .display_start = sim_display_start,
    .display_lock = sim_display_lock,
    .display_unlock = sim_display_unlock,
    .set_brightness = sim_set_brightness,
    .panel_sleep = sim_panel_sleep,
    .power_off = sim_power_off,
};

static const muse_board_t *s_selected = &s_sim_board;

bool sim_board_select(const char *name)
{
    if (!strcmp(name, "watcher")) {
        s_selected = &s_sim_board;
        s_window_w = s_window_h = WATCHER_RESOLUTION;
    } else if (!strcmp(name, "tab5")) {
        s_selected = &s_tab5_board;
        s_window_w = TAB5_W;
        s_window_h = TAB5_H;
    } else {
        return false;
    }
    return true;
}

const muse_board_t *sim_board_get(void)
{
    return s_selected;
}

void sim_board_set_keyboard(bool present)
{
    s_keyboard = present;
}

bool sim_board_flipped(void)
{
    return s_flipped;
}

void sim_board_tap(int x, int y, bool down)
{
    s_tap_x = x;
    s_tap_y = y;
    s_tap_down = down;
}

lv_display_t *sim_board_display(void)
{
    return s_display;
}
