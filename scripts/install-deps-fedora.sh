#!/usr/bin/env bash
set -euo pipefail

# Fedora / Nobara:
#   ./scripts/install-deps-fedora.sh
# Bazzite / other rpm-ostree systems:
#   prefer distrobox/toolbox, or use rpm-ostree install with the same package list.

packages=(
  gcc-c++ cmake ninja-build pkgconf-pkg-config git zip
  qt6-qtbase-devel qt6-qtmultimedia-devel
  nlohmann-json-devel openssl-devel
  opus-devel openh264-devel libyuv-devel libdatachannel-devel
)

if command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y "${packages[@]}" || {
    echo "Some optional packages were unavailable. Retry with WebRTC disabled:" >&2
    echo "  LOCALCALL_WITH_WEBRTC=OFF ./scripts/build-linux.sh" >&2
    exit 1
  }
elif command -v rpm-ostree >/dev/null 2>&1; then
  sudo rpm-ostree install "${packages[@]}"
  echo "Reboot after rpm-ostree finishes, then run ./scripts/build-linux.sh"
else
  echo "No dnf/rpm-ostree found. Use your distro package manager." >&2
  exit 1
fi
