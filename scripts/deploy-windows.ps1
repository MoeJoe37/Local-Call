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
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Remove-IfExists([string]$Path) {
    if (Test-Path $Path) { Remove-Item -Recurse -Force $Path }
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
    Get-ChildItem -Path $outDir -Filter "Qt6*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $outDir -Filter "Qt6*.pdb" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $outDir -Filter "*Qt6*.dll" -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path $outDir -Filter "q*.dll" -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "^(qwindows|qgif|qico|qjpeg|qsvg|qwebp|qmodern|qwindowsvistastyle)" } | Remove-Item -Force
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

$oldPath = $env:PATH
try {
    $env:PATH = "$qtBin;$oldPath"
    $mode = if ($Config -match '^[Dd]ebug$') { "--debug" } else { "--release" }
    & $windeployqt $mode --compiler-runtime --no-translations $appPath
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }
}
finally { $env:PATH = $oldPath }

# Copy vcpkg runtime DLLs to build/Release or dist.
foreach ($vcpkgBin in @(
    (Join-Path $buildPath "vcpkg_installed/x64-windows/bin"),
    (Join-Path $buildPath "vcpkg_installed/x64-windows/debug/bin")
)) {
    if (Test-Path $vcpkgBin) {
        Get-ChildItem -Path $vcpkgBin -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -Force $_.FullName (Join-Path $outDir $_.Name)
        }
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

Write-Host "Deployment finished and Qt DLLs were verified against the selected Qt kit."
Write-Host "Run the launcher, not LocalCallApp.exe:"
Write-Host "  $(Join-Path $outDir 'LocalCall.exe')"
