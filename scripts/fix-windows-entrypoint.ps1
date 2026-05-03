<#!
.SYNOPSIS
Fix Qt entry-point runtime errors such as QSlider destructor not found.

.DESCRIPTION
This is the one-command runtime repair script for Windows. It deletes stale Qt
DLLs/plugins from the build output, rebuilds the app if requested, deploys from
one explicit Qt kit, verifies hashes, then starts the app with the app folder
first in PATH.
#>
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$QtDir = "C:/Qt/6.11.0/msvc2022_64",
    [string]$VcpkgRoot = "C:/vcpkg",
    [switch]$Rebuild,
    [string]$OutputDir = "dist/LocalCall-Windows-x64"
)

$ErrorActionPreference = "Stop"

if ($Rebuild) {
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
    if (Test-Path $OutputDir) { Remove-Item -Recurse -Force $OutputDir }
    # Remove the common old output folder too; users often launch this by habit.
    $oldRelease = Join-Path $BuildDir "Release"
    if (Test-Path $oldRelease) { Remove-Item -Recurse -Force $oldRelease }
    $toolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    cmake -S . -B $BuildDir `
        -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
        -DCMAKE_PREFIX_PATH="$QtDir" `
        -DLOCALCALL_WITH_WEBRTC=ON `
        -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON
    cmake --build $BuildDir --config $Config
}

# Repair both locations:
# 1) build/Release, because users often launch it directly after cmake --build.
# 2) dist/LocalCall-Windows-x64, the recommended clean portable folder.
& "$PSScriptRoot\deploy-windows.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir -Clean
& "$PSScriptRoot\check-windows-runtime.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir

& "$PSScriptRoot\deploy-windows.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir -Clean -OutputDir $OutputDir
& "$PSScriptRoot\check-windows-runtime.ps1" -AppDir $OutputDir -QtDir $QtDir
& "$PSScriptRoot\run-windows.ps1" -AppDir $OutputDir -QtDir $QtDir
