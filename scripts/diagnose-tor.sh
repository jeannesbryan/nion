#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

TOR_BINARY="${NION_TOR_BINARY:-}"
if [[ -z "$TOR_BINARY" && -x "$ROOT/runtime/tor/tor" ]]; then
  TOR_BINARY="$ROOT/runtime/tor/tor"
fi
if [[ -z "$TOR_BINARY" && "${NION_ALLOW_SYSTEM_TOR:-0}" == "1" ]]; then
  TOR_BINARY="$(command -v tor || true)"
fi
if [[ -z "$TOR_BINARY" || ! -x "$TOR_BINARY" ]]; then
  echo "Bundled Tor runtime not found." >&2
  echo "Run: ./scripts/fetch-tor-runtime.sh" >&2
  exit 1
fi

DIAG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/nion/tor-diagnostic"
mkdir -p "$DIAG_DIR"
chmod 700 "$DIAG_DIR"
TORRC="$DIAG_DIR/torrc"
printf '# NiOn Tor diagnostic configuration\n' > "$TORRC"
chmod 600 "$TORRC"

PORT=19090
while ss -Hln "sport = :$PORT" 2>/dev/null | grep -q .; do
  PORT=$((PORT + 1))
  if (( PORT > 19110 )); then
    echo "Could not find a free diagnostic port." >&2
    exit 1
  fi
done

printf 'NiOn Tor diagnostic\n'
printf 'Tor: %s\n' "$TOR_BINARY"
printf 'DataDirectory: %s\n' "$DIAG_DIR"
printf 'SOCKS test port: 127.0.0.1:%s\n' "$PORT"
printf 'Stop with Ctrl+C after Bootstrapped 100%% or after an error is shown.\n\n'

exec "$TOR_BINARY" \
  -f "$TORRC" \
  --ClientOnly 1 \
  --DataDirectory "$DIAG_DIR" \
  --SocksPort "127.0.0.1:$PORT" \
  --SafeSocks 1 \
  --WarnUnsafeSocks 1 \
  --ClientRejectInternalAddresses 1 \
  --ClientDNSRejectInternalAddresses 1 \
  --Log "notice stdout"
