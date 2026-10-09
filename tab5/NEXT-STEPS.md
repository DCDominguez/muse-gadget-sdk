# Tab5 next steps — 2026-10-10

## Update: pixel UI redesign (test28 on hardware)

The Tab5 now runs the approved "Cosmo" pixel UI (CONFIG_MUSE_PIXEL_THEME, default
on for the Tab5). Verified on hardware with screenshots (tools/muse/snap.py):

- Dock: status strip, name and mood, hold-to-talk, scrolling chat, pocket of
  quick controls, full screen; Pixelify Sans 19/30 and Silkscreen 16 fonts
  (tools/muse/gen_pixel_fonts.sh, which also redraws Pixelify's 19 px "B").
- Scenery (muse_scene.c) behind a see-through Cosmo; Cosmo turns, walks and
  idles about his island (muse_pose_t.facing/.walk, the dock's wander controller).
- Work props in Cosmo's own pixel style (muse_props.c): book, globe, typewriter,
  easel, camera. Pictures from Muse get a pixel polaroid frame.
- Settings open in a pixel window where the pocket and chat go (right side),
  not a tile over Cosmo; every page restyled (muse_pixel_style.h shared look).
  Docked, the face's own caption, reply and meter overlays are off: the chat shows them.
- Tab5 LVGL task stack raised to 16 KB (the settings Wi-Fi page overflowed 7 KB).
- Sign-in: one refresh rejection no longer unpairs (4 rejections over 30 min do).
- Camera on/off in the pocket, "snap!" notice, upright photos; Wi-Fi drop log,
  console `>cam`, `>wifi`, `>scan`, `>dock=...` (pocket, full, demo, osk, walk,
  turn, think, work=KIND, settings[=PAGE]) for testing without touching the screen.

Muse's activity, captured 2026-10-10 (agent.status payload): activity_code is only
"working", "responding" or "online"; activity_text names the work: "Searching",
"Researching", "Generating image" (after "making something"), "is working", "is
responding", with an optional activity_emoji. task.status carries running,
completed and user_denied (a permission the Muse app asked for and didn't get).
The dock picks Cosmo's prop from activity_text (research before search).
Clock: SNTP once Wi-Fi is up, CONFIG_MUSE_CLOCK_TZ (set in the owner's build directory).

Open choices: 2x scaling for small pictures, bigger Cosmo in full screen.
Settings' MAC address row is empty on the P4 (no local Wi-Fi MAC; pre-existing).
Muse's /chat/stream sometimes answers HTTP 502 after 120 s (server side, seen
2026-10-10): the turn fails with "MUSE DIDN'T TAKE IT".

## Earlier: 2026-10-07

## Current verified state

ESP32-P4 v1.3, ST7123 display/touch, COM4. Muse pairing, Wi-Fi, cloud connection,
spoken replies, touch, Flip, physical keyboard and forward Delete work on hardware.
Startup memory allocation and C6 reset bugs are fixed. PC USB now stops battery
runtime measurement even when charging current is zero. App-only updates preserve NVS.
No C6 firmware or eFuse writes were made.

## Next work, in order

1. **Battery verification complete for this test.** After wall charging, owner
   reports 57% / 7.5 V and confirms the Tab5 stays on without USB. Charging and
   battery-only operation are confirmed. Percentage remains a voltage estimate;
   earlier 1.5–1.8 V readings are unresolved historical observations. Monitor for
   recurrence rather than changing the conversion without new evidence.
2. **Chat on-screen keyboard: fixed, verify on hardware.** It was opening off the bottom of the
   screen (`lv_keyboard_create()` aligns bottom-centre, so the position became an offset).
   A tap on the chat line now always opens it, with or without the Tab5 Keyboard. Reproduced
   and checked in the simulator (`--board tab5`), which now has Tab5 tests. Original notes: Physical unplug detection works and Wi-Fi Other
   network opens its keyboard. Chat input remains focused across unplug, so check
   its click/focus handling and reconnect/disconnect transitions. Allow manually
   opening the keyboard even with a physical keyboard present. Validate first tap,
   repeated tap, close/reopen, hot unplug and physical typing.
3. **Emoji.** Add an appropriate glyph fallback and an input picker for a documented
   initial set. Verify UTF-8 chat transport, rendering and character deletion.
   Include variation selectors/combined sequences in the support limits; do not
   claim full emoji support from a few single-codepoint glyphs.
4. **Regression/release.** Build with ESP-IDF 6.0.1, validate Tab5 overlay and app size,
   and capture a full boot with no panic/brownout. Run host tests in a supported
   POSIX environment: the Windows run is not green. Check voice/typed replies,
   physical/onscreen typing, sleep/wake, Flip and settings persistence. Package
   placeholder-only firmware and publish the Git commit after GitHub authentication
   is restored.

## Known limits

- Existing C6 reports version 0.0.0 and one optional Bluetooth FeatureControl RPC
  times out. Pairing succeeded on hardware; no C6 update is authorized by this plan.
- Voltage-based percentage is an estimate. Invalid sensor values show unavailable.
- Host run: 155 tests, 22 failures, 7 errors, 2 skips, with Windows POSIX/ASAN/newline
  limitations. No clean regression pass is claimed.
- Camera, IMU, RTC tools and OTA hardware validation remain later work.

See bringup-log.md for evidence and SDK-CHANGES.md for the code changes.


## Paused handoff: camera request

Owner requested camera access, then paused implementation to conserve credits.
No camera changes have been implemented, built or flashed. Resume with camera,
then the missing chat keyboard and emoji fixes above.

- Existing generic camera API lives in esp32/components/camera. camera.capture
  registration and its asynchronous handler are currently gated by
  CONFIG_MUSE_WATCHER_CAMERA; do not enable the Watcher backend on Tab5.
- Tab5 BSP has bsp_camera_start() and a CSI/V4L2 backend. Investigate SC202CS
  sensor configuration, bounded JPEG encoding and a Tab5-specific capture driver.
- Expose capture through the existing Muse camera.capture command. Power the
  camera only during capture, release buffers on every error path, and validate
  a real captured image plus a Muse request before claiming it works.
- Working source: work/muse-gadget-sdk-repo, branch
  tab5-hardware-bringup-fixes; draft PR #2. Build wrapper work/build_tab5.py
  still targets the separate e8f49fd source tree: sync changed files there before
  building. ESP-IDF 6.0.1; device COM4; app-only flashing preserves NVS.
- Never commit SDK tokens, patched firmware or private serial output. Ask owner
  for the token again if it is needed in a fresh session. No C6/eFuse/erase work.
- Battery latest owner checks: 57%, 7.5 V, stays powered after USB removal.
  Earlier low readings remain unexplained; charging and battery operation worked
  in the latest test. Voice and Flip also work. Chat on-screen keyboard and emoji
  remain missing; Wi-Fi Other network keyboard works.
