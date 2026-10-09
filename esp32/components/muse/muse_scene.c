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

#include "muse_scene.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "muse_scene";

#define U 4                 /* screen pixels per pixel of the scenery */
#define TWINKLES 12

static lv_draw_buf_t s_buf;
static uint16_t *s_px;      /* RGB565, s_w x s_h, in PSRAM */
static int s_w, s_h, s_gw, s_gh;   /* screen, and in scenery pixels */
static lv_obj_t *s_img;
static lv_obj_t *s_twinkle[TWINKLES];
static uint32_t s_seed;

static inline uint16_t rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 8) & 0xf800) | ((hex >> 5) & 0x07e0) | ((hex >> 3) & 0x001f));
}

static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 16) & 0x7fff;
}

/* One scenery pixel: a U x U block. */
static void dot(int gx, int gy, uint16_t c)
{
    if (gx < 0 || gy < 0 || gx >= s_gw || gy >= s_gh) {
        return;
    }
    uint16_t *p = s_px + gy * U * s_w + gx * U;
    for (int y = 0; y < U; y++, p += s_w) {
        for (int x = 0; x < U; x++) {
            p[x] = c;
        }
    }
}

static void run(int gx, int gy, int n, uint16_t c)
{
    for (int i = 0; i < n; i++) {
        dot(gx + i, gy, c);
    }
}

/* A band of sky dithered in three densities around the line mid(x). */
static void nebula(void)
{
    uint16_t core = rgb(0x2a2052), mid = rgb(0x251c4b), outer = rgb(0x1a1434);
    for (int x = 0; x < s_gw; x++) {
        float m = s_gh * 0.83f - 0.42f * x + 6.0f * sinf(x / 23.0f);
        for (int y = 0; y < s_gh; y++) {
            float d = fabsf(y - m);
            if (d <= 4) {
                dot(x, y, core);
            } else if (d <= 12 && (x + y) % 2 == 0) {
                dot(x, y, mid);
            } else if (d <= 22 && x % 2 == 0 && y % 2 == 0) {
                dot(x, y, outer);
            }
        }
    }
    /* Low mist along the bottom. */
    uint16_t mist = rgb(0x17122d);
    for (int y = s_gh * 5 / 6; y < s_gh; y++) {
        for (int x = (y % 2); x < s_gw; x += 2) {
            if (y % 2 == 0) {
                dot(x, y, mist);
            }
        }
    }
}

static void stars(void)
{
    uint16_t dim = rgb(0x3a3358), bright = rgb(0x8f80c2), spark = rgb(0xb9a7ff), white = rgb(0xf4f0ff);
    for (int i = 0; i < 110; i++) {
        int x = rnd() % s_gw, y = rnd() % (s_gh * 4 / 5);
        dot(x, y, rnd() % 7 ? dim : bright);
    }
    for (int i = 0; i < 7; i++) {
        int x = rnd() % s_gw, y = rnd() % (s_gh * 3 / 4);
        dot(x - 1, y, spark);
        dot(x + 1, y, spark);
        dot(x, y - 1, spark);
        dot(x, y + 1, spark);
        dot(x, y, white);
    }
}

/* A planet lit from the top left, with a ring whose back half it hides. */
static void planet(int cx, int cy, int r)
{
    uint16_t base = rgb(0x33295e), lit = rgb(0x4a3d85), dark = rgb(0x241d45), dark2 = rgb(0x1f1840),
             ring = rgb(0x6f5fae);
    float rx = r * 1.85f, ry = r * 0.35f;
    for (int y = cy - r - 4; y <= cy + r + 4; y++) {
        for (int x = (int)(cx - rx - 2); x <= cx + rx + 2; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            bool in = dx * dx + dy * dy <= r * r;
            float e = (dx / rx) * (dx / rx) + ((dy - 1) / ry) * ((dy - 1) / ry);
            bool on_ring = fabsf(e - 1.0f) < 0.22f;
            if (in && !(on_ring && dy >= 1)) {
                float v = dx + dy;
                dot(x, y, v > r * 0.6f ? ((x + y) % 2 ? dark : dark2) : v < -r * 0.75f ? lit : base);
            } else if (on_ring && !(in && dy < 1)) {
                dot(x, y, ring);
            }
        }
    }
}

