# Build Guide

## Fast Linux Build

```bash
./scripts/build-linux.sh
./build/LocalCall
```

By default, the build tries to enable secure WebRTC calls automatically when the distro provides the needed packages.

## Fedora / Nobara

```bash
./scripts/install-deps-fedora.sh
./scripts/build-linux.sh
```

## Bazzite

Use distrobox/toolbox for the cleanest workflow:

```bash
distrobox create --name localcall-dev --image fedora:latest
distrobox enter localcall-dev
cd /path/to/LocalCall
./scripts/install-deps-fedora.sh
./scripts/build-linux.sh
```

## Arch-based Systems

```bash
./scripts/install-deps-arch.sh
./scripts/build-linux.sh
```

## NixOS

```bash
nix build
./result/bin/LocalCall
```

## Windows

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" -DLOCALCALL_WITH_WEBRTC=AUTO
cmake --build build --config Release
cd build\Release
windeployqt --release LocalCall.exe
LocalCall.exe
```

## Important CMake Flags

```text
LOCALCALL_WITH_MULTIMEDIA = ON | OFF
LOCALCALL_WITH_OPENCV     = AUTO | ON | OFF
LOCALCALL_WITH_WEBRTC     = AUTO | ON | OFF
```

Use this fallback when a distro does not provide libdatachannel/OpenH264/Opus/libyuv CMake packages:

```bash
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
cmake --build build --parallel
```

## Low-Latency Internet Calling

Configure STUN/TURN with:

```bash
export LOCALCALL_ICE_SERVERS="stun:stun.l.google.com:19302,turn:user:pass@turn.example.com:3478"
./build/LocalCall
```

Latency priority order:

1. Same LAN / direct host ICE candidate.
2. STUN-discovered peer-to-peer route.
3. TURN relay only when direct ICE fails.

TURN is more reliable across strict NATs, but it usually adds latency because media is relayed through a server.
