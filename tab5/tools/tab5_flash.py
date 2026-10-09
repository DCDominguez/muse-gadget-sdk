#!/usr/bin/env python3
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
"""Put your Muse SDK token into a Tab5 test image and flash it (plan.md 1.2).

The kit's app image carries a 48-byte placeholder where the token goes. This
swaps the token in, fixes the image's checksum and SHA-256, signs it with the
SDK's committed dev key (as the build does), checks it, and flashes it. The
token is read from stdin or a token file, never printed, never passed on a
command line, and the patched image is deleted afterwards.

  python tab5_flash.py --port COM4 --first  < token     first flash (base + app)
  python tab5_flash.py --port COM4          < token     update the app only
  python tab5_flash.py --ota-out app.bin    < token     signed image for device.ota

An OTA image holds the token: keep it off anything public (a GitHub release
of the public fork, a public bucket). Muse installs it from an HTTPS URL with
device.ota, and only if its version is newer than the running one (build kits
with KIT_VERSION) unless the request says force.

Safety (plan.md): refuses unless the board is an ESP32-P4 with the expected
MAC, every kit file matches SHA256SUMS.txt, and a 16 MB backup of this board
is found. Never erases the whole chip, touches eFuses or writes the ESP32-C6.
Afterwards it captures the boot log to tab5-boot.log, including the log since
power-on (the firmware's ">log" command).
"""
import argparse
import glob
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import time

KIT = os.path.dirname(os.path.abspath(__file__))
PLACEHOLDER = b"mgst_TAB5LOCALPLACEHOLDER" + b"0" * 22 + b"A"
TOKEN_RE = re.compile(r"^mgst_[A-Za-z0-9_-]*[AEIMQUYcgkosw048]$")
# CONFIG_MUSE_ELEVENLABS_API_KEY in kits (spoken replies): 64 bytes, room for
# a key and its terminator. No key: zeros, and replies stay text.
KEY_PLACEHOLDER = b"elk_TAB5LOCALPLACEHOLDER" + b"0" * 40
KEY_RE = re.compile(r"^[A-Za-z0-9_-]{20,63}$")
MERGED = "tab5-muse-base.bin"
APP = "tab5-muse-app-unsigned.bin"
KEY = "dev_signing_key.pem"
APP_OFFSET = 0x20000
PROD_DATA = (0x820000, 0x2000)
FLASH_BYTES = 16 * 1024 * 1024


def die(msg):
    print(f"STOP: {msg}", file=sys.stderr)
    sys.exit(1)


def esptool(*args, capture=False):
    cmd = [sys.executable, "-m", "esptool", *args]
    print("> esptool " + " ".join(args), flush=True)
    r = subprocess.run(cmd, capture_output=capture, text=True)
    if r.returncode:
        if capture:
            print(r.stdout + r.stderr)
        die(f"esptool failed ({r.returncode}). Nothing more will be written.")
    return r.stdout if capture else ""


def check_kit():
    sums = {}
    with open(os.path.join(KIT, "SHA256SUMS.txt")) as f:
        for line in f:
            if line.strip():
                h, name = line.split(None, 1)
                sums[name.strip().lstrip("*")] = h.lower()
    for name in (MERGED, APP, KEY):
        with open(os.path.join(KIT, name), "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() != sums.get(name):
                die(f"{name} doesn't match SHA256SUMS.txt; re-download the kit")
    print("kit: files match SHA256SUMS.txt")


def read_token(path):
    if path:
        with open(path, encoding="utf-8") as f:
            token = f.read().strip()
    else:
        token = sys.stdin.readline().strip()
    if len(token) != 48 or not TOKEN_RE.match(token):
        die("that isn't an SDK token (48 characters, starting mgst_); copy it again from gadgets.muse.ai")
    return token.encode()


def read_key(path):
    """The ElevenLabs key: from its file, else stdin's second line; empty for none."""
    if path:
        with open(path, encoding="utf-8") as f:
            key = f.read().strip()
    else:
        key = sys.stdin.readline().strip()
    if key and not KEY_RE.match(key):
        die("that doesn't look like an ElevenLabs API key; copy it again from elevenlabs.io")
    return key.encode()


def check_board(port, mac):
    """Checks the chip and returns its MAC. With `mac`, the board must have it;
    without, check_backup() still insists on a backup of this very board."""
    out = esptool("--chip", "esp32p4", "-p", port, "chip-id", capture=True)
    if "ESP32-P4" not in out:
        die("the chip on that port isn't an ESP32-P4")
    rev = re.search(r"revision v(\d+)\.(\d+)", out)
    if not rev or rev.group(1) != "1":
        die("expected a v1.x ESP32-P4 (this build is for pre-v3 silicon)")
    out = esptool("--chip", "esp32p4", "-p", port, "read-mac", capture=True)
    found = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out)
    if not found:
        die("couldn't read the board's MAC")
    if mac and mac.lower() != found.group(1).lower():
        die(f"MAC isn't {mac}: wrong board")
    mac = found.group(1).lower()
    print(f"board: ESP32-P4 v{rev.group(1)}.{rev.group(2)}, MAC {mac}")
    return mac


