#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

# Build the release AppImage inside the pinned release environment (2.2.0).
#
# Why this exists: NiOn ships GTK/GLib/WebKitGTK but leaves the C library to the
# host, so an AppImage produced on a host newer than the supported floor dies at
# the loader with "GLIBC_2.x not found" for users on older distributions. That
# is exactly what happened to the 2.1.0 artifact, which was built on a glibc 2.43
# machine while claiming a glibc 2.39 floor.
#
# The pinned image is the oldest distribution line NiOn supports, so libraries
# taken from it are guaranteed to be usable by every supported system. The
# resulting AppDir is then re-checked with scripts/check-glibc-floor.sh, which
# fails the build if anything above the floor slipped in.
#
# Usage: build-release-container.sh [--image IMAGE] [--keep-container]
#   --image IMAGE       container image to build in (default: the pinned one)
#   --keep-container    do not remove the image after the build
#
# Environment:
#   NION_RELEASE_CONTAINER_IMAGE   overrides the pinned image
#
# Engines: podman is preferred over docker. Both are rootless-friendly.

PINNED_IMAGE="ubuntu:24.04"   # Ubuntu 24.04 LTS ships glibc 2.39 = NiOn's floor
IMAGE="${NION_RELEASE_CONTAINER_IMAGE:-$PINNED_IMAGE}"
KEEP=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image) IMAGE="${2:-}"; [[ -n "$IMAGE" ]] || { echo "--image needs a value" >&2; exit 2; }; shift 2 ;;
    --keep-container) KEEP=1; shift ;;
    -h|--help) sed -n '5,25p' "$0"; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

engine=""
command -v podman >/dev/null 2>&1 && engine=podman
if [[ -z "$engine" ]] && command -v docker >/dev/null 2>&1; then engine=docker; fi
if [[ -z "$engine" ]]; then
  cat >&2 <<'MSG'
podman (preferred) or docker is required to build in the pinned container.

Install one of them, or build natively on a host that is at or below the
supported glibc floor (see release/manifest/GLIBC_FLOOR). If you build natively
on a newer host, scripts/check-glibc-floor.sh will refuse the artifact.
MSG
  exit 2
fi

echo "NiOn $NION_VERSION release build"
echo "  engine          $engine"
echo "  image           $IMAGE"
echo "  supported floor glibc $NION_GLIBC_FLOOR"

# Verify the chosen image really is at or below the floor before spending time
# on the full build; a too-new image would produce an unusable AppImage.
image_glibc="$("$engine" run --rm "$IMAGE" sh -c 'getconf GNU_LIBC_VERSION 2>/dev/null | awk "{print \$2}"' 2>/dev/null || true)"
if [[ -z "$image_glibc" ]]; then
  echo "Could not determine the glibc version of $IMAGE" >&2
  exit 1
fi

greater_than() {
  awk -v a="$1" -v b="$2" 'BEGIN{
    n=split(a,x,"."); m=split(b,y,".");
    for (i=1;i<=3;i++) { xi=(i<=n?x[i]:0)+0; yi=(i<=m?y[i]:0)+0;
      if (xi>yi) exit 0; if (xi<yi) exit 1; }
    exit 1 }'
}

echo "  image glibc     $image_glibc"
if greater_than "$image_glibc" "$NION_GLIBC_FLOOR"; then
  cat >&2 <<MSG

$IMAGE provides glibc $image_glibc, which is newer than the supported floor
glibc $NION_GLIBC_FLOOR. Bundled libraries taken from it would require a newer
C library than NiOn promises, so this image cannot produce a supported AppImage.

Use the pinned image ($PINNED_IMAGE) or an older one.
MSG
  exit 1
fi

echo
echo "Building inside the container (this downloads Tor and appimagetool)…"

"$engine" run --rm \
  -v "$ROOT:/src" \
  -w /src \
  -e "NION_GLIBC_FLOOR=$NION_GLIBC_FLOOR" \
  -e "DEBIAN_FRONTEND=noninteractive" \
  "$IMAGE" \
  bash -lc './scripts/install-deps-debian.sh && ./scripts/build-appimage.sh'

# The container ran as root; hand the artifacts back to the invoking user so the
# working tree stays usable and does not accumulate root-owned files.
"$engine" run --rm -v "$ROOT:/src" "$IMAGE" \
  chown -R "$(id -u):$(id -g)" /src/dist /src/NiOn.AppDir 2>/dev/null || true

if (( KEEP == 0 )); then
  "$engine" rmi "$IMAGE" >/dev/null 2>&1 || true
fi

APPIMAGE="$ROOT/dist/$NION_APPIMAGE_BASENAME"
[[ -x "$APPIMAGE" ]] || { echo "Expected artifact missing: $APPIMAGE" >&2; exit 1; }

echo
echo "Verifying the container-built artifact against the glibc floor…"
./scripts/check-glibc-floor.sh "$APPIMAGE"

printf '\nContainer release build complete:\n  %s\n  %s.sha256\n' "$APPIMAGE" "$APPIMAGE"
