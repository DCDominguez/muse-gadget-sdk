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

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The screen's fonts are LVGL's builds of unscii and Montserrat: ASCII, and
 * Montserrat's symbols. Text from elsewhere (replies, network names) gets
 * ASCII stand-ins for what they lack: curly quotes become straight ones, an em
 * dash "--", accented letters their plain ones. Emoji go; anything else stays,
 * and with CONFIG_MUSE_CJK_FONT the captions draw CJK from a fallback font.
 */

/* The stand-in for the UTF-8 character at s into out, and its length (0 drops
 * the character); -1 to keep it. *len is the character's length in bytes. */
int muse_text_ascii(const char *s, size_t *len, char out[4]);

/* CJK runs have no spaces, so a caption line may break between their
 * characters, but not before closing punctuation such as "，" or "。". */
typedef enum {
    MUSE_TEXT_NOT_CJK,
    MUSE_TEXT_CJK,          /* a line may break before or after it */
    MUSE_TEXT_CJK_CLOSE,    /* closing punctuation: never starts a line */
} muse_text_cjk_t;

/* What the UTF-8 character at s is, for line breaking. */
muse_text_cjk_t muse_text_cjk(const char *s);

/* Whether the UTF-8 text s has any CJK in it. */
bool muse_text_has_cjk(const char *s);

/* Puts the stand-ins into s, which has room for cap bytes. If one doesn't fit,
 * the text ends there. */
void muse_text_to_ascii(char *s, size_t cap);

/* The same, but leaves alone each character keep() says the screen can draw
 * (a fallback font's glyphs, such as the dock's emoji). */
void muse_text_to_ascii_keeping(char *s, size_t cap, bool (*keep)(uint32_t cp));

/* How much of next's start repeats the end of text: a caption that grew, or
 * slid along a streaming reply, starts with what was already shown. An
 * overlap under min bytes counts only as whole words, so only what's new is
 * added. */
size_t muse_text_overlap(const char *text, const char *next, size_t min);

/* text, or if it needs stand-ins and fits in cap bytes, a copy with them in buf. */
const char *muse_text_showable(const char *text, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
