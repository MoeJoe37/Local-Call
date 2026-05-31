param(
    [string]$QtDir = "",
    [string]$VcpkgRoot = "",
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$Clean,
    [string]$WebRTC = "ON",
    [string]$OpenCV = "AUTO",
    [string]$Multimedia = "ON",
    [string]$DistDir = "dist/LocalCall",
    [switch]$NoDist
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function ConvertFrom-Qt6ConfigPath {
    param([Parameter(Mandatory=$true)][string]$ConfigFile)
    $qt6Dir = Split-Path -Parent $ConfigFile
    $cmakeDir = Split-Path -Parent $qt6Dir
    $libDir = Split-Path -Parent $cmakeDir
    return (Split-Path -Parent $libDir)
}

function Resolve-QtPrefix {
    param([string]$Candidate)
    if ([string]::IsNullOrWhiteSpace($Candidate)) { return $null }
    $candidatePath = $Candidate.Trim('"')

    if (Test-Path -LiteralPath (Join-Path $candidatePath "lib/cmake/Qt6/Qt6Config.cmake")) {
        return (Resolve-Path -LiteralPath $candidatePath).Path
    }

    if (Test-Path -LiteralPath (Join-Path $candidatePath "Qt6Config.cmake")) {
        return (Resolve-Path -LiteralPath (ConvertFrom-Qt6ConfigPath (Join-Path $candidatePath "Qt6Config.cmake"))).Path
    }

    return $null
}

function Get-QtKitScore {
    param([Parameter(Mandatory=$true)][string]$Path)
    $score = 0
    if ($Path -match "msvc2022_64") { $score += 100000000 }
    elseif ($Path -match "msvc2019_64") { $score += 90000000 }
    elseif ($Path -match "mingw|gcc_64") { $score -= 10000000 }

    if ($Path -match "[\\/](?<major>\d+)\.(?<minor>\d+)\.(?<patch>\d+)[\\/]") {
        $score += ([int]$Matches.major * 1000000) + ([int]$Matches.minor * 10000) + ([int]$Matches.patch * 100)
    } elseif ($Path -match "[\\/](?<major>\d+)\.(?<minor>\d+)[\\/]") {
        $score += ([int]$Matches.major * 1000000) + ([int]$Matches.minor * 10000)
    }

    return $score
}

function Find-QtDir {
    param([string]$RequestedQtDir)

    $explicitQtDir = Resolve-QtPrefix $RequestedQtDir
    if ($explicitQtDir) { return $explicitQtDir }

    $candidates = New-Object System.Collections.Generic.List[string]

    foreach ($candidate in @(
        $env:LOCALCALL_QT_DIR,
        $env:QTDIR,
        $env:QT_DIR,
        $env:Qt6_ROOT,
        $env:Qt6_DIR
    )) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) { $candidates.Add($candidate) }
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
        foreach ($part in $env:CMAKE_PREFIX_PATH -split ';') {
            if (-not [string]::IsNullOrWhiteSpace($part)) { $candidates.Add($part) }
        }
    }

    foreach ($candidate in @(
        "C:/Qt/6.11.0/msvc2022_64",
        "C:/Qt/6.11.0/msvc2019_64",
        "C:/Qt/6.10.0/msvc2022_64",
        "C:/Qt/6.10.0/msvc2019_64",
        "C:/Qt/6.9.0/msvc2022_64",
        "C:/Qt/6.9.0/msvc2019_64",
        "C:/Qt/6.8.0/msvc2022_64",
        "C:/Qt/6.8.0/msvc2019_64",
        "C:/Qt/6.7.0/msvc2022_64",
        "C:/Qt/6.7.0/msvc2019_64",
        "C:/Qt/6.6.2/msvc2022_64",
        "C:/Qt/6.6.2/msvc2019_64"
    )) { $candidates.Add($candidate) }

    $roots = @("C:/Qt")
    if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE)) {
        $roots += (Join-Path $env:USERPROFILE "Qt")
    }

    foreach ($root in $roots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Recurse -Filter Qt6Config.cmake -ErrorAction SilentlyContinue |
                ForEach-Object {
                    $prefix = ConvertFrom-Qt6ConfigPath $_.FullName
                    if (-not [string]::IsNullOrWhiteSpace($prefix)) { $candidates.Add($prefix) }
                }
        }
    }

    $valid = @()
    foreach ($candidate in $candidates) {
        $resolved = Resolve-QtPrefix $candidate
        if ($resolved -and ($valid -notcontains $resolved)) { $valid += $resolved }
    }

    if ($valid.Count -eq 0) { return $null }

    return ($valid | Sort-Object @{ Expression = { Get-QtKitScore $_ }; Descending = $true } | Select-Object -First 1)
}

