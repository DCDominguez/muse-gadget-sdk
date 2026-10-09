# Hardware bridge: an agent on the PC the Tab5 is plugged into

The cloud session that writes this firmware can't reach a USB port. A Claude
Code session on the PC can: it flashes, reads the serial console, sends test
commands and reports back, with no copying of files or logs between you and
another agent.

## Set it up (once)

1. On the PC with the Tab5 (Windows, COM4): install Claude Code, either the
   Claude desktop app (its Code tab) or the CLI.
2. Get this repo's `tab5-port` branch:
   `git clone -b tab5-port https://github.com/DCDominguez/muse-gadget-sdk`
   (or `git pull` in your existing clone).
3. Have Python with esptool (the ESP-IDF PowerShell has it, or
   `pip install esptool pyserial`).
4. Start the session in that folder:
   - desktop app: open the folder in the Code tab; or
   - terminal: `claude remote-control` in the folder. It then shows up in
     the Claude app, and the cloud session can message it (it appears in the
     cloud session's agent list while it runs).
5. Put your token where only the flash script reads it: open
   `tab5/tools/tab5-sdk-token.html` in a browser, save `tab5-sdk-token.txt`
   to `%USERPROFILE%\tab5-secrets\` (and `tab5-elevenlabs-key.txt` if you use
   ElevenLabs). The agent never opens these files.

Firmware comes as a kit from the cloud session (`tab5/kits/` is not in git;
you get the zip in the Claude app), or the PC session builds it with ESP-IDF
v6.0.1 (`esp32/AGENTS.md`).

## Rules for the agent on the PC

These are the owner's rules; follow them exactly.

- **Secrets.** Never open, read, print, copy or upload `tab5-sdk-token.txt`,
  `tab5-elevenlabs-key.txt` or anything in `%USERPROFILE%\tab5-secrets\`. Pass
  them only as `--token-file` / `--key-file` to `tab5_flash.py`. Never print a
  log line that holds `SDK token`, `mgst_` or a key: use `tab5_console.py`,
  which redacts them.
- **Never:** `erase-flash`, anything with eFuses (`espefuse`), Secure Boot or
  flash encryption, writing to the ESP32-C6 (esp_hosted OTA or slave updates),
  `--force`, or flashing anything but a kit's files.
- **Backups.** `%USERPROFILE%\tab5-backups\tab5-80f1b2d1447d-*.bin` (16 MB)
  must exist before any write; `tab5_flash.py` checks.
- **Reboot loops.** Three `ESP-ROM` banners in a row: stop and report. The
  rollback (`FLASHING.md`) is the owner's call.
- **Report** the redacted log lines that matter, not whole logs, and nothing
  from the user's network beyond what the test needs.

## Commands

From the unzipped kit folder (the scripts are also in `tab5/tools/`):

```powershell
# Flash the app (keeps pairing and Wi-Fi), keep the token file for next time
python tab5_flash.py --port COM4 --token-file $env:USERPROFILE\tab5-secrets\tab5-sdk-token.txt --keep-secret-files --log-secs 40

# Console: status, the log since power-on, a test, waiting for a line
python tab5_console.py --port COM4 --send ">status" --secs 3
python tab5_console.py --port COM4 --send ">log" --secs 6 --out log.txt
python tab5_console.py --port COM4 --send ">imu" --secs 2
python tab5_console.py --port COM4 --send ">cam" --secs 15 --until "@cam"
python tab5_console.py --port COM4 --send ">wifi" --secs 3
python tab5_console.py --port COM4 --send ">img=http://HOST/pic.jpg" --secs 20 --until "@img"
python tab5_console.py --port COM4 --secs 30 --until "shaken"
python tab5_console.py --port COM4 --reset --secs 40 --out boot.log
```

Console lines: `status`, `log`, `power`, `imu`, `img=URL`, `cam`, `wifi`, `scan`, `tts`, `tts=URL`,
`face=NAME`, `chat=TEXT` (types to Muse), `nap`. Keys (`--key`): `d`/`u`
talk button down/up, `m` test MP3, `z`/`w` sleep/wake.

`TESTING.md` is the checklist; the PC agent can run every console-driven
test itself and asks the owner only for the physical ones (shake, touch,
look at the screen).
