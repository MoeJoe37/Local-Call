# Local Call Pro — Python C++ Compatible Secure RTC Edition

Local Call Pro is a cross-platform Python desktop client for low-latency local/internet calls. This package is updated to interoperate with the current C++ Local Call secure RTC build.

The important compatibility change is that Python no longer sends normal WebRTC audio/video tracks. It now uses the same low-latency WebRTC **DataChannel media protocol** as the C++ app:

- `localcall-video` — H.264 Annex-B video frames
- `localcall-audio` — raw Opus audio packets
- `LCM1` frame chunking/reassembly
- signed `localcall.v1` JSON signaling
- ICE/STUN/TURN for NAT traversal
- DTLS/SCTP DataChannel transport

## Compatibility target

This Python package is designed to communicate with:

```text
Local Call C++ secure RTC v2.0.15 or newer
```

It keeps the same signaling envelope used by the C++ app:

```json
{
  "protocol": "localcall.v1",
  "schema": 1,
  "type": "rtc_offer",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "target_id": "peer1234",
  "transport": "webrtc-dtls-srtp",
  "sdp_type": "offer",
  "sdp": "...",
  "auth_alg": "ed25519",
  "auth_public_key": "...",
  "auth_fingerprint": "...",
  "auth_signature": "...",
  "ts": 1777780000000
}
```

Unknown signed fields were removed from the critical C++ path so C++ signature verification does not fail.

## Feature status

| Feature | Status |
|---|---:|
| Python ↔ C++ signed LAN signaling | Yes |
| Python ↔ C++ call invite/accept flow | Yes |
| Python ↔ C++ WebRTC DataChannel negotiation | Yes |
| Python ↔ C++ video payload format | H.264 Annex-B |
| Python ↔ C++ audio payload format | raw Opus |
| NAT traversal | ICE/STUN/TURN |
| Internet signaling relay | Included |
| Legacy raw UDP Python calls | Removed from default path |

## Requirements

Recommended:

- Python 3.10 or newer
- webcam and microphone
- `ffmpeg`/PyAV codec support for H.264 and Opus
- PortAudio for microphone/speaker access

Install Python dependencies:

```bash
pip install -r requirements.txt
```

## Linux system dependencies

### Fedora / Nobara / Bazzite

```bash
sudo dnf install -y \
  python3 python3-pip python3-virtualenv \
  portaudio-devel gcc gcc-c++ make \
  opencv opencv-devel \
  ffmpeg ffmpeg-devel
```

For Bazzite, prefer running inside a toolbox/distrobox or from your home directory.

### Arch / EndeavourOS / CachyOS

```bash
sudo pacman -S --needed \
  python python-pip python-virtualenv \
  portaudio base-devel opencv ffmpeg
```

### Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y \
  python3 python3-pip python3-venv \
  portaudio19-dev build-essential \
  python3-opencv ffmpeg libavdevice-dev
```

### Windows

Install Python 3.10+ and then:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r requirements.txt
python Localcall.py
```

If PyAudio fails to build on Windows, install a prebuilt PyAudio wheel matching your Python version or install it through your preferred Python distribution.

## Quick start — same LAN with the C++ app

1. Start the C++ Local Call app.
2. Start this Python app:

```bash
python Localcall.py
```

3. Both apps should appear through UDP discovery.
4. In the C++ app, send/accept the friend request if you want persistent friend-list calling.
5. Start a call from either side.

The Python client uses C++-compatible call modes: it sends `call_inv.mode = "video"` for camera/screen calls while keeping the internal Python capture mode as camera or screen.

## Quick start — internet calling

Internet calls need a signaling relay for SDP/candidate exchange. The included relay only forwards JSON; it does not decrypt media.

Start relay:

```bash
python signaling_server.py --host 0.0.0.0 --port 8765
```

Then in both clients:

```text
Signaling URL: ws://SERVER_IP_OR_DOMAIN:8765/ws
Room: my-room-name
```

Press **Connect Internet Signaling**.

## ICE/STUN/TURN

Default:

```text
stun:stun.l.google.com:19302
```

Custom semicolon format:

```bash
export LOCALCALL_ICE_SERVERS='stun:stun.l.google.com:19302;turn:turn.example.com:3478,username,password'
```

JSON format:

```bash
export LOCALCALL_ICE_SERVERS='[{"urls":"stun:stun.l.google.com:19302"},{"urls":"turn:turn.example.com:3478","username":"user","credential":"pass"}]'
```

## Media protocol

Each encoded media frame is chunked with this binary header:

```text
0..3   magic:  "LCM1"
4      tag:    "V" for video, "A" for audio
5      version: 1
6..7   chunk index, big-endian uint16
8..9   total chunks, big-endian uint16
10..13 frame id, big-endian uint32
14..15 payload length, big-endian uint16
16..   payload bytes
```

Chunk payload size is `16 KiB`. Maximum media frame size is `2 MiB`.

## Security model

The app creates a persistent Ed25519 device identity on first launch:

```text
Linux:   ~/.local/share/local-call-pro/identity-ed25519.json
Windows: %LOCALAPPDATA%\LocalCallPro\identity-ed25519.json
macOS:   ~/Library/Application Support/LocalCallPro/identity-ed25519.json
```

Critical signaling is signed:

- `friend_req`
- `friend_acc`
- `friend_rej`
- `call_inv`
- `call_acc`
- `call_rej`
- `call_end`
- `rtc_offer`
- `rtc_answer`
- `rtc_ice`

Peer keys are pinned using trust-on-first-use in `trusted-peers.json`.

## Troubleshooting

### Peer appears but calls do not connect

Use a TURN server. Some NAT/router combinations require relay fallback.

### C++ blocks the Python signal as unauthenticated

Delete old trust entries for that peer on both sides, then reconnect. This happens if the Python identity file was regenerated.

### Video is blank

Check that PyAV has an H.264 encoder available. On Linux, make sure FFmpeg development/runtime packages are installed.

### Audio is silent

Check that PyAudio/PortAudio can open your microphone and speaker. On Linux, also check PipeWire/PulseAudio permissions.
