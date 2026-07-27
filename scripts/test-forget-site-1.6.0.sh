#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "1.6.0 FORGET THIS SITE: FAIL: $*" >&2; exit 1; }
SRC=src/main.c

grep -Fq 'Forget This Site…' "$SRC" || fail 'Forget This Site UI missing'
grep -Fq '"forget-site", action_forget_site' "$SRC" || fail 'Forget This Site action missing'
grep -Fq 'nion_forget_site_local_state' "$SRC" || fail 'local site-state reset helper missing'
grep -Fq 'g_hash_table_remove(app->site_zoom, site_key)' "$SRC" || fail 'site zoom is not reset'
grep -Fq 'g_hash_table_remove(app->site_javascript_disabled, site_key)' "$SRC" || fail 'JavaScript site rule is not reset'
grep -Fq 'g_hash_table_remove(app->site_javascript_enabled, site_key)' "$SRC" || fail 'Safest JavaScript enable exception is not reset'
grep -Fq 'g_hash_table_remove(app->content_blocking_disabled, site_key)' "$SRC" || fail 'content-blocking exception is not reset'
grep -Fq 'g_hash_table_remove(app->autoplay_allowed_sites, site_key)' "$SRC" || fail 'autoplay exception is not reset'
grep -Fq 'nion_clear_temporary_permissions_for_origin(app, origin)' "$SRC" || fail 'temporary permissions are not reset'
grep -Fq 'nion_stop_capture_for_origin(app, origin)' "$SRC" || fail 'active camera/microphone capture is not stopped'
grep -Fq 'Bookmarks and downloaded files are not removed.' "$SRC" || fail 'explicit user-data boundary is not documented in confirmation'
# Forget must reuse the existing targeted WebKit website-data removal rather than clear every site.
grep -Fq 'nion_website_data_matches_host(data, request->host)' "$SRC" || fail 'targeted WebKit website-data matching missing'
grep -Fq 'webkit_website_data_manager_remove(manager' "$SRC" || fail 'targeted WebKit website-data removal missing'

printf 'NION 1.6.0 FORGET THIS SITE: PASS\n'
