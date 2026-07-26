#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src/main.c
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "1.4.0" ]] || fail "manifest version is not 1.4.0"
grep -Eq '^(Stable|Privacy Controls & Reliability development — Stage 2)$' release/manifest/RELEASE_STATUS || fail "Stage 2/final release status incompatible"
pass "1.4.0 Stage 2 manifest"

grep -Fq 'site_info_icon = gtk_image_new_from_icon_name' "$SRC" || fail "persistent Site Information icon missing"
grep -Fq 'gtk_menu_button_set_child(GTK_MENU_BUTTON(app->site_info_button), app->site_info_icon)' "$SRC" || fail "Site Information icon is not a stable MenuButton child"
grep -Fq 'gtk_image_set_from_icon_name(GTK_IMAGE(app->site_info_icon)' "$SRC" || fail "Site Information icon is not updated through GtkImage"
if grep -F 'gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button)' "$SRC" >/dev/null; then
  fail "Site Information still rebuilds GtkMenuButton icon child during refresh"
fi
pass "Site Information popover-stability fix"

grep -Fq 'clean-shutdown' "$SRC" || fail "clean shutdown marker missing"
grep -Fq 'crash_recovery_decision_pending' "$SRC" || fail "crash-recovery decision gate missing"
grep -Fq 'NiOn did not shut down normally.' "$SRC" || fail "unclean-shutdown recovery prompt missing"
grep -Fq 'Start Fresh' "$SRC" || fail "Start Fresh recovery choice missing"
grep -Fq 'Restore Tabs' "$SRC" || fail "Restore Tabs recovery choice missing"
grep -Fq '!app->tor_ready || !app->notebook || app->crash_recovery_decision_pending' "$SRC" || fail "pending recovery does not block automatic tab restore"
grep -Fq 'app->previous_shutdown_clean && state && *state' "$SRC" || fail "unclean recovery does not suppress opaque WebKit session state"
grep -Fq 'nion_quarantine_profile_file(app->session_file, "session")' "$SRC" || fail "corrupt session quarantine missing"
pass "crash recovery and crash-loop protection"

grep -Fq 'Browsing data manager' "$SRC" || fail "Browsing Data Manager UI missing"
grep -Fq 'Cookies and website storage' "$SRC" || fail "website-data selector missing"
grep -Fq 'Web cache' "$SRC" || fail "cache selector missing"
grep -Fq 'Saved site zoom levels' "$SRC" || fail "zoom selector missing"
grep -Fq 'Saved JavaScript site rules' "$SRC" || fail "JavaScript-rule selector missing"
grep -Fq 'Temporary site permissions' "$SRC" || fail "permission selector missing"
grep -Fq 'WEBKIT_WEBSITE_DATA_DISK_CACHE' "$SRC" || fail "disk-cache clear mask missing"
grep -Fq 'WEBKIT_WEBSITE_DATA_MEMORY_CACHE' "$SRC" || fail "memory-cache clear mask missing"
grep -Fq 'g_hash_table_remove_all(app->site_zoom)' "$SRC" || fail "zoom clear path missing"
grep -Fq 'g_hash_table_remove_all(app->site_javascript_disabled)' "$SRC" || fail "JavaScript-rule clear path missing"
grep -Fq 'nion_clear_all_temporary_permissions(app)' "$SRC" || fail "temporary permission clear path missing"
grep -Fq 'Clear Data for This Site…' "$SRC" || fail "existing per-site clear action regressed"
pass "selective Browsing Data Manager"

echo 'NION 1.4.0 STAGE 2 RECOVERY & DATA CONTROLS: PASS'
