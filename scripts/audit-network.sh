#!/usr/bin/env bash
set -euo pipefail

DURATION="${1:-20}"
if ! [[ "$DURATION" =~ ^[0-9]+$ ]] || (( DURATION < 1 )); then
  echo "Usage: $0 [seconds]" >&2
  exit 2
fi

command -v ss >/dev/null 2>&1 || {
  echo "ss is required (usually provided by iproute2)." >&2
  exit 1
}
command -v ps >/dev/null 2>&1 || {
  echo "ps is required." >&2
  exit 1
}

USER_NAME="$(id -un)"
TMP_TCP="$(mktemp)"
TMP_UDP="$(mktemp)"
trap 'rm -f "$TMP_TCP" "$TMP_UDP"' EXIT

find_pids() {
  ps -eo pid=,user=,args= | awk -v u="$USER_NAME" '
    $2 == u && ($0 ~ /(^|\/)nion([[:space:]]|$)/ ||
                $0 ~ /WebKitNetworkProcess/ ||
                $0 ~ /WebKitWebProcess/ ||
                $0 ~ /WebKitGPUProcess/) { print $1 }
  ' | sort -u
}

printf 'NiOn 1.1.0 runtime network audit\n'
printf 'Sampling NiOn/WebKit TCP and UDP sockets for %s seconds.\n' "$DURATION"
STATE_FILE="${XDG_DATA_HOME:-$HOME/.local/share}/nion/tor-runtime.ini"
SOCKS_PORT="$(awk -F= '$1 == "socks-port" {print $2; exit}' "$STATE_FILE" 2>/dev/null || true)"
if [[ ! "$SOCKS_PORT" =~ ^[0-9]+$ ]]; then
  echo "Could not read NiOn's active SOCKS port from $STATE_FILE. Start NiOn first." >&2
  exit 1
fi

printf 'Expected web-network TCP peer: 127.0.0.1:%s (NiOn Tor SOCKS).\n' "$SOCKS_PORT"
printf 'Expected outbound UDP: none; Tor SOCKS transports browser streams over TCP.\n'
printf 'The Tor process itself is intentionally excluded because it connects to Tor relays.\n\n'
printf 'While this runs, browse both a clearnet page and a .onion page in NiOn.\n\n'

violations=0
observed=0

for ((second=1; second<=DURATION; second++)); do
  mapfile -t pids < <(find_pids)
  if (( ${#pids[@]} == 0 )); then
    printf '[%02d/%02d] NiOn/WebKit processes not found yet.\n' "$second" "$DURATION"
    sleep 1
    continue
  fi

  ss -Htnp state established 2>/dev/null > "$TMP_TCP" || true
  ss -Hunp 2>/dev/null > "$TMP_UDP" || true
  sample_seen=0

  for pid in "${pids[@]}"; do
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      [[ "$line" != *"pid=$pid,"* ]] && continue

      sample_seen=1
      observed=1
      peer="$(awk '{print $4}' <<<"$line")"

      case "$peer" in
        127.0.0.1:"$SOCKS_PORT"|\[::1\]:"$SOCKS_PORT")
          ;;
        *)
          printf 'ALERT: PID %s has established TCP peer %s\n' "$pid" "$peer" >&2
          printf '       %s\n' "$line" >&2
          violations=1
          ;;
      esac
    done < "$TMP_TCP"

    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      [[ "$line" != *"pid=$pid,"* ]] && continue

      # ss -u includes unconnected sockets with wildcard peers. Those are not
      # evidence of an outbound packet. A concrete non-loopback peer is.
      peer="$(awk '{print $5}' <<<"$line")"
      case "$peer" in
        ''|'*:*'|'0.0.0.0:*'|'[::]:*'|127.0.0.1:*|'[::1]':*)
          ;;
        *)
          sample_seen=1
          observed=1
          printf 'ALERT: PID %s has UDP peer %s (Tor SOCKS does not carry UDP)\n' "$pid" "$peer" >&2
          printf '       %s\n' "$line" >&2
          violations=1
          ;;
      esac
    done < "$TMP_UDP"
  done

  if (( sample_seen == 0 )); then
    printf '[%02d/%02d] no active NiOn/WebKit network socket visible in this sample.\n' "$second" "$DURATION"
  else
    printf '[%02d/%02d] sampled active NiOn/WebKit network socket(s).\n' "$second" "$DURATION"
  fi
  sleep 1
done

printf '\n'
if (( violations != 0 )); then
  echo 'RESULT: FAIL — a NiOn/WebKit network path was observed outside the expected Tor SOCKS endpoint.' >&2
  echo 'Do not treat this build as leak-audited until the connection is explained.' >&2
  exit 1
fi

if (( observed == 0 )); then
  echo 'RESULT: INCONCLUSIVE — no active NiOn/WebKit network socket was captured.'
  echo 'Run the audit again for longer while actively loading pages.'
  exit 3
fi

echo 'RESULT: PASS for this sample — captured TCP peers used Tor SOCKS and no outbound UDP peer was observed.'
echo 'This is a runtime sample, not a mathematical proof that every possible WebKit path is leak-free.'
