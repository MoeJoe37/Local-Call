@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set "CLEAN_ARG="
set "CONFIG_ARG=Release"
set "MAKE_INSTALLER=0"
set "PASS_ARGS="

:parse_args
if "%~1"=="" goto run_build
if /I "%~1"=="clean" (
    set "CLEAN_ARG=-Clean"
    shift
    goto parse_args
)
if /I "%~1"=="debug" (
    set "CONFIG_ARG=Debug"
    shift
    goto parse_args
)
if /I "%~1"=="release" (
    set "CONFIG_ARG=Release"
    shift
    goto parse_args
)
if /I "%~1"=="installer" (
    set "MAKE_INSTALLER=1"
    shift
    goto parse_args
)
set "PASS_ARGS=!PASS_ARGS! ^"%~1^""
shift
goto parse_args

:run_build
echo Building LocalCall (%CONFIG_ARG%)...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-windows.ps1" %CLEAN_ARG% -Config %CONFIG_ARG% !PASS_ARGS!
if errorlevel 1 (
    echo.
    echo Build failed. Check the error above.
    echo.
    pause
    exit /b 1
)

if /I "%CONFIG_ARG%"=="Debug" (
    echo.
    echo Debug build completed, but Debug builds are NOT installable on another PC.
    echo Use: build.bat clean release
    echo.
    pause
    exit /b 0
)

echo.
echo Build completed successfully.
echo Installer-ready folder:
echo   %~dp0dist\LocalCall
echo.
echo For Inno Setup, package ONLY this folder: dist\LocalCall
echo Do not package build\Debug or build\Release manually.
echo.

if "%MAKE_INSTALLER%"=="1" (
    call "%~dp0make-installer.bat"
    if errorlevel 1 exit /b 1
)

pause
exit /b 0
