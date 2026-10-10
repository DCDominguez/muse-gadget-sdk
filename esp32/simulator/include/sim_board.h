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

#include "lvgl.h"
#include "muse_board.h"

/* The desktop board profile and the SDL display it creates. */
const muse_board_t *sim_board_get(void);
lv_display_t *sim_board_display(void);
/* Before muse_ui_start(): "watcher" (default, 412x412) or "tab5" (1280x720
 * with an 800x480 Muse and muse_dock). False for an unknown name. */
bool sim_board_select(const char *name);
/* The Tab5 profile's external keyboard, as muse_dock sees it. */
void sim_board_set_keyboard(bool present);
bool sim_board_flipped(void);
/* A scripted touch at x, y, pressed or released. */
void sim_board_tap(int x, int y, bool down);
