# Flashing Muse onto a Tab5

Windows steps, with the Tab5 on its USB-C port (the chip's own USB,
`303a:1001`; COM4 below). Each step can be handed to an agent; the token step
can't.

## 1. Back up the Tab5 (once, before anything is written)

From an ESP-IDF PowerShell (anywhere `python -m esptool` works):

```powershell
.\tab5\tools\tab5-preflight.ps1 -Port COM4
```

Read-only. It checks the chip is an ESP32-P4 with 16 MB of flash, reads the
MAC, reads the whole flash twice, compares the SHA-256 of both reads, and keeps
`%USERPROFILE%\tab5-backups\tab5-<mac>-<time>.bin` and a report next to it. The
flash script refuses to run without this backup. Keep a second copy of it
somewhere else, and don't share it: it can hold the old firmware's Wi-Fi
passwords. The backup covers the P4 only, not the ESP32-C6.

## 2. Build a kit

On a machine with ESP-IDF v6.0.1:

```sh
tab5/tools/make_kit.sh test4          # -> tab5/kits/tab5-kit-test4.zip
```

The firmware in the kit holds a 48-character placeholder where the SDK token
goes; no token is built in. The kit holds:

| File | |
|---|---|
| `tab5-muse-base.bin` | Bootloader, partition table, OTA data and the app, merged; written at `0x0` on a first flash |
| `tab5-muse-app-unsigned.bin` | The app, unsigned, with the placeholder |
| `dev_signing_key.pem` | The SDK's committed development key (`esp32/dev_signing_key.pem`), which the build signs with too |
| `tab5_flash.py`, `tab5-flash.ps1` | Checks, token, sign, flash, boot log |
| `tab5-sdk-token.html` | Offline token form |
| `CODEX_PROMPT.md` | For an agent doing the flashing |
| `SHA256SUMS.txt` | Checked before anything is written |

## 3. Put in the SDK token (only you)

Get it from gadgets.muse.ai → Account → SDK tokens. Then either:

- open `tab5-sdk-token.html` in a browser, paste the token and save
  `tab5-sdk-token.txt` into the kit folder or Downloads. The page's Content
  Security Policy blocks every network request, so it can't send the token
  anywhere; or
- skip it, and `tab5-flash.ps1` opens a masked Windows dialog during the flash.

**Spoken replies (optional).** Muse's replies are text; the Tab5 speaks them
with ElevenLabs if the firmware has your ElevenLabs API key (with the Text to
Speech permission). The form's second field saves it as
`tab5-elevenlabs-key.txt`; the dialog has a second field too. It's patched in
like the token. Without it, replies stay text.

The token reaches `tab5_flash.py` on stdin or as that file, never on a command
line. It is never printed (the firmware itself logs its first 12 characters as
a hint). The patched image and the token file are overwritten and deleted after
flashing. `tab5-sdk-token.txt` is gitignored.

## 4. Flash

```powershell
.\tab5-flash.ps1 -Port COM4 -First     # first time, over other firmware
.\tab5-flash.ps1 -Port COM4            # later: app only, keeps pairing and Wi-Fi
```

Before writing, it stops (`STOP: ...`) unless the kit matches
`SHA256SUMS.txt`, the token has the right form, the board is an ESP32-P4 v1.x
with the expected MAC (`--mac` in `tab5_flash.py`), and the backup exists. Then:

1. Swaps the token for the placeholder, fixes the image's XOR checksum and
   SHA-256, signs it (`espsecure sign-data --version 2`), verifies the
   signature and checks the image with `esptool image-info`.
2. With `-First`: erases Muse's factory-data slot (`0x820000`, 8 KB, which
   holds leftovers of the old firmware) and writes the base image at `0x0`.
   The base image also blanks Muse's settings area (`0x11000`-`0x1D000`).
3. Writes the signed app at `0x20000` and resets the board.
4. Captures 60 s of serial output to `tab5-boot.log`. After 20 s it sends
   `>log`, which prints the log kept in memory since power-on: Windows
   re-enumerates the USB port after a reset and misses the first seconds.

It never erases the whole chip, touches eFuses, enables Secure Boot or flash
encryption, or writes to the ESP32-C6.

## 5. Read the boot log

A good boot shows `link.main: Muse Gadget starting`, `muse: board: M5Stack
Tab5`, `board: display 1280x720, rotated 90 degrees`, the BSP's `Discovered
board version N`, esp_hosted's transport and slave lines, `WIFI STA MAC`,
`muse_dock: dock:` and `UI up: 800x480`. Watch for `Guru Meditation`,
`abort()`, `assert`, or repeated `ESP-ROM` banners (a reboot loop: stop after
three, per `plan.md`).

The Muse console also answers `>status`, `>power` and `>log` at any time.

## Update over the air

Muse installs firmware with `device.ota` from an HTTPS URL it's given. The
image must be signed with the SDK's dev key (checked before it's installed),
and newer than the running version unless the request says `force`. Every
build is `999.0.0` (`esp32/version.txt`), so build OTA kits with a higher
version:

```sh
KIT_VERSION=999.1.0 tab5/tools/make_kit.sh ota1
```

On your PC, in the unzipped kit, make the image with your token in it:

```powershell
python tab5_flash.py --ota-out tab5-ota.bin --token-file tab5-sdk-token.txt
```

**`tab5-ota.bin` holds your SDK token.** Host it privately over HTTPS (a
private bucket or a short-lived signed URL), never as a release asset of the
public fork. Then ask Muse to update the Tab5 from that URL. After the reboot
the new image must reach Muse's control session within the rollback window
(`main/app.c`), or the Tab5 goes back to the previous image by itself.

## Roll back

To restore the Tab5 exactly as it was backed up:

```powershell
python -m esptool --chip esp32p4 -p COM4 -b 460800 write-flash 0 "%USERPROFILE%\tab5-backups\tab5-<mac>-<time>.bin"
```

If esptool can't connect, put the Tab5 in download mode with the button sequence in
M5Stack's Tab5 docs (not yet verified on this unit; see `plan.md`) and try again.
