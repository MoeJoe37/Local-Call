@echo off
set HOST=%LOCALCALL_SIGNALING_HOST%
if "%HOST%"=="" set HOST=0.0.0.0
set PORT=%LOCALCALL_SIGNALING_PORT%
if "%PORT%"=="" set PORT=8765
python signaling_server.py --host %HOST% --port %PORT%
