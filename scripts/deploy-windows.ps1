<#
.SYNOPSIS
Deploy LocalCall on Windows with the exact Qt kit used by CMake.

.DESCRIPTION
Windows builds now contain two executables:
  LocalCall.exe     - native launcher with no Qt imports
  LocalCallApp.exe  - real Qt application

The launcher prevents Windows loader entry-point popups on LocalCall.exe. This
script deploys Qt for LocalCallApp.exe, writes the Qt kit prefix for the launcher,
and verifies that deployed Qt DLLs match the selected Qt kit.
#>
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$QtDir = "",
    [switch]$Clean,
    [string]$OutputDir = "",
    [string]$Triplet = ""
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Remove-IfExists([string]$Path) {
    if (Test-Path $Path) { Remove-Item -Recurse -Force $Path }
}

function Test-NoDebugRuntimeImports {
    param([Parameter(Mandatory=$true)][string]$Directory)

    if ($Config -match '^[Dd]ebug$') { return }

    $debugRuntimeNames = @(
        'VCRUNTIME140D.dll',
        'VCRUNTIME140_1D.dll',
        'MSVCP140D.dll',
        'ucrtbased.dll'
    )

    $bad = @()
    Get-ChildItem -Path $Directory -File -Include *.exe,*.dll -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
        $bytes = [System.IO.File]::ReadAllBytes($_.FullName)
        $text = [System.Text.Encoding]::ASCII.GetString($bytes)
        foreach ($runtime in $debugRuntimeNames) {
            if ($text.IndexOf($runtime, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $bad += "$($_.FullName) imports $runtime"
            }
        }
    }

    if ($bad.Count -gt 0) {
        throw @"
Release deployment contains Debug MSVC runtime imports. Do not distribute this folder.
This usually means Debug vcpkg DLLs were copied into a Release package.

$($bad -join "`n")

Fix: delete build and dist, then run: build.bat clean release
"@
    }
}

$buildPath = Resolve-FullPath $BuildDir
$buildOutDir = Join-Path $buildPath $Config
if (-not (Test-Path $buildOutDir)) { $buildOutDir = $buildPath }

$builtLauncher = Join-Path $buildOutDir "LocalCall.exe"
$builtApp = Join-Path $buildOutDir "LocalCallApp.exe"

# Backward compatibility with older builds where the Qt app was LocalCall.exe.
if (-not (Test-Path $builtApp) -and (Test-Path $builtLauncher)) {
    $builtApp = $builtLauncher
}
if (-not (Test-Path $builtApp)) {
    throw "LocalCallApp.exe was not found. Build first: cmake --build $BuildDir --config $Config"
}

if ([string]::IsNullOrWhiteSpace($QtDir)) {
    $cachePath = Join-Path $buildPath "CMakeCache.txt"
    if (Test-Path $cachePath) {
        $qtLine = Select-String -Path $cachePath -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
        if ($qtLine) {
            $qtCmakeDir = $qtLine.Matches[0].Groups[1].Value
            $QtDir = Split-Path (Split-Path (Split-Path $qtCmakeDir -Parent) -Parent) -Parent
        }
    }
}
if ([string]::IsNullOrWhiteSpace($QtDir)) {
    throw "QtDir was not provided and could not be detected from CMakeCache.txt. Pass -QtDir C:\Qt\6.11.0\msvc2022_64"
}

$QtDir = Resolve-FullPath $QtDir
$qtBin = Join-Path $QtDir "bin"
$windeployqt = Join-Path $qtBin "windeployqt.exe"
$qmake = Join-Path $qtBin "qmake.exe"
if (-not (Test-Path $windeployqt)) { throw "windeployqt.exe not found: $windeployqt" }
if (-not (Test-Path $qmake)) { throw "qmake.exe not found: $qmake" }

if (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
    $outDir = Resolve-FullPath $OutputDir
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $appPath = Join-Path $outDir "LocalCallApp.exe"
    Copy-Item -Force $builtApp $appPath
    if (Test-Path $builtLauncher) {
        Copy-Item -Force $builtLauncher (Join-Path $outDir "LocalCall.exe")
    } else {
        Copy-Item -Force $builtApp (Join-Path $outDir "LocalCall.exe")
    }
    foreach ($srcExe in @($builtApp, $builtLauncher)) {
        if (Test-Path $srcExe) {
            $pdbPath = [System.IO.Path]::ChangeExtension($srcExe, ".pdb")
            if (Test-Path $pdbPath) { Copy-Item -Force $pdbPath (Join-Path $outDir ([System.IO.Path]::GetFileName($pdbPath))) }
        }
    }
    $iconPath = Join-Path (Get-Location) "icon.ico"
    if (Test-Path $iconPath) { Copy-Item -Force $iconPath (Join-Path $outDir "icon.ico") }
} else {
    $outDir = Split-Path $builtApp -Parent
    $appPath = $builtApp
}

