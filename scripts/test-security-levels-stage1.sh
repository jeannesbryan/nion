#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "1.7.0 SECURITY LEVELS STAGE 1: FAIL: $*" >&2; exit 1; }
SRC=src/main.c

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "1.7.0" ]] || fail 'manifest version is not 1.7.0'
release_type="$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)"
release_status="$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)"
[[ "$release_type" == "development" || "$release_type" == "stable" ]] || fail 'invalid AppStream release type'
[[ "$release_status" == 'Stable' || "$release_status" == Security\ Levels\ \&\ Escape\ Guards\ development* ]] || fail '1.7.0 release status missing'

grep -Fq 'NION_SECURITY_STANDARD' "$SRC" || fail 'Standard level missing'
grep -Fq 'NION_SECURITY_SAFER' "$SRC" || fail 'Safer level missing'
grep -Fq 'NION_SECURITY_SAFEST' "$SRC" || fail 'Safest level missing'
grep -Fq '"security-level"' "$SRC" || fail 'security level preference persistence missing'
grep -Fq 'nion-security-dropdown' "$SRC" || fail 'security level Preferences dropdown missing'
grep -Fq 'nion_security_level_label(app->security_level)' "$SRC" || fail 'security level not exposed in Site Information/audit'

grep -Fq 'webkit_settings_set_enable_webrtc(settings, FALSE)' "$SRC" || fail 'WebRTC baseline must remain blocked'
grep -Fq 'webkit_settings_set_enable_webgl(settings, FALSE)' "$SRC" || fail 'WebGL baseline must remain blocked'
grep -Fq 'webkit_settings_set_enable_webaudio(settings, FALSE)' "$SRC" || fail 'WebAudio baseline must remain blocked'
grep -Fq 'webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE)' "$SRC" || fail 'automatic JS popup baseline must remain blocked'
grep -Fq 'level != NION_SECURITY_SAFEST' "$SRC" || fail 'Safest JavaScript/MediaStream policy missing'
grep -Fq '"enable-fullscreen"' "$SRC" || fail 'security-level fullscreen policy missing'
grep -Fq 'WEBKIT_AUTOPLAY_DENY' "$SRC" || fail 'Safer/Safest autoplay deny policy missing'

grep -Fq '#define NION_SITE_JAVASCRIPT_FORMAT 2' "$SRC" || fail 'site JavaScript format v2 missing'
grep -Fq 'site_javascript_enabled' "$SRC" || fail 'Safest explicit JavaScript enable exceptions missing'
grep -Fq '(format != 1 && format != NION_SITE_JAVASCRIPT_FORMAT)' "$SRC" || fail 'site JavaScript v1 migration compatibility missing'
grep -Fq 'g_hash_table_remove(app->site_javascript_enabled, site_key)' "$SRC" || fail 'Forget This Site does not clear Safest enable exception'
grep -Fq 'g_hash_table_remove_all(app->site_javascript_enabled)' "$SRC" || fail 'Browsing Data does not clear Safest enable exceptions'

grep -Fq 'app->security_level = owner->security_level' "$SRC" || fail 'Private Window does not inherit Security Level'
grep -Fq 'g_hash_table_remove_all(app->temporary_permissions)' "$SRC" || fail 'level change does not revoke temporary permissions'
grep -Fq 'webkit_web_view_set_camera_capture_state' "$SRC" || fail 'level change does not stop camera capture'
grep -Fq 'webkit_web_view_set_microphone_capture_state' "$SRC" || fail 'level change does not stop microphone capture'

grep -Eq '\\*\\*(Current development|Stable release): 1\.7\.0' README.md || fail 'README 1.7.0 marker missing'
grep -Fq '### Security Levels' README.md || fail 'README Security Levels section missing'
grep -Fq 'Stage 2 — Escape Guards' README.md || fail 'README Stage 2 handoff missing'

printf 'NION 1.7.0 SECURITY LEVELS STAGE 1: PASS\n'
