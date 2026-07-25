#!/usr/bin/env bash
# shellcheck shell=bash
# Canonical NiOn release/dependency manifest loader.
# This file contains no release values of its own; it only reads release/manifest/.

_nion_manifest_root="${NION_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
_nion_manifest_dir="$_nion_manifest_root/release/manifest"

_nion_manifest_read() {
  local key="$1" file="$_nion_manifest_dir/$1" value
  [[ -f "$file" ]] || { echo "NiOn manifest missing: $file" >&2; return 1; }
  IFS= read -r value < "$file" || true
  [[ -n "$value" ]] || { echo "NiOn manifest value is empty: $key" >&2; return 1; }
  if [[ $(wc -l < "$file") -ne 1 ]]; then
    echo "NiOn manifest value must contain exactly one line: $key" >&2
    return 1
  fi
  printf '%s' "$value"
}

NION_VERSION="$(_nion_manifest_read NION_VERSION)"
NION_RELEASE_STATUS="$(_nion_manifest_read RELEASE_STATUS)"
NION_APPSTREAM_RELEASE_TYPE="$(_nion_manifest_read APPSTREAM_RELEASE_TYPE)"
NION_APPIMAGE_ARCH="$(_nion_manifest_read APPIMAGE_ARCH)"
NION_TOR_BROWSER_VERSION="$(_nion_manifest_read TOR_BROWSER_VERSION)"
NION_TOR_DAEMON_VERSION="$(_nion_manifest_read TOR_DAEMON_VERSION)"
NION_TOR_SIGNING_FPR="$(_nion_manifest_read TOR_SIGNING_FINGERPRINT)"
NION_TOR_RUNTIME_LAYOUT="$(_nion_manifest_read TOR_RUNTIME_LAYOUT)"
NION_GTK_MIN_VERSION="$(_nion_manifest_read GTK_MIN_VERSION)"
NION_WEBKITGTK_MIN_VERSION="$(_nion_manifest_read WEBKITGTK_MIN_VERSION)"

[[ "$NION_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]] || {
  echo "Invalid NION_VERSION in manifest: $NION_VERSION" >&2; return 1 2>/dev/null || exit 1;
}
[[ "$NION_APPSTREAM_RELEASE_TYPE" =~ ^(stable|development)$ ]] || {
  echo "Invalid APPSTREAM_RELEASE_TYPE: $NION_APPSTREAM_RELEASE_TYPE" >&2; return 1 2>/dev/null || exit 1;
}
[[ "$NION_APPIMAGE_ARCH" == x86_64 ]] || {
  echo "Unsupported APPIMAGE_ARCH: $NION_APPIMAGE_ARCH" >&2; return 1 2>/dev/null || exit 1;
}
[[ "$NION_TOR_SIGNING_FPR" =~ ^[0-9A-F]{40}$ ]] || {
  echo "Invalid Tor signing fingerprint in manifest" >&2; return 1 2>/dev/null || exit 1;
}
[[ "$NION_TOR_RUNTIME_LAYOUT" =~ ^[0-9]+$ ]] || {
  echo "Invalid TOR_RUNTIME_LAYOUT: $NION_TOR_RUNTIME_LAYOUT" >&2; return 1 2>/dev/null || exit 1;
}

NION_APPIMAGE_BASENAME="NiOn-${NION_VERSION}-${NION_APPIMAGE_ARCH}.AppImage"
NION_SOURCE_BASENAME="NiOn-${NION_VERSION}-source.tar.gz"
export NION_VERSION NION_RELEASE_STATUS NION_APPSTREAM_RELEASE_TYPE NION_APPIMAGE_ARCH
export NION_TOR_BROWSER_VERSION NION_TOR_DAEMON_VERSION NION_TOR_SIGNING_FPR NION_TOR_RUNTIME_LAYOUT
export NION_GTK_MIN_VERSION NION_WEBKITGTK_MIN_VERSION NION_APPIMAGE_BASENAME NION_SOURCE_BASENAME
