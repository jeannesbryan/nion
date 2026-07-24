#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME="$ROOT/runtime/tor"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/nion/tor-bundle"

TOR_BROWSER_VERSION="15.0.19"
TOR_DAEMON_VERSION="0.4.9.11"
TOR_SIGNING_FPR="EF6E286DDA85EA2A4BA7DE684E2C6E8793298290"
RUNTIME_LAYOUT="2"

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|amd64) BUNDLE_ARCH="x86_64" ;;
  *)
    echo "NiOn currently pins the stable Tor Expert Bundle for GNU/Linux x86_64." >&2
    echo "Detected architecture: $ARCH" >&2
    exit 2
    ;;
esac

ARCHIVE="tor-expert-bundle-linux-${BUNDLE_ARCH}-${TOR_BROWSER_VERSION}.tar.gz"
SIG="$ARCHIVE.asc"
BASE="https://archive.torproject.org/tor-package-archive/torbrowser/${TOR_BROWSER_VERSION}"
URL="$BASE/$ARCHIVE"
SIG_URL="$BASE/$SIG"
KEY_URL="https://keys.openpgp.org/vks/v1/by-fingerprint/${TOR_SIGNING_FPR}"

for tool in curl tar find sort awk grep gpg; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "$tool is required. Run ./scripts/install-deps-debian.sh first." >&2
    exit 1
  }
done

mkdir -p "$CACHE"
chmod 700 "$CACHE"

# Self-repair runtimes created by the original 0.6.0 launcher. The old launcher
# put every *.so-containing directory on LD_LIBRARY_PATH, including the Expert
# Bundle debug directory. That can make the loader pick debug symbol objects
# instead of real shared libraries (for example libevent-2.1.so.7).
if [[ -d "$RUNTIME/expert" && "${NION_FORCE_TOR_FETCH:-0}" != "1" ]]; then
  if "$ROOT/scripts/repair-tor-runtime.sh" >/dev/null 2>&1; then
    if grep -qx "runtime-layout=$RUNTIME_LAYOUT" "$RUNTIME/MANIFEST.ini" 2>/dev/null; then
      current="$($RUNTIME/tor --version 2>/dev/null | head -n1 || true)"
      if [[ "$current" == *"$TOR_DAEMON_VERSION"* ]]; then
        echo "Bundled Tor runtime repaired/validated: $current"
        exit 0
      fi
    fi
  fi
fi

if [[ -x "$RUNTIME/tor" && -f "$RUNTIME/MANIFEST.ini" && "${NION_FORCE_TOR_FETCH:-0}" != "1" ]]; then
  current="$($RUNTIME/tor --version 2>/dev/null | head -n1 || true)"
  if [[ "$current" == *"$TOR_DAEMON_VERSION"* ]] && grep -qx "runtime-layout=$RUNTIME_LAYOUT" "$RUNTIME/MANIFEST.ini"; then
    echo "Bundled Tor runtime already prepared: $current"
    exit 0
  fi
fi

printf 'Downloading Tor Expert Bundle %s (Tor %s)…\n' "$TOR_BROWSER_VERSION" "$TOR_DAEMON_VERSION"
curl --fail --location --proto '=https' --tlsv1.2 --retry 3 \
  -o "$CACHE/$ARCHIVE" "$URL"
curl --fail --location --proto '=https' --tlsv1.2 --retry 3 \
  -o "$CACHE/$SIG" "$SIG_URL"
curl --fail --location --proto '=https' --tlsv1.2 --retry 3 \
  -o "$CACHE/tor-browser-developers.asc" "$KEY_URL"

GNUPGHOME_TMP="$(mktemp -d)"
EXTRACT="$(mktemp -d)"
trap 'rm -rf "$GNUPGHOME_TMP" "$EXTRACT"' EXIT
chmod 700 "$GNUPGHOME_TMP"

gpg --batch --homedir "$GNUPGHOME_TMP" --import "$CACHE/tor-browser-developers.asc" >/dev/null 2>&1
if ! gpg --batch --homedir "$GNUPGHOME_TMP" --with-colons --fingerprint "$TOR_SIGNING_FPR" 2>/dev/null \
    | awk -F: '$1 == "fpr" {print $10}' | grep -qx "$TOR_SIGNING_FPR"; then
  echo "Tor Browser Developers signing-key fingerprint verification failed." >&2
  exit 1
fi

gpg --batch --homedir "$GNUPGHOME_TMP" --verify "$CACHE/$SIG" "$CACHE/$ARCHIVE"

tar -xzf "$CACHE/$ARCHIVE" -C "$EXTRACT"

REAL_TOR="$(find "$EXTRACT" -type f -name tor -perm -u+x ! -path '*/debug/*' -print -quit)"
if [[ -z "$REAL_TOR" ]]; then
  echo "The verified Expert Bundle did not contain an executable named 'tor'." >&2
  exit 1
fi

# Validate with the same narrow loader path NiOn will use at runtime.
TOR_BIN_DIR="$(dirname "$REAL_TOR")"
BUNDLE_LD="$TOR_BIN_DIR"
if [[ -d "$TOR_BIN_DIR/libstdc++" ]]; then
  BUNDLE_LD="$BUNDLE_LD:$TOR_BIN_DIR/libstdc++"
fi
version="$(env LD_LIBRARY_PATH="$BUNDLE_LD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$REAL_TOR" --version 2>&1 | head -n1 || true)"
if [[ "$version" != *"$TOR_DAEMON_VERSION"* ]]; then
  echo "Unexpected or unusable Tor runtime in Expert Bundle: ${version:-unknown}" >&2
  echo "Expected Tor runtime directory: $TOR_BIN_DIR" >&2
  exit 1
fi

rm -rf "$RUNTIME"
mkdir -p "$RUNTIME/expert"
cp -a "$EXTRACT"/. "$RUNTIME/expert"/
chmod -R u+rwX,go-rwx "$RUNTIME"

cat > "$RUNTIME/MANIFEST.ini" <<EOF_MANIFEST
[NiOn Tor Runtime]
source=$URL
signature=$SIG_URL
signing-key-fingerprint=$TOR_SIGNING_FPR
tor-browser-version=$TOR_BROWSER_VERSION
tor-version=$TOR_DAEMON_VERSION
architecture=linux-$BUNDLE_ARCH
runtime-layout=$RUNTIME_LAYOUT
EOF_MANIFEST
chmod 600 "$RUNTIME/MANIFEST.ini"

"$ROOT/scripts/repair-tor-runtime.sh"

printf '\nBundled Tor runtime ready.\n'
printf '  Launcher: %s\n' "$RUNTIME/tor"
printf '  Version:  %s\n' "$version"
printf '  Source:   %s\n' "$URL"
