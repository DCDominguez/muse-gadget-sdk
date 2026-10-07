# Task: flash the Muse test build onto my M5Stack Tab5 and report the boot log

You're on my Windows PC. The Tab5 is on **COM4**. This folder is the flashing
kit. Do only what's below. Do not edit, rebuild or "fix" the firmware, and do
not improvise around a failure: stop and report it.

## The SDK token and the ElevenLabs key are secret
- Never ask me for them, read them, print them, copy them, or open
  `tab5-sdk-token.txt` or `tab5-elevenlabs-key.txt`.
- `tab5-flash.ps1` handles it. If a window titled "Muse SDK key for the Tab5"
  appears, that's for me: wait for me to fill it in.
- When you quote the log, redact the line containing `SDK token:`.

## Steps
1. Close anything using COM4 (miniterm, serial monitors, Arduino IDE).
2. Check the kit: `Get-FileHash -Algorithm SHA256` on every file named in
   `SHA256SUMS.txt` must match. Stop if any differs.
3. Confirm the stock backup exists: `%USERPROFILE%\tab5-backups\tab5-80f1b2d1447d-*.bin` (from `tab5-preflight.ps1`),
   exactly 16777216 bytes. Stop if not.
4. Run, from this folder, in PowerShell:
   `.\tab5-flash.ps1 -Port COM4 -First`
   It checks the board (ESP32-P4 v1.x, MAC 80:f1:b2:d1:44:7d) and the backup,
   puts my token into the image, signs and verifies it, flashes, then captures
   60 s of log to `tab5-boot.log` (it asks the firmware for the log since
   power-on with `>log`). Flashing takes a few minutes; don't interrupt it.
   If it prints `STOP:`, report that line verbatim and end.
   If esptool can't connect, tell me: the Tab5 may need its download-mode
   button sequence. Don't retry more than once.
5. Read `tab5-boot.log` and report:
   - Did it reach `Muse Gadget starting`? Any `Guru Meditation`, `abort()`,
     `panic`, `assert`, or more than 2 `ESP-ROM` banners (a reboot loop)?
   - Lines matching (case-insensitive): `board:`, `display`, `rotated`,
     `Discovered board version`, `Unsupported board`, `esp_hosted`, `hosted`,
     `slave`, `transport`, `co-processor`, `WIFI STA MAC`, `wifi`, `BLE`,
     `nimble`, `Tab5 Keyboard`, `INA226`, `dock:`, `UI up`, `PSRAM`,
     `chip revision`, `E (`, `W (`.
   - Paste the `@log begin` ... `@log end` block in full (it's the boot from
     power-on), with the `SDK token:` line redacted.

## If it reboot-loops
Stop. Report the log. Don't reflash or erase anything. The rollback, which I
decide on, is:
`python -m esptool --chip esp32p4 -p COM4 -b 460800 write-flash 0 "%USERPROFILE%\tab5-backups\<that backup>.bin"`

## Never
- `erase-flash`, `espefuse`/anything with eFuses, Secure Boot or flash encryption.
- Anything that writes to the ESP32-C6 (esp_hosted OTA, slave updates).
- `--force`, or flashing any file not in this kit.

## Then ask me to check on the screen
Muse face on the left, status and chat on the right, talk button and controls
at the bottom; taps land where I touch (if not: the Flip button); battery shows
about 6.0-8.4 V; the keyboard line changes when I plug the Tab5 Keyboard in;
typed text appears in the chat box; Sleep then a touch wakes it.
