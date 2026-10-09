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
| 2.6a | Keyboard plugged in, Settings > Wi-Fi > a network `[new]` | The password page opens without the on-screen keyboard; typing on the Tab5 Keyboard fills the field; Enter joins; Esc cancels; Backspace and Delete work |
| 2.6b | On that page, tap the field `[new]` | The on-screen keyboard appears; both keyboards type into the field |
| 2.6c | Unplug the keyboard while the password page is open `[new]` | The on-screen keyboard appears within a second |
| 2.6d | Keyboard plugged in, Settings closed | Typing still goes to the chat line, as before |
| 2.7 | Emoji button (🙂, left of Send) `[new]` | A grid of 24 emoji opens over the chat; a tap puts one in the line and closes it; Delete removes it whole |
| 2.8 | Send an emoji, and ask Muse to reply with some `[new]` | They show in both bubbles. Faces, hands, hearts and common symbols show; others, skin tones and joined emoji (families, flags) show as their plain parts or not at all, never as boxes |

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
| 4.1a | A long reply, spoken or captioned `[new]` | The chat column shows it once, with no repeated phrases |
| 4.6 | All messages on (Settings), type to Muse in the Muse app | The reply appears (and is spoken) on the Tab5 |

## 5. Commands Muse can use

Ask Muse in plain words; it picks the command. The log shows the command name.

| # | Ask Muse | Command | Pass when |
|---|---|---|---|
| 5.1 | "Show me a picture of a cat" | `display.draw_url` | The image fills the Muse area; log `link.image: image download done: jpeg WxH` |
| 5.1a | `>img=http://URL-of-a-baseline.jpg` on the console `[new]` | (no command) | The same without Muse: `@img {"ok":true,...}` and the image shows. On a fail, `@img` gives the reason (`out_of_memory`, `invalid_image`, HTTP status) |
| 5.2 | "Go back to your face" | `display.show_animation` | Avatar returns |
| 5.3 | "Set the volume to 60" `[new]` | `voice.configure` | Volume changes; the dock slider and Settings show 60 |
| 5.4 | "What's your volume?" `[new]` | `voice.configure` | Muse says the level |
| 5.4a | Camera button (strip under Muse) `[new]` | Reads Camera Off after a first flash; a tap turns it purple, Camera On; it stays so after a restart. Off, "take a photo" fails with "the camera is turned off on the device" and the camera isn't powered |
| 5.4b | `>cam` on the console, camera On `[new]` | The red notice shows; the photo appears over Muse for 5 s; `@cam {"ok":true,"jpeg_bytes":N,...}`. With the camera Off: `"ok":false` and "turned off" |
| 5.5 | "Take a photo and tell me what you see" `[new]` | `camera.capture` | Muse describes the scene; log `tab5.camera: 1280x720 -> 640x352, 180 degrees, N bytes JPEG` |
| 5.6 | "What's your battery and uptime?" | `device.health` | Sensible battery %, mV, charging |
| 5.7 | "What devices are on my network?" | `device.discover` | A device list |
| 5.8 | "Is my router's web page up?" (any LAN address) | tunnel | Muse reaches it |
| 5.9 | OTA (`FLASHING.md`, "Update over the air") | `device.ota` | New version after the reboot; `>status` shows it |

If 5.1 fails, run 5.1a with a plain `http://` JPEG, then an `https://` one: it
separates the download (memory, TLS) from drawing. `@img` ok with nothing on
screen points at the image box; a fail at `could not connect` or
`out_of_memory` points at the download.

For 5.5, turn the camera on first (5.4a). A failure logs `link.app: camera.capture failed: <reason>`, which `>log` keeps; the camera's own lines (`tab5.camera`) show only on the live console.

For 5.5, if the photo is upside down or sideways, note how it's turned; the fix
is `CONFIG_MUSE_TAB5_CAMERA_ROTATION`. Strange colours (blue faces) mean the
RGB565 byte order needs swapping; say so.

## 5b. Wi-Fi `[new]`

| # | Test | Pass when |
|---|---|---|
| 5b.1 | `>wifi` | `@wifi {...}`: connected, RSSI and channel; `drops` counts drops from a working connection; `recent` lists the last 8 with uptime `t_s`, `reason` (ESP-IDF `wifi_err_reason_t`), `rssi` and `back_ms` (time to the next IP; -1 while still down) |
| 5b.1a | `>scan` `[new]` | `@scan {"passive":[...],"active":[...]}` lists the networks heard. During an outage: `mochimesh` in `passive` but not `active` means the router ignores this Tab5's probes; in neither, its 2.4 GHz side is silent |
| 5b.2 | After a drop | Before restarting, run `>wifi` and `>log`: the reason and how long the way back took |

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
