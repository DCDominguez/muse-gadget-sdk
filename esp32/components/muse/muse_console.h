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
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * The serial port the Muse tools (tools/muse) talk over, shared with the log:
 * the chip's own USB Serial/JTAG port where it has one on the USB connector,
 * else the console UART behind the board's USB bridge (the Watcher, the
 * classic ESP32; CONFIG_MUSE_CONSOLE_UART).
 */

/* rx_buf bytes are kept; what doesn't fit is dropped. */
esp_err_t muse_console_install(size_t rx_buf);
/* Blocks for the next byte; false on a driver error. */
bool muse_console_getc(uint8_t *c);
void muse_console_write(const void *buf, size_t n);
/* The ">log" command: prints the log since boot, which a host that opened the
 * port late (Windows re-enumerating after a reset) missed. Home Link provides
 * it from its in-memory capture; the default says there is none. */
void muse_console_dump_log(void);
/* The ">img=URL" command: draws an image as display.draw_url would, without
 * asking Muse, and prints "@img" with the result. The default says the board
 * can't. */
void muse_console_image(const char *url);
/* ">cam": takes a photo as camera.capture would, without asking Muse, and
 * prints "@cam" with its size or the reason it failed. */
void muse_console_camera(void);
/* ">wifi": the connection and its last few drops, as "@wifi" JSON. */
void muse_console_wifi(void);
/* ">scan": networks heard by a passive scan, then an active one, as "@scan". */
void muse_console_scan(void);
/* A USB host is reading the port. A UART bridge can't tell: false. */
bool muse_console_host(void);
