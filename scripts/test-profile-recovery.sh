#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[[ -x build/nion ]] || { echo 'build/nion is required. Run ./scripts/run-dev.sh once, close NiOn, then rerun this script.' >&2; exit 1; }
command -v timeout >/dev/null || { echo 'timeout (coreutils) is required.' >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export XDG_DATA_HOME="$TMP/data"
export XDG_CONFIG_HOME="$TMP/config"
export XDG_CACHE_HOME="$TMP/cache"
mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"
printf 'this is not a key file\n[broken' > "$XDG_DATA_HOME/nion/session.ini"
printf '[General]\nrestore-session=definitely\n' > "$XDG_CONFIG_HOME/nion/preferences.ini"
printf '[History]\ncount=99999999\n' > "$XDG_DATA_HOME/nion/downloads.ini"
printf 'not sqlite\n' > "$XDG_DATA_HOME/nion/cookies.sqlite"

# /bin/false makes Tor fail immediately so this test never reaches the public network.
set +e
NION_TOR_BINARY=/bin/false timeout --signal=TERM 4s ./build/nion >/dev/null 2>"$TMP/nion.log"
status=$?
set -e
# timeout may return 124; the point is that startup survives long enough to quarantine files.
if (( status != 0 && status != 124 && status != 143 )); then
  echo "NiOn exited unexpectedly during isolated recovery test (status $status)." >&2
  cat "$TMP/nion.log" >&2
  exit 1
fi

fail=0
for pattern in \
  "$XDG_DATA_HOME/nion/session.ini.corrupt-"'*' \
  "$XDG_CONFIG_HOME/nion/preferences.ini.corrupt-"'*' \
  "$XDG_DATA_HOME/nion/downloads.ini.corrupt-"'*' \
  "$XDG_DATA_HOME/nion/cookies.sqlite.corrupt-"'*'; do
  compgen -G "$pattern" >/dev/null || { echo "FAIL missing quarantine match: $pattern" >&2; fail=1; }
done

if (( fail )); then
  echo 'NiOn log:' >&2
  cat "$TMP/nion.log" >&2
  exit 1
fi

echo 'PASS isolated corrupt-profile recovery: malformed session/preferences/download history/cookie DB were quarantined.'
echo "Temporary test profile: $TMP (removed automatically)"
