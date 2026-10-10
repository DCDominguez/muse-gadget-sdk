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

#include <stdlib.h>

#include "camera.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"

char *camera_capture_base64(const char **error)
{
    camera_frame_t frame;
    esp_err_t err = camera_capture(&frame);
    if (err != ESP_OK) {
        *error = err == ESP_ERR_NOT_SUPPORTED   ? "no camera"
                 : err == ESP_ERR_NOT_ALLOWED   ? "the camera is turned off on the device; its owner can turn it on with the Camera button"
                 : err == ESP_ERR_INVALID_STATE ? "camera busy"
                 : err == ESP_ERR_TIMEOUT       ? "the camera didn't deliver a frame"
                 : err == ESP_ERR_NO_MEM        ? "camera memory allocation failed"
                                                : "camera capture failed";
        return NULL;
    }
    size_t cap = (frame.len + 2) / 3 * 4 + 1, n = 0;
    char *b64 = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b64) {
        b64 = malloc(cap);
    }
    if (b64 && mbedtls_base64_encode((unsigned char *)b64, cap, &n, frame.jpeg, frame.len) != 0) {
        free(b64);
        b64 = NULL;
    }
    camera_release(&frame);
    *error = b64 ? NULL : "camera memory allocation failed";
    return b64;
}
