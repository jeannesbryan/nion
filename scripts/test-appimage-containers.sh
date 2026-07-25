#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPIMAGE="${1:-$ROOT/dist/NiOn-1.1.0-x86_64.AppImage}"
[[ -x "$APPIMAGE" ]] || { echo "AppImage not found/executable: $APPIMAGE" >&2; exit 1; }

engine=""
command -v podman >/dev/null && engine=podman
[[ -z "$engine" ]] && command -v docker >/dev/null && engine=docker
[[ -n "$engine" ]] || { echo "podman or docker is required for container smoke tests" >&2; exit 2; }

# Loader/runtime-only checks; GUI display and real Tor browsing must still be
# tested on actual desktop distributions.
images=(debian:stable-slim ubuntu:24.04 fedora:latest)
for image in "${images[@]}"; do
  echo "== $image =="
  "$engine" run --rm -v "$APPIMAGE:/opt/NiOn.AppImage:ro" "$image" \
    sh -lc 'chmod +x /opt/NiOn.AppImage 2>/dev/null || true; APPIMAGE_EXTRACT_AND_RUN=1 /opt/NiOn.AppImage --appimage-diagnose'
done
