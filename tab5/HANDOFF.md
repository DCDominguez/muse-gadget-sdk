# Handoff (2026-10-08)

Where the Tab5 work stands, for the next session (on the PC with the Tab5:
read `BRIDGE.md` first).

## On the Tab5 now: kit test5 (d2b7c73)

Hardware results so far (owner): boots, Wi-Fi, Muse connected, BMI270 found;
rub, tickle and volume drag work. **Shake doesn't**, **images from Muse don't
show**, **emoji don't render**.

## Since test5, pushed, not yet flashed

- `d84a109` **Shake fix**: `muse_imu_poll_shake()` ignored a board reader, so
  the BMI270 was never polled. Console `>imu` prints live readings.
- `384dcbe` Bridge: `tools/tab5_console.py`, `tab5_flash.py --keep-secret-files`.

Build a kit (`tools/make_kit.sh test6`, ESP-IDF v6.0.1) or build and flash
from `esp32/` directly; then run `TESTING.md`.

## Open, in order

1. **Images** (`display.draw_url`): nothing shows. Capture the log while asking
   Muse for a picture (`image_fetch`, `muse_ui` lines). Suspects: the JPEG
   decode via esp_jpeg on the P4 (`main/image_fetch.c`, `CONFIG_JD_SZBUF`
   path), the image box at `ui_x`/`ui_y` (`muse_ui_image_draw`), or the cover
   hiding it.
2. **Emoji** in the chat: the dock's fonts have no emoji glyphs
   (`muse_dock.c`). Options: an LVGL emoji image font, or map common emoji to
   text.
3. **Shake**: confirm after flashing; if still silent, tune with `>imu`
   (`MUSE_IMU_SWING_G` 1.2 g may be too high for the Tab5's weight).
4. **Camera** (`camera.capture`): untested; rotation
   (`CONFIG_MUSE_TAB5_CAMERA_ROTATION`), RGB565 byte order, possible
   blocking DQBUF if no frames.
5. Boot log noise: `ECDSA peripheral not supported on this chip revision`
   (harmless; the owner declined the overlay change for now), C6 slave
   reports version 0.0.0 and BT controller enable times out (BLE pairing
   unavailable; never update the C6 without the owner).
6. Battery reads 1478 mV (INA226 raw 0x049f): is a pack fitted?
7. Volume was 97 at boot: feedback risk; check it was intended.
8. Then: avatar animations and customization (`ROADMAP.md` §4).
