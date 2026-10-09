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
 * The Tab5's camera (SC202CS on MIPI CSI) behind the SDK's camera_driver_t,
 * for camera.capture. Espressif's BSP powers it and starts esp_video; frames
 * come through V4L2 as RGB565 from the P4's ISP, whose pipeline controller
 * runs auto exposure and white balance. Each capture streams for a moment so
 * the exposure settles, keeps the last frame, then scales and turns it with
 * the PPA and encodes it with the P4's JPEG engine.
 */

#include "tab5_camera.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bsp/esp-bsp.h"
#include "camera.h"
#include "driver/jpeg_encode.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "muse_ui.h"
#include "linux/videodev2.h"
#include "nvs.h"

static const char *TAG = "tab5.camera";

#define OUT_W 640                 /* the long side of a capture */
#define SETTLE_FRAMES 20          /* about 0.7 s at 30 fps for exposure to settle */
#define FRAME_TIMEOUT_MS 2000
#define JPEG_QUALITY 80
#define BUFS 2
#define ALIGN 128                 /* PPA output: L1 and L2 cache lines */

static int (*s_rotation)(void);
static jpeg_encoder_handle_t s_jpeg;
static ppa_client_handle_t s_ppa;
static bool s_enabled;   /* the owner's switch, from NVS at registration */
/* When the on-screen notice may go: INT64_MAX while a capture runs. Set from
 * the capture task, read from the LVGL task; a 64-bit atomic keeps it whole. */
static _Atomic int64_t s_in_use_until;
#define IN_USE_AFTER_US (2 * 1000 * 1000)

bool tab5_camera_enabled(void)
{
    return s_enabled;
}

bool tab5_camera_in_use(void)
{
    return esp_timer_get_time() < s_in_use_until;
}

/* From muse_dock's button, in the LVGL task; the same NVS namespace as Flip. */
void tab5_camera_set_enabled(bool on)
{
    s_enabled = on;
    nvs_handle_t h;
    if (nvs_open("muse_tab5", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "camera", on);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "camera turned %s", on ? "on" : "off");
}

/* After a photo: it shows over Muse for PHOTO_SHOWN_US, so the owner sees
 * what Muse was sent; a tap or talk press hides it sooner (muse_ui). */
#define PHOTO_SHOWN_US (5 * 1000 * 1000)
static esp_timer_handle_t s_hide_timer;

static void hide_photo(void *arg)
{
    (void)arg;
    muse_ui_image_hide();
}

/* pixels: native RGB565 from the PPA; muse_ui takes it high byte first. */
static void show_photo(const uint16_t *pixels, int w, int h)
{
    int uw, uh;
    if (!muse_ui_image_size(&uw, &uh) || w > uw || h > uh) {
        return;
    }
    uint8_t *row = malloc((size_t)w * 2);
    if (!row) {
        return;
    }
    int x = (uw - w) / 2, y = (uh - h) / 2;
    for (int r = 0; r < h; r++) {
        const uint16_t *src = pixels + (size_t)r * w;
        for (int i = 0; i < w; i++) {
            row[2 * i] = src[i] >> 8;
            row[2 * i + 1] = src[i] & 0xff;
        }
        muse_ui_image_draw(x, y + r, w, 1, (const uint16_t *)row);
    }
    free(row);
    if (!s_hide_timer) {
        const esp_timer_create_args_t args = { .callback = hide_photo, .name = "photo_hide" };
        esp_timer_create(&args, &s_hide_timer);
    }
    if (s_hide_timer) {
        esp_timer_stop(s_hide_timer);
        esp_timer_start_once(s_hide_timer, PHOTO_SHOWN_US);
    }
}

static esp_err_t cam_init(void)
{
    if (!s_enabled) {
        return ESP_ERR_NOT_ALLOWED;   /* not even powered */
    }
    esp_err_t err = bsp_camera_start(NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "camera didn't start: %s", esp_err_to_name(err));
        return err;
    }
    const jpeg_encode_engine_cfg_t jcfg = { .timeout_ms = 1000 };
    err = jpeg_new_encoder_engine(&jcfg, &s_jpeg);
    if (err == ESP_OK) {
        const ppa_client_config_t pcfg = { .oper_type = PPA_OPERATION_SRM };
        err = ppa_register_client(&pcfg, &s_ppa);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "JPEG or PPA unavailable: %s", esp_err_to_name(err));
    }
    return err;
}

