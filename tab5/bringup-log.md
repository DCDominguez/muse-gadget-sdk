# Tab5 bring-up log

Unit: M5Stack Tab5, MAC `80:f1:b2:d1:44:7d`. No secrets in this file.

## Gate 1.0 preflight (2026-10-07) — approved by the owner

| Item | Finding | Source |
|---|---|---|
| Chip | ESP32-P4 **rev v1.3**, 16 MB flash | `tab5-preflight.ps1` (esptool 5.4.0, COM4) |
| USB | `303a:1001` USB Serial/JTAG (COM4 on the owner's PC) | Windows device list |
| Backup | 16 777 216 bytes, SHA256 `39241495935A3C7482D1E98B950F0144466D28524BA4B806F4BD4B38AAF30A06`, two reads matched. Kept on the owner's PC only. | preflight report |
| Running firmware | Owner's own Cartographer app (ESP-IDF 5.5.x, `espressif/m5stack_tab5` BSP 1.3.0, pre-v3 build), not M5's factory demo | `DCDominguez/ESP32-Projects` |
| Download mode | Not needed: esptool connected over the native USB without buttons | preflight |
| Display revision | Not read. The BSP detects it at boot (touch probe): v1 ILI9881C+GT911, v2 ST7123, v3 ST7121. Cartographer drives it, so detection works on this unit. | `esp-bsp/bsp/m5stack_tab5/src/bsp_display.c` |
| C6 firmware | Not read on the board. M5's factory C6 image is `ESP32C6-WiFi-SDIO-Interface-V1.4.1` (esp_hosted SDIO slave 1.4.1); Cartographer never touches Wi-Fi, so the C6 should still run it. esp_hosted host 3.x is documented back-compatible with 1.x slaves. Confirm from the first boot log. | `m5stack/M5Tab5-UserDemo/platforms/tab5/wifi_c6_fw/`, `esp-hosted-mcu/docs/migration.md` |
| Boot log | The board's USB console re-enumerates after reset; lines before ~7 s are lost on Windows | miniterm / pyserial capture |

Restore: `python -m esptool --chip esp32p4 -p COM4 -b 460800 write-flash 0 <backup .bin>`

## Phase 1.1 build-only port

Built, not run. Nothing flashed.

- Board `m5stack_tab5` (`MUSE_BOARD_M5STACK_TAB5`, ESP32-P4), alias `tab5` in `tools/muse/`.
- Display, touch, codecs and expanders through `espressif/m5stack_tab5` 1.3.1. LCD_EN released
  500 ms before the BSP's panel probe (Cartographer's fix for the rev v1.3 cold-start assert).
  Native portrait 720x1280, no software rotation; orientation to settle in 1.2.
