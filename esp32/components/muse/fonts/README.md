# Fonts

`muse_font_cjk_16.c` is the CJK fallback for the caption font, built in only
with `CONFIG_MUSE_CJK_FONT`. It holds GNU Unifont 16.0.04's 16x16 bitmaps,
the same cell as unscii-16, for CJK punctuation, kana, every CJK Unified
Ideograph (U+4E00 to U+9FFF) and the fullwidth forms: about 850 KB of flash.
`tools/muse/gen_cjk_font.sh` regenerates it.

GNU Unifont is by Roman Czyborra, Paul Hardy and contributors
(https://unifoundry.com/unifont/). Its compiled fonts are licensed under the
SIL Open Font License, version 1.1 (https://openfontlicense.org), and under
the GNU GPL version 2 or later with the GNU font embedding exception. This
file is a conversion of an unaltered subset of the font.

`muse_font_emoji_16.c` is the emoji fallback for the dock's chat font (the
Tab5), built in only with `CONFIG_MUSE_EMOJI_FONT`. It holds Noto Emoji's
monochrome outlines at 16 px, 4 bits per pixel, for the emoticons block
(U+1F600 to U+1F64F) and about 125 other common single-codepoint emoji: about
30 KB of flash. `tools/muse/gen_emoji_font.sh` regenerates it and lists the set.

Noto Emoji is by Google (https://github.com/googlefonts/noto-emoji), taken
from google/fonts at commit 8b0a1d0f. It is licensed under the SIL Open Font
License, version 1.1 (https://openfontlicense.org). This file is a conversion
of an unaltered subset of the font.

`muse_font_pixel_19.c`, `muse_font_pixel_30.c` and `muse_font_label_16.c` are
the Tab5 dock's pixel fonts, built in only with `CONFIG_MUSE_PIXEL_THEME`:
Pixelify Sans at 19 and 30 px (2 bits per pixel; the sizes where its pixel grid
lands on the screen's, so it stays sharp) for chat and titles, and Silkscreen
at 16 px (1 bit per pixel) for labels, each for printable ASCII and a little
typographic punctuation. `tools/muse/gen_pixel_fonts.sh` regenerates them.

Pixelify Sans is copyright 2021 The Pixelify Sans Project Authors
(https://github.com/eifetx/Pixelify-Sans); Silkscreen is copyright 2001 The
Silkscreen Project Authors (https://github.com/googlefonts/silkscreen). Both are taken from google/fonts
at commit 8b0a1d0f and licensed under the SIL Open Font License, version 1.1
(https://openfontlicense.org). These files are conversions of unaltered
subsets of the fonts.