def check_backup(mac, backup_dir):
    tag = mac.replace(":", "").lower()
    found = [p for p in glob.glob(os.path.join(backup_dir, f"tab5-{tag}-*.bin"))
             if not p.endswith(".verify.bin") and os.path.getsize(p) == FLASH_BYTES]
    if not found:
        die(f"no 16 MB backup of this board in {backup_dir}; run tab5-preflight.ps1 first")
    print(f"backup: {found[0]}")


def segments_end(img):
    """Offset of the checksum byte: after the segments, padded to 16 bytes."""
    if img[0] != 0xE9:
        die("app image has no ESP image header")
    off = 24
    for _ in range(img[1]):
        size = int.from_bytes(img[off + 4:off + 8], "little")
        off += 8 + size
    return off + (15 - off % 16)


def patch(token, key=b""):
    img = bytearray(open(os.path.join(KIT, APP), "rb").read())
    if img.count(PLACEHOLDER) != 1:
        die("app image doesn't hold exactly one token placeholder")
    if img.count(KEY_PLACEHOLDER) > 1:
        die("app image holds more than one ElevenLabs key placeholder")
    ck = segments_end(img)
    # Self-check against the image as built before changing anything.
    xor = 0xEF
    for b in segments_data(img):
        xor ^= b
    if img[ck] != xor or hashlib.sha256(img[:ck + 1]).digest() != bytes(img[ck + 1:ck + 33]):
        die("app image checksum or hash doesn't verify as shipped")
    swaps = [(PLACEHOLDER, token)]
    if KEY_PLACEHOLDER in img:     # kits built since spoken replies
        swaps.append((KEY_PLACEHOLDER, key.ljust(len(KEY_PLACEHOLDER), b"\0")))
    elif key:
        die("this kit has no spoken replies; drop the ElevenLabs key or use a newer kit")
    for old_bytes, new_bytes in swaps:
        at = img.index(old_bytes)
        for old, new in zip(old_bytes, new_bytes):
            img[ck] ^= old ^ new      # the checksum is an XOR over segment data
        img[at:at + len(new_bytes)] = new_bytes
    img[ck + 1:ck + 33] = hashlib.sha256(img[:ck + 1]).digest()
    return img


def segments_data(img):
    off = 24
    for _ in range(img[1]):
        size = int.from_bytes(img[off + 4:off + 8], "little")
        yield from img[off + 8:off + 8 + size]
        off += 8 + size


def sign(img, work):
    unsigned = os.path.join(work, "app-unsigned.bin")
    signed = os.path.join(work, "app-signed.bin")
    with open(unsigned, "wb") as f:
        f.write(img)
    key = os.path.join(KIT, KEY)
    for args in (["sign-data", "--version", "2", "--keyfile", key, "--output", signed, unsigned],
                 ["verify-signature", "--version", "2", "--keyfile", key, signed]):
        r = subprocess.run([sys.executable, "-m", "espsecure", *args], capture_output=True, text=True)
        if r.returncode:
            die("signing failed: " + (r.stderr or r.stdout).strip().splitlines()[-1])
    r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32p4", "image-info", signed],
                       capture_output=True, text=True)
    if r.returncode or "(valid)" not in r.stdout:
        die("the patched image doesn't validate")
    print("app: secrets in, checksum, hash and signature verified")
    return unsigned, signed


def scrub(path):
    if os.path.exists(path):
        with open(path, "r+b") as f:
            f.write(b"\0" * os.path.getsize(path))
            f.flush()
            os.fsync(f.fileno())
        os.remove(path)


SECRET_LINE = re.compile(r"^.*(SDK token|mgst_|xi-api-key|elk_).*$", re.M)