- Wi-Fi: `esp_wifi_remote` + `esp_hosted` 2.12.x (host line nearest the C6's slave 1.4.1),
  SDIO pins from esp_hosted's `ESP32P4_TAB5_C6_BOARD` preset, cross-checked with M5's demo.
- C6 power: new optional `muse_board_t.radio_init`, run from `muse_glue_start()` before
  Home Link starts Wi-Fi, switches on BSP_WIFI_EN. Without it `esp_wifi_init()`'s
  `ESP_ERROR_CHECK` would abort into a reboot loop.
- BLE: NimBLE on the P4 over hosted HCI (`ESP_HOSTED_ENABLE_BT_NIMBLE`); `ble_server.c`
  asks the C6 to enable its controller and logs (not aborts) if slave 1.4.1 refuses.
- Default volume 30%. `power_off` pulses expander 0x44 pin 4 (M5Unified's PWROFF_PULSE).
- Host tests: 181 run, OK (2 skipped: no host mbedcrypto), with cJSON fetched temporarily.
- **Blocked:** `idf.py build` needs components.espressif.com, which this environment's
  network policy denies.

Open risks for 1.2: esp_hosted 2.12 host vs C6 slave 1.4.1 (Wi-Fi RPCs and BT controller
requests); BSP 1.3.1 and esp_video on IDF 6.0.1; portrait layout of the Muse UI.

### First clean build (2026-10-07)

- `tools/muse/board.sh build tab5`: OK. 0x221000 bytes, 47% of the 4 MB slot free.
  Image header: chip rev v1.0 to v1.99 (this unit v1.3). Wi-Fi library: esp_hosted.
- Every line of `devices/sdkconfig.muse-m5stack-tab5` applied. From the shared
  `sdkconfig.muse`, `CONFIG_ESP_WIFI_{IRAM_OPT,RX_IRAM_OPT}=n`, the RX buffer count and
  BA windows, and `CONFIG_MBEDTLS_HKDF_C` did not apply (Wi-Fi options belong to the C6
  side under esp_wifi_remote).
- Fixes needed for IDF 6.0.1 on the P4: `components_p4/esp_lvgl_port` (DSI callback
  rename), esp_jpeg for `image_fetch.c` (no TJpgDec in the P4's ROM).
- Regression: CoreS3 build OK. Host tests: 181 run, OK (2 skipped).
- Test image `tab5-muse-test1.bin` (merged, flash at 0x0), SHA256
  `8d78797212e7be21fa29c8eaca9be79244445d95236c715a82837d00a82b8a09`. Built without an
  SDK token. Blanks Muse's NVS range (0x11000-0x1D000), which held Cartographer code.

### Test image 2 (2026-10-07): dock, keyboard, battery, flip

- 800x480 Muse at top left; muse_dock fills the rest (status, typed chat, hold-to-talk,
  volume/brightness, Sleep, Flip, hold-to-power-off; on-screen keyboard without a Tab5 Keyboard).
- Tab5 Keyboard (A164): I2C 0x6D on Ext.Port1 (G0/G1, I2C port 0), HID mode, polled every 20 ms,
  probed every 2 s for hot-plug. Tab held = talk.
- Battery: INA226 0x41 bus/shunt + CHG_STAT (expander 0x44 pin 6). USB presence inferred.
- Sleep is backlight-off with LVGL running (touch or keyboard wakes); no light sleep yet.
- Builds: Tab5, CoreS3, Cardputer ADV. Host tests OK.
- `tab5-muse-test2.bin` (merged, first flash, blanks NVS) SHA256
  `68ed98cc0268ede73c64834ab122c12297a9b5cf18f6cf57fd3a9d16b53df6bf`;
  `tab5-muse-test2-app-only.bin` for updates at 0x20000 (keeps pairing).

### Kit 3 (2026-10-07): token put in on the owner's PC; `>log`

- Builds carry a 48-character token placeholder; `tools/tab5_flash.py` swaps the owner's
  token in, fixes the checksum and SHA-256, signs with the dev key and verifies. Tested
  here: the placeholder round-trips byte for byte; a test token validates and its
  signature verifies; the signed size matches the build's.
- `>log` on the Muse console prints the log since boot (Windows misses the first seconds
  after a reset).
- Not yet run on the Tab5. The Windows scripts and the token form haven't run on Windows.

### Housekeeping

- Tab5 files moved to `tab5/` (this log, `plan.md`) and `tab5/tools/`; `README.md`,
  `FLASHING.md` and `SDK-CHANGES.md` added. Kits are built with `tools/make_kit.sh` into
  the gitignored `tab5/kits/`.
- The Tab5 is listed in `esp32/AGENTS.md`, `esp32/README.md` and `esp32/devices/`.

### Hardware bring-up and user checks (2026-10-07)

- ESP32-P4 v1.3, MAC 80:f1:b2:d1:44:7d, COM4. Kit hashes and original 16 MiB backup verified. Built with ESP-IDF v6.0.1. No C6 firmware or eFuse writes.
- Fixed startup SDIO mempool assertion: prefer aligned PSRAM, omit unsupported P4 PSRAM DMA capability, retain internal DMA fallback.
- Fixed active-low SDIO reset: leave C6 reset deasserted before enumeration. Card init and Wi-Fi scan now work.
- P4 identity now uses factory base MAC instead of nonexistent local Wi-Fi MAC.
- Initial 60-second capture: no assertions/aborts. Touch, Flip and built-in speaker sample confirmed by owner. Spoken Muse replies subsequently confirmed working.
- Brownouts during scan/pairing at brightness 100%; reducing brightness to 30% stabilized operation. Owner attached a charged rear battery. BLE pairing completed, Wi-Fi connected, Muse Link Online and cloud transports up.
- Added physical HID Delete 0x4C -> LV_KEY_DEL and forward deletion in chat. Owner understands Delete versus Backspace behavior.
- On-screen chat keyboard does not open even after physical keyboard removal; status correctly changes to Keyboard: not connected. Keyboard opens in Other network settings. Fix pending in chat focus/click handling.
- Emoji display/input support requested, pending.
- Rear pack reports 69% on its own display; firmware shows 0% and 1.50-1.80 V. Wall charger does not change this. Removing USB turns Tab5 off despite seated rear pack. Battery power path investigation is next; physical failure and firmware initialization have not yet been distinguished.
- Display correction built but not yet flashed: unknown battery percentage must not be displayed as 0%.
- Host tests on Windows: 155 run, 22 failures, 7 errors, 2 skipped, including POSIX/ASAN availability and newline differences. Build passed; no clean host-test pass claimed.
- SDK tokens are excluded from logs and source. Updated firmware kits retain token placeholders only.

#### Battery follow-up implementation

Owner authorized resuming battery investigation. Compared M5Unified Power_Class::begin (https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.inl) with Espressif PI4IOE reset defaults. The BSP resets IO1 to inputs, low output latches; our board restored only radio/status. Added restoration of G7 CHG_EN high, G5 nCHG_QC_EN low, G4 PWROFF_PLUSE low using driver APIs; latch setup precedes enabling output drivers. Added INA226 raw bus/pack voltage logging. Invalid percentage now renders Battery: unavailable with voltage rather than 0%. Build passed, app-only flash initiated; battery power operation still requires hardware validation. These controls cannot establish that a rear pack physically contacts the power terminals.

Battery update flashed at 0x20000, hash verified. Boot sensor read raw 0x055d = 1716 mV; Muse reconnects with wifi/ws/raw up. No power-control API errors. Sensor value remains implausible for a charged 2S pack. Battery-only power-button test requested; not yet confirmed.

Owner battery-only test: Tab5 starts and stays on after unplugging USB and pressing power. Initially reported 6%; next reading 6.48 V / 0%. This is a plausible 2S pack voltage (3.24 V per cell); the existing M5Unified-derived estimate clamps below 3.30 V/cell to 0%. Pack display previously reported 69%, so percentage agreement remains unverified. Owner reports charging on wall outlet but not PC USB; charging/source detection still requires validation. The earlier 1.7 V was not representative of the later battery-only operating state.

USB measurement follow-up: owner requested loading fix after console power JSON showed running=true while COM4 USB was connected. Board read_power now recognizes ESP-IDF USB Serial/JTAG host SOF connection in addition to charger status/current. This fixes PC USB classification when charging is idle. Wall charger detection still relies on charging activity because there is no direct VBUS sense. This changes power classification/runtime measurement, not charge rate or percentage calibration. Build/flash validation pending.

USB detection build and app-only flash passed; write hash verified. 60-second capture: wifi/ws/raw up, no crash/brownout markers. USB power query confirms boot=usb, started=false, running=false: runtime measurement is stopped while on PC USB. Historical saved measurement remains visible as a completed run. Rear pack read around 1.74 V in this USB-connected state; charging and battery-voltage discrepancy remain unresolved beyond the earlier battery-only startup confirmation.
