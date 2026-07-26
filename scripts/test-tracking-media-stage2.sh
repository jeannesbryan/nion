#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src/main.c
fail(){ echo "TRACKING/MEDIA STAGE 2: FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "1.5.0" ]] || fail "manifest version is not 1.5.0"
status="$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)"
[[ "$status" == 'Tracking & Content Protection development — Stage 2' || "$status" == 'Stable' ]] || fail "Stage 2/final release status missing"

grep -Fq 'webkit_network_session_set_itp_enabled(app->network_session, TRUE)' "$SRC" || fail "WebKit ITP default enable path missing"
grep -Fq 'WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS' "$SRC" || fail "ITP-compatible cookie policy missing"
grep -Fq 'webkit_network_session_set_itp_enabled(app->network_session, FALSE)' "$SRC" || fail "strict-cookie alternative does not disable ITP"
grep -Fq 'Strictly block third-party cookies (disables WebKit ITP)' "$SRC" || fail "strict-cookie preference wording missing"
pass "ITP default with explicit strict-cookie alternative"

grep -Fq 'WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND' "$SRC" || fail "default audible-autoplay protection missing"
grep -Fq 'WEBKIT_AUTOPLAY_ALLOW' "$SRC" || fail "per-site autoplay allow policy missing"
grep -Fq 'webkit_website_policies_new_with_policies("autoplay", policy, NULL)' "$SRC" || fail "native WebKit autoplay policies missing"
grep -Fq 'webkit_policy_decision_use_with_policies' "$SRC" || fail "navigation policy is not applied per origin"
grep -Fq 'autoplay.ini' "$SRC" || fail "normal autoplay exception store missing"
grep -Fq 'NION_MAX_AUTOPLAY_EXCEPTIONS 2048' "$SRC" || fail "autoplay exception bound missing"
grep -Fq 'app->is_private || !app->autoplay_file' "$SRC" || fail "private autoplay persistence guard missing"
grep -Fq 'Saved autoplay exceptions' "$SRC" || fail "Browsing Data autoplay selector missing"
grep -Fq 'Private autoplay exceptions' "$SRC" || fail "Private Browsing Data autoplay selector missing"
pass "autoplay policy and normal/private exception handling"

# Blink regression: no GtkPopover or GtkMenuButton may be used for Site Information.
grep -Fq 'app->site_info_button = gtk_button_new();' "$SRC" || fail "Site Information is not a plain GtkButton"
grep -Fq 'app->site_info_window = gtk_window_new();' "$SRC" || fail "transient Site Information window missing"
grep -Fq 'gtk_window_set_transient_for(GTK_WINDOW(app->site_info_window)' "$SRC" || fail "Site Information window is not transient"
grep -Fq 'g_signal_connect(app->site_info_button, "clicked"' "$SRC" || fail "Site Information click handler missing"
if grep -E 'site_info_(popover|button).*gtk_(popover|menu_button)|gtk_(popover|menu_button).*site_info_' "$SRC" >/dev/null; then
    fail "Site Information still uses GtkPopover/GtkMenuButton compositing path"
fi
pass "Site Information popover/compositing path removed"

printf 'NION 1.5.0 STAGE 2 TRACKING & MEDIA: PASS\n'
