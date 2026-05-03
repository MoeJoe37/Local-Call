# Local Call Pro — Python Secure RTC Edition

Local Call Pro is a cross-platform Python desktop app for low-latency voice, video, and screen-sharing calls. This edition replaces the old raw UDP media path with a **WebRTC-first** design using `aiortc`, adds **Ed25519 signed signaling**, supports **STUN/TURN ICE configuration**, and includes a minimal WebSocket signaling relay for internet calling.

The main goal of this version is **low latency first** while still fixing the major limitations of the original Python LAN prototype.

---

## What changed in this secure RTC version

| Area | Old Python version | Secure RTC Python version |
|---|---|---|
| Media transport | Raw UDP JPEG/audio packets | WebRTC via `aiortc` |
| Media encryption | No | Yes, WebRTC DTLS-SRTP/SRTP |
| User/device authentication | No | Yes, Ed25519 device identity + signed critical signaling |
| NAT traversal | No | Yes, ICE with STUN/TURN configuration |
| Internet calling | No | Yes, through the included WebSocket signaling relay plus ICE/STUN/TURN |
| Packet loss / congestion handling | No | WebRTC-managed RTP/RTCP behavior; live media stays latency-prioritized |
| LAN discovery | UDP broadcast only | UDP broadcast still available for local peer discovery |
| C++ protocol alignment | No signed RTC events | Uses Local Call `localcall.v1` signed JSON envelope |

---

## Important latency design choices

This version is tuned for real-time calling rather than file-like reliability:

- WebRTC is the default media path.
- Media prefers UDP through ICE when the network allows it.
- TURN relay is supported but should be used as a fallback because it can add latency.
- Audio uses 48 kHz mono frames with a 10 ms packetization target.
- Default video profile is 360p/30 FPS for lower encoding and network delay.
- The control data channel is unordered with `maxRetransmits=0`.
- The app does **not** add TCP-style retransmission on top of live audio/video because that increases delay.

For the lowest latency, use a nearby STUN/TURN server and avoid routing through distant relays.

---

## Project files

```text
Localcall.py                 Main PyQt6 desktop app
signaling_server.py          Minimal WebSocket signaling relay
requirements.txt             Python package dependencies
README.md                    This guide
docs/SECURITY_AND_PROTOCOL.md Security and wire protocol notes
scripts/run_app.sh           Linux helper: create venv, install deps, run app
scripts/run_signaling_server.sh Linux helper: run signaling relay
```

---

## Requirements

Recommended:

- Python 3.10 or newer
- Webcam and microphone for calls
- Speaker/headset for audio output
- Network access between peers or a reachable signaling server

Python packages are listed in `requirements.txt`:

```bash
pip install -r requirements.txt
```

---

## Linux system dependencies

### Fedora / Nobara / Bazzite

```bash
sudo dnf install -y \
  python3 python3-pip python3-virtualenv \
  portaudio-devel gcc gcc-c++ make \
  opencv opencv-devel \
  ffmpeg ffmpeg-devel
```

Bazzite note: prefer running the app inside your home directory or inside a distrobox/toolbox if your base image is immutable.

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

---

## Quick start — same LAN

1. On both devices:

```bash
python -m venv .venv
source .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install --upgrade pip
pip install -r requirements.txt
python Localcall.py
```

2. Both clients should appear in the peer list through LAN discovery.
3. Select the peer.
4. Press **Start Secure Video Call** or **Start Secure Screen Share**.

The app also starts a TCP signaling listener on port `50010`, using Local Call length-prefixed JSON signaling.

---

## Quick start — internet calling

Internet calls need a signaling relay so peers can exchange signed WebRTC offers/answers. The relay does **not** decrypt media; it only forwards JSON signaling events.

### 1. Start the relay on a reachable server

```bash
python -m venv .venv
source .venv/bin/activate
pip install aiohttp
python signaling_server.py --host 0.0.0.0 --port 8765
```

Open TCP port `8765` in the server firewall.

### 2. Connect both clients to the same relay and room

In the app:

```text
Signaling URL: ws://SERVER_IP_OR_DOMAIN:8765/ws
Room: my-room-name
```

Press **Connect Internet Signaling** on both clients.

### 3. Add TURN for difficult networks

Set the ICE server field in the app, or start the app with:

