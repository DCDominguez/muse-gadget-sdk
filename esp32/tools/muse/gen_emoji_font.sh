#!/bin/sh
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Regenerates components/muse/fonts/muse_font_emoji_16.c, the emoji fallback
# for the Tab5 dock's chat font (CONFIG_MUSE_EMOJI_FONT): Google's monochrome
# Noto Emoji at 16 px, 4 bits per pixel, for the emoticons block and a chosen
# set of common single-codepoint emoji. Needs curl, shasum and npx (Node.js).
# The set must keep every emoji muse_dock.c's picker offers.
set -eu

# google/fonts, the commit that last changed ofl/notoemoji.
COMMIT=8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5
SHA256=de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$HERE/components/muse/fonts/muse_font_emoji_16.c"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

curl -fsSL -o "$TMP/notoemoji.ttf" \
    "https://raw.githubusercontent.com/google/fonts/$COMMIT/ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf"
echo "$SHA256  $TMP/notoemoji.ttf" | shasum -a 256 -c - >/dev/null

# Emoticons (faces and hands) whole, then the rest one by one: hearts,
# gestures, weather, food, animals, objects, marks.
SYMBOLS="0x1F600-0x1F64F"
SYMBOLS="$SYMBOLS,0x2764,0x1F494-0x1F49F,0x1F5A4,0x1F90D,0x1F9E1"
SYMBOLS="$SYMBOLS,0x1F440,0x1F44B-0x1F44F,0x1F4AA,0x270C,0x1F918-0x1F91E,0x1F926,0x1F937"
SYMBOLS="$SYMBOLS,0x1F914,0x1F917,0x1F923,0x1F929,0x1F92A,0x1F92F,0x1F970,0x1F971,0x1F973,0x1F97A"
SYMBOLS="$SYMBOLS,0x1F916,0x1F47B,0x1F480,0x1F4A9,0x1F9E0,0x1F483,0x1F57A"
SYMBOLS="$SYMBOLS,0x2600,0x2601,0x26A1,0x2744,0x1F308,0x1F319,0x1F31E,0x1F31F,0x1F327"
SYMBOLS="$SYMBOLS,0x2728,0x2B50,0x1F525,0x1F4A1,0x1F4A4,0x1F4A5,0x1F4A6,0x1F4A8,0x1F4AB,0x1F4AC,0x1F4AD,0x1F4AF"
SYMBOLS="$SYMBOLS,0x1F338,0x1F33B,0x1F340,0x1F30D"
SYMBOLS="$SYMBOLS,0x1F354,0x1F355,0x1F369,0x1F36A,0x1F370,0x2615,0x1F377,0x1F37A"
SYMBOLS="$SYMBOLS,0x1F422,0x1F427,0x1F41D,0x1F431,0x1F436,0x1F43B,0x1F984,0x1F98A,0x1F98B"
SYMBOLS="$SYMBOLS,0x1F381,0x1F382,0x1F389,0x1F38A,0x1F3B5,0x1F3B6,0x1F3A7,0x1F3A8,0x1F3AE,0x1F3B8"
SYMBOLS="$SYMBOLS,0x1F3C6,0x1F947,0x26BD,0x1F3C0"
SYMBOLS="$SYMBOLS,0x1F680,0x1F697,0x2708,0x1F3E0,0x1F4F7,0x1F4F1,0x1F4BB,0x1F4DA,0x270F,0x1F4DD,0x23F0,0x231B"
SYMBOLS="$SYMBOLS,0x2705,0x2714,0x2716,0x274C,0x2753,0x2757,0x26A0,0x1F6A8"

npx -y lv_font_conv@1.5.3 --font "$TMP/notoemoji.ttf" --size 16 --bpp 4 \
    --format lvgl --lv-font-name muse_font_emoji_16 --no-compress \
    -r "$SYMBOLS" -o "$TMP/font.c"
# The project includes LVGL as "lvgl.h"; the header names no temp paths.
sed -e 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' -e 's|[^ ]*/notoemoji.ttf|notoemoji.ttf|' -e 's|[^ ]*/font.c|font.c|' "$TMP/font.c" > "$OUT"
echo "wrote $OUT"
