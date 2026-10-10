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
#include "sdkconfig.h"

/*
 * The look shared by the dock and the settings beside Muse on a big screen
 * (the Tab5): Muse's palette (night sky, Muse's cream, lavender sparkles),
 * square borders a "pixel" thick with notched corners, and pixel fonts with
 * CONFIG_MUSE_PIXEL_THEME. For this component's own files.
 */

#define C_NIGHT 0x07060d
#define C_PANEL 0x100d1e
#define C_TITLE 0x1d1838
#define C_EDGE 0x2c2550
#define C_EDGE_BTN 0x3a3062
#define C_TILE 0x1a1530
#define C_TILE_DOWN 0x2a2148
#define C_CREAM 0xefe4cf
#define C_CREAM_SHADE 0xd9c9aa
#define C_INK 0x1b1530
#define C_LAV 0xa98bff
#define C_LAV_LO 0x7f62d6
#define C_LAV_SOFT 0xb9a7ff
#define C_TEXT 0xcfc4ee
#define C_DIM 0x9a8cc9
#define C_FAINT 0x6f638f
#define C_MINT 0x7de0b5
#define C_MINT_LO 0x4fa883
#define C_SUN 0xffd36b
#define C_RED 0xe04a5a
#define C_RED_LO 0x7a1f2c
#define C_RED_TILE 0x3a1420
#define C_RED_INK 0xff8a96
#define C_USER 0x3b2f6b
#define C_USER_SHADE 0x2e2456
#define C_METER_OFF 0x241d40

#define PX 4    /* one pixel of the look: borders, notches, icon cells */

#if CONFIG_MUSE_PIXEL_THEME
LV_FONT_DECLARE(muse_font_pixel_19)
LV_FONT_DECLARE(muse_font_pixel_30)
LV_FONT_DECLARE(muse_font_label_16)
#define F_BODY (&muse_font_pixel_19)
#define F_TITLE (&muse_font_pixel_30)
#define F_LABEL (&muse_font_label_16)
#else
#define F_BODY (&lv_font_montserrat_16)
#define F_TITLE (&lv_font_montserrat_20)
#define F_LABEL (&lv_font_montserrat_14)
#endif

/* Notches a box's corners: its border's corner cells in the backdrop's colour.
 * Floating, so they stay out of a flex layout. Set the border and padding first. */
static inline void muse_pixel_notches(lv_obj_t *o, uint32_t backdrop)
{
    static const lv_align_t AT[4] = { LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT,
                                      LV_ALIGN_BOTTOM_RIGHT };
    /* Aligned children sit in the content box, inside the border and padding. */
    int border = lv_obj_get_style_border_width(o, 0);
    int l = border + lv_obj_get_style_pad_left(o, 0), r = border + lv_obj_get_style_pad_right(o, 0);
    int t = border + lv_obj_get_style_pad_top(o, 0), b = border + lv_obj_get_style_pad_bottom(o, 0);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *c = lv_obj_create(o);
        lv_obj_remove_style_all(c);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(c, border, border);
        lv_obj_set_style_bg_color(c, lv_color_hex(backdrop), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_align(c, AT[i], (i & 1) ? r : -l, (i & 2) ? b : -t);
    }
}
