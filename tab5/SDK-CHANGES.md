# What the Tab5 port changes outside its board

Most of the port is new files for one board. These are the changes to code
other boards share, with why and what they do elsewhere. Upstream is
`b139b45` (the fork point); `git diff b139b45 -- esp32` shows all of it.

## Board interface (`components/muse/muse_board.h`)

| Addition | Why | Other boards |
|---|---|---|
| `radio_init` | The Tab5's radio is an ESP32-C6 behind an IO expander; it must be powered before Home Link starts Wi-Fi, or `esp_wifi_init()`'s `ESP_ERROR_CHECK` aborts into a reboot loop. Called from `muse_glue_start()` in `app_main`. | NULL: nothing runs |
| `ui_x`, `ui_y` | Muse's UI as a box on a larger panel | 0: unchanged |
| `flip_display`, `keyboard_present` | For the dock's Flip button and status line | NULL |
| `muse_input_post_buttons()` | Button edges from other tasks (on-screen talk, keyboard, dock buttons) through the normal input path | Unused |

## UI

- `muse_ui.c`: when the display is larger than the board's `width`×`height`,
  builds the UI in a box at `ui_x`, `ui_y` (page dots, images and the pairing
  card stay in it; the sleep cover still covers the screen) and calls
  `muse_dock_build()` for the rest. Otherwise unchanged.
- `muse_dock.c` / `.h` (new): status, typed chat, hold-to-talk and quick
  controls. Built only in that case. Typed turns need `CONFIG_MUSE_HATCH`.
- `muse_chat.h`, `muse_chat_text.c`: `muse_hatch_set_console_hook()`. Typed
  turns' replies went only to the serial console as `@chat` lines; the hook
  hands the dock the same events. No hook set: unchanged.

## Console

- `muse_input.c`: `>log` prints the log captured since boot. A weak default
  says `@log none`; `main/muse_glue.c` provides the real one from
  `diagnostic_log` (with `CONFIG_HOMEHUB_SUPPORT_BUG_REPORT`). Available on
  every board; a host that reconnects late after a reset still sees the boot.

## Radio on a co-processor (`main/`)

- `ble_server.c`: with `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`, connects to the
  co-processor and asks it to enable its BT controller before
  `nimble_port_init()` (a failure is logged, not fatal: an older C6 may not
  know the request), and turns it off where other boards release controller
  memory. `esp_bt.h` is included only without esp_hosted.
- `idf_component.yml`: `esp_wifi_remote` (>=1.3.1), `esp_hosted` (~2.12.13)
  and `esp_jpeg` (^1.3.1), each only for `esp32p4`.
- `image_fetch.c`: the P4's ROM has no TJpgDec, so P4 builds use `esp_jpeg`'s
  copy (a later TJpgDec with `size_t` lengths). Keyed on `CONFIG_JD_SZBUF`;
  other chips keep the ROM decoder.

## Build

- `cmake/project.cmake` adds `components_p4/` to `EXTRA_COMPONENT_DIRS` for
  `esp32p4` only. It holds `esp_lvgl_port` 2.9.0 with one fix: ESP-IDF
  6.0.1's DPI panel callbacks dropped `on_frame_buf_complete`, so its DSI path
  doesn't compile ([`components_p4/README.md`](../esp32/components_p4/README.md)).
  Drop it when a registry release builds on IDF 6.0.1.
- `components/muse/Kconfig`: `MUSE_BOARD_M5STACK_TAB5`
  (`depends on IDF_TARGET_ESP32P4`), `MUSE_BOARD_ID "m5stack_tab5"`,
  `MUSE_TAB5_ROTATE_270`. `CMakeLists.txt` and `idf_component.yml`
  (`espressif/m5stack_tab5` 1.3.1, keyed on `MUSE_BOARD_ID`) as for any board.
- `tools/muse/board.sh`, `ports.py`, `avatar.py`: the `tab5` alias.

## Spoken replies and All messages (from wupsbr/waveshare-muse-gadget-sdk)

