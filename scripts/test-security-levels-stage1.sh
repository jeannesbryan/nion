#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "2.1.0 SECURITY LEVELS STAGE 1: FAIL: $*" >&2; exit 1; }
SRC=src

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "2.1.0" ]] || fail 'manifest version is not 2.1.0'
release_type="$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)"
release_status="$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)"
[[ "$release_type" == "development" || "$release_type" == "stable" ]] || fail 'invalid AppStream release type'
[[ "$release_status" == 'Stable' || "$release_status" == Security\ Levels\ \&\ Escape\ Guards\ development* ]] || fail '2.1.0 release status missing'

grep -rFq 'NION_SECURITY_STANDARD' "$SRC" || fail 'Standard level missing'
grep -rFq 'NION_SECURITY_SAFER' "$SRC" || fail 'Safer level missing'
grep -rFq 'NION_SECURITY_SAFEST' "$SRC" || fail 'Safest level missing'
grep -rFq '"security-level"' "$SRC" || fail 'security level preference persistence missing'
grep -rFq 'nion-security-dropdown' "$SRC" || fail 'security level Preferences dropdown missing'
grep -rFq 'nion_security_level_label(app->security_level)' "$SRC" || fail 'security level not exposed in Site Information/audit'

grep -rFq 'webkit_settings_set_enable_webrtc(settings, FALSE)' "$SRC" || fail 'WebRTC baseline must remain blocked'
grep -rFq 'webkit_settings_set_enable_webgl(settings, FALSE)' "$SRC" || fail 'WebGL baseline must remain blocked'
grep -rFq 'webkit_settings_set_enable_webaudio(settings, FALSE)' "$SRC" || fail 'WebAudio baseline must remain blocked'
grep -rFq 'webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE)' "$SRC" || fail 'automatic JS popup baseline must remain blocked'
grep -rFq 'level != NION_SECURITY_SAFEST' "$SRC" || fail 'Safest JavaScript/MediaStream policy missing'
grep -rFq '"enable-fullscreen"' "$SRC" || fail 'security-level fullscreen policy missing'
grep -rFq 'WEBKIT_AUTOPLAY_DENY' "$SRC" || fail 'Safer/Safest autoplay deny policy missing'

grep -rFq '#define NION_SITE_JAVASCRIPT_FORMAT 2' "$SRC" || fail 'site JavaScript format v2 missing'
grep -rFq 'site_javascript_enabled' "$SRC" || fail 'Safest explicit JavaScript enable exceptions missing'
grep -rFq '(format != 1 && format != NION_SITE_JAVASCRIPT_FORMAT)' "$SRC" || fail 'site JavaScript v1 migration compatibility missing'
grep -rFq 'g_hash_table_remove(app->site_javascript_enabled, site_key)' "$SRC" || fail 'Forget This Site does not clear Safest enable exception'
grep -rFq 'g_hash_table_remove_all(app->site_javascript_enabled)' "$SRC" || fail 'Browsing Data does not clear Safest enable exceptions'

grep -rFq 'app->security_level = owner->security_level' "$SRC" || fail 'Private Window does not inherit Security Level'
grep -rFq 'g_hash_table_remove_all(app->temporary_permissions)' "$SRC" || fail 'level change does not revoke temporary permissions'
grep -rFq 'webkit_web_view_set_camera_capture_state' "$SRC" || fail 'level change does not stop camera capture'
grep -rFq 'webkit_web_view_set_microphone_capture_state' "$SRC" || fail 'level change does not stop microphone capture'

grep -Eq '\\*\\*(Current development|Stable release): 2\.1\.0' README.md || fail 'README 2.1.0 marker missing'
grep -Fq '### Security Levels' README.md || fail 'README Security Levels section missing'
grep -Fq 'Stage 2 — Escape Guards' README.md || fail 'README Stage 2 handoff missing'

printf 'NION 2.1.0 SECURITY LEVELS STAGE 1: PASS\n'
