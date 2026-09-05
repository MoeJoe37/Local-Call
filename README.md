# Local Call

Peer-to-peer chat and calling for your local network. Qt 6, C++20, no server, no account —
two machines on the same LAN discover each other and talk directly.

Chat, files, voice notes, and real Opus/H.264 audio, video and screen-share calls, all over
sockets you can see in `netstat`.

## Quick start

**Linux (Fedora / Nobara):**

```bash
./scripts/install-deps-fedora.sh    # or install-deps-arch.sh
./scripts/build-linux.sh
./build/LocalCall
```

**Windows:** install a Qt 6 kit and [vcpkg](https://vcpkg.io), then from the project root:

```bat
build.bat
```

Run two instances on two machines on the same subnet. They appear in each other's peer list
within a second or two; send a friend request, accept it, then chat or press call.

## Features

- **Discovery** — UDP broadcast plus multicast, with a TCP subnet scan as fallback for
  networks that drop broadcast traffic.
- **Friends** — requests, accept/decline, remove, block; contacts persist across restarts.
- **Chat** — 1-to-1 text, images, arbitrary files up to 2 GB, and recorded voice notes.
- **Groups** — group chat with owner/helper roles and per-member permissions.
- **Calls** — voice, video and screen share, with mute, camera, screen and stats toggles in
  the call window.
- **Live stats overlay** — transport, codecs, up/down bitrate, RTT, loss, jitter, resolution,
  in/out FPS and dropped frames.
- **Signed identity** — Ed25519 device keypair; friend requests, call invites and call control
  are signed and peer fingerprints are pinned.
- **Toast notifications** for messages, calls and requests, plus persistent local history.
- **Firewall helpers** — automatic rules on Windows, a script on Linux.

## How calls work

Calls run on Local Call's own LAN media stack rather than on a WebRTC library:

- **Audio** — Opus, 48 kHz mono, 20 ms frames, ~32 kbit/s, inband FEC. Every capture device is
  resampled to 48 kHz mono before encoding, so two machines never have to agree on a
  sound-card format.
- **Video** — OpenH264 baseline, encoded and decoded on dedicated threads so a 30 fps camera
  callback never touches the GUI thread. Resolution (144p–1080p or source) and frame rate are
  picked in the call window, and bitrate adapts downward when the transport reports loss.
- **Transport** — one UDP socket on port 50100 carries audio, video and control, multiplexed
  by packet tag. If no reply arrives within 1.5 s — blocked UDP, hostile firewall — the call
  falls back to TCP on port 50120 and the call window's transport badge says `TCP`.
- **Playback** — an adaptive de-jitter buffer (60 ms nominal, 240 ms cap) reorders packets and
  conceals losses with Opus PLC. The audio sink pulls from it on the sound card's clock
  instead of being written to as packets land.

**libdatachannel is optional.** It adds a WebRTC ICE/DTLS transport for calls beyond the LAN;
without it, LAN calls behave identically. Calls need only Qt Multimedia + Opus for voice, plus
OpenH264 + libyuv for video. CMake prints what it found:

```text
-- === Local Call build configuration ===
--   Version       : 2.0.22
--   Qt Multimedia : TRUE
--   Opus          : TRUE
--   OpenH264      : TRUE
--   libyuv        : TRUE
--   Calls         : voice+video
--   WebRTC P2P    : FALSE (optional transport)
```

`Calls` reads `voice only (install OpenH264 + libyuv for video)` or `disabled` with the missing
package named when something is absent. Check this line first if a call connects but carries
nothing.

## Ports

| Purpose | Protocol | Port |
|---|---:|---:|
| LAN discovery | UDP | 50005 |
| Signalling, events, file transfer | TCP | 50010 |
| Call media — audio + video + control | UDP | 50100 |
| Call media fallback | TCP | 50120 |
| Group calls — reserved, not yet implemented | UDP | 50200 |
| WebRTC media, optional build | UDP/TCP via ICE | dynamic |

Windows rules are created on first run (elevation prompt). On Linux, for trusted LANs only:

```bash
./scripts/open-firewall-linux.sh
```

## Protocol

Signalling is length-prefixed JSON:

```text
[4 bytes big-endian length][JSON body]
```

Media uses a fixed 24-byte `LCM3` header — tag, flags, sequence, capture timestamp, chunk
index/count and a sender token — shared by both the UDP and TCP transports. Both formats are
specified in [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Security

- Each device generates a persistent Ed25519 keypair via OpenSSL on first run.
- Friend requests, call invites, accepts and ends are signed. Friends store the peer's
  fingerprint, so a swapped key is visible.
- **Media on the LAN path is not encrypted.** Local Call is built for networks you control.
  For encrypted media across untrusted networks, build with libdatachannel
  (`-DLOCALCALL_WITH_WEBRTC=ON`), which carries media over DTLS.

## Building

### Dependencies

Required:

- CMake 3.20+ and a C++20 compiler
- Qt 6 — Core, Gui, Widgets, Network, Concurrent
- OpenSSL Crypto
- nlohmann/json — vendored at `third_party/nlohmann/json.hpp`, a system package is used if present

For calls:

- Qt 6 Multimedia + Opus — voice
- OpenH264 + libyuv — video and screen share

Optional:

- libdatachannel — WebRTC ICE/DTLS transport

### Linux

```bash
./scripts/install-deps-fedora.sh     # Fedora / Nobara
./scripts/install-deps-arch.sh       # Arch / EndeavourOS / CachyOS
./scripts/build-linux.sh
./build/LocalCall
```

`build-linux.sh` honours `BUILD_DIR`, `BUILD_TYPE`, `GENERATOR`, `LOCALCALL_WITH_WEBRTC` and
`LOCALCALL_WITH_MULTIMEDIA` as environment variables.

On image-based distros such as Bazzite, build inside a container:

```bash
distrobox create --name localcall-dev --image fedora:latest
distrobox enter localcall-dev
sudo dnf install -y git
cd /path/to/Local-Call
./scripts/install-deps-fedora.sh
./scripts/build-linux.sh
```

### NixOS

```bash
nix build
./result/bin/LocalCall
```

### Windows

Install a Qt 6 kit including Qt Multimedia, CMake, and vcpkg. Dependencies come from
`vcpkg.json` in manifest mode, so there is nothing to `vcpkg install` by hand. Then:

```bat
build.bat                  :: also: build.bat clean debug, build.bat clean release installer
```

`build.bat` calls `scripts/build-windows.ps1`, which auto-detects a Qt kit under `C:\Qt`,
`%USERPROFILE%\Qt`, `QTDIR`, `Qt6_DIR`, `LOCALCALL_QT_DIR` or `CMAKE_PREFIX_PATH`, then picks
everything else to match it:

| Qt kit | Generator | vcpkg triplet | Build dir | libdatachannel |
|---|---|---|---|---|
| `msvc2022_64` / `msvc2019_64` | Visual Studio 17 2022 x64 | `x64-windows` | `build` | available |
| `mingw_64` | Ninja + Qt's MinGW toolchain | `x64-mingw-dynamic` | `build-mingw` | never — LAN calls don't need it |

An MSVC kit wins when both are installed, because MSVC and MinGW Qt DLLs cannot be mixed in
one build. To force a kit:

```powershell
.\scripts\build-windows.ps1 -QtDir "C:/Qt/6.11.1/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Clean
.\build\Release\LocalCall.exe        # MinGW kit: .\build-mingw\LocalCall.exe
```

Launch `LocalCall.exe`, the native launcher — not `LocalCallApp.exe`, the Qt binary beside it.
The launcher sanity-checks the runtime folder before starting Qt, which is what prevents
entry-point popups from stale DLLs.

Never deploy with a `windeployqt.exe` picked up from `PATH`. `Entry Point Not Found` almost
always means the executable was linked against one Qt build and is loading another's DLLs; the
build and deploy scripts always use the `windeployqt.exe` from the kit in `CMAKE_PREFIX_PATH`.
See [`docs/RUNTIME_DEPLOYMENT.md`](docs/RUNTIME_DEPLOYMENT.md) and [`BUILD.md`](BUILD.md).

### CMake options

```text
LOCALCALL_WITH_MULTIMEDIA      = ON | OFF            Qt Multimedia capture/playback, required for calls
LOCALCALL_WITH_WEBRTC          = AUTO | ON | OFF     optional libdatachannel ICE/DTLS transport
LOCALCALL_INSTALL_QT_RUNTIME   = ON | OFF
LOCALCALL_POST_BUILD_DEPLOY_QT = ON | OFF
LOCALCALL_INSTALL_DESKTOP_FILE = ON | OFF            Linux .desktop launcher and icon
```

```bash
# LAN calls, skip the libdatachannel probe entirely
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
cmake --build build --parallel
```

## Install and package

```bash
./scripts/deploy-linux.sh                      # cmake --install into dist/LocalCall
./scripts/deploy-macos.sh
cpack --config build/CPackConfig.cmake
```

On Windows, `build.bat clean release` produces the installer-ready tree at `dist\LocalCall`.
Package only that folder — never `build\Debug` or hand-picked files from `build\Release`, which
is how Debug runtime DLLs (`VCRUNTIME140D.dll` and friends) end up in a release installer.
`build.bat clean release installer` runs Inno Setup 6 against
`packaging\windows\LocalCall.iss`.

## Project layout

```text
Local-Call/
├── CMakeLists.txt                  # dependency detection + call capability summary
├── build.bat, make-installer.bat   # Windows entry points
├── flake.nix
├── include/, src/                  # UI, signalling, friends, media stack
├── resources/theme/localcall.qss   # the entire app stylesheet
├── scripts/                        # per-distro deps, build, deploy, firewall, runtime checks
├── packaging/linux/, packaging/windows/
├── docs/PROTOCOL.md, docs/RUNTIME_DEPLOYMENT.md, docs/PARSEC_BACKEND.md
└── third_party/nlohmann/json.hpp
```

## Troubleshooting

**Peers never appear.** Both machines must be on the same subnet, and UDP 50005 must be open.
Some networks drop broadcast; the TCP scan fallback covers that but is slower.

**A call connects but there is no audio or video.** Check the `Calls` line in the CMake
configure summary. If it reads `disabled` or `voice only`, the codec libraries were missing at
build time and the call window has nothing to send.

**The call window badge reads `TCP`.** UDP 50100 is blocked somewhere between the two machines.
Calls still work, with slightly higher latency under loss.

**Remote video stays black.** The decoder requests a keyframe automatically. If it persists,
the peer's video encoder failed to start and the call is audio-only on that side.

**Windows entry-point error.** Qt DLLs from a different kit are being loaded. Run
`scripts/fix-windows-entrypoint.ps1 -QtDir "<your kit>" -VcpkgRoot "C:/vcpkg" -Rebuild`.

Version history is in [`CHANGELOG.md`](CHANGELOG.md).

## Notes

- Local Call borrows ideas from Matrix — versioned events, extensible JSON envelopes — but it
  is not a Matrix client and does not federate.
- `python version/` is an older, separate reference implementation and is not part of this
  build.
- Licensed under the terms in [`LICENSE.txt`](LICENSE.txt).
