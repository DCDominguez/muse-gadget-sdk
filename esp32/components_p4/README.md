# ESP32-P4-only component overrides

`cmake/project.cmake` adds this directory to `EXTRA_COMPONENT_DIRS` only when
the target is `esp32p4` (the M5Stack Tab5). A component here replaces the
managed one of the same name for those builds; every other board keeps the
registry version.

- `esp_lvgl_port/`: `espressif/esp_lvgl_port` 2.9.0 (Apache-2.0, unchanged
  apart from the line below; examples, docs and images removed). ESP-IDF
  6.0.1's `esp_lcd_dpi_panel_event_callbacks_t` has no
  `on_frame_buf_complete`, so the DSI path doesn't compile. The fix in
  `src/lvgl9/esp_lvgl_port_disp.c` uses `on_refresh_done` on IDF 6.
  Drop this override once a registry release builds on IDF 6.0.1.
