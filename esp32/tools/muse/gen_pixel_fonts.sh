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

# Regenerates the Tab5 dock's pixel fonts in components/muse/fonts/:
# Pixelify Sans for chat and titles (muse_font_pixel_19, muse_font_pixel_30: the
# sizes that land on its pixel grid, so it stays sharp)
# and Silkscreen for small labels (muse_font_label_16), printable ASCII plus a
# little typographic punctuation. Needs curl, shasum and npx (Node.js).
set -eu

# google/fonts, the commit that last changed ofl/pixelifysans and ofl/silkscreen.
COMMIT=8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5
PIXELIFY_SHA256=9ba86cd010a4de309d263ceff8e8044092c9db7efda869620cb9ff1c4389e8a5
SILKSCREEN_SHA256=c845473330b94c2079ce9af01c51ac8ba2d99c24f4d14c039843bbb8e642ebd8
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
FONTS="$HERE/components/muse/fonts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

curl -fsSL -o "$TMP/pixelify.ttf" \
    "https://raw.githubusercontent.com/google/fonts/$COMMIT/ofl/pixelifysans/PixelifySans%5Bwght%5D.ttf"
curl -fsSL -o "$TMP/silkscreen.ttf" \
    "https://raw.githubusercontent.com/google/fonts/$COMMIT/ofl/silkscreen/Silkscreen-Regular.ttf"
echo "$PIXELIFY_SHA256  $TMP/pixelify.ttf" | shasum -a 256 -c - >/dev/null
echo "$SILKSCREEN_SHA256  $TMP/silkscreen.ttf" | shasum -a 256 -c - >/dev/null

# Printable ASCII, degree, bullet, en and em dash, curly quotes, ellipsis.
RANGE="0x20-0x7E,0xB0,0x2022,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026"

# font file, size, bits per pixel, name
gen() {
    npx -y lv_font_conv@1.5.3 --font "$TMP/$1.ttf" --size "$2" --bpp "$3" \
        --format lvgl --lv-font-name "$4" --no-compress -r "$RANGE" -o "$TMP/$4.c"
    # The project includes LVGL as "lvgl.h"; the header names no temp paths.
    sed -e 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' -e "s|[^ ]*/$1.ttf|$1.ttf|" \
        -e "s|[^ ]*/$4.c|$4.c|" "$TMP/$4.c" > "$FONTS/$4.c"
    echo "wrote $FONTS/$4.c"
}
gen pixelify 19 2 muse_font_pixel_19
# At 19 px Pixelify's "B" loses its stem's corners and reads as an "8" or a
# "G" ("Gluetooth"): give it a straight stem and square bowls, 9x13 at 2 bpp.
perl -0pi -e 's|(/\* U\+0042 "B" \*/\s*)0xf, 0xfc, 0x3, 0xff, 0xf, 0x0, 0x3f, 0xc0,\s*0xf, 0xf0, 0x3, 0xfc, 0x0, 0xff, 0x1f, 0xc3,\s*0xc7, 0xff, 0xf0, 0x3, 0xfc, 0x0, 0xff, 0x0,\s*0x3c, 0x3f, 0xf0, 0xf, 0xfc, 0x0,|${1}0xff, 0xfc, 0x3f, 0xff, 0xf, 0x0, 0x3f, 0xc0,\n    0xf, 0xf0, 0x3, 0xfc, 0x0, 0xff, 0xff, 0xc3,\n    0xff, 0xf0, 0xf0, 0x3, 0xfc, 0x0, 0xff, 0x0,\n    0x3f, 0xff, 0xf0, 0xff, 0xfc, 0x0,|' \
    "$FONTS/muse_font_pixel_19.c"
grep -q '0xff, 0xfc, 0x3f, 0xff, 0xf, 0x0, 0x3f, 0xc0' "$FONTS/muse_font_pixel_19.c" || { echo "B patch didn't apply" >&2; exit 1; }
gen pixelify 30 2 muse_font_pixel_30
gen silkscreen 16 1 muse_font_label_16
