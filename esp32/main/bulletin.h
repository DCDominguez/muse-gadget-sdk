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

// The bulletin board, two ways. Notes built into the firmware
// (CONFIG_HOMEHUB_BULLETIN_FILE, tab5/BULLETIN.md on the Tab5) tell Muse what's
// new in this build and what's being worked on; Muse pins ideas back with
// bulletin.post, kept in NVS across restarts, for the owner to pick up.

#pragma once

#include <stdbool.h>

#include "cJSON.h"

// bulletin.read: {"ok": true, "payload": {"text": the notes, "firmware": the
// app's version, "ideas": [{"idea": ..., "at": local time or null}, ...]}}.
cJSON *bulletin_command(void);

// bulletin.post {"idea": text}: pins an idea (at most BULLETIN_IDEA_MAX bytes)
// and answers with how many are pinned. The oldest go when the board is full.
cJSON *bulletin_post_command(const cJSON *params);

// Console ">ideas": prints the pinned ideas as "@ideas" JSON; with `clear`,
// takes them all down first, after printing them.
void bulletin_console_ideas(bool clear);

#define BULLETIN_IDEA_MAX 600
