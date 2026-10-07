# Tab5 roadmap

What comes after bring-up, in order. Each item names its source and its first
check. Hardware state is in [`NEXT-STEPS.md`](NEXT-STEPS.md).

## 1. Verify what's already built in

The Tab5 build already has these on (the shared `devices/sdkconfig.muse`):
`CONFIG_HOMEHUB_DISPLAY_COMMANDS`, `CONFIG_HOMEHUB_TUNNEL`,
`CONFIG_HOMEHUB_OTA_ENABLED` with app rollback and signature checks on update.
The tunnel and OTA code use no Wi-Fi-driver calls that `esp_wifi_remote`
lacks. None has been exercised on the device.

| Feature | P4-specific risk | Test |
|---|---|---|
| Images from Muse (`display.draw_url`) | JPEG decoding comes from `esp_jpeg`, not ROM; images fill the 800×480 Muse box, not the whole screen | Ask Muse to show a picture |
| Home-network tunnel | Runs over `esp_wifi_remote`'s netif, not a local Wi-Fi netif | Muse reaches a device on the home network |
| OTA updates | Signed with the dev key, `partitions_muse.csv`; the update arrives over the C6 link. Muse installs from a URL; the image must be newer (`KIT_VERSION`) and holds the SDK token, so it's hosted privately (`FLASHING.md`) | `device.ota` with a `KIT_VERSION=999.1.0` image, then `>status` after the reboot. A broken URL should leave the running image untouched (a failed download installs nothing) |

## 2. Voice and message sync, from wupsbr/waveshare-muse-gadget-sdk

[wupsbr/waveshare-muse-gadget-sdk](https://github.com/wupsbr/waveshare-muse-gadget-sdk)
(Apache-2.0) forks the same upstream commit as ours (`b139b45`), so its
shared-code changes port almost directly. Its
[`docs/CHANGES-FROM-UPSTREAM.md`](https://github.com/wupsbr/waveshare-muse-gadget-sdk/blob/main/docs/CHANGES-FROM-UPSTREAM.md)
documents each one.

| Their change | For the Tab5 | Files | Overlap with ours |
|---|---|---|---|
| **Spoken replies** (ElevenLabs streaming TTS into the existing MP3 decoder) | Upstream has no speech: since upstream #12, replies are text and `start_tts` only paces captions. This is what makes Muse talk. Needs an ElevenLabs key: a second secret for the flash kit's patching, or set at run time | `muse_chat_session.cpp`, `Kconfig`, overlay (cross-signed TLS bundle) | None |
| **All messages** (pushes): messages Muse sends first, or replies to what you typed in the Muse app, are shown and spoken; the session stays connected | **Message sync between the Muse app and the Tab5**: app conversation reaches the device. Feed pushes into the dock's chat too | `muse_chat_session.cpp`, `muse_voice.*`, `muse_settings*`, `muse_chat.h` | `muse_chat.h` (our console hook): small |
| Avatar reactions: dizzy when shaken, sleepy with a snore, waking, tickled | Shake needs the Tab5's **BMI270** (BSP `bsp_sensor_init`) instead of their QMI8658 probe; the rest is board-independent | `muse_imu.*`, `muse_state.*`, `muse_input.c`, `muse_ui.c`, `muse_pixel.h`, `avatar/muse_pixel.c` | `muse_input.c`, `muse_ui.c`: mechanical |
| Touch-drag volume | The dock has a volume slider; keep or skip | `muse_ui.c` | `muse_ui.c` |
| Battery time-left estimate (`muse_battery_eta`) | Works from voltage; useful with the INA226 | `muse_battery.*`, `muse_settings_ui.c` | None |
| Watchdogs on, display-freeze fix (`muse_lcd_bands.c`, upstream PR #34) | Watchdogs worth adopting; the band fix doesn't apply (the Tab5 uses DSI) | overlay | None |
| `secrets/` + `tools/muse/secrets.py` | Our kit keeps tokens off the build machine; reuse their files for local builds | `tools/muse/` | `board.sh` |

**Licensing.** Their firmware changes are Apache-2.0: keep their copyright
lines and note the source. Their Jollybot animations aren't Apache: Jollybot
is Meta's character, and they say so. Bringing those drawings over carries the
same status; keep them in a separate commit, as they did.

**Order:** spoken replies, then all messages (with the dock showing pushes and
app-typed turns), then reactions on the BMI270.

**Status:** spoken replies and All messages are ported (`SDK-CHANGES.md`),
with the ElevenLabs key patched in by the flashing kit. A free alternative,
a Kokoro server on the owner's PC set with `>tts=` ([`KOKORO.md`](KOKORO.md)),
is used before ElevenLabs. Neither is tried on the Tab5 yet. Pushes play through the voice path, so the dock shows them as spoken
replies. Reactions are next.

## 3. Voice quality on the Tab5

- ES7210 mic slots and echo: today both slots are mixed (`mic_slot = -1`).
  Check which slot carries the echo reference before raising the volume.
- Feedback at default volume, push-to-talk during speech (tap to interrupt).

## 4. Avatar animations and customization

After the reactions in 2: new animations on the existing avatar, and options
to customize it (`tools/muse/avatar.py` already builds a custom avatar from
Muse). Design to come.

## 5. Later

Camera (`camera.capture` on the Tab5's CSI camera, see `NEXT-STEPS.md`), IMU
and RTC tools, emoji in the chat, light sleep.
