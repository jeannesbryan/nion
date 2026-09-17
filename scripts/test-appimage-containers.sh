#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/manifest.sh"
APPIMAGE="${1:-$ROOT/dist/$NION_APPIMAGE_BASENAME}"
[[ -e "$APPIMAGE" ]] || { echo "AppImage not found: $APPIMAGE" >&2; exit 1; }
# Resolve to an absolute path: a container bind mount needs one. Given a
# relative path, podman treats "dist/NiOn-....AppImage" as a *named volume* and
# fails with "names must match [a-zA-Z0-9][a-zA-Z0-9_.-]*", which says nothing
# about the real problem.
APPIMAGE="$(cd "$(dirname "$APPIMAGE")" && pwd)/$(basename "$APPIMAGE")"
[[ -f "$APPIMAGE" ]] || { echo "Not a file: $APPIMAGE" >&2; exit 1; }

# Cross-distribution smoke test (wired into release preflight in 2.2.0).
#
# This is the check that would have caught the 2.1.0 "GLIBC_2.43 not found"
# release: the AppImage was built on a glibc 2.43 host and never executed on a
# machine that looked like a supported user system.
#
# Scope: does the AppImage extract, and do its loader paths resolve on a real
# desktop of each supported distribution? NiOn bundles GTK/GLib/WebKitGTK but
# deliberately does NOT bundle the C library nor the graphics driver stack
# (Mesa, libdrm, libgbm, Vulkan) -- those belong to the host, exactly like they
# do for every other browser. The bare distribution images contain neither, so
# each one gets the minimal runtime libraries a desktop of that family ships
# before the check runs. Without that step this test would fail on every image
# for a reason that has nothing to do with what we ship.
#
# What this does not do: open a window or browse. A real GUI/Tor run still has
# to be verified on a desktop (see TESTING.md).

engine=""
command -v podman >/dev/null 2>&1 && engine=podman
if [[ -z "$engine" ]] && command -v docker >/dev/null 2>&1; then engine=docker; fi
[[ -n "$engine" ]] || { echo "podman or docker is required for container smoke tests" >&2; exit 2; }

# Host libraries NiOn relies on instead of bundling. Packages are per family.
# shellcheck disable=SC2016
APT_LIBS='libgl1 libegl1 libgbm1 libdrm2 libvulkan1'
FEDORA_LIBS='mesa-libGL mesa-libEGL mesa-libgbm libdrm mesa-vulkan-drivers'

# image|prepare command
targets=(
  "docker.io/library/ubuntu:24.04|apt-get update -qq && apt-get install -y -qq --no-install-recommends $APT_LIBS"
  "docker.io/library/debian:stable-slim|apt-get update -qq && apt-get install -y -qq --no-install-recommends $APT_LIBS"
  "docker.io/library/fedora:latest|dnf install -y -q $FEDORA_LIBS"
)

echo "NiOn $NION_VERSION container smoke test"
echo "  engine   $engine"
echo "  artifact $APPIMAGE"
echo "  floor    glibc $NION_GLIBC_FLOOR"
echo

fail=0
for target in "${targets[@]}"; do
  image="${target%%|*}"
  prepare="${target#*|}"
  echo "== $image =="
  if ! timeout 900 "$engine" run --rm -v "$APPIMAGE:/opt/NiOn.AppImage:ro" "$image" \
       sh -lc "$prepare >/dev/null 2>&1 || { echo 'could not install the host runtime libraries'; exit 2; }
                chmod +x /opt/NiOn.AppImage 2>/dev/null || true
                APPIMAGE_EXTRACT_AND_RUN=1 /opt/NiOn.AppImage --appimage-diagnose"; then
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
