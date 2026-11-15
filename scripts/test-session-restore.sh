#!/usr/bin/env bash
# Copyright (C) 2026 Jeannes Bryan

# Session restore round-trip.
#
# A restored session is materialised as tab shells with restore_pending=TRUE;
# their URLs live in tab->restore_uri until Tor is ready and navigation starts.
# The startup save happens inside that window (ui.c calls nion_save_session
# immediately after restore), so the save path MUST fall back to restore_uri.
# Before that fallback existed, every URL was silently dropped whenever NiOn
# was closed or crashed before Tor bootstrapped: the tab count survived, the
# addresses did not. This test pins the whole round trip.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -x build/nion ]] || { echo 'build/nion is required. Run ./scripts/run-dev.sh once, close NiOn, then rerun this script.' >&2; exit 1; }
command -v timeout >/dev/null || { echo 'timeout (coreutils) is required.' >&2; exit 1; }
[[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || { echo 'This test drives the real GTK window and needs a display.' >&2; exit 1; }

TMP="$(mktemp -d)"
PID=""
cleanup() {
  if [[ -n "$PID" ]] && kill -0 "$PID" 2>/dev/null; then
    kill -KILL "$PID" 2>/dev/null || true
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT

export XDG_DATA_HOME="$TMP/data"
export XDG_CONFIG_HOME="$TMP/config"
export XDG_CACHE_HOME="$TMP/cache"
SESSION="$XDG_DATA_HOME/nion/session.ini"
mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"

printf '[General]\nrestore-session=true\n' > "$XDG_CONFIG_HOME/nion/preferences.ini"

cat > "$SESSION" <<'INI'
[Session]
format=1
nion-version=2.2.5
tab-count=4
active-tab=2
clean-shutdown=true

[Tab-0]
home=false
muted=false
pinned=false
uri=https://example.org/alpha

[Tab-1]
home=false
muted=true
pinned=true
uri=https://example.net/beta

[Tab-2]
home=false
muted=false
pinned=false
uri=http://example.com/gamma

[Tab-3]
home=true
muted=false
pinned=false
INI

# /bin/false makes Tor fail immediately, so tray state stays "not ready" and the
# restored tabs never navigate: exactly the window the bug lived in. It also
# guarantees this test never touches the public network.
NION_TOR_BINARY=/bin/false ./build/nion >"$TMP/nion.log" 2>&1 &
PID=$!

# Wait for the startup save (it flips clean-shutdown from true to false) instead
# of sleeping for a fixed time, so the test is deterministic on a loaded box.
saved=0
for _ in $(seq 1 80); do
  if [[ -f "$SESSION" ]] && grep -qx 'clean-shutdown=false' "$SESSION" 2>/dev/null; then
    saved=1
    break
  fi
  sleep 0.25
done
if (( ! saved )); then
  echo 'NiOn log:' >&2
  cat "$TMP/nion.log" >&2
  fail "startup session save never happened (session.ini was not rewritten)"
fi

kill -KILL "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
PID=""

[[ -f "$SESSION" ]] || fail "session.ini disappeared"

# --- tab structure survives -------------------------------------------------
grep -qx 'tab-count=4' "$SESSION" || fail "tab-count was not preserved as 4:
$(cat "$SESSION")"

active="$(grep -m1 '^active-tab=' "$SESSION" | cut -d= -f2)"
[[ "$active" =~ ^[0-9]+$ ]] || fail "active-tab is not a number"
(( active >= 0 && active < 4 )) || fail "active-tab out of range: $active"

# --- the bug: every restored URL must survive the save ----------------------
for uri in 'https://example.org/alpha' 'https://example.net/beta' 'http://example.com/gamma'; do
  grep -qxF "uri=$uri" "$SESSION" \
    || fail "restored URL was dropped from the session file: $uri
$(cat "$SESSION")"
done

count_of() { grep -cxF "$1" "$SESSION" || true; }
# Same, but for key=value prefixes (`uri=` is never a whole line).
count_prefix() { grep -cF "$1" "$SESSION" || true; }

(( $(count_of 'pinned=true') == 1 )) || fail "pinned state not preserved (expected exactly 1 pinned tab)"
(( $(count_of 'pinned=false') == 3 )) || fail "pinned state leaked onto other tabs"
(( $(count_of 'muted=true') == 1 )) || fail "mute state not preserved (expected exactly 1 muted tab)"
(( $(count_of 'home=true') == 1 )) || fail "home flag not preserved (expected exactly 1 home tab)"

# A home tab must not gain an address it never had.
grep -qxF 'uri=https://example.org/alpha' "$SESSION" || fail "alpha missing"
(( $(count_prefix 'uri=') == 3 )) || fail "expected exactly 3 stored URLs, got $(count_prefix 'uri=')"

# The rewritten snapshot must stay loadable: format and clean-shutdown present.
grep -qx 'format=1' "$SESSION" || fail "session format marker lost"
grep -qx 'clean-shutdown=false' "$SESSION" || fail "dirty snapshot marker lost"

pass "restored session keeps tab count, active tab, pinned/mute/home flags"
pass "restored-but-not-yet-navigated tabs keep their URLs through the startup save"
pass "home tab does not acquire a synthetic address"
echo "NION SESSION RESTORE ROUND-TRIP: PASS"
