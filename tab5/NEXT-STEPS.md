# Tab5 next steps — 2026-10-07

## Current verified state

ESP32-P4 v1.3, ST7123 display/touch, COM4. Muse pairing, Wi-Fi, cloud connection,
spoken replies, touch, Flip, physical keyboard and forward Delete work on hardware.
Startup memory allocation and C6 reset bugs are fixed. PC USB now stops battery
runtime measurement even when charging current is zero. App-only updates preserve NVS.
No C6 firmware or eFuse writes were made.

## Next work, in order

1. **Battery after charging.** Owner is leaving it on a wall charger for 15–30 minutes.
   Record displayed voltage/percentage and charging indicator before and after USB
   removal. Battery-only startup was confirmed after pressing power. USB readings
   around 1.5–1.8 V conflict with battery-only readings around 6.48 V; the pack's
   display previously said 69%. Establish actual pack-terminal voltage with a meter
   or a known-good compatible pack if discrepancies persist. Check contacts and
   charger path before changing the voltage conversion or inventing a percentage.
   USB host detection fixes classification, not charging or calibration. Wall-charger
   detection still relies on charger status/current because there is no VBUS sense.
2. **Chat on-screen keyboard.** Physical unplug detection works and Wi-Fi Other
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
