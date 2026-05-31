<#
.SYNOPSIS
Check that LocalCall's Windows runtime folder is self-consistent.
#>
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$QtDir = "C:/Qt/6.11.0/msvc2022_64",
    [string]$AppDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Test-NoDebugRuntimeImports {
    param([Parameter(Mandatory=$true)][string]$Directory)

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
This runtime folder contains Debug MSVC runtime imports and must not be used in an installer.

$($bad -join "`n")

Build a clean Release distribution instead:
  build.bat clean release

Then package this folder only:
  dist\LocalCall
"@
    }
    Write-Host "OK: no Debug MSVC runtime imports were found"
}

$QtDir = Resolve-FullPath $QtDir
$qtBin = Join-Path $QtDir "bin"
if (-not (Test-Path $qtBin)) { throw "Qt bin folder not found: $qtBin" }

if (-not [string]::IsNullOrWhiteSpace($AppDir)) {
    $outDir = Resolve-FullPath $AppDir
} else {
    $buildPath = Resolve-FullPath $BuildDir
    $outDir = Join-Path $buildPath $Config
    if (-not (Test-Path $outDir)) { $outDir = $buildPath }
}

$launcher = Join-Path $outDir "LocalCall.exe"
$app = Join-Path $outDir "LocalCallApp.exe"
if (-not (Test-Path $launcher)) { throw "LocalCall.exe launcher not found: $launcher" }
if (-not (Test-Path $app)) { throw "LocalCallApp.exe Qt app not found: $app" }

Write-Host "Checking runtime folder: $outDir"
Write-Host "Expected Qt kit       : $QtDir"

# Launcher must not import Qt. If it does, Windows can show entry-point popups
# before our repair/verification code gets control.
$launcherText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($launcher))
if ($launcherText.Contains("Qt6Widgets") -or $launcherText.Contains("Qt6Core")) {
    throw "LocalCall.exe still imports Qt. Delete build/dist and rebuild v2.0.14. The launcher must be native-only."
}
Write-Host "OK: LocalCall.exe is native launcher and has no Qt imports"

$required = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Network.dll", "Qt6Concurrent.dll")
foreach ($name in $required) {
    $local = Join-Path $outDir $name
    $kit = Join-Path $qtBin $name
    if (-not (Test-Path $local)) { throw "Missing deployed Qt DLL: $local" }
    if (-not (Test-Path $kit)) { throw "Missing Qt kit DLL: $kit" }
    $localHash = (Get-FileHash -Algorithm SHA256 $local).Hash
    $kitHash = (Get-FileHash -Algorithm SHA256 $kit).Hash
    if ($localHash -ne $kitHash) {
        throw "$name does not match the selected Qt kit. Run scripts\deploy-windows.ps1 -Clean."
    }
    Write-Host "OK: $name matches selected Qt kit"
}

$qwindowsLocal = Join-Path $outDir "platforms/qwindows.dll"
$qwindowsKit = Join-Path $QtDir "plugins/platforms/qwindows.dll"
if (-not (Test-Path $qwindowsLocal)) { throw "Missing platform plugin: $qwindowsLocal" }
if ((Get-FileHash -Algorithm SHA256 $qwindowsLocal).Hash -ne (Get-FileHash -Algorithm SHA256 $qwindowsKit).Hash) {
    throw "qwindows.dll does not match the selected Qt kit."
}
Write-Host "OK: qwindows.dll matches selected Qt kit"

$prefixFile = Join-Path $outDir "localcall-qt-prefix.txt"
if (-not (Test-Path $prefixFile)) { throw "Missing localcall-qt-prefix.txt. Redeploy with scripts\deploy-windows.ps1 -Clean." }
Write-Host "OK: localcall-qt-prefix.txt exists"

Test-NoDebugRuntimeImports -Directory $outDir

Write-Host "Runtime check passed. Launch LocalCall.exe, not LocalCallApp.exe."
