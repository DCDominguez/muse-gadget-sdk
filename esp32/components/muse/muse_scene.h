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

#include "lvgl.h"

/*
 * The world behind a see-through Muse on a large screen (the Tab5's dock with
 * CONFIG_MUSE_PIXEL_THEME): a night sky with a dithered nebula, stars, a
 * ringed planet and distant floating islands, and the island Muse stands on.
 * It's painted in 4 px pixels into one full-screen image, and only again when
 * the layout changes; a few twinkling stars on top are the only motion.
 */

/* Creates the scenery behind everything else on `scr`. In the LVGL task. */
void muse_scene_build(lv_obj_t *scr);

/*
 * Paints it for a layout: Muse's stage spans stage_x .. stage_x + stage_w,
 * Muse's feet are at feet_x, feet_y (screen pixels), and `full` spreads the
 * scenery across the whole screen. In the LVGL task.
 */
void muse_scene_paint(int stage_x, int stage_w, int feet_x, int feet_y, bool full);
