#!/usr/bin/env bash
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
#
# Builds the Tab5 flashing kit: the firmware with a token placeholder (no SDK
# token is ever built in), the flash scripts, the offline token form, the
# Codex prompt and SHA256SUMS.txt, zipped. The owner's PC puts the real token
# in (tab5_flash.py). Needs ESP-IDF v6.0.1 (see esp32/AGENTS.md).
#
#   tab5/tools/make_kit.sh NAME [OUT_DIR]     e.g. make_kit.sh test3 /tmp/kits
#   KIT_VERSION=999.1.0 tab5/tools/make_kit.sh ota1
#
# KIT_VERSION sets the firmware version (default: esp32/version.txt). An OTA
# installs only a newer version than the running one, unless forced.
set -euo pipefail

name=${1:?kit name, e.g. test3}
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
out=${2:-$repo/tab5/kits}
kit=$out/tab5-kit-$name
esp=$repo/esp32
B=build-muse-m5stack-tab5

if ! command -v idf.py >/dev/null 2>&1; then
    for d in "${IDF_EXPORT:-}" "$HOME/.espressif/esp-idf-v6.0.1/export.sh" "$HOME/esp/esp-idf-v6.0.1/export.sh"; do
        [ -n "$d" ] && [ -f "$d" ] && { . "$d" >/dev/null 2>&1; break; }
    done
fi
command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found; activate ESP-IDF v6.0.1" >&2; exit 1; }

# Must match PLACEHOLDER in tab5_flash.py: 48 characters, valid token form.
placeholder="mgst_TAB5LOCALPLACEHOLDER0000000000000000000000A"
[ ${#placeholder} -eq 48 ] || { echo "placeholder length" >&2; exit 1; }
(cd "$here" && python3 -I -c "import sys; sys.path.insert(0, '.'); import tab5_flash as t
sys.exit(t.PLACEHOLDER.decode() != sys.argv[1] or t.KEY_PLACEHOLDER.decode() != sys.argv[2])" \
    "$placeholder" "$key_placeholder") ||
    { echo "tab5_flash.py's placeholder changed; update make_kit.sh" >&2; exit 1; }
rm -rf "$here/__pycache__"
defaults=$(mktemp)
trap 'rm -f "$defaults"' EXIT
# The ElevenLabs key's placeholder (KEY_PLACEHOLDER in tab5_flash.py): 64
# bytes, so the owner's key or zeros fit in place.
key_placeholder="elk_TAB5LOCALPLACEHOLDER0000000000000000000000000000000000000000"
[ ${#key_placeholder} -eq 64 ] || { echo "key placeholder length" >&2; exit 1; }
printf 'CONFIG_GADGET_SDK_TOKEN="%s"\nCONFIG_MUSE_ELEVENLABS_API_KEY="%s"\n' \
    "$placeholder" "$key_placeholder" > "$defaults"

cd "$esp"
rm -rf "$B" managed_components dependencies.lock
ver_arg=()
[ -n "${KIT_VERSION:-}" ] && ver_arg=(-DPROJECT_VER="$KIT_VERSION")
idf.py -B "$B" -DIDF_TARGET=esp32p4 -DSDKCONFIG="$B/sdkconfig" "${ver_arg[@]}" \
    -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-m5stack-tab5;$defaults" \
    build | tail -3
python3 - "$B/muse-gadget-unsigned.bin" "$placeholder" "$key_placeholder" <<'EOF'
import sys
img = open(sys.argv[1], 'rb').read()
for p in sys.argv[2:]:
    n = img.count(p.encode())
    if n != 1:
        sys.exit(f"expected one {p[:4]} placeholder in the app, found {n}")
EOF

rm -rf "$kit" && mkdir -p "$kit"
(cd "$B" && python -m esptool --chip esp32p4 merge-bin -o "$kit/tab5-muse-base.bin" @flash_args >/dev/null)
cp "$B/muse-gadget-unsigned.bin" "$kit/tab5-muse-app-unsigned.bin"
cp dev_signing_key.pem "$kit/"
cp "$here/tab5_flash.py" "$here/tab5-flash.ps1" "$here/tab5-sdk-token.html" "$here/CODEX_PROMPT.md" "$kit/"
cat > "$kit/README.md" <<EOF
# Muse for M5Stack Tab5: kit $name ($(git -C "$repo" rev-parse --short HEAD))

1. Open \`tab5-sdk-token.html\` in your browser (offline; sends nothing). Paste
   your SDK token and save \`tab5-sdk-token.txt\` here or in Downloads. For
   spoken replies, also save your ElevenLabs key as \`tab5-elevenlabs-key.txt\`.
2. First flash over other firmware: \`.\\tab5-flash.ps1 -Port COM4 -First\`.
   Later: drop \`-First\` (app only; keeps pairing and Wi-Fi).
3. Or give Codex \`CODEX_PROMPT.md\`.

See tab5/FLASHING.md in DCDominguez/muse-gadget-sdk (branch tab5-port).
EOF
(cd "$kit" && sha256sum tab5-muse-base.bin tab5-muse-app-unsigned.bin dev_signing_key.pem \
    tab5_flash.py tab5-flash.ps1 tab5-sdk-token.html CODEX_PROMPT.md > SHA256SUMS.txt)
(cd "$kit" && python3 -I -c "import sys; sys.path.insert(0, '.'); sys.argv = ['x']
import tab5_flash as t; t.check_kit(); assert bytes(t.patch(t.PLACEHOLDER)) == open(t.APP, 'rb').read()" \
    && rm -rf __pycache__)
(cd "$out" && rm -f "tab5-kit-$name.zip" && python3 -I -m zipfile -c "tab5-kit-$name.zip" "tab5-kit-$name")
echo "kit: $out/tab5-kit-$name.zip"
