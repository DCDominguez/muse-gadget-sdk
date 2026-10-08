# Tab5 next steps — 2026-10-09

Start with [HANDOFF.md](HANDOFF.md), which records the current state and all
session results. Older camera planning and battery observations remain in Git
history and bringup-log.md; they are not the current feature-status checklist.

1. **Reboot regression:** follow [REBOOT-RECOVERY.md](REBOOT-RECOVERY.md), capture
   reset reason and running version, check power, compare the previously fixed
   memory/reset/power paths with the later Claude source. Cause is unverified.
2. **Hardware regression:** validate charging/battery operation, radio/cloud,
   voice, Flip, physical keyboard and runtime USB classification.
3. **Test5 additions:** use [the supplied checklist](handoffs/test5/TESTING.md).
   Verify chat screen keyboard, camera, reactions, voice.configure and sleep/wake.
   Test5 was flashed and reached cloud connectivity; these feature tests remain open.
4. **Speech:** Kokoro PC setup and Tab5 configuration were not performed. Follow
   [the kit notes](handoffs/test5/KOKORO.md) when requested; ElevenLabs is optional.
5. **Emoji and release:** emoji support remains unverified/missing from kit docs.
   Obtain a supported host-test pass, validate app size and run OTA separately.

Preserve pairing/Wi-Fi with app-only flashes. No chip erase, C6 update or eFuse
writes. Keep secrets out of Git. Baseline and later kit provenance differ; see
HANDOFF.md before selecting a build tree.
