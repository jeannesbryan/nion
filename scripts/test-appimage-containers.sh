#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/manifest.sh"
APPIMAGE="${1:-$ROOT/dist/$NION_APPIMAGE_BASENAME}"
[[ -x "$APPIMAGE" ]] || { echo "AppImage not found/executable: $APPIMAGE" >&2; exit 1; }

# Cross-distribution smoke test (wired into release preflight in 2.2.0).
#
# This is the check that would have caught the 2.1.0 "GLIBC_2.43 not found"
# release: the AppImage was built on a glibc 2.43 host and never executed on a
# machine that looked like a supported user system.
#
# The first image is the pinned release environment (the supported floor); the
# others are ordinary downstream distributions. Loader/runtime checks only:
# GUI display and real Tor browsing still have to be verified on a desktop.

engine=""
command -v podman >/dev/null && engine=podman
[[ -z "$engine" ]] && command -v docker >/dev/null && engine=docker
[[ -n "$engine" ]] || { echo "podman or docker is required for container smoke tests" >&2; exit 2; }

# Fully-qualified names so rootless podman does not have to guess a registry.
images=(
  docker.io/library/ubuntu:24.04
  docker.io/library/debian:stable-slim
  docker.io/library/fedora:latest
)

echo "NiOn $NION_VERSION container smoke test"
echo "  engine   $engine"
echo "  artifact $APPIMAGE"
echo "  floor    glibc $NION_GLIBC_FLOOR"
echo

fail=0
for image in "${images[@]}"; do
  echo "== $image =="
  if ! timeout 600 "$engine" run --rm -v "$APPIMAGE:/opt/NiOn.AppImage:ro" "$image" \
       sh -lc 'chmod +x /opt/NiOn.AppImage 2>/dev/null || true; APPIMAGE_EXTRACT_AND_RUN=1 /opt/NiOn.AppImage --appimage-diagnose'; then
    echo "FAIL  $image could not run the NiOn AppImage" >&2
    fail=1
  fi
  echo
done

if (( fail )); then
  echo 'RESULT: FAIL — the AppImage did not run on every tested distribution.' >&2
  echo 'Run ./scripts/check-glibc-floor.sh <AppImage> to see which libraries exceed the floor.' >&2
  exit 1
fi

echo 'RESULT: PASS — the AppImage extracted and resolved its bundled libraries on every tested distribution.'
