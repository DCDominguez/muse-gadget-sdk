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

#include "lvgl.h"

/*
 * Small pixel-art things drawn the way Muse's renderer draws Muse, so they sit
 * beside Muse as if from the same hand: a dark outline, light from the top
 * left in three tones per material with ordered dithering between them, and
 * each cell's last row and column a touch dimmer so the pixel grid shows.
 */

typedef struct {
    char key;               /* the character standing for it in a prop's map */
    uint32_t hi, mid, lo;   /* lit, plain and shaded; flat details give all three alike */
} muse_prop_mat_t;

/*
 * An image of the prop: `map` is w x h characters, row after row, '.' for
 * nothing and otherwise a material's key. Drawn `cell` screen pixels a cell,
 * with a cell of outline all round (so the image is (w + 2) x (h + 2) cells).
 * Hidden until shown. Without `outline` the edge cells stay clear (paint on a
 * canvas, say). NULL if there's no memory for it.
 */
lv_obj_t *muse_prop_create(lv_obj_t *parent, const char *map, int w, int h, const muse_prop_mat_t *mats, int nmats,
                           int cell, bool outline);
