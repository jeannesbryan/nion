#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src/main.c
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

version="$(tr -d '\r\n' < release/manifest/NION_VERSION)"
python3 - "$version" <<'PYV' || fail "current version predates the 1.4.0 Site Controls baseline"
import sys
parts=lambda v: tuple(int(x) for x in v.split('.'))
raise SystemExit(0 if parts(sys.argv[1]) >= parts('1.4.0') else 1)
PYV
pass "1.4.0 Site Controls feature baseline"

grep -Fq 'webkit_settings_set_enable_webrtc(settings, FALSE);' "$SRC" || fail "WebRTC peer connections not disabled"
grep -Fq 'webkit_settings_set_enable_media_stream(settings, TRUE);' "$SRC" || fail "MediaStream permission path not enabled"
grep -Fq 'WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)' "$SRC" || fail "geolocation permission gate missing"
grep -Fq 'WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)' "$SRC" || fail "notification permission gate missing"
grep -Fq 'WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)' "$SRC" || fail "camera/microphone permission gate missing"
grep -Fq 'webkit_user_media_permission_is_for_display_device(media)' "$SRC" || fail "display capture hard-block missing"
grep -Fq 'Allow temporarily' "$SRC" || fail "temporary permission prompt missing"
grep -Fq 'same_origin' "$SRC" || fail "stale-navigation origin recheck missing"
grep -Fq 'tab->app->tor_ready' "$SRC" || fail "permission allow is not Tor-ready gated"
grep -Fq 'temporary_permissions' "$SRC" || fail "temporary permission memory store missing"
pass "temporary permission gate"

grep -Fq 'site-javascript.ini' "$SRC" || fail "site JavaScript profile missing"
grep -Fq 'NION_MAX_SITE_JAVASCRIPT_ENTRIES 2048' "$SRC" || fail "site JavaScript entry bound missing"
grep -Fq 'app->is_private || !app->site_javascript_file' "$SRC" || fail "private site-JavaScript persistence guard missing"
grep -Fq 'nion_write_key_file_atomic(key_file, app->site_javascript_file)' "$SRC" || fail "atomic site-JavaScript write missing"
grep -Fq 'nion_apply_site_javascript(tab, uri)' "$SRC" || fail "pre-navigation JavaScript application missing"
grep -Fq 'nion_apply_site_javascript(tab, committed_uri)' "$SRC" || fail "redirect destination JavaScript application missing"
grep -Fq 'site_info_javascript_switch' "$SRC" || fail "Site Information JavaScript switch missing"
grep -Fq 'site_info_permissions_reset_button' "$SRC" || fail "temporary permission reset UI missing"
pass "per-site JavaScript and Site Information controls"

grep -Fq '"related-view", related_view' "$SRC" || fail "related popup view fix missing"
grep -Fq 'WebKitSettings *settings = webkit_settings_new();' "$SRC" || fail "popup does not get independent mutable settings"
pass "popup settings isolation"

grep -Fq 'set_camera_capture_state' "$SRC" || fail "camera capture reset missing"
grep -Fq 'set_microphone_capture_state' "$SRC" || fail "microphone capture reset missing"
grep -Fq 'g_strcmp0(candidate_origin, origin) == 0' "$SRC" || fail "origin-wide capture reset missing"
grep -Fq 'g_hash_table_remove_all(app->temporary_permissions)' "$SRC" || fail "Tor-failure temporary permission purge missing"
pass "permission reset revokes matching captures"

echo 'NION 1.4.0+ SITE CONTROLS REGRESSION: PASS'