Write-Host "LocalCall launcher : $(Join-Path $outDir 'LocalCall.exe')"
Write-Host "LocalCall Qt app   : $appPath"
Write-Host "Qt prefix          : $QtDir"
Write-Host "Qt deploy tool     : $windeployqt"
Write-Host "Qt version         : $(& $qmake -query QT_VERSION)"
Write-Host "Qt install prefix  : $(& $qmake -query QT_INSTALL_PREFIX)"

if ($Clean) {
    Write-Host "Cleaning stale runtime files from: $outDir"
    # This folder is a deployment folder, so it must not keep DLLs from a
    # previous Debug or different Qt/vcpkg build. Re-deploy everything cleanly.
    Get-ChildItem -Path $outDir -Filter "*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $outDir -Filter "*.pdb" -ErrorAction SilentlyContinue | Remove-Item -Force
    Remove-IfExists (Join-Path $outDir "qt.conf")
    foreach ($dir in @(
        "platforms", "styles", "imageformats", "iconengines", "generic",
        "networkinformation", "multimedia", "mediaservice", "audio", "tls",
        "sqldrivers", "printsupport", "translations", "accessible", "bearer",
        "qml", "resources"
    )) { Remove-IfExists (Join-Path $outDir $dir) }
}

# Write the selected Qt prefix before launch. LocalCall.exe can use this to repair
# stale DLLs before it starts the Qt child process. This is the key protection
# against recurring Windows entry-point popups when users launch build/Release.
Set-Content -Path (Join-Path $outDir "localcall-qt-prefix.txt") -Value $QtDir -Encoding UTF8

# MinGW Qt kits link against Qt's own GCC runtime. windeployqt --compiler-runtime
# only finds those DLLs when the toolchain is in PATH, so locate it here and copy
# the runtime explicitly afterwards.
$mingwBin = ""
if ($QtDir -match "mingw|gcc_64") {
    $qtRoot = Split-Path -Parent (Split-Path -Parent $QtDir)
    $toolsRoot = Join-Path $qtRoot "Tools"
    if (Test-Path $toolsRoot) {
        $mingwBin = @(Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter "mingw*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin" } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ "libstdc++-6.dll") }) |
            Select-Object -First 1
    }
    if ([string]::IsNullOrWhiteSpace($mingwBin)) {
        throw "The MinGW runtime for $QtDir was not found under $toolsRoot. Install 'MinGW 64-bit' with the Qt Maintenance Tool."
    }
}

$oldPath = $env:PATH
try {
    $env:PATH = if ($mingwBin) { "$qtBin;$mingwBin;$oldPath" } else { "$qtBin;$oldPath" }
    $mode = if ($Config -match '^[Dd]ebug$') { "--debug" } else { "--release" }
    & $windeployqt $mode --compiler-runtime --no-translations $appPath
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
}
finally { $env:PATH = $oldPath }

if ($mingwBin) {
    Write-Host "Copying MinGW runtime DLLs from: $mingwBin"
    foreach ($runtimeDll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
        $src = Join-Path $mingwBin $runtimeDll
        if (Test-Path $src) { Copy-Item -Force $src (Join-Path $outDir $runtimeDll) }
    }
    foreach ($required in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
        if (-not (Test-Path (Join-Path $outDir $required))) {
            throw "Required MinGW runtime DLL was not deployed: $required"
        }
    }
}

# Copy vcpkg runtime DLLs that match the selected build configuration.
#
# IMPORTANT: never copy x64-windows/debug/bin into a Release deployment.
# Debug vcpkg DLLs import VCRUNTIME140D.dll, VCRUNTIME140_1D.dll,
# MSVCP140D.dll and ucrtbased.dll. Those files exist on the developer PC
# because Visual Studio is installed, but they are not redistributable and will
# fail on a clean Windows 11 PC.
#
# The triplet depends on the Qt kit (x64-windows for MSVC, x64-mingw-dynamic for
# MinGW), so take it from the build tree when the caller did not pass one.
$vcpkgInstalled = Join-Path $buildPath "vcpkg_installed"
if ([string]::IsNullOrWhiteSpace($Triplet)) {
    $cachePath = Join-Path $buildPath "CMakeCache.txt"
    if (Test-Path $cachePath) {
        $tripletLine = Select-String -Path $cachePath -Pattern '^VCPKG_TARGET_TRIPLET:[^=]*=(.+)$' | Select-Object -First 1
        if ($tripletLine) { $Triplet = $tripletLine.Matches[0].Groups[1].Value }
    }
}
if ([string]::IsNullOrWhiteSpace($Triplet) -and (Test-Path $vcpkgInstalled)) {
    $Triplet = @(Get-ChildItem -LiteralPath $vcpkgInstalled -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne "vcpkg" } |
        ForEach-Object { $_.Name }) | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($Triplet)) { $Triplet = "x64-windows" }

