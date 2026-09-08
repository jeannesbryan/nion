#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source ./scripts/manifest.sh

fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }
need() { grep -rFq "$1" "$2" || fail "$3"; }

python3 - "$NION_VERSION" <<'PYV' || fail 'current version predates the 1.4.0 hardening baseline'
import sys
parts=lambda v: tuple(int(x) for x in v.split('.'))
raise SystemExit(0 if parts(sys.argv[1]) >= parts('1.4.0') else 1)
PYV

# Synthetic Home must preserve a return target because load_html(about:blank)
# is not a reliable normal history entry.
need 'WebKitBackForwardListItem *home_return_item;' src 'Home return history field missing'
need 'g_set_object(&tab->home_return_item, current);' src 'Home does not capture current history item'
need 'if (tab->home_page && tab->home_return_item)' src 'Home-aware Back sensitivity/handling missing'
need 'webkit_web_view_go_to_back_forward_list_item(tab->web_view, target);' src 'Home Back does not return to saved WebKit history item'
need 'g_clear_object(&tab->home_return_item);' src 'Home return history reference is not cleared'

# 1.4 Site Controls invariants.
need 'webkit_settings_set_enable_webrtc(settings, FALSE);' src 'WebRTC peer connections are not disabled'
need 'site-javascript.ini' src 'Per-site JavaScript store missing'
need 'temporary_permissions' src 'Temporary permission store missing'
need 'TOR OFFLINE — navigation blocked' src 'Tor-offline navigation guard missing'

# Recovery/data-control invariants.
need 'Restore Tabs' src 'Crash recovery Restore Tabs choice missing'
need 'Start Fresh' src 'Crash recovery Start Fresh choice missing'
need 'crash_recovery_decision_pending' src 'Crash recovery decision gate missing'
need 'Browsing Data' src 'Browsing Data manager missing'
need 'site_info_icon = gtk_image_new_from_icon_name' src 'Stable Site Information icon child missing'

need '## 1.4.0 — 2026-07-26' CHANGELOG.md 'Final 1.4.0 changelog entry missing'

printf 'NION 1.4.0+ HARDENING REGRESSION: PASS\n'
