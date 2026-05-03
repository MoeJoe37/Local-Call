# Local Call

Local Call is a Qt/C++ peer-to-peer communication app for LAN chat, friend requests, file sharing, voice notes, and low-latency audio/video calls.

This package updates the previous build into a **secure RTC-first** version. The old raw UDP call path is kept only as a compatibility fallback when WebRTC dependencies are unavailable.

## What Changed in Version 2.0.0

- Added **WebRTC/libdatachannel-first calls** using ICE + STUN/TURN-capable negotiation.
- Added **encrypted media transport** through WebRTC DTLS-SRTP.
- Added persistent **Ed25519 device identity** using OpenSSL.
- Added signed critical signaling for friend requests, call invites, call accept/end, SDP offers/answers, and ICE candidates.
- Added fingerprint pinning fields to saved friends and pending requests.
- Added RTC signaling events: `rtc_offer`, `rtc_answer`, and `rtc_ice`.
- Tuned the media path for lower latency:
  - 10 ms Opus frames at 48 kHz.
  - Opus DTX/FEC disabled by default to avoid extra buffering.
  - moderate encoder complexity.
  - 360p/30 FPS default video profile with lower bitrate bias.
- Added configurable ICE servers through `LOCALCALL_ICE_SERVERS`.
- Updated Fedora/Nobara/Bazzite, Arch, and NixOS dependency paths.
- Preserved the original length-prefixed JSON framing so older Local Call builds can still understand non-RTC events.

## Security / Network Capability Matrix

| Capability | Secure WebRTC path | Legacy raw UDP fallback |
|---|---:|---:|
| End-to-end encrypted call media | Yes, DTLS-SRTP | No |
| User/device authentication | Yes, Ed25519 signed critical signaling | No |
| NAT traversal | Yes, ICE with STUN/TURN configuration | No |
| Internet calling | Yes, when peers can exchange signaling and ICE succeeds; TURN may be required on strict NATs | No |
| Packet retransmission / congestion handling | Yes for WebRTC control/data layers; media uses real-time RTP behavior to avoid latency spikes | No custom UDP recovery |

For the lowest latency, use direct ICE/STUN whenever possible. TURN relay works as a fallback but adds extra network distance and therefore more latency.

## Features

- LAN peer discovery through UDP broadcast, multicast, and TCP scan fallback.
- Friend requests, accept/decline, remove, block, and persistent contacts.
- 1-to-1 chat with text, images, files, and voice notes.
- Group chat with owner/helper roles and per-member permissions.
- Secure WebRTC audio/video calls when dependencies are available.
- Optional legacy LAN media fallback when WebRTC is disabled.
- Toast-style notifications for messages, calls, and requests.
- Dark UI theme.
- Persistent local chat history.
- Windows firewall helper and Linux firewall helper script.

## ICE / STUN / TURN Configuration

By default, Local Call uses:

```bash
stun:stun.l.google.com:19302
```

You can override this with a comma-separated environment variable:

```bash
export LOCALCALL_ICE_SERVERS="stun:stun.l.google.com:19302,turn:user:pass@turn.example.com:3478"
./build/LocalCall
```

Use TURN only when direct peer-to-peer ICE fails, because TURN relays media through a server and usually increases latency.

## Network Protocol

Classic signaling still uses:

```text
[4 bytes big-endian length][JSON body]
```

Secure calls add signed JSON events over the same signaling channel:

- `call_inv`
- `call_acc`
- `call_end`
- `rtc_offer`
- `rtc_answer`
- `rtc_ice`

See [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Ports

| Purpose | Protocol | Port |
|---|---:|---:|
| LAN discovery | UDP | 50005 |
| Local signaling/events | TCP | 50010 |
| Legacy audio stream | UDP | 50100 |
| Legacy video stream | UDP | 50105 |
| Legacy group call | UDP | 50200 |
| WebRTC media | UDP/TCP selected by ICE | dynamic |

## Dependencies

Required:

- CMake 3.20+
- C++20 compiler
- Qt 6 Core, Gui, Widgets, Network, Concurrent
- OpenSSL Crypto
- nlohmann/json

Recommended for secure calls:

- Qt 6 Multimedia
- Qt 6 WebSockets
- libdatachannel
- OpenH264
- Opus
- libyuv

Optional fallback video/screen-share:

- OpenCV

## Build on Fedora / Nobara

```bash
./scripts/install-deps-fedora.sh
./scripts/build-linux.sh
./build/LocalCall
```

If optional WebRTC packages are missing:

```bash
LOCALCALL_WITH_WEBRTC=OFF ./scripts/build-linux.sh
```

## Build on Bazzite

Bazzite is rpm-ostree based. The cleanest build workflow is inside a toolbox/distrobox Fedora container:

```bash
distrobox create --name localcall-dev --image fedora:latest
distrobox enter localcall-dev
sudo dnf install -y git
cd /path/to/LocalCall
./scripts/install-deps-fedora.sh
./scripts/build-linux.sh
./build/LocalCall
```

For host-layer installation, use `rpm-ostree install` with the same package list from `scripts/install-deps-fedora.sh`, reboot, then build.

## Build on Arch / EndeavourOS / CachyOS

```bash
./scripts/install-deps-arch.sh
./scripts/build-linux.sh
./build/LocalCall
```

## Build on NixOS

```bash
nix build
./result/bin/LocalCall
```

For development:

```bash
nix develop
cmake -S . -B build -G Ninja
cmake --build build --parallel
./build/LocalCall
```

## Build on Windows

Install:

- Visual Studio 2022 Build Tools
- CMake
- Qt 6 MSVC 2022 64-bit kit
- Qt Multimedia
- OpenSSL
- libdatachannel/OpenH264/Opus/libyuv through vcpkg or another CMake-compatible package source

Then:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" -DLOCALCALL_WITH_WEBRTC=AUTO
cmake --build build --config Release
cd build\Release
windeployqt --release LocalCall.exe
LocalCall.exe
```

## CMake Options

```bash
-DLOCALCALL_WITH_MULTIMEDIA=ON|OFF
-DLOCALCALL_WITH_OPENCV=AUTO|ON|OFF
-DLOCALCALL_WITH_WEBRTC=AUTO|ON|OFF
```

Examples:

```bash
# Secure RTC preferred build
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=ON

# Compatibility-only LAN build without WebRTC
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
```

## Linux Firewall

On trusted LANs only:

```bash
./scripts/open-firewall-linux.sh
```

## Install on Linux

```bash
cmake --install build --prefix "$HOME/.local"
```

This installs the binary plus a desktop launcher/icon when supported.

## Project Structure

```text
LocalCall/
├── CMakeLists.txt
├── flake.nix
├── docs/
│   └── PROTOCOL.md
├── include/
├── src/
├── scripts/
├── packaging/linux/
└── third_party/nlohmann/json.hpp
```

## Notes

- The secure low-latency path depends on WebRTC dependencies being found at build time.
- TURN can make internet calling work behind strict NATs, but direct peer-to-peer ICE is preferred for latency.
- Legacy clients can still use non-RTC LAN events, but secure calls require the new v2 WebRTC signaling.
- The app is inspired by Matrix design ideas such as versioned events and extensible JSON envelopes, but it is not a Matrix client and does not federate with Matrix homeservers.