def redact(text):
    """Log text with any line that could show the token or a key replaced."""
    return SECRET_LINE.sub("[line redacted: may hold a secret]", text)


def capture(port, secs, out_path):
    import serial  # comes with esptool
    print(f"capturing the boot log for {secs} s to {out_path} ...", flush=True)
    end = time.time() + secs
    asked = False
    start = time.time()
    with open(out_path, "w", encoding="utf-8", errors="replace") as log:
        while time.time() < end:
            try:
                # DTR and RTS off before opening: the P4's USB Serial/JTAG
                # resets on changes to them (tab5_console.py does the same).
                s = serial.Serial(baudrate=115200, timeout=0.5)
                s.port = port
                s.dtr = False
                s.rts = False
                s.open()
                with s:
                    while time.time() < end:
                        if not asked and time.time() - start > 20:
                            s.write(b">log\n")     # the log since power-on
                            asked = True
                        d = s.read(4096)
                        if d:
                            log.write(redact(d.decode("utf-8", "replace")))
                            log.flush()
            except Exception:
                time.sleep(0.3)                    # the port drops while the board resets
    print(f"boot log: {out_path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="the Tab5's serial port (not needed with --ota-out)")
    ap.add_argument("--ota-out", help="write the signed app for device.ota here instead of flashing")
    ap.add_argument("--first", action="store_true",
                    help="first flash: base image at 0x0 (blanks Muse's settings) and the factory-data slot")
    ap.add_argument("--mac", help="the board's MAC, to refuse any other; without it, the board "
                    "must have a backup from tab5-preflight.ps1 (named by its MAC)")
    ap.add_argument("--backups", default=os.path.join(os.path.expanduser("~"), "tab5-backups"))
    ap.add_argument("--token-file", help="read the token from this file and delete it afterwards")
    ap.add_argument("--key-file", help="ElevenLabs API key for spoken replies, from this file (deleted "
                    "afterwards); without it, a second stdin line, or none")
    ap.add_argument("--keep-secret-files", action="store_true",
                    help="keep --token-file and --key-file afterwards (for repeated flashes on your own PC); "
                         "the patched image is still deleted")
    ap.add_argument("--baud", default="460800")
    ap.add_argument("--log-secs", type=int, default=60)
    ap.add_argument("--log", default=os.path.join(KIT, "tab5-boot.log"))
    a = ap.parse_args()

    check_kit()
    token = read_token(a.token_file)
    key = read_key(a.key_file) if a.key_file or not a.token_file else b""
    if a.ota_out:
        work = tempfile.mkdtemp(prefix="tab5-")
        unsigned = signed = ""
        try:
            unsigned, signed = sign(patch(token, key), work)
            with open(signed, "rb") as src, open(a.ota_out, "wb") as dst:
                dst.write(src.read())
        finally:
            scrub(unsigned)
            scrub(signed)
            try:
                os.rmdir(work)
            except OSError:
                pass
            for path in (a.token_file, a.key_file):
                if path and not a.keep_secret_files:
                    scrub(path)
        print(f"OTA image: {a.ota_out}. It holds your SDK token: host it privately, over HTTPS.")
        return
    if not a.port:
        die("--port is required to flash")
    mac = check_board(a.port, a.mac)
    check_backup(mac, a.backups)
    work = tempfile.mkdtemp(prefix="tab5-")
    unsigned = signed = ""
    try:
        img = patch(token, key)
        unsigned, signed = sign(img, work)
        del img, token, key
        common = ["--chip", "esp32p4", "-p", a.port, "-b", a.baud]
        if a.first:
            esptool(*common, "--after", "no-reset", "erase-region", hex(PROD_DATA[0]), hex(PROD_DATA[1]))
            esptool(*common, "--after", "no-reset", "write-flash", "0x0", os.path.join(KIT, MERGED))
        esptool(*common, "--after", "hard-reset", "write-flash", hex(APP_OFFSET), signed)
    finally:
        scrub(unsigned)
        scrub(signed)
        try:
            os.rmdir(work)
        except OSError:
            pass
        for path in (a.token_file, a.key_file):
            if path and not a.keep_secret_files:
                scrub(path)
    print("flashed. The patched image is gone" + ("." if a.keep_secret_files else ", and the token file."))
    capture(a.port, a.log_secs, a.log)


if __name__ == "__main__":
    main()
