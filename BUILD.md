# Build Guide

## Fast Linux Build

```bash
./scripts/build-linux.sh
./build/LocalCall
```

By default, the build now requires the secure WebRTC/RTC dependency set. This prevents accidentally producing a legacy LAN-only build when one dependency is missing. Pass `-DLOCALCALL_WITH_WEBRTC=OFF` only when you intentionally want the compatibility fallback.

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

## Deploy / Package

Windows build output is deployed automatically after `cmake --build` when `LOCALCALL_POST_BUILD_DEPLOY_QT=ON`. For an install/package tree on any supported desktop OS, use:

```bash
cmake --install build --prefix ./dist/LocalCall
cpack --config build/CPackConfig.cmake
```

Linux helper:

```bash
./scripts/deploy-linux.sh
```

macOS helper:

```bash
./scripts/deploy-macos.sh
```

## Windows

Use the project script so the deployed Qt DLLs always come from the same Qt kit used by CMake:

```powershell
.\scripts\build-windows.ps1 -QtDir "C:/Qt/6.11.0/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Clean
.\build\Release\LocalCall.exe
```

Manual equivalent:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" `
  -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON `
  -DLOCALCALL_INSTALL_QT_RUNTIME=ON `
  -DLOCALCALL_WITH_WEBRTC=ON
cmake --build build --config Release
.\scripts\deploy-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64" -Clean
.\build\Release\LocalCall.exe
```


## Windows QSlider Entry-Point Error Repair

If Windows shows:

```text
The procedure entry point ??1QSlider@@UEAA@XZ could not be located...
```

it means `LocalCall.exe` is loading Qt DLLs from a different Qt kit than the one used during compilation. Run the repair command below from the project root. It deletes stale Qt DLLs/plugins, deploys from the exact Qt kit, verifies file hashes, and starts the app with the app folder first in `PATH`:

```powershell
.\scripts\fix-windows-entrypoint.ps1 `
  -QtDir "C:/Qt/6.11.0/msvc2022_64" `
  -VcpkgRoot "C:/vcpkg" `
  -Rebuild
```

For normal builds, `LOCALCALL_POST_BUILD_DEPLOY_QT` is now forced ON on Windows unless you explicitly pass `-DLOCALCALL_DISABLE_POST_BUILD_DEPLOY_QT=ON`. This prevents old CMake caches from keeping deployment disabled.

## Important CMake Flags

```text
LOCALCALL_WITH_MULTIMEDIA = ON | OFF
LOCALCALL_WITH_OPENCV     = AUTO | ON | OFF
LOCALCALL_WITH_WEBRTC     = AUTO | ON | OFF
LOCALCALL_INSTALL_QT_RUNTIME = ON | OFF
LOCALCALL_POST_BUILD_DEPLOY_QT = ON | OFF
```

Use this fallback when a distro does not provide libdatachannel/OpenH264/Opus/libyuv CMake packages:

```bash
cmake -S . -B build -G Ninja -DLOCALCALL_WITH_WEBRTC=OFF
cmake --build build --parallel
```



## Windows error: Target executable does not exist during deploy

If MSBuild prints an error like this during the post-build deploy step:

```text
Target executable does not exist: ".../build/Release/LocalCall.exe"
```

use this fixed version of the project. The deploy script now strips generator-added literal quotes before checking paths. You can also bypass the post-build deploy and deploy manually:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" `
  -DLOCALCALL_WITH_WEBRTC=ON `
  -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON
cmake --build build --config Release
.\scripts\deploy-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64" -Clean
```

The default secure RTC build no longer requires the separate Qt WebSockets add-on. If CMake reports a missing RTC dependency, delete `build/`, keep using the same vcpkg root, and configure again. Do not mix Qt DLLs from another Qt version or compiler kit.

## Windows + vcpkg / Qt Runtime Notes

This project uses vcpkg manifest mode through `vcpkg.json`. The Opus dependency is `opus`, not `libopus`, and OpenSSL is included in the manifest for Windows builds.

If CMake previously failed during vcpkg manifest install or if the executable shows an entry-point error, delete the old build directory and redeploy from the matching Qt kit:

```powershell
Remove-Item -Recurse -Force build
.\scripts\build-windows.ps1 -QtDir "C:/Qt/6.11.0/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Clean
```

Do not deploy with a `windeployqt.exe` found randomly through `PATH`. Qt's Windows deployment tool copies Qt libraries/plugins into the app folder, so using a different Qt version can make the executable load incompatible DLLs.

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


## Windows Qt runtime verification

Version 2.0.13 includes a runtime mismatch guard. After building, run:

```powershell
.\scripts\deploy-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64" -Clean
.\scripts\check-windows-runtime.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64"
.\scripts\run-windows.ps1 -BuildDir build -Config Release -QtDir "C:/Qt/6.11.0/msvc2022_64"
```

This prevents Qt entry-point errors caused by stale or mismatched `Qt6*.dll` files.

## v2.0.13 CMake Windows path-string fix

This release fixes a CMake configure failure caused by a Windows-style path in an option description. CMake treats backslashes inside quoted strings as escape characters, so `build\Release` in CMake code can fail as an invalid `\R` escape. The project now uses `build/Release` in CMake strings, which is valid on Windows and Unix-like systems.


## v2.0.13 Windows launcher runtime fix

On Windows, run `build/Release/LocalCall.exe` or `dist/LocalCall-Windows-x64/LocalCall.exe`. This executable is a native launcher. The Qt application is `LocalCallApp.exe` and should not be launched directly. The launcher prevents Qt entry-point popups by refreshing stale DLLs from the Qt kit recorded during deployment and by starting the Qt process only after the runtime folder passes sanity checks.
