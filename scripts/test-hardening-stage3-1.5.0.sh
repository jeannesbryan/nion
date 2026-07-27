#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src/main.c
fail(){ echo "1.5.0 FINAL HARDENING: FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ -s release/manifest/NION_VERSION ]] || fail "canonical NiOn version manifest missing"
[[ "$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)" == "Stable" ]] || fail "release status is not Stable"
[[ "$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)" == "stable" ]] || fail "AppStream type is not stable"
grep -Eq '\*\*Stable release: 1\.[5-9]\.[0-9]+\*\*' README.md || fail "README stable marker missing"
pass "stable release metadata"

grep -Fq 'app->site_info_button = gtk_button_new();' "$SRC" || fail "Site Information plain button missing"
grep -Fq 'app->site_info_window = gtk_window_new();' "$SRC" || fail "Site Information transient window missing"
grep -Fq 'GtkWidget *site_info_scroller = gtk_scrolled_window_new();' "$SRC" || fail "Site Information scroller missing"
grep -Fq 'GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC' "$SRC" || fail "vertical automatic scrolling missing"
grep -Fq 'gtk_scrolled_window_set_max_content_height' "$SRC" || fail "Site Information bounded height missing"
grep -Fq 'gtk_window_set_default_size(GTK_WINDOW(app->site_info_window), 420, 500);' "$SRC" || fail "compact Site Information default size missing"
grep -Fq 'gtk_window_set_resizable(GTK_WINDOW(app->site_info_window), TRUE);' "$SRC" || fail "Site Information is not resizable"
grep -Fq 'gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(site_info_scroller), site_info_box);' "$SRC" || fail "Site Information content not inside scroller"
if grep -E 'site_info_(popover|button).*gtk_(popover|menu_button)|gtk_(popover|menu_button).*site_info_' "$SRC" >/dev/null; then
  fail "legacy Site Information popover/menu-button path returned"
fi
pass "compact scrollable Site Information window"

grep -Fq 'webkit_network_session_set_itp_enabled(app->network_session, TRUE)' "$SRC" || fail "ITP default missing"
grep -Fq 'WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND' "$SRC" || fail "autoplay protection missing"
grep -Fq 'nion_content_blocking_enabled_for_uri' "$SRC" || fail "content blocking state missing"
grep -Fq 'socks://127.0.0.1:9' "$SRC" || fail "Tor fail-closed dead proxy missing"
grep -Fq 'webkit_settings_set_enable_webrtc(settings, FALSE)' "$SRC" || fail "WebRTC hardening missing"
pass "tracking/media/content/fail-closed invariants"

printf 'NION 1.5.0 FINAL HARDENING: PASS\n'
