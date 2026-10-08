# Tab5 test checklist

Everything the Tab5 build does, how to try it, and what the log should say.
Run it after flashing a kit (`FLASHING.md`). Log lines come from the serial
console (`python -m serial.tools.miniterm COM4 115200`, or `>log` for the log
since power-on). Mark each one **pass**, **fail** (with the log lines around
it) or **skip**.

`[new]` marks what no earlier kit had.

## 1. Start-up

| # | Test | Pass when |
|---|---|---|
| 1.1 | Power on | `Muse Gadget starting`, `board: display 1280x720, rotated 90 degrees`, `UI up: 800x480`; no `Guru Meditation`, `abort()` or repeated `ESP-ROM` |
| 1.2 | Radio | esp_hosted transport and slave lines, `WIFI STA MAC`, joins Wi-Fi |
| 1.3 | Motion sensor `[new]` | `muse_imu: BMI270 +-8 g, 200 Hz: shake reaction on` (a `BMI270 at 0x68: ...` warning means no shake reaction) |
| 1.4 | Muse | Status card says online; `>status` shows the board and Wi-Fi |

## 2. Screen, touch, keyboard

| # | Test | Pass when |
|---|---|---|
| 2.1 | Tap Muse | Hearts; taps land where you touch (if not, try Flip) |
| 2.2 | Swipe left on Muse | Settings; swipe back |
| 2.3 | Flip | The picture turns 180° and stays so after a restart |
| 2.4 | Chat line, keyboard unplugged | An on-screen keyboard opens over the bottom strip; Send works |
| 2.5 | Tab5 Keyboard | Plug it in: the status line changes; typing goes to the chat; Enter sends; Esc clears; ↑/↓ scroll; Delete deletes forward |
| 2.6 | Hold Tab, or the on-screen talk button | Muse listens while held |

## 3. Reactions `[new]`

Only while Muse is idle (not listening, thinking or speaking).

| # | Test | Pass when |
|---|---|---|
| 3.1 | Shake the Tab5 hard, back and forth, about three times | Dizzy: spiral eyes and stars for 4 s; log `shaken: dizzy` |
| 3.2 | Rub Muse's face quickly with one finger | Startle, then giggling while you keep going; log `tickled (rub)` |
| 3.3 | Tap Muse four times quickly | Tickled; log `tickled (taps)` |
| 3.4 | Drag up or down on Muse | Volume indicator; the level is heard and kept; the dock's slider follows |
| 3.5 | Leave it idle until auto-sleep (Settings sets the time) | 4 s before the screen goes dark: yawn, drooping eyes, two soft snores (Speaker on) |
| 3.6 | Touch during the yawn | Muse wakes with a blink and stretch |
| 3.7 | Rub or tap on the dock (chat, buttons) | No tickle, no volume change |

## 4. Voice and speech

| # | Test | Pass when |
|---|---|---|
| 4.1 | Talk to Muse | Reply appears in captions and the chat |
| 4.2 | Kokoro (`KOKORO.md`; `>tts=http://PC:8880`) | Reply is spoken; log `speech: first audio after ...`, `speech: N bytes of MP3` |
| 4.3 | `>tts.voice=bf_emma` | Next reply in another voice |
| 4.4 | Kokoro stopped | Reply stays text; log `speech: can't reach the speech server` |
| 4.5 | Speaker off (Settings) | Replies stay text |
| 4.6 | All messages on (Settings), type to Muse in the Muse app | The reply appears (and is spoken) on the Tab5 |

## 5. Commands Muse can use

Ask Muse in plain words; it picks the command. The log shows the command name.

| # | Ask Muse | Command | Pass when |
|---|---|---|---|
| 5.1 | "Show me a picture of a cat" | `display.draw_url` | The image fills the Muse area |
| 5.2 | "Go back to your face" | `display.show_animation` | Avatar returns |
| 5.3 | "Set the volume to 60" `[new]` | `voice.configure` | Volume changes; the dock slider and Settings show 60 |
| 5.4 | "What's your volume?" `[new]` | `voice.configure` | Muse says the level |
| 5.5 | "Take a photo and tell me what you see" `[new]` | `camera.capture` | Muse describes the scene; log `tab5.camera: 1280x720 -> 640x352, 180 degrees, N bytes JPEG` |
| 5.6 | "What's your battery and uptime?" | `device.health` | Sensible battery %, mV, charging |
| 5.7 | "What devices are on my network?" | `device.discover` | A device list |
| 5.8 | "Is my router's web page up?" (any LAN address) | tunnel | Muse reaches it |
| 5.9 | OTA (`FLASHING.md`, "Update over the air") | `device.ota` | New version after the reboot; `>status` shows it |

For 5.5, if the photo is upside down or sideways, note how it's turned; the fix
is `CONFIG_MUSE_TAB5_CAMERA_ROTATION`. Strange colours (blue faces) mean the
RGB565 byte order needs swapping; say so.

## 6. Power

| # | Test | Pass when |
|---|---|---|
| 6.1 | Battery card | 6.0-8.4 V, a percentage, charging on USB |
| 6.2 | `>power` | INA226 readings, charger state |
| 6.3 | Sleep button, then a touch or a key | Screen off, then back with the waking reaction |
| 6.4 | Shake while asleep `[new]` | Screen wakes |
| 6.5 | Hold Off 1.5 s on battery | Powers off (on USB it stays on) |

## Reporting

For each fail: the test number, what happened, and the log from a few seconds
before to after. `>log` after a restart has everything since power-on.
