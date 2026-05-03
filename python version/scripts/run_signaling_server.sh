#!/usr/bin/env bash
set -euo pipefail
HOST="${LOCALCALL_SIGNALING_HOST:-0.0.0.0}"
PORT="${LOCALCALL_SIGNALING_PORT:-8765}"
python3 signaling_server.py --host "$HOST" --port "$PORT"
