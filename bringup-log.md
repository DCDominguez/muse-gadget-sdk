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

## Phase 1.1 build-only port — code written, not yet compiled

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