function Find-VcpkgRoot {
    param([string]$RequestedVcpkgRoot)

    foreach ($candidate in @($RequestedVcpkgRoot, $env:VCPKG_ROOT, "C:/vcpkg")) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        $toolchain = Join-Path $candidate.Trim('"') "scripts/buildsystems/vcpkg.cmake"
        if (Test-Path -LiteralPath $toolchain) {
            return (Resolve-Path -LiteralPath $candidate.Trim('"')).Path
        }
    }

    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($vcpkgCmd) {
        $root = Split-Path -Parent $vcpkgCmd.Source
        $toolchain = Join-Path $root "scripts/buildsystems/vcpkg.cmake"
        if (Test-Path -LiteralPath $toolchain) { return $root }
    }

    return $null
}

function Enter-MSVCEnvironment {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "Visual Studio Build Tools were not found. Install 'Desktop development with C++' for Visual Studio 2022."
    }

    $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ([string]::IsNullOrWhiteSpace($vsInstall)) {
        throw "MSVC C++ tools were not found. Install 'Desktop development with C++' for Visual Studio 2022."
    }

    $vcvars = Join-Path $vsInstall "VC/Auxiliary/Build/vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found: $vcvars" }

    Write-Host "Loading MSVC environment from: $vcvars"
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$resolvedQtDir = Find-QtDir $QtDir
if (-not $resolvedQtDir) {
    throw @"
Qt6Config.cmake was not found.
Install Qt 6 for MSVC 2022 64-bit from the Qt Maintenance Tool, then run build.bat again.
Expected example path: C:\Qt\6.x.x\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake
You can also run: .\build.bat -QtDir C:\Qt\6.x.x\msvc2022_64
"@
}

if ($resolvedQtDir -match "mingw|gcc_64") {
    throw "The detected Qt kit is not compatible with the Visual Studio generator: $resolvedQtDir. Install/use a Qt MSVC 2022 64-bit kit."
}

$resolvedVcpkgRoot = Find-VcpkgRoot $VcpkgRoot
if (-not $resolvedVcpkgRoot) {
    throw "vcpkg was not found. Install vcpkg to C:\vcpkg or set VCPKG_ROOT to its folder."
}

$toolchain = Join-Path $resolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchain)) { throw "vcpkg toolchain not found: $toolchain" }

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

Enter-MSVCEnvironment

Write-Host "Using Qt:    $resolvedQtDir"
Write-Host "Using vcpkg: $resolvedVcpkgRoot"
Write-Host "Config:      $Config"

cmake -S . -B $BuildDir `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
    -DCMAKE_PREFIX_PATH="$resolvedQtDir" `
    -DLOCALCALL_WITH_WEBRTC="$WebRTC" `
    -DLOCALCALL_WITH_OPENCV="$OpenCV" `
    -DLOCALCALL_WITH_MULTIMEDIA="$Multimedia" `
    -DLOCALCALL_POST_BUILD_DEPLOY_QT=ON `
    -DLOCALCALL_INSTALL_QT_RUNTIME=ON

cmake --build $BuildDir --config $Config --parallel

./scripts/deploy-windows.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir -Clean
./scripts/check-windows-runtime.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir

$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$exe = Join-Path $buildPath "$Config/LocalCall.exe"

if (-not $NoDist) {
    if ($Config -match '^[Dd]ebug$') {
        Write-Warning "Debug builds depend on Debug MSVC runtime DLLs and are not redistributable. Skipping installer-ready dist folder. Use: build.bat clean release"
    } else {
        $distPath = Resolve-FullPath $DistDir
        if ($Clean -and (Test-Path -LiteralPath $distPath)) {
            Remove-Item -Recurse -Force $distPath
        }
        ./scripts/deploy-windows.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir -Clean -OutputDir $distPath
        ./scripts/check-windows-runtime.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir -AppDir $distPath
        Write-Host ""
        Write-Host "Installer-ready folder: $distPath"
        Write-Host "Use this folder as the source in Inno Setup."
    }
}

if (Test-Path -LiteralPath $exe) {
    Write-Host ""
    Write-Host "Build completed: $exe"
}