/* A small far-off island, dark against the sky. */
static void far_island(int cx, int cy, int w)
{
    static const uint32_t shades[] = { 0x1d1838, 0x17132e, 0x120f24, 0x0e0b1c, 0x0e0b1c };
    for (int i = 0; i < 5; i++) {
        int rw = w - i * w / 5 - (i ? 2 : 0);
        if (rw < 2) {
            break;
        }
        run(cx - rw / 2, cy + i, rw, rgb(shades[i]));
    }
}

/* Muse's island: a top surface in perspective, a banded cliff, craters,
 * hanging crystals, a sprout and a star lantern. */
static void island(int cx, int cy, int rx, int ry)
{
    uint16_t rim = rgb(0x5a4e96), top = rgb(0x3a3166), back = rgb(0x332b5c), edge = rgb(0x2b2450),
             band_a = rgb(0x231d43), band_b = rgb(0x1d1838), tip = rgb(0x151127), crystal = rgb(0x8f7fe0),
             crystal_lit = rgb(0xd8ccff);
    for (int x = cx - rx; x < cx + rx; x++) {
        float t = (x + 0.5f - cx) / rx;
        float k = sqrtf(fmaxf(0.0f, 1.0f - t * t));
        int y_top = (int)lroundf(cy - ry * k), y_front = (int)lroundf(cy + ry * k);
        int y_bot = (int)lroundf(y_front + 4 + 20 * (1 - t * t) * (0.85f + 0.15f * sinf(x * 1.7f)));
        dot(x, y_top, rim);
        for (int y = y_top + 1; y < y_front; y++) {
            bool far = (y - cy) < -0.35f * ry;
            dot(x, y, far && (x + y) % 2 ? back : top);
        }
        dot(x, y_front, edge);
        for (int y = y_front + 1, band = 0; y < y_bot; band++) {
            for (int i = 0; i < 4 && y < y_bot; i++, y++) {
                dot(x, y, band % 2 ? band_b : band_a);
            }
        }
        dot(x, y_bot, tip);
        if ((x - cx + 1000) % 17 == 3) {
            dot(x, y_bot + 1, crystal);
            dot(x, y_bot + 2, crystal_lit);
            dot(x, y_bot + 3, crystal);
        }
    }
    /* Craters. */
    uint16_t pit = rgb(0x2f2856), pit_lit = rgb(0x4a3f80);
    const int craters[][3] = { { -rx * 2 / 5, -2, 5 }, { rx * 3 / 10, 3, 4 }, { -rx / 15, -ry * 3 / 5, 3 } };
    for (int i = 0; i < 3; i++) {
        int x0 = cx + craters[i][0], y0 = cy + craters[i][1], r = craters[i][2];
        run(x0 - r, y0, 2 * r, pit);
        run(x0 - r + 1, y0 + 1, 2 * r - 2, pit_lit);
    }
    /* A sprout at the back left. */
    int sx = cx - rx * 7 / 10, sy = cy - ry / 2;
    uint16_t leaf = rgb(0x7de0b5), leaf_dark = rgb(0x5bbf95);
    dot(sx, sy, leaf); dot(sx, sy - 1, leaf); dot(sx, sy - 2, leaf);
    dot(sx - 1, sy - 3, leaf); dot(sx - 2, sy - 3, leaf); dot(sx - 1, sy - 2, leaf_dark);
    dot(sx + 1, sy - 4, leaf); dot(sx + 2, sy - 4, leaf); dot(sx + 1, sy - 3, leaf_dark);
    /* A star lantern at the back right. */
    int lx = cx + rx * 3 / 4, ly = cy - ry / 2;
    uint16_t pole = rgb(0x6f638f), glow = rgb(0xffd36b), hot = rgb(0xfff6d6);
    for (int i = 0; i < 6; i++) {
        dot(lx, ly - i, pole);
    }
    run(lx - 1, ly - 6, 3, pole);
    run(lx - 1, ly - 9, 3, glow);
    run(lx - 1, ly - 8, 3, glow);
    run(lx - 1, ly - 7, 3, glow);
    dot(lx, ly - 8, hot);
    run(lx - 1, ly - 10, 3, pole);
}

