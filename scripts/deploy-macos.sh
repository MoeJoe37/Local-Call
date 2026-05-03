#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
PREFIX="${PREFIX:-$PWD/dist/LocalCall-macOS}"
CONFIG_ARG=()
if [[ -n "${CONFIG:-}" ]]; then
  CONFIG_ARG=(--config "$CONFIG")
fi

cmake --install "$BUILD_DIR" "${CONFIG_ARG[@]}" --prefix "$PREFIX"

cat <<MSG

Installed macOS deploy tree:
  $PREFIX

The Qt CMake deployment script runs macdeployqt during install when supported.
The app bundle should be under:
  $PREFIX/LocalCall.app
MSG
