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

(in progress)
