# PLAN: Muse gadget on the M5Stack Tab5

**Goal:** a full Muse device on the Tab5: on-screen agent, push-to-talk voice in and out, typed chat on the touchscreen, Wi-Fi, onboard hardware exposed to Muse as tools.

**Repo:** fork `DCDominguez/muse-gadget-sdk` (upstream `facebookincubator/muse-gadget-sdk`), branch `tab5-port`, work in `esp32/`. Read `esp32/AGENTS.md` and `esp32/devices/AGENTS.md` first. They are the source of truth for build, flash, new-board and new-command procedures.

**Format:** 3 phases. Each ends at a hard gate. Do not start a phase until the user approves the previous gate.

---

## Repo setup (fork)

Work happens in the user's **fork**, not upstream. A fork of a public repo is public, so no secret may ever be committed.

```sh
git clone https://github.com/DCDominguez/muse-gadget-sdk.git
cd muse-gadget-sdk
git remote add upstream https://github.com/facebookincubator/muse-gadget-sdk.git
git checkout -b tab5-port
```

- Keep `plan.md` and `bringup-log.md` in `tab5/` (not inside `esp32/`), with the Tab5 tools in `tab5/tools/`.
- Before the first build, check `.gitignore`. The SDK ignores generated `sdkconfig` files (where the token lives). Add flash backups (`tab5-backup.bin`, `*-backup.bin`) to `.gitignore`, or keep them outside the repo.
- **Before every commit**, confirm no secrets are staged: `git diff --cached | grep -i mgst_` must return nothing. Also confirm no `sdkconfig`, `managed_components/`, `dependencies.lock` or `.bin` backups are staged.
- Commit per gate on the `tab5-port` branch. Pull SDK updates with `git fetch upstream` and merge deliberately; do not rebase or force-push without asking.
- Upstream PRs need Meta's CLA (`CONTRIBUTING.md`). Never open a PR against upstream without asking.

---

## Verified facts (from the SDK and vendor docs)

- The SDK has **no ESP32-P4 support**. Targets: esp32, esp32s3, esp32c5, esp32c6. Every `MUSE_BOARD_*` option depends on one of those. This is a port.
- ESP-IDF **v6.0.1 only** is supported by the SDK.
- Full UI already exists (avatar, voice, touch settings, LVGL 9.5.0, `esp_codec_dev`). It needs a `muse_board_t` for the Tab5 (`components/muse/muse_board.h`). Closest references: `boards/board_waveshare_s3_lcd7.c`, `boards/board_m5stack_cores3.c`.
- Wi-Fi uses plain `esp_wifi` (`main/wifi_mgr.c`). The P4 has no radio: use `esp_wifi_remote` + `esp_hosted` to reach the on-board ESP32-C6 (SDIO).
- BLE pairing uses NimBLE on the local controller (`main/ble_server.c`, `esp_bt.h`). On the Tab5 the radio is on the C6, so this needs NimBLE over hosted HCI. This is the highest-risk item.
- **Typed chat does not exist on any board.** `muse_hatch_text_turn` is fed only from the serial console and its replies go only to the console. LVGL keyboard code exists only for Wi-Fi passwords/settings.
- `camera.capture` exists but is Watcher-gated. New commands follow the recipe in `esp32/AGENTS.md` ("Adding a command").
- Tab5 (M5 docs, ESPHome, NuttX pages): ESP32-P4 (this unit: rev v1.3, per esptool), 16 MB flash, 32 MB PSRAM, 5" 1280x720 MIPI-DSI, ES8388 codec + ES7210 mics, SC2356 camera, BMI270 IMU, RX8130CE RTC, INA226 power monitor, microSD, USB-A host, NP-F550 battery. One shared I2C bus (SDA G31, SCL G32). Two PI4IOE5V6408 IO expanders (0x43, 0x44) gate LCD/touch enables, resets, USB and Wi-Fi power.
- **Display revisions differ:** early units are ILI9881C + GT911 touch; newer units use ST7123/ST7121 with integrated touch. ESPHome reports only the original revision as supported.

## Unverified (resolve in Phase 1, do not assume)

1. ESP-IDF v6.0.1 builds and boots on P4 rev v1.3. Needs `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` (IDF 6.0.1 defaults to rev >= v3.1; the two are mutually exclusive).
2. The Tab5 download-mode procedure.
3. What the C6 currently runs (esp_hosted slave? which version?).
4. This unit's display/touch revision.
5. Whether a Tab5 BSP or panel drivers (ILI9881C, ST7123) exist for IDF 6.0.1 or the component registry.
6. How complete the SDK's "token set by hand" pairing path is.
7. PSRAM mode and speed settings for the P4.

---

## Safety rules (apply to every phase)

