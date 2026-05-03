# Local Call

Local Call is a Qt/C++ peer-to-peer communication app for LAN chat, friend requests, file sharing, voice notes, and low-latency audio/video calls.

This package updates the previous build into a **secure RTC-first** version. The old raw UDP call path is kept only as a compatibility fallback when WebRTC dependencies are unavailable.

## What Changed in Version 2.0.13

### v2.0.13 Windows launcher runtime fix

Windows builds now produce two executables:

- `LocalCall.exe` — a native launcher with no Qt imports. Run this file.
- `LocalCallApp.exe` — the real Qt application. Do not run this directly.

The launcher repairs stale Qt DLLs from `localcall-qt-prefix.txt`, prepares the DLL/plugin search path, smoke-loads Qt from the deployed folder, and then starts `LocalCallApp.exe`. This prevents the repeated Windows loader entry-point popups that appeared for different Qt Widgets methods when an old MinGW/old-version `Qt6Widgets.dll` was loaded.


### Runtime Fixes in 2.0.13

- Windows post-build Qt deployment is now enabled by default, so a normal `cmake --build` deploys the matching Qt runtime beside `LocalCall.exe`.
- `scripts/deploy-windows.ps1` now deletes stale Qt DLLs/plugins, runs the exact `windeployqt.exe` from the Qt kit passed to CMake, writes `qt.conf`, and verifies every deployed `Qt6*.dll` by SHA-256 hash against the selected Qt kit.
- Added `scripts/check-windows-runtime.ps1` to detect Qt DLL mismatches before launching.
- Added `scripts/run-windows.ps1` to run the app with the deployed folder and selected Qt kit first in `PATH`.

If Windows shows `Entry Point Not Found` for a Qt symbol such as `QSlider`, delete the entire `build` folder and use the clean commands below.


- Added **WebRTC/libdatachannel-first calls** using ICE + STUN/TURN-capable negotiation.
- Added **encrypted media transport** through WebRTC ICE/DTLS DataChannels.
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


### Build Fixes in 2.0.13

- Removed the invalid `rtc/opusrtpdepacketizer.hpp` include and now uses `rtc/rtpdepacketizer.hpp`, which provides `rtc::OpusRtpDepacketizer`.
- Replaced unsupported OpenH264 symbols with portable ones used by current OpenH264 headers: `PRO_BASELINE` and `VIDEO_BITSTREAM_DEFAULT`.
- Replaced non-portable libdatachannel RTP helper classes with stable low-latency DataChannels and Local Call frame chunking.

## Security / Network Capability Matrix

| Capability | Secure WebRTC path | Legacy raw UDP fallback |
|---|---:|---:|
| End-to-end encrypted call media | Yes, DTLS/SCTP | No |
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
- Qt WebSockets is not required by the default build
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

If you intentionally want the legacy LAN-only fallback:

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
- Qt 6 MSVC 2022 64-bit kit, including Qt Multimedia
- vcpkg at `C:\vcpkg`

Recommended command:

```powershell
.\scripts\build-windows.ps1 -QtDir "C:/Qt/6.11.0/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Clean
.\build\Release\LocalCall.exe
```

Manual command:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" `
  -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON `
  -DLOCALCALL_WITH_WEBRTC=ON
cmake --build build --config Release
.\scripts\deploy-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64" -Clean
.\build\Release\LocalCall.exe
```

Do **not** run a random `windeployqt` from `PATH`. `LocalCall.exe - Entry Point Not Found` almost always means the executable was linked against one Qt build but is loading different `Qt6*.dll` files at runtime. The build/deploy scripts use the `windeployqt.exe` from the same Qt kit passed in `CMAKE_PREFIX_PATH`.


### Windows post-build deploy error fix

If MSBuild prints `Target executable does not exist: ".../build/Release/LocalCall.exe"`, you are seeing a generator quoting issue in the deploy step. Version 2.0.13 fixes it by normalizing quoted paths inside `cmake/deploy_windows_qt.cmake`. A safe manual fallback is:

```powershell
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" `
  -DLOCALCALL_WITH_WEBRTC=ON `
  -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON
cmake --build build --config Release
.\scripts\deploy-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64" -Clean
```

## CMake Options

```bash
-DLOCALCALL_WITH_MULTIMEDIA=ON|OFF
-DLOCALCALL_WITH_OPENCV=AUTO|ON|OFF
-DLOCALCALL_WITH_WEBRTC=ON|AUTO|OFF
-DLOCALCALL_INSTALL_QT_RUNTIME=ON|OFF
-DLOCALCALL_POST_BUILD_DEPLOY_QT=ON|OFF
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

## Install / Deploy on Linux

```bash
./scripts/deploy-linux.sh
```

This runs `cmake --install` into `dist/LocalCall`. The installed executable uses relative RPATH so bundled libraries can be found next to the app when Qt's deployment script copies them. For real distribution across Fedora/Nobara/Bazzite/Arch/NixOS, use the install tree as the input to an AppImage or Flatpak package, or build native packages per distro.

Manual equivalent:

```bash
cmake --install build --prefix "$PWD/dist/LocalCall"
```

## Deploy on macOS

```bash
./scripts/deploy-macos.sh
```

The Qt CMake deployment script uses macOS bundle deployment when supported by the host Qt installation.

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

## Runtime Deployment Troubleshooting

See `docs/RUNTIME_DEPLOYMENT.md` for the Qt DLL/shared-library mismatch RCA and the correct deployment commands.

## Notes

- The secure low-latency path depends on WebRTC dependencies being found at build time.
- TURN can make internet calling work behind strict NATs, but direct peer-to-peer ICE is preferred for latency.
- Legacy clients can still use non-RTC LAN events, but secure calls require the new v2 WebRTC signaling.
- The app is inspired by Matrix design ideas such as versioned events and extensible JSON envelopes, but it is not a Matrix client and does not federate with Matrix homeservers.

### Build note for v2.0.13

v2.0.13 removes use of libdatachannel's non-portable RTP helper classes and uses stable WebRTC DataChannels for the encrypted low-latency media payload tunnel. This fixes MSVC/vcpkg errors such as `RtpPacketizationConfig is not a member of rtc`, `H264RtpPacketizer is not a member of rtc`, and `RtcpNackResponder is not a member of rtc`.


### Recommended Windows run command

```powershell
.\scripts\run-windows.ps1 `
  -BuildDir build `
  -Config Release `
  -QtDir "C:/Qt/6.11.0/msvc2022_64"
```

### v2.0.13 configure fix

Fixed a CMake configure error on Windows caused by a backslash in the `LOCALCALL_POST_BUILD_DEPLOY_QT` option description. The project now uses forward slashes in CMake strings so CMake does not parse `\R` as an invalid escape sequence.


## Windows QSlider entry-point repair

If the app builds but shows `??1QSlider@@UEAA@XZ could not be located` at launch, the executable is loading stale Qt DLLs. Run:

```powershell
.\scripts\fix-windows-entrypoint.ps1 -QtDir "C:/Qt/6.11.0/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Rebuild
```

The script redeploys Qt from the selected kit and verifies the copied Qt DLL/plugin hashes before launching.

### v2.0.13 Windows entry-point hardening

This release removes `QSlider` from the call UI and replaces it with a quality dropdown. This prevents `QSlider`-specific Qt entry-point errors such as `??1QSlider...` or `?paintEvent@QSlider...` when an old Qt DLL is accidentally loaded. The Windows repair script now checks that the rebuilt EXE no longer contains `QSlider` references and validates both `build/Release` and `dist/LocalCall-Windows-x64`.