/* Streams SETTLE_FRAMES and copies the last into *rgb (w x h RGB565). */
static esp_err_t grab(uint8_t **rgb, int *w, int *h)
{
    int fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "open %s: errno %d", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, errno);
        return ESP_FAIL;
    }
    esp_err_t err = ESP_FAIL;
    void *maps[BUFS] = { 0 };
    size_t lens[BUFS] = { 0 };
    bool streaming = false;
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) != 0) {
        goto out;
    }
    /* esp_video 2.0.1's G_FMT copies back a stored format whose type is never
     * set before the first S_FMT, so S_FMT would find no stream and fail
     * (EINVAL, unlogged). Put the type back. */
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "RGB565 not accepted");
        goto out;
    }
    *w = fmt.fmt.pix.width;
    *h = fmt.fmt.pix.height;
    struct v4l2_requestbuffers req = {
        .count = BUFS, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        goto out;
    }
    for (int i = 0; i < BUFS; i++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = i };
        if (ioctl(fd, VIDIOC_QUERYBUF, &b) != 0) {
            goto out;
        }
        maps[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        lens[i] = b.length;
        if (maps[i] == MAP_FAILED || ioctl(fd, VIDIOC_QBUF, &b) != 0) {
            maps[i] = NULL;
            goto out;
        }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        goto out;
    }
    streaming = true;
    int64_t until = esp_timer_get_time() + (int64_t)FRAME_TIMEOUT_MS * 1000 * 2;
    for (int n = 0; n < SETTLE_FRAMES; n++) {
        struct v4l2_buffer b = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
        if (ioctl(fd, VIDIOC_DQBUF, &b) != 0 || esp_timer_get_time() > until) {
            err = ESP_ERR_TIMEOUT;
            goto out;
        }
        if (n == SETTLE_FRAMES - 1) {
            size_t len = (size_t)*w * *h * 2;
            *rgb = heap_caps_aligned_alloc(ALIGN, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (!*rgb) {
                err = ESP_ERR_NO_MEM;
                goto out;
            }
            memcpy(*rgb, maps[b.index], len < b.bytesused || !b.bytesused ? len : b.bytesused);
            err = ESP_OK;
        }
        ioctl(fd, VIDIOC_QBUF, &b);
    }
out:
    if (streaming) {
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    for (int i = 0; i < BUFS; i++) {
        if (maps[i]) {
            munmap(maps[i], lens[i]);
        }
    }
    close(fd);
    if (err != ESP_OK && err != ESP_ERR_NO_MEM && err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "V4L2 capture failed (errno %d)", errno);
    }
    return err;
}

