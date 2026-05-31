# Runtime Deployment and Qt DLL Compatibility

## Problem

A Windows error like this:

```text
LocalCall.exe - Entry Point Not Found
The procedure entry point ?size@QWidget@@QEBA?AVQSize@@XZ could not be located in the dynamic link library LocalCall.exe
```

usually means the executable was built against one Qt version or compiler kit, but Windows loaded a different `Qt6Widgets.dll` at runtime.

Common causes:

1. Running `windeployqt` from another Qt installation found in `PATH`.
2. Leaving stale `Qt6*.dll` files in `build/Release` from an older deployment.
3. Mixing MSVC and MinGW Qt kits.
4. Mixing Debug and Release Qt DLLs.
5. Running the executable from a folder that contains incompatible Qt DLLs.

## Fix in this project

The CMake build now has two deployment protections:

1. `LOCALCALL_POST_BUILD_DEPLOY_QT=ON` on Windows runs the `windeployqt.exe` from the exact Qt kit found by CMake.
2. `LOCALCALL_INSTALL_QT_RUNTIME=ON` uses Qt's CMake deployment API during `cmake --install` on Windows, Linux, and macOS when supported by the installed Qt version.

## Recommended Windows command

```powershell
.\scripts\build-windows.ps1 -QtDir "C:/Qt/6.11.0/msvc2022_64" -VcpkgRoot "C:/vcpkg" -Clean
.\build\Release\LocalCall.exe
```

The script cleans stale Qt DLLs/plugins and redeploys from the matching Qt kit.

## Linux

```bash
./scripts/build-linux.sh
./scripts/deploy-linux.sh
./dist/LocalCall/bin/LocalCall
```

The installed executable uses relative RPATH so bundled libraries can be found beside the app when Qt's deployment script copies them.

## macOS

```bash
./scripts/deploy-macos.sh
```

The Qt deployment script uses macOS bundle deployment when available.

## v2.0.13 Windows entry-point fix

If Windows shows an error such as:

```text
LocalCall.exe - Entry Point Not Found
The procedure entry point ??1QSlider@@UEAA@XZ could not be located...
```

then the executable is still loading Qt DLLs that do not match the Qt kit used at compile time. Version 2.0.13 hardens this in three ways:

1. Windows post-build deployment is enabled by default for normal CMake builds.
2. `scripts/deploy-windows.ps1 -Clean` deletes stale Qt DLLs/plugins, runs the matching `windeployqt.exe`, writes `qt.conf`, and verifies deployed `Qt6*.dll` files by SHA-256 hash against the selected Qt kit.
3. `scripts/run-windows.ps1` starts the app with the deployed executable folder and selected Qt kit first in `PATH`.

Recommended clean command:

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

cmake -S . -B build `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64" `
  -DLOCALCALL_WITH_WEBRTC=ON

cmake --build build --config Release

.\scripts\deploy-windows.ps1 `
  -BuildDir build `
  -Config Release `
  -QtDir "C:/Qt/6.11.0/msvc2022_64" `
  -Clean

.\scripts\run-windows.ps1 `
  -BuildDir build `
  -Config Release `
  -QtDir "C:/Qt/6.11.0/msvc2022_64"
```

Do not launch the EXE from an old build folder that was previously deployed with another Qt kit. Delete `build` first.

## v2.0.13 hardening

On Windows/MSVC, Local Call links Qt normally because Qt imports data symbols and MSVC cannot delay-load `Qt6Widgets.dll` in that case. Runtime consistency is handled by deploying the exact Qt DLLs beside the executable, verifying their hashes against the selected Qt kit, setting Qt plugin paths before `QApplication`, and validating the `QSlider` MSVC ABI export in `Qt6Widgets.dll`. The recommended repair path deploys to `dist/LocalCall-Windows-x64`, which avoids launching stale binaries from old `build/Release` folders.

## v2.0.13 QSlider entry-point hardening

The call quality control no longer uses `QSlider`. It now uses a `QComboBox` quality selector. This removes direct `QSlider`-specific imports from `LocalCall.exe`, so old/stale `Qt6Widgets.dll` files cannot trigger `QSlider` destructor or `QSlider::paintEvent` entry-point popups from this app binary. The Windows runtime check also scans the EXE for leftover `QSlider` references and fails early if an old binary is being launched.

The repair script now deploys and validates both `build/Release` and `dist/LocalCall-Windows-x64`, because users often run `build/Release/LocalCall.exe` directly after building.

## Missing VCRUNTIME140D.dll / MSVCP140D.dll / ucrtbased.dll on another PC

These errors mean a Debug build or Debug dependency was installed. Debug MSVC runtime DLLs are not part of the normal Microsoft Visual C++ Redistributable and are not suitable for end-user installers.

Fixed workflow:

```bat
build.bat clean release
```

Then package only:

```text
dist\LocalCall
```

For Inno Setup, use:

```text
packaging\windows\LocalCall.iss
```

The Release deployment now copies only Release vcpkg runtime DLLs and scans the final folder for Debug runtime imports before it is considered installer-ready.