static void place_twinkles(int x0, int x1, int y1)
{
    for (int i = 0; i < TWINKLES; i++) {
        int x = x0 + (int)(rnd() % (uint32_t)(x1 - x0 - 8));
        int y = 8 + (int)(rnd() % (uint32_t)(y1 - 16));
        lv_obj_set_pos(s_twinkle[i], x - x % U, y - y % U);
    }
}

static void twinkle_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

void muse_scene_build(lv_obj_t *scr)
{
    lv_display_t *disp = lv_obj_get_display(scr);
    s_w = lv_display_get_horizontal_resolution(disp);
    s_h = lv_display_get_vertical_resolution(disp);
    s_gw = s_w / U;
    s_gh = s_h / U;
    size_t size = (size_t)s_w * s_h * sizeof(uint16_t);
    s_px = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_px) {
        ESP_LOGW(TAG, "no room for the scenery (%u bytes)", (unsigned)size);
        return;
    }
    lv_draw_buf_init(&s_buf, s_w, s_h, LV_COLOR_FORMAT_RGB565, s_w * sizeof(uint16_t), s_px, size);
    s_img = lv_image_create(scr);
    lv_obj_remove_flag(s_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_move_to_index(s_img, 0);   /* behind everything */

    for (int i = 0; i < TWINKLES; i++) {
        lv_obj_t *t = lv_obj_create(scr);
        lv_obj_remove_style_all(t);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(t, i % 3 ? U : 2 * U, i % 3 ? U : 2 * U);
        lv_obj_set_style_bg_color(t, lv_color_hex(i % 3 ? 0x8f80c2 : 0xd8ccff), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_move_to_index(t, 1 + i);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, t);
        lv_anim_set_exec_cb(&a, twinkle_cb);
        lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
        lv_anim_set_duration(&a, 900 + (i * 377) % 1400);
        lv_anim_set_reverse_duration(&a, 900 + (i * 377) % 1400);
        lv_anim_set_delay(&a, (i * 613) % 2000);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
        s_twinkle[i] = t;
    }
}

void muse_scene_paint(int stage_x, int stage_w, int feet_x, int feet_y, bool full)
{
    if (!s_px) {
        return;
    }
    int64_t t0 = esp_timer_get_time();
    s_seed = 7;
    uint16_t night = rgb(0x07060d);
    for (size_t i = 0, n = (size_t)s_w * s_h; i < n; i++) {
        s_px[i] = night;
    }
    nebula();
    stars();
    int fx = feet_x / U, fy = feet_y / U;
    if (full) {
        planet(s_gw - 52, 40, 14);
        far_island(36, 62, 30);
        far_island(s_gw - 70, 96, 22);
        far_island(fx - 74, 38, 14);
        island(fx, fy - 3, 92, 15);
        place_twinkles(0, s_w, s_h * 2 / 3);
    } else {
        int sx = stage_x / U, sw = stage_w / U;
        planet(sx + sw - 16, fy + 14, 9);
        far_island(sx + 15, fy - 54, 24);
        far_island(sx + sw - 30, fy - 74, 16);
        island(fx, fy - 3, 62, 14);
        place_twinkles(stage_x, stage_x + stage_w, s_h * 2 / 3);
    }
    lv_image_set_src(s_img, &s_buf);
    lv_obj_invalidate(s_img);
    ESP_LOGI(TAG, "scenery painted (%s) in %d ms", full ? "full screen" : "beside the chat",
             (int)((esp_timer_get_time() - t0) / 1000));
}
