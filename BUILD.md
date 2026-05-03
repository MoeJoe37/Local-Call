# Build Guide

## Fast Linux Build

```bash
./scripts/build-linux.sh
./build/LocalCall
```

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
cmake -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
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

Use this stable baseline when a distro does not provide libdatachannel/OpenH264 CMake packages:

```bash
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
cmake --build build --parallel
```
