# ESP32-P4-only component overrides

`cmake/project.cmake` adds this directory to `EXTRA_COMPONENT_DIRS` only when
the target is `esp32p4` (the M5Stack Tab5). A component here replaces the
managed one of the same name for those builds; every other board keeps the
registry version.

- `esp_hosted/`: esp-hosted-mcu v2.12.13 (Apache-2.0), with its protobuf-c
  submodule at `abc67a1` (BSD-2-Clause, `common/protobuf-c/LICENSE`). Only what
  the host build uses is kept: `CMakeLists.txt`, `Kconfig`,
  `sdkconfig.rename`, `host/` and `common/`. The examples, the co-processor
  (`slave/`) firmware, docs, tools and upstream CI are removed. Two changes,
  both in `host/`, are the only differences from upstream. For P4 SDIO with
  `ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`, allocate cache-aligned PSRAM without
  `MALLOC_CAP_DMA`: IDF 6.0.1 does not advertise that capability for PSRAM,
  although its SDMMC driver supports external buffers and cache synchronization.
  The Tab5 overlay enables this option to avoid exhausting internal SRAM in
  the hosted startup constructor. Other transport allocation paths are unchanged.
  For P4 active-low SDIO reset, the reset pulse now ends deasserted (high)
  before card initialization; upstream's final active level held the C6 in reset.

- `esp_lvgl_port/`: `espressif/esp_lvgl_port` 2.9.0 (Apache-2.0, unchanged
  apart from the line below; examples, docs and images removed). ESP-IDF
  6.0.1's `esp_lcd_dpi_panel_event_callbacks_t` has no
  `on_frame_buf_complete`, so the DSI path doesn't compile. The fix in
  `src/lvgl9/esp_lvgl_port_disp.c` uses `on_refresh_done` on IDF 6.
  Drop this override once a registry release builds on IDF 6.0.1.
