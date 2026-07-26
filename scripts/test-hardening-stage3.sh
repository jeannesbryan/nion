#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source ./scripts/manifest.sh

fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }
need() { grep -Fq "$1" "$2" || fail "$3"; }

[[ "$NION_VERSION" == "1.4.0" ]] || fail 'unexpected NiOn version'
[[ "$NION_RELEASE_STATUS" == "Stable" ]] || fail '1.4.0 final must be Stable'
[[ "$NION_APPSTREAM_RELEASE_TYPE" == "stable" ]] || fail 'AppStream release type must be stable'

# Synthetic Home must preserve a return target because load_html(about:blank)
# is not a reliable normal history entry.
need 'WebKitBackForwardListItem *home_return_item;' src/main.c 'Home return history field missing'
need 'g_set_object(&tab->home_return_item, current);' src/main.c 'Home does not capture current history item'
need 'if (tab->home_page && tab->home_return_item)' src/main.c 'Home-aware Back sensitivity/handling missing'
need 'webkit_web_view_go_to_back_forward_list_item(tab->web_view, target);' src/main.c 'Home Back does not return to saved WebKit history item'
need 'g_clear_object(&tab->home_return_item);' src/main.c 'Home return history reference is not cleared'

# 1.4 Site Controls invariants.
need 'webkit_settings_set_enable_webrtc(settings, FALSE);' src/main.c 'WebRTC peer connections are not disabled'
need 'site-javascript.ini' src/main.c 'Per-site JavaScript store missing'
need 'temporary_permissions' src/main.c 'Temporary permission store missing'
need 'TOR OFFLINE — navigation blocked' src/main.c 'Tor-offline navigation guard missing'

# Recovery/data-control invariants.
need 'Restore Tabs' src/main.c 'Crash recovery Restore Tabs choice missing'
need 'Start Fresh' src/main.c 'Crash recovery Start Fresh choice missing'
need 'crash_recovery_decision_pending' src/main.c 'Crash recovery decision gate missing'
need 'Browsing Data' src/main.c 'Browsing Data manager missing'
need 'site_info_icon = gtk_image_new_from_icon_name' src/main.c 'Stable Site Information icon child missing'

need '**Stable release: 1.4.0**' README.md 'README is not marked Stable 1.4.0'
need '## 1.4.0 — 2026-07-26' CHANGELOG.md 'Final 1.4.0 changelog entry missing'

printf 'NION 1.4.0 HARDENING / FINAL CHECK: PASS\n'
