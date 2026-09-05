param(
    [string]$QtDir = "",
    [string]$VcpkgRoot = "",
    [string]$BuildDir = "",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$Clean,
    [string]$WebRTC = "AUTO",
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

function Get-QtKitToolchain {
    param([Parameter(Mandatory=$true)][string]$Path)
    if ($Path -match "mingw|gcc_64") { return "mingw" }
    return "msvc"
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

function Enter-MinGWEnvironment {
    param([Parameter(Mandatory=$true)][string]$QtKitDir)

    # Qt installs its own MinGW toolchain next to the kits:
    #   C:\Qt\6.11.1\mingw_64  ->  C:\Qt\Tools\mingw1310_64\bin
    $qtRoot = Split-Path -Parent (Split-Path -Parent $QtKitDir)
    $toolsRoot = Join-Path $qtRoot "Tools"

    $mingwBins = @()
    if (Test-Path -LiteralPath $toolsRoot) {
        $mingwBins = @(Get-ChildItem -LiteralPath $toolsRoot -Directory -Filter "mingw*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "bin" } |
            Where-Object { Test-Path -LiteralPath (Join-Path $_ "g++.exe") })
    }

    if ($mingwBins.Count -eq 0) {
        throw "No MinGW toolchain was found for $QtKitDir. Install 'MinGW 64-bit' with the Qt Maintenance Tool (Qt / Developer and Designer Tools)."
    }

    # Prepend so Qt's toolchain wins over any other gcc in PATH (MSYS2, Cygwin, ...).
    $env:PATH = "$($mingwBins[0]);$($env:PATH)"
    return $mingwBins[0]
}

function Find-NinjaExecutable {
    param([Parameter(Mandatory=$true)][string]$QtKitDir)

    $qtRoot = Split-Path -Parent (Split-Path -Parent $QtKitDir)
    foreach ($candidate in @(
        (Join-Path $qtRoot "Tools/Ninja/ninja.exe"),
        (Join-Path $qtRoot "Tools/CMake_64/bin/ninja.exe")
    )) {
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }

    $ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
    if ($ninjaCmd) { return $ninjaCmd.Source }

    throw "Ninja was not found. Install 'Ninja' with the Qt Maintenance Tool or add ninja.exe to PATH."
}

Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)

$resolvedQtDir = Find-QtDir $QtDir
if (-not $resolvedQtDir) {
    throw @"
Qt6Config.cmake was not found.
Install Qt 6 (MSVC 2022 64-bit or MinGW 64-bit) from the Qt Maintenance Tool, then run build.bat again.
Expected example path: C:\Qt\6.x.x\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake
You can also run: .\build.bat -QtDir C:\Qt\6.x.x\msvc2022_64
"@
}

# Both Qt kit flavours are supported. The kit decides the generator, the
# compiler and the vcpkg triplet, because an MSVC build cannot link MinGW Qt
# DLLs and vice versa.
$kitToolchain = Get-QtKitToolchain $resolvedQtDir

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    # Keep both flavours in separate trees: their CMake caches, vcpkg triplets
    # and generators are not interchangeable.
    $BuildDir = if ($kitToolchain -eq "mingw") { "build-mingw" } else { "build" }
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

$ninja = ""
if ($kitToolchain -eq "mingw") {
    $mingwBin = Enter-MinGWEnvironment $resolvedQtDir
    $ninja = Find-NinjaExecutable $resolvedQtDir
    $vcpkgTriplet = "x64-mingw-dynamic"
    if ($WebRTC -eq "AUTO") {
        # vcpkg's libdatachannel (usrsctp) does not build with MinGW, and LAN
        # calls do not need it, so the optional transport stays off here.
        $WebRTC = "OFF"
    }
    Write-Host "Using MinGW: $mingwBin"
    Write-Host "Using Ninja: $ninja"
} else {
    Enter-MSVCEnvironment
    $vcpkgTriplet = "x64-windows"
}

Write-Host "Using Qt:    $resolvedQtDir"
Write-Host "Using vcpkg: $resolvedVcpkgRoot"
Write-Host "Triplet:     $vcpkgTriplet"
Write-Host "Config:      $Config"

$cmakeArgs = @(
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_PREFIX_PATH=$resolvedQtDir",
    "-DVCPKG_TARGET_TRIPLET=$vcpkgTriplet",
    "-DVCPKG_HOST_TRIPLET=x64-windows",
    "-DLOCALCALL_WITH_WEBRTC=$WebRTC",
    "-DLOCALCALL_WITH_MULTIMEDIA=$Multimedia",
    "-DLOCALCALL_POST_BUILD_DEPLOY_QT=ON",
    "-DLOCALCALL_INSTALL_QT_RUNTIME=ON"
)

# libdatachannel is a vcpkg manifest feature, so it is only installed when the
# optional WebRTC transport is actually wanted.
if ($WebRTC -ne "OFF") { $cmakeArgs += "-DVCPKG_MANIFEST_FEATURES=webrtc" }

if ($kitToolchain -eq "mingw") {
    cmake -S . -B $BuildDir -G "Ninja" `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        -DCMAKE_C_COMPILER=gcc `
        -DCMAKE_CXX_COMPILER=g++ `
        "-DCMAKE_BUILD_TYPE=$Config" `
        @cmakeArgs
} else {
    cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 @cmakeArgs
}
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

./scripts/deploy-windows.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir -Triplet $vcpkgTriplet -Clean
./scripts/check-windows-runtime.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir

$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$exe = Join-Path $buildPath "$Config/LocalCall.exe"
# Single-config generators (Ninja) write straight into the build folder.
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $buildPath "LocalCall.exe" }

if (-not $NoDist) {
    if ($kitToolchain -eq "msvc" -and $Config -match '^[Dd]ebug$') {
        Write-Warning "Debug builds depend on Debug MSVC runtime DLLs and are not redistributable. Skipping installer-ready dist folder. Use: build.bat clean release"
    } else {
        $distPath = Resolve-FullPath $DistDir
        if ($Clean -and (Test-Path -LiteralPath $distPath)) {
            Remove-Item -Recurse -Force $distPath
        }
        ./scripts/deploy-windows.ps1 -BuildDir $BuildDir -Config $Config -QtDir $resolvedQtDir -Triplet $vcpkgTriplet -Clean -OutputDir $distPath
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
