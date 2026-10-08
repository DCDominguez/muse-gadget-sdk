# Tab5 reboot regression handoff — 2026-10-09

Owner reports rebooting again after later Claude work. Current firmware revision,
reset reason and trigger are not yet captured. This is a reported regression;
we have not established which change caused it. No firmware was changed today.

## Previously working fixes to preserve

The implementation is already committed in 85e81be on
`tab5-hardware-bringup-fixes` (draft PR #2). Later commits record hardware checks.
Use the source changes as a comparison baseline; do not blindly cherry-pick the
large vendored component commit over a newer tree.

1. **SDIO startup memory assertion.** The local ESP32-P4 esp_hosted override
   prefers aligned PSRAM for its mempool and omits unsupported MALLOC_CAP_DMA
   on that P4 PSRAM allocation; internal DMA allocation remains a fallback.
   Keep CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y in the Tab5 overlay and verify
   the build actually resolves esp32/components_p4/esp_hosted. The original
   failure asserted in sdio_mempool_create on buf_mp_g before the UI appeared.
2. **C6 held in reset / SDIO enumeration timeout.** Leave the active-low reset
   on GPIO15 deasserted HIGH at the end of the host reset helper. Preserve the
   Tab5 SDIO pin configuration and active-low setting. This restored card init,
   scans and pairing. No C6 firmware update was needed or performed.
3. **P4 identity.** esp32/main/identity.c uses esp_efuse_mac_get_default for
   the P4 factory base MAC instead of a nonexistent local Wi-Fi interface MAC.
4. **Power-control initialization.** board_m5stack_tab5.c restores IO expander
   1 (0x44): G7 CHG_EN high, G5 nCHG_QC_EN low, G4 PWROFF low. Set output latches
   before enabling output drivers; BSP reset otherwise leaves these as inputs.
5. **USB/runtime detection and battery display.** Include
   usb_serial_jtag_is_connected() in USB source detection, alongside charger
   status/current. PC USB must stop runtime measurement even at zero charge
   current. Unknown percentage displays unavailable, not an invented 0%.
   Percentage is voltage-derived; anomalous 1.5–1.8 V readings remain unresolved.
6. **Physical Delete.** HID 0x4C maps to LV_KEY_DEL and forward textarea deletion;
   Backspace deletes before the cursor. Preserve this independently of reboot work.

## Power evidence and test5 result

- Earlier radio activity at 100% brightness produced confirmed brownouts.
  Reducing brightness to 30% stabilized it; a charged rear battery also helped.
- After wall charging, owner reported 57% / 7.5 V and confirmed battery-only
  operation. This was a successful test, not proof all battery issues were fixed.
- On 2026-10-08, kit test5 (README commit d2b7c73) was flashed app-only on COM4.
  Hash verification passed. During its 60-second capture, Muse reached UI up,
  Wi-Fi and cloud connection; no panic/assert/brownout markers were found.
  BMI270 shake handler initialized. C6 version mismatch and the optional
  FeatureControl timeout persisted, but connection succeeded.
- That boot showed about 1.48 V. Owner subsequently reported the battery had
  run out and chose a full wall charge. Low power must remain a candidate for
  the new rebooting; do not infer a software cause from timing alone.
- Test5 binary includes dizzy/tickle/snore/waking reaction code, camera.capture
  and Kokoro strings. Visual reactions, camera and Kokoro were not verified.
  The local baseline branch does not contain Claude's newer feature source.

## Next diagnostic steps

1. Identify the exact running build and collect boot/reset logs on COM4,
   including >log when it can run. Redact SDK tokens and other credentials.
2. Distinguish brownout, watchdog, panic/assert and deliberate software reset.
   Record whether reset occurs at boot, radio connection, camera or speech use.
3. Repeat on a charged battery and stable wall supply/powered hub, with modest
   brightness, before changing memory or radio code.
4. Compare the later source with the six fixes above. Check effective sdkconfig
   and resolved components, not merely sdkconfig.defaults; a cached config can
   override defaults. Examine task stacks and allocations for added camera/TTS.
5. Repair only the evidenced cause, build with ESP-IDF 6.0.1 and validate app size.
   Use app-only flashing to preserve NVS, then repeat boot and feature tests.

Do not erase flash, write eFuses/security settings or update the C6. Original
16 MiB backup was verified previously. Keep tokens and patched images out of Git.
Chat on-screen keyboard and emoji results still need confirmation in the later build.