static esp_err_t capture_frame(camera_frame_t *out)
{
    if (!s_enabled) {
        return ESP_ERR_NOT_ALLOWED;   /* turned off after an earlier capture */
    }
    int64_t t0 = esp_timer_get_time();
    uint8_t *rgb = NULL;
    int w = 0, h = 0;
    esp_err_t err = grab(&rgb, &w, &h);
    if (err != ESP_OK) {
        return err;
    }
    int rot = s_rotation ? ((s_rotation() % 360) + 360) % 360 : 0;
    bool sideways = rot == 90 || rot == 270;
    float scale = (float)OUT_W / (float)(w > h ? w : h);
    int ow = (int)(w * scale) & ~15, oh = (int)(h * scale) & ~15;   /* JPEG blocks */
    if (sideways) {
        int t = ow;
        ow = oh;
        oh = t;
    }
    size_t small_len = (size_t)ow * oh * 2;
    size_t small_cap = (small_len + ALIGN - 1) / ALIGN * ALIGN;
    const jpeg_encode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER };
    size_t jcap = 0;
    /* The PPA writes it, so it needs cache-line alignment in address and size;
     * jpeg_alloc_encoder_mem() aligns only the encoder's output buffers. */
    uint8_t *small = heap_caps_aligned_calloc(ALIGN, 1, small_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    uint8_t *jpeg = jpeg_alloc_encoder_mem(small_len / 2, &out_cfg, &jcap);
    err = small && jpeg ? ESP_OK : ESP_ERR_NO_MEM;
    if (err == ESP_OK) {
        /* The PPA scales the whole input block, which must then fit the output:
         * rounding the output to JPEG blocks (720 rows -> 352, not 360) would
         * overflow it, so take the centre of the frame that scales to exactly
         * ow x oh (before turning, the sides swap). */
        int bw = (int)((sideways ? oh : ow) / scale), bh = (int)((sideways ? ow : oh) / scale);
        bw = bw > w ? w : bw;
        bh = bh > h ? h : bh;
        const ppa_srm_oper_config_t srm = {
            .in = {
                .buffer = rgb, .pic_w = w, .pic_h = h, .block_w = bw, .block_h = bh,
                .block_offset_x = (w - bw) / 2, .block_offset_y = (h - bh) / 2,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .out = {
                .buffer = small, .buffer_size = small_cap, .pic_w = ow, .pic_h = oh,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
            },
            .rotation_angle = rot == 90    ? PPA_SRM_ROTATION_ANGLE_90
                              : rot == 180 ? PPA_SRM_ROTATION_ANGLE_180
                              : rot == 270 ? PPA_SRM_ROTATION_ANGLE_270
                                           : PPA_SRM_ROTATION_ANGLE_0,
            .scale_x = scale,
            .scale_y = scale,
            .mode = PPA_TRANS_MODE_BLOCKING,
        };
        err = ppa_do_scale_rotate_mirror(s_ppa, &srm);
    }
    heap_caps_free(rgb);
    uint32_t jlen = 0;
    if (err == ESP_OK) {
        const jpeg_encode_cfg_t enc = {
            .width = ow, .height = oh,
            .src_type = JPEG_ENCODE_IN_FORMAT_RGB565,
            .sub_sample = JPEG_DOWN_SAMPLING_YUV420,
            .image_quality = JPEG_QUALITY,
        };
        err = jpeg_encoder_process(s_jpeg, &enc, small, small_len, jpeg, jcap, &jlen);
    }
    if (err == ESP_OK) {
        show_photo((const uint16_t *)small, ow, oh);
    }
    heap_caps_free(small);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scale or encode failed: %s", esp_err_to_name(err));
        free(jpeg);
        return err;
    }
    ESP_LOGI(TAG, "%dx%d -> %dx%d, %d degrees, %u bytes JPEG in %d ms", w, h, ow, oh, rot, (unsigned)jlen,
             (int)((esp_timer_get_time() - t0) / 1000));
    *out = (camera_frame_t){ .jpeg = jpeg, .len = jlen, .width = ow, .height = oh, .priv = jpeg };
    return ESP_OK;
}

/* The on-screen notice is up while the camera streams, and for a moment after. */
static esp_err_t cam_capture(camera_frame_t *out)
{
    if (!s_enabled) {
        return ESP_ERR_NOT_ALLOWED;
    }
    s_in_use_until = INT64_MAX;
    ESP_LOGI(TAG, "taking a photo");
    esp_err_t err = capture_frame(out);
    s_in_use_until = esp_timer_get_time() + IN_USE_AFTER_US;
    return err;
}

static void cam_release(camera_frame_t *frame)
{
    free(frame->priv);
}

static const camera_driver_t s_driver = {
    .name = "SC202CS (M5Stack Tab5)",
    .max_width = OUT_W,
    .max_height = OUT_W,
    .init = cam_init,
    .capture = cam_capture,
    .release = cam_release,
};

void tab5_camera_register(int (*rotation)(void))
{
    nvs_handle_t h;
    uint8_t on = 0;
    if (nvs_open("muse_tab5", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "camera", &on);
        nvs_close(h);
    }
    s_enabled = on != 0;
    ESP_LOGI(TAG, "camera %s (dock button)", s_enabled ? "on" : "off");
    s_rotation = rotation;
    camera_register(&s_driver);
}
