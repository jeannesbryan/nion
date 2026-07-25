#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/manifest.sh"

sudo apt update
sudo apt install -y \
  build-essential \
  meson \
  ninja-build \
  pkg-config \
  libgtk-4-dev \
  libwebkitgtk-6.0-dev \
  libsoup-3.0-dev \
  libglib2.0-dev-bin \
  bubblewrap \
  xdg-dbus-proxy \
  desktop-file-utils \
  appstream \
  binutils \
  file \
  ca-certificates \
  curl \
  gnupg \
  tar

echo "A system Tor package is not required by NiOn."
echo "Run ./scripts/fetch-tor-runtime.sh to prepare the signed bundled Tor runtime."
echo "Run ./scripts/build-appimage.sh for the NiOn $NION_VERSION AppImage pipeline."
