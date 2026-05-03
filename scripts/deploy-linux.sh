#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
PREFIX="${PREFIX:-$PWD/dist/LocalCall}"
CONFIG_ARG=()
if [[ -n "${CONFIG:-}" ]]; then
  CONFIG_ARG=(--config "$CONFIG")
fi

cmake --install "$BUILD_DIR" "${CONFIG_ARG[@]}" --prefix "$PREFIX"

cat <<MSG

Installed deploy tree:
  $PREFIX

Run:
  $PREFIX/bin/LocalCall

Notes:
- The executable is installed with relative RPATH so bundled libraries can be
  found beside the app when Qt's deployment script copies them.
- For maximum portability across Fedora/Nobara/Bazzite/Arch/NixOS, prefer
  building the package on the oldest compatible distro or create an AppImage/
  Flatpak from this install tree.
MSG
