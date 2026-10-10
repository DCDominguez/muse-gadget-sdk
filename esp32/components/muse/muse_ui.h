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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Bring up the display and build the UI: the avatar on the first tile,
 * settings one swipe to the left. Also owns screen sleep and brightness.
 */
esp_err_t muse_ui_start(void);

/* From any task: the screen has gone dark for sleep (and not yet woken). */
bool muse_ui_dark(void);

/* The functions below run in the LVGL task (or with the display lock held). */

/* Slide back to the face (e.g. when a talk starts). */
void muse_ui_show_face(void);
/* Slide to the settings tile (the dock's pocket opens it). */
void muse_ui_show_settings(void);
/* Where Muse's own area sits on a larger screen (the Tab5's full-screen
 * mode moves it); no effect when Muse has the whole screen. */
void muse_ui_set_origin(int x, int y);
/* Where Muse stands on the screen: the middle of Muse's feet, in screen
 * pixels (the dock puts the ground there). False before the UI is up. */
bool muse_ui_feet(int *x, int *y);
/*
 * The dock walking Muse about its stage: Muse's feet dx, dy from where Muse
 * stands at home (dy < 0 is further away), drawn px across (whole multiples
 * of the 64-cell grid), facing as in muse_pose_t, walk 0..1. Answer layouts
 * still place Muse themselves while they show.
 */
void muse_ui_set_motion(int dx, int dy, int px, float facing, float walk);
/* Settings sub-pages turn off the tile swipe so they can use horizontal gestures. */
void muse_ui_set_swipe_enabled(bool enabled);
/* Temporarily applies a brightness while a slider is dragged. */
void muse_ui_preview_brightness(int pct);

/*
 * display.draw_url, from any task. An image covers the face until a tap, a
 * talk, the menu or muse_ui_image_hide(). Pixels are RGB565, high byte first.
 * The size is false without PSRAM for the image or before the UI is up.
 */
bool muse_ui_image_size(int *w, int *h);
bool muse_ui_image_draw(int x, int y, int w, int h, const uint16_t *pixels);
void muse_ui_image_hide(void);
/* Watcher camera mode: shows an on-screen shutter hint over the live image. */
void muse_ui_camera_hint(bool visible);

/* Bench testing, from any task: streams the screen over USB serial. */
void muse_ui_request_snapshot(void);
