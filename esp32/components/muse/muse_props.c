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

#include "muse_props.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#define OUTLINE 0x3a2b22    /* Muse's own outline (avatar/muse_pixel.c C_OUT) */

static const uint8_t BAYER4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

static inline uint16_t rgb565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
}

/* About 82% as bright: a cell's grid edge, as Muse's renderer draws it. */
static inline uint32_t dim(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    return ((r * 21 / 26) << 16) | ((g * 21 / 26) << 8) | (b * 21 / 26);
}

static const muse_prop_mat_t *mat_of(char k, const muse_prop_mat_t *mats, int n)
{
    for (int i = 0; i < n; i++) {
        if (mats[i].key == k) {
            return &mats[i];
        }
    }
    return NULL;
}

lv_obj_t *muse_prop_create(lv_obj_t *parent, const char *map, int w, int h, const muse_prop_mat_t *mats, int nmats,
                           int cell, bool outline)
{
    int gw = w + 2, gh = h + 2;             /* a cell of outline all round */
    int pw = gw * cell, ph = gh * cell;
    size_t rgb_size = (size_t)pw * ph * 2, size = rgb_size + (size_t)pw * ph;
    uint8_t *data = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_draw_buf_t *buf = heap_caps_calloc(1, sizeof(*buf), MALLOC_CAP_8BIT);
    if (!data || !buf) {
        free(data);
        free(buf);
        return NULL;
    }
    lv_draw_buf_init(buf, pw, ph, LV_COLOR_FORMAT_RGB565A8, pw * 2, data, size);
    uint16_t *px = (uint16_t *)data;
    uint8_t *alpha = data + rgb_size;

#define AT(x, y) (((x) >= 0 && (y) >= 0 && (x) < w && (y) < h) ? map[(y) * w + (x)] : '.')
    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            int x = gx - 1, y = gy - 1;
            char k = AT(x, y);
            uint32_t c;
            if (k == '.') {
                /* Outline where the prop's edge is: next to a filled cell. */
                if (!outline
                    || (AT(x - 1, y) == '.' && AT(x + 1, y) == '.' && AT(x, y - 1) == '.' && AT(x, y + 1) == '.')) {
                    continue;
                }
                c = OUTLINE;
            } else {
                const muse_prop_mat_t *m = mat_of(k, mats, nmats);
                if (!m) {
                    continue;
                }
                /* Light from the top left: edges facing it lit, edges facing
                 * away shaded, and across the body a dithered slope. */
                bool up = AT(x, y - 1) != k, left = AT(x - 1, y) != k;
                bool down = AT(x, y + 1) != k, right = AT(x + 1, y) != k;
                bool lit = up || left, shaded = down || right;
                if (lit && !shaded) {
                    c = m->hi;
                } else if (shaded && !lit) {
                    c = m->lo;
                } else if (lit) {
                    c = m->mid;   /* a part one cell thin */
                } else {
                    float v = ((float)x / w + (float)y / h) * 0.5f;   /* 0 top left .. 1 bottom right */
                    int t = (int)(v * 24) - 6;                          /* dither band in the lower right */
                    c = t > BAYER4[y & 3][x & 3] ? m->lo : m->mid;
                }
            }
            uint16_t col = rgb565(c), edge = rgb565(dim(c));
            for (int yy = 0; yy < cell; yy++) {
                uint16_t *row = px + (size_t)(gy * cell + yy) * pw + gx * cell;
                uint8_t *arow = alpha + (size_t)(gy * cell + yy) * pw + gx * cell;
                for (int xx = 0; xx < cell; xx++) {
                    row[xx] = (yy == cell - 1 || xx == cell - 1) && cell >= 3 ? edge : col;
                    arow[xx] = 255;
                }
            }
        }
    }
#undef AT

    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(img, buf);
    return img;
}
