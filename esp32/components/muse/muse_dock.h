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

#include <stdint.h>

#include "lvgl.h"

/*
 * The space around Muse on a panel larger than the board's UI box (the Tab5's
 * 1280x720 around an 800x480 Muse). The column beside Muse shows status and a
 * typed chat with Muse; the strip below holds a hold-to-talk button and quick
 * controls (volume, brightness, sleep, flip, power). With no keyboard
 * attached, an on-screen one opens over the strip for typing.
 */

/* Builds the dock on `scr` around the UI box. In the LVGL task. */
void muse_dock_build(lv_obj_t *scr, int ui_x, int ui_y, int ui_w, int ui_h);

/*
 * From any task: a key from an external keyboard, as an LV_KEY_* code
 * (ENTER, BACKSPACE, ESC, UP, DOWN, LEFT, RIGHT) or a Unicode character.
 * Typing goes to the chat; Enter sends it, or confirms pairing when the line
 * is empty and the Muse app is waiting for the press.
 */
void muse_dock_key(uint32_t key);
