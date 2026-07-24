#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME="$ROOT/runtime/tor"
EXPERT="$RUNTIME/expert"
EXPECTED_TOR_VERSION="0.4.9.11"
RUNTIME_LAYOUT="2"

if [[ ! -d "$EXPERT" ]]; then
  echo "NiOn Tor Expert Bundle is not present at: $EXPERT" >&2
  echo "Run ./scripts/fetch-tor-runtime.sh first." >&2
  exit 1
fi

REAL_TOR="$(find "$EXPERT" -type f -name tor -perm -u+x ! -path '*/debug/*' -print -quit)"
if [[ -z "$REAL_TOR" ]]; then
  echo "Could not find the executable Tor daemon inside the Expert Bundle." >&2
  exit 1
fi

TOR_DIR="$(dirname "$REAL_TOR")"
GEOIP="$(find "$EXPERT" -type f -name geoip ! -path '*/debug/*' -print -quit || true)"
GEOIP6="$(find "$EXPERT" -type f -name geoip6 ! -path '*/debug/*' -print -quit || true)"
REAL_REL="${REAL_TOR#$EXPERT/}"
GEOIP_REL="${GEOIP#$EXPERT/}"
GEOIP6_REL="${GEOIP6#$EXPERT/}"

# Tor Expert Bundle ships runtime libraries beside the Tor daemon. Do not add
# every directory containing *.so files: the bundle can contain debug-symbol
# files with library-like names, which the ELF loader must never search first.
{
  cat <<'WRAP_HEAD'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
WRAP_HEAD
  printf 'REAL="$HERE/expert/%s"\n' "$REAL_REL"
  cat <<'WRAP_LIBS'
TOR_DIR="$(dirname "$REAL")"
LIB_PATH="$TOR_DIR"
if [[ -d "$TOR_DIR/libstdc++" ]]; then
  LIB_PATH="$LIB_PATH:$TOR_DIR/libstdc++"
fi
export LD_LIBRARY_PATH="$LIB_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# AppImage-only WebKit subprocess hooks belong to the NiOn UI process, never
# to the bundled Tor daemon.
unset LD_PRELOAD NION_WEBKIT_EXEC_DIR WEBKIT_EXEC_PATH || true
EXTRA=()
WRAP_LIBS
  if [[ -n "$GEOIP" ]]; then
    printf 'EXTRA+=(--GeoIPFile "$HERE/expert/%s")\n' "$GEOIP_REL"
  fi
  if [[ -n "$GEOIP6" ]]; then
    printf 'EXTRA+=(--GeoIPv6File "$HERE/expert/%s")\n' "$GEOIP6_REL"
  fi
  cat <<'WRAP_END'
exec "$REAL" "${EXTRA[@]}" "$@"
WRAP_END
} > "$RUNTIME/tor"
chmod 700 "$RUNTIME/tor"

version="$($RUNTIME/tor --version 2>&1 | head -n1 || true)"
if [[ "$version" != *"$EXPECTED_TOR_VERSION"* ]]; then
  echo "Bundled Tor runtime validation failed." >&2
  echo "Tor directory: $TOR_DIR" >&2
  echo "Result: ${version:-no output}" >&2
  exit 1
fi

MANIFEST="$RUNTIME/MANIFEST.ini"
if [[ -f "$MANIFEST" ]]; then
  tmp="${MANIFEST}.tmp"
  grep -v '^runtime-layout=' "$MANIFEST" > "$tmp" || true
  printf 'runtime-layout=%s\n' "$RUNTIME_LAYOUT" >> "$tmp"
  mv "$tmp" "$MANIFEST"
else
  cat > "$MANIFEST" <<EOF_MANIFEST
[NiOn Tor Runtime]
tor-version=$EXPECTED_TOR_VERSION
runtime-layout=$RUNTIME_LAYOUT
EOF_MANIFEST
fi
chmod 600 "$MANIFEST"

printf 'NiOn Tor runtime repaired and validated.\n'
printf '  Tor:      %s\n' "$REAL_TOR"
printf '  Lib path: %s\n' "$TOR_DIR"
printf '  Version:  %s\n' "$version"
