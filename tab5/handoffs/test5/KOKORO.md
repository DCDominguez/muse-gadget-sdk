# Free spoken replies with Kokoro

Muse's replies arrive as text; the Tab5 turns them into speech. Instead of
ElevenLabs (a paid service with a small free allowance), it can use
[Kokoro](https://huggingface.co/hexgrad/Kokoro-82M), an open (Apache-2.0)
voice model, running on your PC through
[Kokoro-FastAPI](https://github.com/remsky/Kokoro-FastAPI). Free, no limits,
no key, and the replies' text stays on your home network. The PC has to be on
for the Tab5 to speak; when it isn't, replies stay text.

Kokoro-FastAPI answers OpenAI's speech API (`/v1/audio/speech`), so the same
server can speak for other tools that let you set an OpenAI-compatible speech
URL (`http://<PC>:8880/v1`).

## 1. Run the server on your PC

With [Docker Desktop](https://www.docker.com/products/docker-desktop/):

```powershell
docker run -d --name kokoro --restart unless-stopped -p 8880:8880 ghcr.io/remsky/kokoro-fastapi-cpu:latest
```

(`kokoro-fastapi-gpu` with `--gpus all` on an NVIDIA card. Without Docker, see
the project's README.) The first start downloads the model. Then check it from
the PC:

```powershell
Invoke-WebRequest http://localhost:8880/v1/audio/speech -Method Post -ContentType 'application/json' `
  -Body '{"model":"kokoro","input":"Hello from Muse.","voice":"af_heart","response_format":"mp3"}' `
  -OutFile kokoro-test.mp3
start kokoro-test.mp3
```

## 2. Let the Tab5 reach it

- Find the PC's address on your network: `ipconfig` → IPv4 Address, e.g.
  `192.168.1.20`. Reserve it in your router (DHCP reservation) so it doesn't
  change.
- Allow port 8880 in Windows Firewall, on private networks only (an
  administrator PowerShell):

  ```powershell
  New-NetFirewallRule -DisplayName "Kokoro TTS" -Direction Inbound -Protocol TCP -LocalPort 8880 -Action Allow -Profile Private
  ```

  Your home Wi-Fi must be set to **Private** in Windows for this rule to apply.

## 3. Point the Tab5 at it

Over the USB cable, in a serial terminal (`python -m serial.tools.miniterm COM4
115200`), type:

```
>tts=http://192.168.1.20:8880
```

It answers `@tts {"url":"http://192.168.1.20:8880","voice":"af_heart"}` and
keeps the setting through restarts and app-only flashes. A first flash
(`-First`) clears it. Other commands:

| | |
|---|---|
| `>tts` | Show the setting |
| `>tts.voice=bf_emma` | Another voice ([the list](https://huggingface.co/hexgrad/Kokoro-82M/blob/main/VOICES.md)); blends work, e.g. `af_bella+af_sky`. `>tts.voice=` restores `af_heart` |
| `>tts=` | No server: back to ElevenLabs if the firmware has a key, else text only |

A server address with no path gets `/v1/audio/speech`; give a full URL for a
server that uses another path. The server is used before ElevenLabs.

Ask Muse something. The boot log (`>log`) shows `speech: first audio after
...` and `speech: N bytes of MP3`, or `speech: can't reach the speech server`
(PC off, wrong address, firewall) or `speech: the speech server HTTP 4xx ...`
(an unknown voice, for instance).

## Notes

- **Plain HTTP on your network.** Each reply's text goes from the Tab5 to the
  PC unencrypted. That's fine on a home network you trust; don't point it at a
  server across the internet over `http://`. `https://` works when the server
  has a certificate from a public authority.
- **Speed.** On a CPU, a sentence takes about a second or two to start; a GPU
  is much faster. Long replies stream, so speech starts before the whole reply
  is ready.
- Kokoro's English voices are the strongest; it has a few other languages
  (`VOICES.md`).
