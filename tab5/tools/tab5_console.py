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
"""Talk to Muse on the Tab5 over its USB serial console, with secrets redacted.

For an agent on the PC the Tab5 is plugged into (tab5/BRIDGE.md). It sends
console lines and keys, captures what comes back for a while, and prints and
saves it with every line that could show the SDK token or an API key replaced.

    python tab5_console.py --port COM4 --send ">status" --secs 3
    python tab5_console.py --port COM4 --reset --secs 40 --send ">log" --out boot.log
    python tab5_console.py --port COM4 --secs 30 --until "shaken"    # wait for a line

Console lines (">..."): status, log, power, tts, tts=URL, tts.voice=NAME,
face=NAME, chat=TEXT, nap, imu. Single keys: d / u (talk button down / up),
m (play a test MP3), z / w (sleep / wake), p (screenshot to the cloud), a / s
(menu down / select). Send keys with --key.
"""

import argparse
import re
import subprocess
import sys
import time

SECRET_LINE = re.compile(r"^.*(SDK token|mgst_|xi-api-key|elk_).*$", re.M)


def redact(text):
    return SECRET_LINE.sub("[line redacted: may hold a secret]", text)


def reset(port):
    """Resets the board the way esptool does: into the bootloader, then out."""
    r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", port,
                        "--after", "hard-reset", "chip-id"], capture_output=True, text=True)
    if r.returncode:
        sys.exit("reset failed: " + (r.stdout + r.stderr)[-400:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--send", action="append", default=[], help="a console line, e.g. \">status\"; repeatable")
    ap.add_argument("--key", action="append", default=[], help="single keys, e.g. d; repeatable")
    ap.add_argument("--secs", type=float, default=5, help="how long to listen")
    ap.add_argument("--after", type=float, default=0.5, help="seconds to listen before sending")
    ap.add_argument("--until", help="stop early once a line matches this regex")
    ap.add_argument("--reset", action="store_true", help="reset the board first")
    ap.add_argument("--out", help="also save the redacted output here")
    a = ap.parse_args()

    import serial  # comes with esptool

    if a.reset:
        reset(a.port)
    until = re.compile(a.until) if a.until else None
    out = open(a.out, "w", encoding="utf-8") if a.out else None
    start = time.time()
    end = start + a.secs
    sent = False
    pending = ""
    while time.time() < end:
        try:
            with serial.Serial(a.port, 115200, timeout=0.2) as s:
                while time.time() < end:
                    if not sent and time.time() - start >= a.after:
                        for line in a.send:
                            s.write(line.encode() + b"\n")
                            time.sleep(0.2)
                        for k in a.key:
                            s.write(k.encode())
                            time.sleep(0.2)
                        sent = True
                    d = s.read(4096)
                    if not d:
                        continue
                    pending += d.decode("utf-8", "replace")
                    *lines, pending = pending.split("\n")
                    for line in lines:
                        clean = redact(line.rstrip("\r"))
                        print(clean, flush=True)
                        if out:
                            out.write(clean + "\n")
                        if until and until.search(line):
                            end = 0
                            break
        except serial.SerialException:
            time.sleep(0.3)  # the port drops while the board resets
    if pending.strip():
        clean = redact(pending)
        print(clean)
        if out:
            out.write(clean + "\n")
    if out:
        out.close()
    if until and end != 0:
        sys.exit(f"no line matched {a.until!r} in {a.secs:g} s")


if __name__ == "__main__":
    main()
