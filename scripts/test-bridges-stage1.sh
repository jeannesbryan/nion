#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

# Bridges / pluggable transports (NiOn 2.2.0, Stage 1).
#
# The interesting part of this feature is what it refuses to do, so the suite
# combines static wiring checks with the binary's own validator self-check
# (NION_BRIDGE_SELFCHECK=1), which exercises the real parser and the real torrc
# builder rather than grepping around them.

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }
warn() { echo "WARN: $*"; }

[[ -s src/bridge.c && -s src/bridge.h ]] || fail "bridge module missing"
grep -Fq "'src/bridge.c'" meson.build || fail "src/bridge.c is not registered in meson.build (source-list switch)"
pass "bridge module present and registered"

# --- Fail-closed contract -----------------------------------------------------
grep -Fq 'UseBridges 1' src/bridge.c || fail "bridge torrc fragment does not enable UseBridges"
grep -Fq 'ClientTransportPlugin %s exec %s' src/bridge.c || fail "pluggable transports are not declared"
grep -Fq 'Bridge %s' src/bridge.c || fail "bridge lines are not written to the torrc"
grep -Fq 'Bridge mode is enabled but no bridge line is configured' src/bridge.c || fail "empty-list fail-closed guard missing"
grep -Fq 'Bundled Tor transport for' src/bridge.c || fail "missing-transport fail-closed guard missing"
grep -Fq 'Bridge configuration refused' src/tor-core.c || fail "tor-core does not refuse a broken bridge configuration"
grep -Fq 'nion_bridge_torrc_fragment' src/tor-core.c || fail "tor-core does not consume the bridge torrc fragment"
pass "fail-closed bridge contract wired into the Tor start-up path"

# A bridge line must never be able to become a second torrc directive.
grep -Fq '0x20' src/bridge.c || fail "control-character rejection missing from the bridge validator"
grep -Fq 'nion_bridge_reserved_keywords' src/bridge.c || fail "torrc keyword blocklist missing from the bridge validator"
grep -Fq 'ClientTransportPlugin' src/bridge.c || fail "reserved keyword set does not cover ClientTransportPlugin"
pass "bridge line validation rejects torrc injection surfaces"

# --- No new control-port surface ---------------------------------------------
# src/bridge.c unavoidably *mentions* control-port keywords: its forbidden-torrc
# blocklist and its self-check cases both have to name them. What must not exist
# is an emitted directive, so scan every other source literally, and let the
# self-check below prove the generated torrc behaviourally.
if grep -REqs --exclude='*.md' --exclude='test-*.sh' --exclude='bridge.c' \
     'ControlPort [0-9a-zA-Z]|--ControlPort|HashedControlPassword|CookieAuthentication' src packaging; then
  fail "bridge support must not introduce a Tor ControlPort surface"
fi
pass "no Tor ControlPort surface added outside the validator's own blocklist"

# --- Persistence hygiene ------------------------------------------------------
grep -Fq 'NION_MAX_BRIDGE_FILE_BYTES' src/bridge.c || fail "bridge file size bound missing"
grep -Fq 'NION_MAX_BRIDGES' src/bridge.c || fail "bridge entry bound missing"
grep -Fq 'NION_MAX_BRIDGE_LINE_CHARS' src/bridge.c || fail "bridge line length bound missing"
grep -Fq 'nion_quarantine_profile_file(app->bridge_file' src/bridge.c || fail "corrupt bridge file is not quarantined"
grep -Fq 'nion_write_key_file_atomic(key_file, app->bridge_file)' src/bridge.c || fail "bridge file is not written atomically"
grep -Fq 'bridges.ini' src/app.c || fail "bridge profile path missing"
grep -Fq 'app->is_private || !app->bridge_file' src/bridge.c || fail "private windows must not write bridge state"
pass "bridge persistence is bounded, atomic, quarantined and never written by private windows"

# --- UI / action wiring -------------------------------------------------------
grep -Fq '{ "bridges", action_bridges' src/main.c || fail "bridges action is not registered"
grep -Fq 'win.bridges' src/ui.c || fail "bridges menu entry missing"
grep -Fq 'nion_load_bridges(app)' src/main.c || fail "bridges are not loaded at start-up"
grep -Fq 'nion_restart_tor_for_bridge_change' src/main.c || fail "bridge restart callback is not registered"
grep -Fq 'nion_restart_tor_for_bridge_change' src/app.c || fail "bridge restart implementation missing"
pass "menu, action, start-up load and restart wiring present"

# The bridge restart must not behave like New Identity (no identity purge).
if awk '/nion_restart_tor_for_bridge_change\(NionApp \*app\)/,/^}/' src/app.c |
     grep -qE 'nion_start_identity_data_purge|nion_wipe_all_site_rules|nion_rotate_tor_guard_state'; then
  fail "bridge restart must not purge the browsing identity"
fi
pass "bridge restart is a routing change only (no identity purge)"

# --- Documentation surfaces ---------------------------------------------------
grep -Fq 'Bridges (censorship circumvention)' README.md || fail "README has no bridge section"
grep -Fq 'Bridges (Censorship Circumvention)' README.md || fail "README does not name the menu entry"
grep -Fq 'Bridges (censorship circumvention)' TESTING.md || fail "TESTING.md has no bridge validation steps"
pass "user-facing documentation covers bridges"

# --- Real validator -----------------------------------------------------------
binary=""
for candidate in build/nion build-appimage/nion; do
  [[ -x "$candidate" ]] && binary="$candidate" && break
done

if [[ -z "$binary" ]]; then
  warn "no native binary found (build/nion); run ./scripts/run-dev.sh or meson compile -C build to exercise the validator self-check"
else
  output="$(NION_BRIDGE_SELFCHECK=1 "$binary" 2>&1)" || {
    echo "$output" >&2
    fail "bridge validator self-check failed"
  }
  grep -Fq 'validator' <<<"$output" || fail "bridge self-check produced no validator report"
  grep -Fq 'fail-closed' <<<"$output" || fail "bridge self-check did not cover the fail-closed path"
  echo "$output" | sed 's/^/PASS: /'
fi

echo "NION BRIDGES STAGE 1 CHECK: PASS"
