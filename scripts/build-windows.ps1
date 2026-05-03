param(
    [string]$QtDir = "C:/Qt/6.11.0/msvc2022_64",
    [string]$VcpkgRoot = "C:/vcpkg",
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$Clean,
    [string]$WebRTC = "ON",
    [string]$OpenCV = "AUTO",
    [string]$Multimedia = "ON"
)

$ErrorActionPreference = "Stop"

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

$toolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }
if (-not (Test-Path (Join-Path $QtDir "lib/cmake/Qt6/Qt6Config.cmake"))) { throw "Qt6Config.cmake not found under: $QtDir" }

cmake -S . -B $BuildDir `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DCMAKE_PREFIX_PATH="$QtDir" `
    -DLOCALCALL_WITH_WEBRTC="$WebRTC" `
    -DLOCALCALL_WITH_OPENCV="$OpenCV" `
    -DLOCALCALL_WITH_MULTIMEDIA="$Multimedia" `
    -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON `
    -DLOCALCALL_INSTALL_QT_RUNTIME=ON

cmake --build $BuildDir --config $Config

./scripts/deploy-windows.ps1 -BuildDir $BuildDir -Config $Config -QtDir $QtDir -Clean
./scripts/check-windows-runtime.ps1 -BuildDir $BuildDir -Config $Config -QtDir $QtDir