Ported from [wupsbr/waveshare-muse-gadget-sdk](https://github.com/wupsbr/waveshare-muse-gadget-sdk)
(Apache-2.0), which forks the same upstream commit; see its
`docs/CHANGES-FROM-UPSTREAM.md`, changes 4, 5 and 10.

- `muse_chat_session.cpp`: theirs as is, plus one change: the ElevenLabs key
  is read through a volatile pointer (`elevenlabs_key()`), so a key written
  into the image after the build (the flashing kit) takes effect.
- Ours: a speech server on the home network (OpenAI's `/v1/audio/speech`, as
  Kokoro-FastAPI serves it), set at run time with the console command `>tts=URL`
  (`muse_input.c`; `>tts.voice=`), kept in NVS (`muse_settings.*`: `tts_url`,
  `tts_voice`) and used before ElevenLabs. `Kconfig`: `MUSE_TTS_VOICE`
  (`af_heart`), `MUSE_TTS_MODEL` (`kokoro`). See [`KOKORO.md`](KOKORO.md).
- `muse_chat.h`, `muse_voice.c`: pushes (`muse_hatch_push_take/drop`,
  `play_push`). The snore and other reaction changes are not ported yet.
  `muse_chat_link.c` (boards without PSRAM) gets no-op push functions, which
  the fork didn't need since it builds no such board.
- `muse_settings.*`, `muse_settings_ui.c`: the **All messages** switch (off by
  default, NVS `pushes`) and the volume slider following changes made
  elsewhere. Their simpler Battery page is not ported.
- `Kconfig`: `MUSE_ELEVENLABS_API_KEY`, `_VOICE_ID`, `_MODEL`. Tab5 overlay:
  `MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` for api.elevenlabs.io.
- `tests/test_muse_chat_session.py`: theirs.

## Reactions (from wupsbr/waveshare-muse-gadget-sdk)

Their commits `1e91a29`, `eaacdae`, `4e3d679` and `090a83f` (Apache-2.0),
firmware only: dizzy when shaken, drowsing with a snore before auto-sleep,
waking, tickle (a rub or a flurry of taps), and the touch-drag volume that
tickle builds on. Every board with the full UI gets drowsing, waking and
tickle; shaking needs an accelerometer.

- `muse_imu.*`: theirs (QMI8658), plus `muse_imu_use()`, which takes a board's
  own reader. The Tab5 hands it the BMI270 through Espressif's `bmi270`
  driver (0x68, +-8 g, 200 Hz).
- `muse_ui.c`: the volume drag and tickle start only on Muse's tiles, not on a
  dock beside them.
- `esp32/avatar/muse_pixel.c`: their Jollybot drawings of the reactions, in a
  commit of their own; not Apache-licensed (`esp32/avatar/REACTIONS-NOTICE.md`).

## Commands Muse can use

- `voice.configure` is answered on every Muse board (it was the Voice PE
  board's): it sets the volume in Muse's settings (`main/muse_glue.c`), so the
  face, Settings and the Tab5 dock follow.
- `camera.capture` is board-neutral: `CONFIG_MUSE_CAMERA_CAPTURE`, on with the
  Watcher's camera or the Tab5's. Boards other than the Watcher answer through
  the camera component's new `camera_capture_base64()`. The Tab5's driver
  (`boards/tab5_camera.c`) starts the SC202CS through the BSP and esp_video,
  lets auto exposure settle for 20 frames, then scales and turns the frame with
  the PPA and encodes it with the P4's JPEG engine: 640x352.
  `CONFIG_MUSE_TAB5_CAMERA_ROTATION` (180) turns it upright; Flip adds 180. The
  overlay sets `CONFIG_CAMERA_SC202CS` and the ISP pipeline controller.

## Simulator

`esp32/simulator` compiles `muse_dock.c` (it no longer linked after
`muse_ui.c` began calling `muse_dock_build()`), with stubs for its services.
`--board tab5` renders the Tab5 layout; `tap=`, `keyboard=` and `type=`
scenario keys and `tests/scenarios_tab5/` cover the dock's chat and on-screen
keyboard. `react=dizzy|drowse|wake|tickle` starts a reaction. The Watcher profile and its tests are unchanged.

## Docs

`esp32/AGENTS.md`, `esp32/README.md`, `esp32/devices/README.md` and
`esp32/devices/AGENTS.md` list the Tab5 like the other boards.

## Checked

Tab5, M5Stack CoreS3 and M5Stack Cardputer ADV (no PSRAM, no typed chat)
build on ESP-IDF 6.0.1. The host tests pass (`python3 -m unittest discover -s
tests -p 'test_*.py'`: 181 run, 2 skipped for host libraries).

## Hardware bring-up corrections (2026-10-07)

- Tab5 overlay prefers the hosted PSRAM pool. Vendored esp_hosted v2.12.13 uses
  aligned PSRAM without MALLOC_CAP_DMA for P4 SDIO; internal DMA fallback remains.
- P4 active-low SDIO reset ends deasserted, allowing C6 card enumeration.
- P4 identity uses the factory base MAC, avoiding an all-zero local Wi-Fi identity.
- Physical HID Delete maps to forward deletion in the chat textarea.
- Battery initialization restores IO1 CHG_EN high, nCHG_QC_EN low and PWROFF low
  following M5Unified; output latches are set before output drivers. INA226 raw
  voltage changes are logged. Invalid percentage renders unavailable, not 0%.
- PC USB detection uses the IDF USB Serial/JTAG connection monitor as well as
  charger status/current, so runtime measurement does not keep running on PC USB.

These changes built with IDF 6.0.1 and were flashed app-only. Hardware evidence and
remaining battery limits are in bringup-log.md. Historical host-test results above
precede this work; the current Windows run is not a clean pass.

## Pixel UI redesign and bulletin board (2026-10-09/10)

Everything below is off for other boards unless noted: the pixel look is
`CONFIG_MUSE_PIXEL_THEME` (default on only for the Tab5), the bulletin board
`CONFIG_HOMEHUB_BULLETIN` (likewise), and the dock only exists on a screen
larger than the board's UI box.

| Where | Change | Other boards |
|---|---|---|
| `avatar/muse_pixel.c`, `muse_pixel.h` | `muse_pose_t` gains `facing` (radians, 0 = front) and `walk` (step phase); new `muse_pixel_scale_alpha()` writes alpha for a see-through avatar | `facing` 0 draws exactly as before; a custom avatar without the function gets a weak fallback |
| `muse_ui.c`, `.h` | See-through avatar over the dock's scenery (RGB565A8 strips); `muse_ui_set_origin()`, `muse_ui_feet()`, `muse_ui_set_motion()` for the wandering; images framed as a polaroid; docked, settings live in the dock's window rather than a tile, and the face's caption, reply and meter overlays are hidden; the pairing card takes the pixel look | Undocked boards: unchanged (the settings tile, captions and answer layouts as before) |
| `muse_settings_ui.c`, `.h` | Pages size themselves from their container; pixel styling under the theme; `muse_settings_ui_home()`, `muse_settings_ui_open()` | Unchanged without the theme |
| `muse_dock.c`, `.h`, `muse_scene.c`, `muse_props.c`, `muse_pixel_style.h` | The dock rewritten; scenery, props, shared palette and fonts (new files); `muse_dock_show_settings()` | Not built |
| `muse_chat_session.cpp` | Logs Muse's `agent.status`/`task.status` and forwards `activity_text` to the console hook as `activity` | Log lines only; no hook, no change |
| `muse_input.c`, `muse_console.h`, `main/muse_glue.c` | Console `>cam`, `>wifi`, `>scan`, `>ideas[.clear]`, `>dock=…` (weak defaults say unsupported) | Answer "unsupported" |
| `main/app.c` | Unpair only after 4 refresh rejections over 30 min; `bulletin.read`/`.post` dispatch | Sign-in change applies to every board |
| `main/noise_control.cpp` | Advertises `bulletin.read` and `bulletin.post` | Only with the option |
| `main/bulletin.c`, `.h`, `main/CMakeLists.txt`, `main/Kconfig.projbuild` | The bulletin board; the notes file is embedded as `bulletin.md` | Only with the option |
| `main/wifi_mgr.c`, `.h` | A drop log and passive scans | Shared; behaviour unchanged |
| `boards/board_m5stack_tab5.c` | Muse area 720×600, avatar 384 px, LVGL task stack 16 KB, camera off at boot | Tab5 only |
| `muse/Kconfig` | `MUSE_PIXEL_THEME`, `MUSE_CLOCK_TZ` | Defaults off / UTC |
| `tools/muse/snap.py`, `gen_pixel_fonts.sh` | Big-screen screenshots; the fonts | — |

New host tests: `tests/test_link_bulletin.py`; the unpair and Wi-Fi scan
contract tests were extended.