```bash
export LOCALCALL_ICE_SERVERS='stun:stun.l.google.com:19302;turn:turn.example.com:3478,username,password'
python Localcall.py
```

You can also use JSON:

```bash
export LOCALCALL_ICE_SERVERS='[{"urls":"stun:stun.l.google.com:19302"},{"urls":"turn:turn.example.com:3478","username":"user","credential":"pass"}]'
```

---

## Environment variables

| Variable | Purpose | Example |
|---|---|---|
| `LOCALCALL_SIGNALING_URL` | Default WebSocket signaling URL | `ws://192.168.1.50:8765/ws` |
| `LOCALCALL_ROOM` | Default signaling room | `office` |
| `LOCALCALL_ICE_SERVERS` | STUN/TURN servers | `stun:stun.l.google.com:19302;turn:host:3478,user,pass` |
| `LOCALCALL_SIGNALING_HOST` | Helper script server host | `0.0.0.0` |
| `LOCALCALL_SIGNALING_PORT` | Helper script server port | `8765` |

---

## Security model

This version creates a persistent Ed25519 identity on first launch:

```text
Linux:   ~/.local/share/local-call-pro/identity-ed25519.json
Windows: %LOCALAPPDATA%\LocalCallPro\identity-ed25519.json
macOS:   ~/Library/Application Support/LocalCallPro/identity-ed25519.json
```

Critical signaling messages are signed:

- `hello`
- `call_inv`
- `call_acc`
- `call_rej`
- `call_end`
- `rtc_offer`
- `rtc_answer`
- `rtc_ice`

The app stores peer public keys using a **trust-on-first-use** model in:

```text
trusted-peers.json
```

If a peer ID later appears with a different public key, the app blocks that message because it may indicate impersonation or a device reset.

For the strongest verification, compare fingerprints out-of-band before accepting calls.

---

## Compatibility notes

The Python app uses the same event envelope style as the updated C++ version:

```json
{
  "protocol": "localcall.v1",
  "schema": 1,
  "app_version": "2.1.0-python-secure-rtc",
  "platform": "linux-x86_64",
  "type": "rtc_offer",
  "from_id": "abcd1234",
  "from_name": "MoeJoe",
  "target_id": "peer1234",
  "transport": "webrtc-dtls-srtp",
  "rtc_session_id": "uuid",
  "sdp_type": "offer",
  "sdp": "v=0...",
  "auth_alg": "ed25519",
  "auth_public_key": "base64url-public-key",
  "auth_fingerprint": "base64url-sha256-public-key",
  "auth_signature": "base64url-signature",
  "ts": 1777780000000
}
```

LAN signaling uses the Local Call TCP frame:

```text
[4-byte big-endian length][UTF-8 JSON body]
```

The old Python UDP media protocol is not used by default because it did not provide encryption, authentication, NAT traversal, or congestion handling.

---

## Troubleshooting

### Peer does not appear on LAN

- Make sure both devices are on the same subnet.
- Allow UDP port `50005` and TCP port `50010` through the firewall.
- Some guest Wi-Fi networks block client-to-client traffic.

### Internet call connects signaling but no media

- Add a TURN server to `LOCALCALL_ICE_SERVERS`.
- Use a TURN server geographically close to both users.
- Check that the TURN username/password are correct.
- Corporate networks may block UDP; use TURN over TCP/TLS if your TURN provider supports it.

### PyAudio install fails

Install PortAudio development headers first. See the Linux dependency section above.

### Camera is black

- Check OS camera permissions.
- Close other apps using the camera.
- Try a lower resolution such as 360p.

---

## Current limitations

- This is a peer-to-peer RTC desktop app, not a full Matrix client.
- The included signaling server is intentionally minimal and should be placed behind HTTPS/WSS for public deployment.
- Group calls are not implemented in this Python edition.
- Live quality changes are applied on the next call to avoid unstable renegotiation during active media.
- TURN credentials must be supplied by the user or server operator.

---

## Recommended production hardening

For public internet use:

1. Put `signaling_server.py` behind Nginx/Caddy with TLS and WSS.
2. Add authentication to the signaling server if rooms should be private.
3. Use a real TURN service with short-lived credentials.
4. Compare fingerprints out-of-band before sensitive calls.
5. Keep dependencies updated.

