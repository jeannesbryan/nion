#!/usr/bin/env bash
set -euo pipefail
DURATION="${1:-10}"
[[ "$DURATION" =~ ^[0-9]+$ ]] && (( DURATION >= 1 )) || { echo "Usage: $0 [seconds]" >&2; exit 2; }
command -v ss >/dev/null || { echo 'ss is required (iproute2).' >&2; exit 1; }
command -v ps >/dev/null || { echo 'ps is required.' >&2; exit 1; }

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
STATE="$DATA_HOME/nion/tor-runtime.ini"
[[ -r "$STATE" ]] || { echo "NiOn Tor runtime state not found: $STATE" >&2; echo 'Start NiOn and wait for Tor to connect first.' >&2; exit 1; }
pid="$(awk -F= '$1 == "pid" {print $2; exit}' "$STATE")"
port="$(awk -F= '$1 == "socks-port" {print $2; exit}' "$STATE")"
[[ "$pid" =~ ^[0-9]+$ && "$port" =~ ^[0-9]+$ ]] || { echo 'Invalid Tor runtime state.' >&2; exit 1; }
cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
case "$cmdline" in
  *tor*"$DATA_HOME/nion/tor"*) ;;
  *) echo "Refusing to signal PID $pid because it does not look like NiOn's Tor." >&2; exit 1 ;;
esac

echo "Stopping NiOn Tor PID $pid (former SOCKS 127.0.0.1:$port) to verify fail-closed behavior…"
kill -TERM "$pid"
for _ in {1..50}; do
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.1
done
kill -0 "$pid" 2>/dev/null && { echo 'Tor did not stop in time.' >&2; exit 1; }

find_pids() {
  ps -eo pid=,user=,args= | awk -v u="$(id -un)" '
    $2 == u && ($0 ~ /(^|\/)nion([[:space:]]|$)/ ||
                $0 ~ /WebKitNetworkProcess/ ||
                $0 ~ /WebKitWebProcess/ ||
                $0 ~ /WebKitGPUProcess/) { print $1 }
  ' | sort -u
}

violations=0
for ((second=1; second<=DURATION; second++)); do
  mapfile -t pids < <(find_pids)
  sockets="$(ss -Htnp state established 2>/dev/null || true)"
  udp="$(ss -Hunp 2>/dev/null || true)"
  for p in "${pids[@]}"; do
    while IFS= read -r line; do
      [[ "$line" == *"pid=$p,"* ]] || continue
      peer="$(awk '{print $4}' <<<"$line")"
      echo "FAIL: PID $p still has established TCP peer $peer after Tor stopped" >&2
      echo "      $line" >&2
      violations=1
    done <<<"$sockets"
    while IFS= read -r line; do
      [[ "$line" == *"pid=$p,"* ]] || continue
      peer="$(awk '{print $5}' <<<"$line")"
      case "$peer" in
        ''|'*:*'|'0.0.0.0:*'|'[::]:*') ;;
        *)
          echo "FAIL: PID $p has UDP peer $peer after Tor stopped" >&2
          echo "      $line" >&2
          violations=1
          ;;
      esac
    done <<<"$udp"
  done
  printf '[%02d/%02d] sampled after Tor shutdown\n' "$second" "$DURATION"
  sleep 1
done

if (( violations )); then
  echo 'RESULT: FAIL — network activity remained after NiOn Tor stopped.' >&2
  exit 1
fi

echo 'RESULT: PASS — no established NiOn/WebKit TCP connection or concrete UDP peer was observed after Tor stopped.'
echo 'NiOn should now show TOR ERROR and block reload/navigation. Restart NiOn after this destructive test.'
