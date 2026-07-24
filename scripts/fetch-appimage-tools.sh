#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS="$ROOT/.tools/appimage"
mkdir -p "$TOOLS"

arch="$(uname -m)"
case "$arch" in
  x86_64|amd64) ai_arch=x86_64 ;;
  *) echo "NiOn AppImage engineering currently targets x86_64; got $arch" >&2; exit 2 ;;
esac

url="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ai_arch}.AppImage"
out="$TOOLS/appimagetool-${ai_arch}.AppImage"
if [[ ! -x "$out" ]]; then
  echo "Downloading official appimagetool…"
  curl --fail --location --proto '=https' --tlsv1.2 --retry 3 -o "$out" "$url"
  chmod +x "$out"
fi
printf '%s\n' "$out"
