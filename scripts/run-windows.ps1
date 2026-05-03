<#
.SYNOPSIS
Run LocalCall through the native launcher.
#>
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$QtDir = "C:/Qt/6.11.0/msvc2022_64",
    [switch]$Redeploy,
    [string]$AppDir = ""
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

if (-not [string]::IsNullOrWhiteSpace($AppDir)) {
    $outDir = Resolve-FullPath $AppDir
} else {
    $buildPath = Resolve-FullPath $BuildDir
    $outDir = Join-Path $buildPath $Config
    if (-not (Test-Path $outDir)) { $outDir = $buildPath }
}
$exePath = Join-Path $outDir "LocalCall.exe"
if (-not (Test-Path $exePath)) { throw "LocalCall.exe launcher not found. Build/deploy first." }

if ($Redeploy) {
    if (-not [string]::IsNullOrWhiteSpace($AppDir)) {
        & "$PSScriptRoot\deploy-windows.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir -Clean -OutputDir $AppDir
    } else {
        & "$PSScriptRoot\deploy-windows.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir -Clean
    }
}

if (-not [string]::IsNullOrWhiteSpace($AppDir)) {
    & "$PSScriptRoot\check-windows-runtime.ps1" -AppDir $AppDir -QtDir $QtDir
} else {
    & "$PSScriptRoot\check-windows-runtime.ps1" -BuildDir $BuildDir -Config $Config -QtDir $QtDir
}

$oldPath = $env:PATH
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldQpaPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:PATH = "$outDir;$oldPath"
    $env:QT_PLUGIN_PATH = $outDir
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $outDir "platforms"
    Push-Location $outDir
    & $exePath
}
finally {
    Pop-Location
    $env:PATH = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldQpaPluginPath
}
