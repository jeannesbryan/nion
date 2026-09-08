#!/usr/bin/env bash
set -euo pipefail
SRC="${1:-src}"
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

# Private downloads use the same Tor-proxied WebKit download pipeline, but history must remain memory-only.
grep -rq 'G_CALLBACK(on_download_started), app' "$SRC" || fail "shared Tor-routed download handler missing"
! grep -rq 'PRIVATE DOWNLOADS ARE DISABLED UNTIL STAGE 3' "$SRC" || fail "Stage 2 private-download block still present"
grep -rq 'app->is_private || !app->downloads_file' "$SRC" || fail "private download history write guard missing"
grep -rq 'app->is_private || !app->downloads_file || !app->downloads_list' "$SRC" || fail "private download history load guard missing"
grep -rq 'PRIVATE DOWNLOAD STARTED (EPHEMERAL HISTORY)' "$SRC" || fail "private download status missing"
grep -rq 'app->is_private ? "Private Downloads" : "Downloads"' "$SRC" || fail "private downloads UI missing"
grep -rq 'History exists only while this Private Window is open' "$SRC" || fail "private download lifetime disclosure missing"
grep -rq 'if (!item->app->is_private)' "$SRC" || fail "private desktop notification suppression missing"
grep -rq 'nion_private_cleanup_partial_downloads' "$SRC" || fail "private partial-download cleanup missing"
grep -rq 'nion_cancel_active_downloads(app);' "$SRC" || fail "private active-download cancellation missing"
grep -rq 'nion_cancel_active_downloads(private_app);' "$SRC" || fail "private Tor-failure download cancellation missing"
pass "private downloads use NiOn/WebKit Tor path"
pass "private download rows/history are memory-only"
pass "private desktop notifications suppressed"
pass "active private download cleanup on close/Tor failure"

# Pinned tabs have an explicit visual pin marker.
grep -rq 'GtkWidget \*pin_indicator;' "$SRC" || fail "pinned tab indicator field missing"
grep -rq 'gtk_label_new("📌")' "$SRC" || fail "visible pin icon missing"
grep -rq 'gtk_widget_set_visible(tab->pin_indicator, tab->pinned)' "$SRC" || fail "pin icon visibility does not follow pinned state"
pass "pinned tab icon"

# Direct and bulk closes in Private require confirmation.
grep -rq 'Close this private tab?' "$SRC" || fail "private single-tab confirmation missing"
grep -rq 'NION_PRIVATE_CLOSE_OTHERS' "$SRC" || fail "private Close Other Tabs confirmation missing"
grep -rq 'NION_PRIVATE_CLOSE_RIGHT' "$SRC" || fail "private Close Tabs to the Right confirmation missing"
grep -rq 'if (tab->app->is_private)' "$SRC" || fail "private close interception missing"
grep -rq 'gtk_button_new_with_label("No")' "$SRC" || fail "private close No action missing"
grep -rq 'gtk_button_new_with_label("Yes")' "$SRC" || fail "private close Yes action missing"
pass "private tab close confirmation"

echo "NION 1.3.0 STAGE 3 STATIC CHECK: PASS"
