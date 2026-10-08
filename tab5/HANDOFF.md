# Tab5 session handoff — updated 2026-10-09

Read this first, then REBOOT-RECOVERY.md for the stability fixes and bringup-log.md
for chronological evidence. This document separates our source baseline from
Claude's later kit. It supersedes older status statements for this test unit.

## Current priority

Owner reports rebooting again after later Claude changes. Exact current build,
reset reason and trigger are not known. No repair or flash was performed on
2026-10-09. Collect evidence before attributing the problem to a particular
change; depleted battery and radio brownouts occurred in earlier sessions.

## Source and handoff locations

- Repository: DCDominguez/muse-gadget-sdk, branch tab5-hardware-bringup-fixes,
  draft PR #2. Hardware implementation baseline: 85e81be; charge validation:
  838f579; paused camera notes: 48d53ef; reboot handoff: 5fd69cd.
- A later remote commit, 97b3414, trims the hosted vendor tree. It was preserved
  during our documentation update; we have not rebuilt that trimmed tree.
- Claude test5 kit identifies d2b7c73. Its source is not in our local baseline;
  locate and compare it before assuming this branch reproduces test5.
- Kit documentation and manifest: handoffs/test5/. Do not treat the kit checklist
  as passed tests. No private token or patched image is checked in.

## Implemented and checked in our baseline

SDIO PSRAM allocation and active-low C6 reset fixes; P4 factory MAC identity;
physical forward Delete; charger/power output restoration; PC USB detection that
stops runtime measurement; invalid battery percentage display; INA226 raw logging.
REBOOT-RECOVERY.md maps these to code and explains the original failures.

## Hardware results

| Item | Result and limit |
|---|---|
| Boot/radio | Baseline built with ESP-IDF 6.0.1, flashed app-only and reached Wi-Fi/cloud without startup assertion. |
| Power | Brownouts confirmed during radio use at 100% brightness; 30% stabilized that test. Rear battery subsequently added. |
| Pairing | Phone app BLE pairing completed; saved Wi-Fi reconnected and Muse was online. |
| Audio | Built-in 3.7-second sample heard; owner confirmed voice works. This does not verify the new Kokoro/ElevenLabs paths. |
| Touch/Flip | Owner confirmed touch and Flip work. |
| Physical keyboard | Connected/unplugged status works. Forward Delete implemented; owner recognized keyboard Delete semantics. |
| Chat screen keyboard | Failed to open after physical unplug in baseline; Wi-Fi Other network keyboard works. Test5 checklist claims it opens, but no later owner pass recorded. |
| Battery | After wall charge, owner reported 57% / 7.5 V and confirmed it stays on without USB. This validates that test only. |
| Battery limits | Earlier 1.5–1.8 V values unexplained; percentage is estimated from voltage. Wall charging reported working, PC charging reported not working. |
| Runtime measurement | USB power query confirmed boot=usb, started=false, running=false after USB detection fix. Historical completed run remained visible. |
| Host tests | Windows: 155 run, 22 failures, 7 errors, 2 skipped (POSIX/ASAN/newline issues among them). No clean regression pass. |

Existing C6 reports version 0.0.0 against host 2.12.0; optional FeatureControl
request times out once. Pairing and Wi-Fi succeeded despite these warnings.
No C6 firmware, flash encryption, Secure Boot or eFuse writes were performed.

## Test5 work performed on 2026-10-08

1. Read CODEX_PROMPT.md, KOKORO.md, TESTING.md and README from the new ZIP.
2. Verified every manifest entry and the existing 16 MiB original backup.
3. Flashed app-only at 0x20000 on COM4; board identity, patched image checksum,
   signature and written hash verified. Pairing/Wi-Fi NVS preserved.
4. Captured 60 seconds: Muse Gadget starting, ST7123 display, UI up, saved Wi-Fi,
   cloud control and tunnel connection. No panic/assert/brownout markers found
   in that capture. This is a limited observation, not long-term stability proof.
5. BMI270 initialized at +/-8 g, 200 Hz with shake reaction enabled. Binary
   inspection found dizzy, tickle, snore and waking reaction code plus camera
   and Kokoro strings. Avatar appearance and camera output were not confirmed.
6. Battery raw 0x049f yielded 1478 mV. Owner later said it ran out of charge,
   chose full wall charging and planned to buy a powered USB hub. Purchase and
   subsequent full charge are not confirmed. We cannot explain the low reading
   solely from that report.

Kit documentation additionally describes volume gestures, yawn/stretch, wake
on shake, voice.configure, camera JPEG resize/rotation, all-messages speech and
OTA tests. These remain unverified. Kokoro setup was not performed; no server,
firewall or TTS URL changes were made. ElevenLabs is the other documented speech
option. Emoji implementation is not documented or verified.

## Local operational context (not required on another developer's machine)

Workspace: C:/Users/dcdom/Documents/Codex/2026-10-07/open.
Git checkout: work/muse-gadget-sdk-repo. Historical build wrapper:
work/build_tab5.py, using work/idf601 and work/idf-tools601. It targets the
separate work/muse-gadget-sdk-e8f49fd04dae26cde2c98cebd48282762b46344d tree,
not this checkout; synchronize intended source before using it. Compiler launch
needed elevated execution on this Windows host; Ninja response files were enabled
for Windows command-length limits. Do not infer this setup applies elsewhere.

Latest test5 redacted capture: outputs/tab5-test5-boot.log in that workspace.
A compact, selected evidence excerpt is checked in at handoffs/test5/boot-evidence.txt.
Original backup: C:/Users/dcdom/tab5-backups/tab5-80f1b2d1447d-20261007-164653.bin,
16777216 bytes, verified before flashes. Kit directory: work/test5/tab5-kit-test5.
GitHub authentication was restored and repository updates pushed successfully.
SDK tokens must never be copied into handoff documents, public logs or binaries.

## Next work

1. Diagnose current rebooting using REBOOT-RECOVERY.md and compare newer source
   against the existing stability fixes, including effective generated sdkconfig.
2. Validate a charged/stable supply and capture a clean boot and runtime session.
3. Run the test5 checklist and record pass/fail/skip with evidence. Prioritize chat
   screen keyboard, physical Delete, camera orientation/colours and avatar reactions.
4. Set up Kokoro only when requested: local speech test, LAN reachability, TTS URL,
   spoken reply and unavailable-server fallback. ElevenLabs remains optional.
5. Implement/verify emoji rendering and input. Re-run relevant host tests in a
   supported environment; validate OTA separately before claiming it works.
6. Keep this handoff, bringup log and checklist results current after each session.
