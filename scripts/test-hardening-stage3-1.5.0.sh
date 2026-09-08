#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src
fail(){ echo "1.5.0 FINAL HARDENING: FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ -s release/manifest/NION_VERSION ]] || fail "canonical NiOn version manifest missing"
version="$(tr -d '\r\n' < release/manifest/NION_VERSION)"
python3 - "$version" <<'PYV' || fail "current version predates the 1.5.0 hardening baseline"
import sys
parts=lambda v: tuple(int(x) for x in v.split('.'))
raise SystemExit(0 if parts(sys.argv[1]) >= parts('1.5.0') else 1)
PYV
pass "1.5.0 hardening baseline retained"

grep -rFq 'app->site_info_button = gtk_button_new();' "$SRC" || fail "Site Information plain button missing"
grep -rFq 'app->site_info_window = gtk_window_new();' "$SRC" || fail "Site Information transient window missing"
grep -rFq 'GtkWidget *site_info_scroller = gtk_scrolled_window_new();' "$SRC" || fail "Site Information scroller missing"
grep -rFq 'GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC' "$SRC" || fail "vertical automatic scrolling missing"
grep -rFq 'gtk_scrolled_window_set_max_content_height' "$SRC" || fail "Site Information bounded height missing"
grep -rFq 'gtk_window_set_default_size(GTK_WINDOW(app->site_info_window), 420, 500);' "$SRC" || fail "compact Site Information default size missing"
grep -rFq 'gtk_window_set_resizable(GTK_WINDOW(app->site_info_window), TRUE);' "$SRC" || fail "Site Information is not resizable"
grep -rFq 'gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(site_info_scroller), site_info_box);' "$SRC" || fail "Site Information content not inside scroller"
if grep -rE 'site_info_(popover|button).*gtk_(popover|menu_button)|gtk_(popover|menu_button).*site_info_' "$SRC" >/dev/null; then
  fail "legacy Site Information popover/menu-button path returned"
fi
pass "compact scrollable Site Information window"

grep -rFq 'webkit_network_session_set_itp_enabled(app->network_session, TRUE)' "$SRC" || fail "ITP default missing"
grep -rFq 'WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND' "$SRC" || fail "autoplay protection missing"
grep -rFq 'nion_content_blocking_enabled_for_uri' "$SRC" || fail "content blocking state missing"
grep -rFq 'socks://127.0.0.1:9' "$SRC" || fail "Tor fail-closed dead proxy missing"
grep -rFq 'webkit_settings_set_enable_webrtc(settings, FALSE)' "$SRC" || fail "WebRTC hardening missing"
pass "tracking/media/content/fail-closed invariants"

printf 'NION 1.5.0 FINAL HARDENING: PASS\n'
