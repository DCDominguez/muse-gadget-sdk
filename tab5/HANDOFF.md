# Handoff (2026-10-10)

Where the Tab5 work stands, for the next session. On the PC with the Tab5,
read [`BRIDGE.md`](BRIDGE.md) first.

## On the Tab5 now: kit test33

The pixel UI redesign around Cosmo, work animations driven by Muse's
activity, settings in a pixel window, the clock and the two-way bulletin
board. Everything is listed in [CHANGELOG.md](CHANGELOG.md) and how to use
it in [MANUAL.md](MANUAL.md). Checked on the device by screenshots and real
requests: a web search showed the globe, an image the easel, and a picture
pushed with `display.draw_url` appeared after approval in the Muse app.
Muse pinned an idea with `bulletin.post`, and `>ideas` read it back.

## Working on the device

- Build: `idf.py -B build-muse-m5stack-tab5 build` with ESP-IDF 6.0.1. The
  build directory's `sdkconfig` holds the owner's settings: the clock's time
  zone and `CONFIG_LV_USE_SNAPSHOT=y` for screenshots. A fresh build directory
  needs both set again.
- Flash a kit app-only (`tools/make_kit.sh testNN`, then `tab5_flash.py` as
  in [FLASHING.md](FLASHING.md)). Pairing and Wi-Fi survive.
- Screenshots: `esp32/tools/muse/snap.py COMx ">dock=…" out.png`. Torn rows
  are USB transfer losses; the tool says "bytes lost", so take it again.
- If the Tab5 loops in ROM after a flash, only a real power-off (unplug,
  hold power) clears it.
- Update [BULLETIN.md](BULLETIN.md) with each build, and read Cosmo's
  pinned ideas with `>ideas`.

## Next

See [NEXT-STEPS.md](NEXT-STEPS.md): check the remaining work animations
during real requests, the owner's open choices (a bigger Cosmo in full screen,
scaling up small pictures), and the small fixes (the MAC row, Wi-Fi
reconnects, the keyboard's case key).
