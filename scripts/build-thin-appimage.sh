#!/usr/bin/env bash
set -euo pipefail
echo "NiOn 1.0.0 replaced the old thin AppImage builder with the full AppImage engineering pipeline." >&2
exec "$(cd "$(dirname "$0")" && pwd)/build-appimage.sh" "$@"