**Never, without explicit user approval in chat:**
- Write to the C6 (flash, erase, esp_hosted slave update). Read-only version queries are fine.
- Enable Secure Boot, flash encryption, or any eFuse burn. Do not enable `CONFIG_HOMEHUB_PAIRING_EFUSE_AUTH`. The SDK signs with the committed dev key and says to keep it this way.
- Run `erase-flash` (wipes pairing). Only after the backup is verified and the user agrees.
- Drive any GPIO or IO-expander pin that is not in the verified pin map. This includes M5-Bus, Grove, RS485 and the USB-A VBUS enable. Unknown pins stay in their default state.
- Commit the SDK token, any generated `sdkconfig`, `managed_components/`, or flash backups.

**Always:**
- **Power order:** initialize the IO expanders before any peripheral that depends on them. Enable the display and touch rails through the expander before touching the panel. Disable them on shutdown or panic paths where possible.
- **Audio safety:** start at low volume, step up gradually. No full-scale tones or square waves through the speaker amp. Cap the default volume in the overlay.
- **Battery:** treat the pack as 6.0 V (shutdown) to 8.23 V (full) per ESPHome's page. Do not run deep-discharge tests. Do not leave sustained max-load tests unattended. Watch for heat and stop if the case or battery is warm.
- **Flash wear / loops:** if the board reboot-loops, stop after 3 cycles, capture the log, and report. Do not keep reflashing blindly.
- **Rollback:** the stock backup must be restorable at all times: `python -m esptool --chip esp32p4 -p PORT write-flash 0 tab5-backup.bin`.
- **Build hygiene:** delete the generated `sdkconfig` after changing overlays. Delete `managed_components/` and `dependencies.lock` when switching between boards. Verify every overlay line landed (script in `devices/AGENTS.md` step 8).
- **Naming:** user-visible text says "Muse", never "Hatch" (see `esp32/AGENTS.md`). Cite the source of every pin and register value in a comment.

**Stop and ask the user when:** pinout or datasheet info conflicts; the display revision is unrecognised; the C6 firmware is incompatible; any step needs a C6 write or an eFuse; the board stops enumerating; a gate's exit criteria fail twice.

---

# Phase 1: Safe bring-up, P4 port, local hardware working (no Muse yet)

**Goal:** the SDK builds for the Tab5, boots, and drives every local peripheral safely. Wi-Fi up.

### 1.0 Preflight (blocking; do in order, report findings before continuing)
1. Identify the port and chip: `lsusb`, `python -m esptool chip-id`. Confirm `esp32p4`. Record the revision.
2. **Back up stock flash** (16 MB) with `esptool read-flash`, store outside the repo, verify its size and hash, and tell the user where it is. On Windows, `tab5/tools/tab5-preflight.ps1` automates steps 1 and 2 read-only: chip check, flash check, MAC, backup read twice with SHA256 comparison, and a report. Review its output, and do not write anything to the board until it passes.
3. Record the download-mode procedure from M5's docs. Confirm the board can recover through it before any write.
4. Identify the display/touch revision (ILI9881C+GT911 vs ST7123/ST7121), using M5's demo repo `M5Tab5-UserDemo` and the I2C bus as evidence. Report which one this unit is.
5. Query the C6 firmware version **read-only**. Report it. Do not write.
6. The user supplies the SDK token locally into the build's `sdkconfig`. Never echo it in full.

**Gate 1.0:** user reads the findings (chip rev, display rev, C6 state, backup location) and approves.

### 1.1 Build-only port (no flashing yet)
- Install ESP-IDF v6.0.1. Create `devices/sdkconfig.muse-m5stack-tab5` (loaded after `sdkconfig.muse`): `CONFIG_IDF_TARGET="esp32p4"`, `CONFIG_MUSE_BOARD_M5STACK_TAB5=y`, 16 MB flash, P4 PSRAM settings, low default volume.
- Wire the board per `devices/AGENTS.md` section 6: Kconfig entry (`depends on IDF_TARGET_ESP32P4`) and `MUSE_BOARD_ID "m5stack_tab5"`, `components/muse/CMakeLists.txt`, `idf_component.yml` rules keyed on `MUSE_BOARD_ID`, `boards/board_m5stack_tab5.c`, helper cases in `tools/muse/board.sh`, `ports.py` and `avatar.py`.
- Add Wi-Fi via `esp_wifi_remote` + `esp_hosted` for the P4 host (SDIO to the C6), keeping the existing `esp_wifi` call sites working.
- Build from scratch. Check the overlay lines applied and the `check_sizes` line (flag under 10% free). Run the host tests.

**Gate 1.1:** clean build, tests pass, no flashing done. If the P4 build fails on IDF 6.0.1 or the P4 rev, stop and report.

### 1.2 Staged hardware bring-up (flash, one subsystem per step, log after each)
1. **Boot + log only:** flash, capture 30 s with `tools/muse/monitor.py`. Expect `link.main: Muse Gadget starting`, PSRAM found, no panic or loop.
2. **IO expanders + power rails:** init in the documented order. Read back state before enabling anything.
3. **Display + backlight + touch** (revision-correct drivers). Low brightness first. Touch test: log coordinates.
4. **Audio out:** low-volume ramp. **Audio in:** record and play back a short clip. Check for feedback and clipping.
5. **Wi-Fi via the C6:** join with `CONFIG_HOMEHUB_WIFI_SSID` and `_PASSWORD` set locally; confirm IP and a reconnect after a drop.
6. **Power/battery:** `read_power` from the INA226 (not AXP). Reading only; no discharge tests.
7. **Board buttons/power key** and a safe `power_off` path.