$vcpkgRuntimeBin = if ($Config -match '^[Dd]ebug$') {
    Join-Path $vcpkgInstalled "$Triplet/debug/bin"
} else {
    Join-Path $vcpkgInstalled "$Triplet/bin"
}
if (Test-Path $vcpkgRuntimeBin) {
    Write-Host "Copying vcpkg runtime DLLs from: $vcpkgRuntimeBin"
    Get-ChildItem -Path $vcpkgRuntimeBin -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item -Force $_.FullName (Join-Path $outDir $_.Name)
    }
}

# Force-copy direct Qt modules from the selected kit. Do not rely on whatever
# windeployqt found in PATH.
foreach ($requiredQtDll in @(
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Concurrent.dll",
    "Qt6Multimedia.dll", "Qt6MultimediaWidgets.dll", "Qt6OpenGL.dll", "Qt6Svg.dll"
)) {
    $kitDll = Join-Path $qtBin $requiredQtDll
    if (Test-Path $kitDll) { Copy-Item -Force $kitDll (Join-Path $outDir $requiredQtDll) }
}

$pluginCopies = @(
    @("platforms/qwindows.dll", "platforms/qwindows.dll"),
    @("styles/qwindowsvistastyle.dll", "styles/qwindowsvistastyle.dll"),
    @("imageformats/qico.dll", "imageformats/qico.dll"),
    @("imageformats/qjpeg.dll", "imageformats/qjpeg.dll"),
    @("iconengines/qsvgicon.dll", "iconengines/qsvgicon.dll")
)
foreach ($pair in $pluginCopies) {
    $kitPlugin = Join-Path (Join-Path $QtDir "plugins") $pair[0]
    $outPlugin = Join-Path $outDir $pair[1]
    if (Test-Path $kitPlugin) {
        New-Item -ItemType Directory -Force -Path (Split-Path $outPlugin -Parent) | Out-Null
        Copy-Item -Force $kitPlugin $outPlugin
    }
}

# Verify every deployed Qt DLL against the selected kit.
foreach ($dll in (Get-ChildItem -Path $outDir -Filter "Qt6*.dll" -ErrorAction SilentlyContinue)) {
    $kitDll = Join-Path $qtBin $dll.Name
    if (-not (Test-Path $kitDll)) { throw "Deployed $($dll.Name), but it does not exist in the selected Qt kit: $qtBin" }
    if ((Get-FileHash -Algorithm SHA256 $dll.FullName).Hash -ne (Get-FileHash -Algorithm SHA256 $kitDll).Hash) {
        Write-Host "Replacing mismatched Qt DLL: $($dll.Name)"
        Copy-Item -Force $kitDll $dll.FullName
    }
    if ((Get-FileHash -Algorithm SHA256 $dll.FullName).Hash -ne (Get-FileHash -Algorithm SHA256 $kitDll).Hash) {
        throw "Failed to replace mismatched Qt DLL: $($dll.Name)"
    }
}

foreach ($required in @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Concurrent.dll")) {
    if (-not (Test-Path (Join-Path $outDir $required))) { throw "Required Qt DLL was not deployed: $required" }
}
if (-not (Test-Path (Join-Path $outDir "platforms/qwindows.dll"))) { throw "qwindows.dll was not deployed beside the application." }

@"
[Paths]
Prefix=.
Plugins=.
Imports=.
Qml2Imports=.
"@ | Set-Content -Path (Join-Path $outDir "qt.conf") -Encoding ASCII

Test-NoDebugRuntimeImports -Directory $outDir

Write-Host "Deployment finished and Qt DLLs were verified against the selected Qt kit."
Write-Host "Run the launcher, not LocalCallApp.exe:"
Write-Host "  $(Join-Path $outDir 'LocalCall.exe')"
