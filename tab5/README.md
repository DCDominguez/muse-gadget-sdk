# Muse on the M5Stack Tab5 (experimental)

A port of the Muse gadget firmware ([`esp32/`](../esp32)) to the
[M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5): Muse's avatar, voice and
settings on a 5" 1280×720 touchscreen, with status, a typed chat with Muse and
quick controls in the space around it, and the
[Tab5 Keyboard](https://docs.m5stack.com/en/tab5/Tab5_Keyboard) as an optional
keyboard.

> **Status (2026-10-09): reboot regression reported after later changes.**
> Earlier pairing, Wi-Fi/cloud, voice, touch, Flip and physical typing passed.
> Battery-only operation passed after wall charge, but low readings remain unresolved.
> Test5 was flashed and reached cloud connection; its new camera/reactions/TTS
> require feature tests. Read [HANDOFF.md](HANDOFF.md) for current evidence.


This lives in a fork (`DCDominguez/muse-gadget-sdk`, branch `tab5-port`). It
isn't part of the upstream SDK; upstream pull requests need Meta's CLA (see
[`CONTRIBUTING.md`](../CONTRIBUTING.md)).

## What you get

```
┌──────────────────────────────┬──────────────────────┐
│                              │ Status               │
│        Muse (800×480)        │  Muse · Wi-Fi ·      │
│   avatar, captions, swipe    │  battery · keyboard  │
│   left for settings          ├──────────────────────┤
│                              │ Chat with Muse       │
│                              │  replies stream in;  │
├────────────┬─────────────────┤  spoken replies too  │
│ Hold to    │ Volume          │                      │
│ talk       │ Brightness      │ [ Type to Muse… ][↵] │
│            │ Sleep Flip Off  │                      │
└────────────┴─────────────────┴──────────────────────┘
```

- **Talk:** hold the on-screen button, or hold **Tab** on the Tab5 Keyboard.
- **Type:** tap the chat line (an on-screen keyboard opens over the bottom
  strip) or type on the Tab5 Keyboard. **Enter** sends, or confirms pairing
  when the line is empty. **Esc** clears it, **←/→** move the cursor and
  **↑/↓** scroll the chat. The keyboard can be plugged in while running.
- **Sleep** turns the screen off; a touch or a key wakes it. **Flip** turns the
  picture 180° and remembers it. Hold **Off** for 1.5 s to power off (on
  battery; on USB the Tab5 stays on).
- **Battery** voltage, percentage and charging, from the Tab5's INA226.

## Hardware notes

| | |
|---|---|
| SoC | ESP32-P4, **pre-v3 silicon** (the bring-up unit is v1.3). The build sets `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`; ESP-IDF builds for v1.x and v3.x are mutually exclusive, so a v3.x Tab5 needs its own overlay. |
| Radio | None on the P4. Wi-Fi and BLE run on the on-board ESP32-C6 over 4-bit SDIO with `esp_hosted` 2.12 (host) and `esp_wifi_remote`; NimBLE stays on the P4. The C6 ships with esp_hosted slave 1.4.1. **This firmware never writes to the C6.** |
| Display | 720×1280 MIPI-DSI, run landscape. Three panel revisions (ILI9881C + GT911, ST7123, ST7121); Espressif's `m5stack_tab5` BSP tells them apart at start. The P4's PPA rotates each frame. |
| Audio | ES8388 speaker codec, ES7210 microphones. Default volume 30%. |
| Power | INA226 at 0x41 (5 mΩ shunt), CHG_STAT on IO expander 0x44 pin 6, power-off by pulsing 0x44 pin 4 (all from M5Unified). |
| Keyboard | Tab5 Keyboard (A164) on Ext.Port1: I2C 0x6D, SDA G0, SCL G1, HID mode, polled. |

## Build

With ESP-IDF v6.0.1 (see [`esp32/AGENTS.md`](../esp32/AGENTS.md)):

```sh
cd esp32
tools/muse/board.sh build tab5          # -> build-muse-m5stack-tab5/
```

For a real device, set your SDK token in that build's `sdkconfig` first, as for
any board, or build a flashing kit instead (below), which keeps the token off
the build machine.

## Flash

See [`FLASHING.md`](FLASHING.md): back up first with
`tools/tab5-preflight.ps1`, build a kit with `tools/make_kit.sh`, put your token
in with the offline form, and flash with `tools/tab5-flash.ps1` (or hand
`tools/CODEX_PROMPT.md` to Codex).

## Files

| | |
|---|---|
| [`HANDOFF.md`](HANDOFF.md) | Current session state, provenance, evidence and remaining work |
| [`plan.md`](plan.md) | The three-phase plan, its gates and safety rules |
| [`bringup-log.md`](bringup-log.md) | What was found and done, step by step |
| [`FLASHING.md`](FLASHING.md) | Backup, kit, token, flash, boot log, rollback |
| [`SDK-CHANGES.md`](SDK-CHANGES.md) | What the port changed outside its own board file |
| `tools/tab5-preflight.ps1` | Read-only chip check and verified 16 MB flash backup (Windows) |
| `tools/make_kit.sh` | Builds a flashing kit with a token placeholder |
| `tools/tab5_flash.py`, `tools/tab5-flash.ps1` | Put the token in on the owner's PC, sign, verify, flash, capture the boot log |
| `tools/tab5-sdk-token.html` | Offline form that saves the token for the flash script |
| `tools/CODEX_PROMPT.md` | Instructions for an agent flashing the kit |

In `esp32/`: the board is `components/muse/boards/board_m5stack_tab5.c`, its
overlay `devices/sdkconfig.muse-m5stack-tab5`, and the dock
`components/muse/muse_dock.c`.

## Known issues and open risks

1. **Battery and chat input remain open.** Battery-only startup works, but USB and
   battery-only voltage readings disagree. The chat keyboard fails to open even
   after physical keyboard removal; Wi-Fi settings keyboard works.
2. **C6 compatibility.** The tested C6 reported version 0.0.0 against host
   2.12.0 and an optional FeatureControl timeout. Wi-Fi and pairing succeeded;
   the compatibility warnings remain. No C6 firmware update was performed.
3. **BLE pairing** succeeded on this unit. Test5 preserved pairing and Wi-Fi;
   future regressions still require a boot log rather than assuming compatibility.
4. **Holding Tab to talk:** the keyboard reports releases without saying which
   key, so releasing any key ends the talk.
5. **USB presence** uses USB Serial/JTAG host connection plus charger status/current; wall supplies still rely on charging activity without direct VBUS sense.
6. **Sleep** is backlight-off only; the P4 doesn't light-sleep yet.
7. Five Wi-Fi tuning options in the shared `sdkconfig.muse` don't apply under
   `esp_wifi_remote`.
8. Not started: camera, IMU and RTC as Muse tools; avatar reactions; OTA tested
   end to end.