**Gate 1 (exit):** UI and avatar on screen, touch works, audible tone and mic loopback at safe levels, Wi-Fi joined, battery readable, no reboot loop over 10 minutes, stock backup still restores. User approves.

---

# Phase 2: Muse link and the voice loop

**Goal:** paired to Muse, talk to it, hear it answer. This is "the agent on screen."

### 2.1 Pairing and session
- Pairing needs BLE. Preferred: NimBLE host on the P4 over hosted HCI through the C6, keeping the SDK's community pairing v5 flow. First check how the SDK's hand-set-token path works as a fallback so progress isn't blocked.
- Reach the connected state: status screen shows connecting, online and offline correctly (the SDK's colours/states in `esp32/AGENTS.md`).
- Pairing confirm uses a physical or on-screen press. Do not bypass the confirmation step.

**Gate 2.1:** device appears in the Muse app, pairs, and shows the connected state across a Wi-Fi drop and reconnect.

### 2.2 Voice
- Push-to-talk on the screen and the power button: voice note up, reply speech down, captions shown. Voice states idle, listening, thinking and speaking drive the avatar.
- Echo and feedback: speaker and mic are close; use the ES7210 AEC front end. Tune gain and volume ceiling before raising volume. Allow tap-to-interrupt during speech.
- Settings UI on the touchscreen: volume, brightness, Wi-Fi, and a battery view. Add dim/sleep and wake on touch; light-sleep (`display_pause`) only after a wake path is verified on the bench.

**Gate 2 (exit):** end-to-end spoken conversation works, with no feedback loop at the default volume, the avatar tracks state, and the device survives a 30-minute soak (Wi-Fi drop included) with no heat, loop or leak. User approves.

---

# Phase 3: Typed chat, hardware tools, character, release

**Goal:** the full functionality target.

### 3.1 Typed chat (new feature)
- On-screen chat view on the large display: scrollable history, LVGL keyboard, send button. Voice turns and typed turns land in one thread.
- Typed replies and transcripts must show on screen. The existing typed-turn API prints to the console only, so extend it. Respect "a typed turn is refused while a voice turn runs" and "a voice press ends a typed turn."
- Optional: USB-A host keyboard input, only after confirming the USB VBUS enable pin and current limits from the verified pin map.

### 3.2 Hardware tools for Muse (one `link.register` command each, per the SDK recipe)
Advertise in `main/noise_control.cpp`, handle in `main/app.c`, async for anything slow, gate with Kconfig, host test in `tests/`, keep `link.register` under 8 KB.
- `camera.capture` (SC2356, MIPI-CSI, powered only during capture).
- `sensors.read` for the IMU (BMI270), RTC time and battery/charge state.
- Optional later: brightness/volume control, microSD, RS485/Grove (only with verified wiring and never on guessed pins).

### 3.3 Character
- Avatar moods mapped to voice states, plus idle, sleepy and happy; touch and IMU (shake, tilt) reactions. A simple local mood that drifts with time since last chat. Use existing avatar mechanisms first (`>face=` modes, `tools/muse/avatar.py`) before building custom art.

### 3.4 Release
- OTA enabled and tested on a rollback-safe partition layout. Persist Wi-Fi/settings across reflashes (NVS is left alone by `flash`).
- Update `devices/README.md`, `esp32/AGENTS.md` and `esp32/README.md` tables. Add a Tab5 README with the verified display-revision notes, the backup/restore commands and known limits.
- Capture a photo/video; post in #projects on the Muse Gadgets Discord (repo + README ready).

**Gate 3 (exit):** all functionality demonstrated on hardware: voice in and out, typed chat with on-screen replies, camera/IMU/RTC/battery tools callable from Muse, character reacting, OTA works, host tests green, `idf.py build` for the Tab5 and one regression board passes. User signs off.

---

## Agent working rules
- One phase at a time; one subsystem per hardware step; log everything to `tab5/bringup-log.md` (not committed secrets).
- If hardware isn't attached for a step, say "built, not run."
- Prefer vendor sources (M5Unified, M5Tab5-UserDemo, M5 docs, Espressif BSPs/esp_hosted) over memory for pins and registers; cite them.
- Before handing back: build passes with size check, host tests pass, boot log shows `starting` with no panic or loop.

---

## Kickoff prompt (first session)

> Read `tab5/plan.md`, `esp32/AGENTS.md` and `esp32/devices/AGENTS.md`. Do only Phase 1.0 preflight, read-only: identify the chip, take and verify the flash backup, find the download-mode procedure, identify the display revision, and read the C6 firmware version without writing to it. Report findings and stop at Gate 1.0. Do not flash Muse firmware, write to the C6, or burn eFuses.
