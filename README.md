# Local Call

Local Call is a Qt/C++ LAN communication app for local chat, friend requests, file sharing, voice notes, and local audio/video calls.

This version was polished for cross-platform Linux builds while preserving the existing Local Call wire protocol so Windows, Fedora/Nobara/Bazzite, Arch-based systems, NixOS, and other future builds can still communicate on the same LAN.

## What Changed in This Version

- Reworked CMake into a cleaner cross-platform build with feature switches.
- Added Fedora/Nobara/Bazzite, Arch, and NixOS build paths.
- Added Linux desktop launcher/icon install support.
- Kept the original TCP length-prefixed JSON framing for compatibility.
- Added optional protocol metadata: `protocol`, `schema`, `app_version`, and `platform`.
- Improved peer discovery on Linux by ranking usable LAN IPv4 interfaces and preferring real adapters over common virtual adapters.
- Made TCP discovery reads more robust when packets arrive partially.
- Made the TCP server able to parse multiple complete frames per connection while still accepting old one-frame clients.
- Moved app data to the normal Qt `AppDataLocation`, with fallback reads from the old `AppDataLocation/Local Call` folder.
- Switched JSON saves to atomic writes to reduce the chance of corrupt history after a crash/power loss.
- Added `docs/PROTOCOL.md` explaining the compatibility rules and Matrix-inspired event-envelope approach.

## Features

- LAN peer discovery through UDP broadcast, multicast, and TCP scan fallback.
- Friend requests, accept/decline, remove, block, and persistent contacts.
- 1-to-1 chat with text, images, files, and voice notes.
- Group chat with owner/helper roles and per-member permissions.
- Optional audio/video calls and screen-sharing when multimedia/OpenCV dependencies are present.
- Toast-style notifications for messages, calls, and requests.
- Dark UI theme.
- Persistent local chat history.
- Windows firewall helper; Linux firewall helper script included.

## Network Protocol

Local Call signaling uses:

```text
[4 bytes big-endian length][JSON body]
```

The protocol intentionally stays JSON-based and extensible. New builds add metadata fields but old builds can ignore them. See [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Ports

| Purpose | Protocol | Port |
|---|---:|---:|
| LAN discovery | UDP | 50005 |
| Signaling/events | TCP | 50010 |
| Audio stream | UDP | 50100 |
| Video stream | UDP | 50105 |
| Group call | UDP | 50200 |

## Dependencies

Required:

- CMake 3.20+
- C++20 compiler
- Qt 6 Core, Gui, Widgets, Network, Concurrent
- nlohmann/json

Recommended:

- Qt 6 Multimedia for audio and voice notes
- OpenCV for video/screen-share features
- Qt 6 WebSockets + libdatachannel/OpenH264/Opus/libyuv for the optional WebRTC module

The stable LAN TCP/UDP app works without the optional WebRTC module.

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
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
cmake --build build --parallel
./build/LocalCall
```

## Build on Windows

Install:

- Visual Studio 2022 Build Tools
- CMake
- Qt 6 MSVC 2022 64-bit kit
- Qt Multimedia

Then:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
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
# Stable basic LAN build with chat, discovery, files, and no optional RTC module
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF

# Force OpenCV and fail if missing
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_OPENCV=ON
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
│   ├── build-linux.sh
│   ├── install-deps-fedora.sh
│   ├── install-deps-arch.sh
│   └── open-firewall-linux.sh
├── packaging/linux/
│   ├── localcall.desktop
│   └── localcall.png
└── third_party/nlohmann/json.hpp
```

## Notes

- All devices must be on the same LAN/subnet unless routed/firewall rules allow the ports above.
- Linux distributions with strict firewalls may need the firewall script.
- Older Local Call clients can still parse the new event JSON because the original field names and framing were preserved.
- This app is inspired by Matrix design principles such as open JSON events, extensibility, and user-controlled communication, but it is not a Matrix protocol client and does not federate with Matrix homeservers.
