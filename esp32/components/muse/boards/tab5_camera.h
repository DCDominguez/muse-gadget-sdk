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

/*
 * Registers the Tab5's camera with the SDK's camera component, for
 * camera.capture. rotation() gives the degrees (counter-clockwise) to turn
 * each frame so it's upright the way the Tab5 is held.
 */
void tab5_camera_register(int (*rotation)(void));

/* The owner's switch for the camera (muse_dock's button), kept across
 * restarts; off until turned on. Off, a capture is refused before the camera
 * is powered, and Muse is told it's turned off. */
bool tab5_camera_enabled(void);
void tab5_camera_set_enabled(bool on);

/* While a capture runs and for 2 s after, for muse_dock's on-screen notice. */
bool tab5_camera_in_use(void);
