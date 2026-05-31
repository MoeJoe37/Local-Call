@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if not exist "%~dp0dist\LocalCall\LocalCall.exe" (
    echo dist\LocalCall does not exist yet. Building Release first...
    call "%~dp0build.bat" clean release
    if errorlevel 1 exit /b 1
)

where ISCC.exe >nul 2>nul
if errorlevel 1 (
    if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" (
        set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    ) else if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" (
        set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
    ) else (
        echo Inno Setup Compiler was not found.
        echo Install Inno Setup 6 or open this script manually:
        echo   packaging\windows\LocalCall.iss
        exit /b 1
    )
) else (
    set "ISCC=ISCC.exe"
)

if not exist "%~dp0dist\installer" mkdir "%~dp0dist\installer"
"%ISCC%" "%~dp0packaging\windows\LocalCall.iss"
if errorlevel 1 (
    echo Inno Setup failed.
    exit /b 1
)

echo.
echo Installer created in:
echo   %~dp0dist\installer
exit /b 0
