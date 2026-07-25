#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

NEED_RUNTIME=0
if [[ ! -x runtime/tor/tor ]]; then
  NEED_RUNTIME=1
elif [[ ! -f runtime/tor/MANIFEST.ini ]] ||
     ! grep -Fqx "runtime-layout=$NION_TOR_RUNTIME_LAYOUT" runtime/tor/MANIFEST.ini ||
     ! grep -Fqx "tor-browser-version=$NION_TOR_BROWSER_VERSION" runtime/tor/MANIFEST.ini ||
     ! grep -Fqx "tor-version=$NION_TOR_DAEMON_VERSION" runtime/tor/MANIFEST.ini; then
  NEED_RUNTIME=1
elif ! runtime/tor/tor --version 2>/dev/null | head -n1 | grep -Fq "$NION_TOR_DAEMON_VERSION"; then
  NEED_RUNTIME=1
fi

if (( NEED_RUNTIME )); then
  echo "NiOn Tor runtime is missing, old, or invalid; repairing/preparing it first."
  ./scripts/fetch-tor-runtime.sh
fi

export NION_TOR_BINARY="$ROOT/runtime/tor/tor"
unset NION_ALLOW_SYSTEM_TOR || true

if [[ ! -d build ]]; then
  meson setup build --buildtype=debug
else
  meson setup build --reconfigure --buildtype=debug
fi

meson compile -C build
exec ./build/nion
