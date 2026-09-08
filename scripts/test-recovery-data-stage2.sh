#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

version="$(tr -d '\r\n' < release/manifest/NION_VERSION)"
python3 - "$version" <<'PYV' || fail "current version predates the 1.4.0 Recovery/Data baseline"
import sys
parts=lambda v: tuple(int(x) for x in v.split('.'))
raise SystemExit(0 if parts(sys.argv[1]) >= parts('1.4.0') else 1)
PYV
pass "1.4.0 Recovery/Data feature baseline"

grep -rFq 'site_info_icon = gtk_image_new_from_icon_name' "$SRC" || fail "persistent Site Information icon missing"
grep -rFq 'gtk_image_set_from_icon_name(GTK_IMAGE(app->site_info_icon)' "$SRC" || fail "Site Information icon is not updated through GtkImage"
if grep -rF 'gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button)' "$SRC" >/dev/null; then
  fail "Site Information still rebuilds GtkMenuButton icon child during refresh"
fi
if grep -rFq 'app->site_info_window = gtk_window_new();' "$SRC"; then
  grep -rFq 'gtk_button_set_child(GTK_BUTTON(app->site_info_button), app->site_info_icon)' "$SRC" || fail "transient Site Information button lost its stable icon child"
else
  grep -rFq 'gtk_menu_button_set_child(GTK_MENU_BUTTON(app->site_info_button), app->site_info_icon)' "$SRC" || fail "Site Information icon is not a stable button child"
fi
pass "Site Information stability fix"

grep -rFq 'clean-shutdown' "$SRC" || fail "clean shutdown marker missing"
grep -rFq 'crash_recovery_decision_pending' "$SRC" || fail "crash-recovery decision gate missing"
grep -rFq 'NiOn did not shut down normally.' "$SRC" || fail "unclean-shutdown recovery prompt missing"
grep -rFq 'Start Fresh' "$SRC" || fail "Start Fresh recovery choice missing"
grep -rFq 'Restore Tabs' "$SRC" || fail "Restore Tabs recovery choice missing"
grep -rFq '!app->tor_ready || !app->notebook || app->crash_recovery_decision_pending' "$SRC" || fail "pending recovery does not block automatic tab restore"
grep -rFq 'app->previous_shutdown_clean && state && *state' "$SRC" || fail "unclean recovery does not suppress opaque WebKit session state"
grep -rFq 'nion_quarantine_profile_file(app->session_file, "session")' "$SRC" || fail "corrupt session quarantine missing"
pass "crash recovery and crash-loop protection"

grep -rFq 'Browsing data manager' "$SRC" || fail "Browsing Data Manager UI missing"
grep -rFq 'Cookies and website storage' "$SRC" || fail "website-data selector missing"
grep -rFq 'Web cache' "$SRC" || fail "cache selector missing"
grep -rFq 'Saved site zoom levels' "$SRC" || fail "zoom selector missing"
grep -rFq 'Saved JavaScript site rules' "$SRC" || fail "JavaScript-rule selector missing"
grep -rFq 'Temporary site permissions' "$SRC" || fail "permission selector missing"
grep -rFq 'WEBKIT_WEBSITE_DATA_DISK_CACHE' "$SRC" || fail "disk-cache clear mask missing"
grep -rFq 'WEBKIT_WEBSITE_DATA_MEMORY_CACHE' "$SRC" || fail "memory-cache clear mask missing"
grep -rFq 'g_hash_table_remove_all(app->site_zoom)' "$SRC" || fail "zoom clear path missing"
grep -rFq 'g_hash_table_remove_all(app->site_javascript_disabled)' "$SRC" || fail "JavaScript-rule clear path missing"
grep -rFq 'nion_clear_all_temporary_permissions(app)' "$SRC" || fail "temporary permission clear path missing"
grep -rFq 'Clear Data for This Site…' "$SRC" || fail "existing per-site clear action regressed"
pass "selective Browsing Data Manager"

echo 'NION 1.4.0+ RECOVERY & DATA CONTROLS REGRESSION: PASS'
