#!/usr/bin/env bash
set -euo pipefail

# Arch / EndeavourOS / CachyOS / Manjaro-style systems.
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf git zip \
  qt6-base qt6-multimedia qt6-websockets qt6-wayland \
  opencv nlohmann-json opus openh264 libyuv libdatachannel
