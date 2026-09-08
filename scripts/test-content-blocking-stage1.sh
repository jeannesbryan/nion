#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
SRC=src
RULES=data/content-blocking.json
fail(){ echo "CONTENT BLOCKING STAGE 1: FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ -s release/manifest/NION_VERSION ]] || fail "canonical NiOn version manifest missing"
[[ -s "$RULES" ]] || fail "bundled content-blocking rules are missing"

python3 - "$RULES" <<'PY_RULES'
import json, sys
rules=json.load(open(sys.argv[1], encoding='utf-8'))
if not (20 <= len(rules) <= 100):
    raise SystemExit(f"CONTENT BLOCKING STAGE 1: FAIL: expected a small bundled ruleset, got {len(rules)}")
for i, rule in enumerate(rules):
    if rule.get('action',{}).get('type') != 'block':
        raise SystemExit(f"CONTENT BLOCKING STAGE 1: FAIL: rule {i} is not a block rule")
    trigger=rule.get('trigger',{})
    if trigger.get('load-type') != ['third-party']:
        raise SystemExit(f"CONTENT BLOCKING STAGE 1: FAIL: rule {i} is not third-party scoped")
    types=trigger.get('resource-type',[])
    if 'document' in types:
        raise SystemExit(f"CONTENT BLOCKING STAGE 1: FAIL: rule {i} blocks top-level documents")
    if not trigger.get('url-filter'):
        raise SystemExit(f"CONTENT BLOCKING STAGE 1: FAIL: rule {i} has no URL filter")
print(f"PASS: bounded bundled JSON ruleset ({len(rules)} rules)")
PY_RULES

grep -Fq '<file>content-blocking.json</file>' data/nion.gresource.xml || fail "ruleset not embedded as a GResource"
grep -rFq 'webkit_user_content_filter_store_new' "$SRC" || fail "WebKit filter store missing"
grep -rFq 'webkit_user_content_filter_store_save' "$SRC" || fail "native WebKit filter compilation missing"
grep -rFq 'webkit_user_content_manager_add_filter' "$SRC" || fail "filter application missing"
grep -rFq 'webkit_user_content_manager_remove_filter_by_id' "$SRC" || fail "per-site filter removal missing"
grep -rFq 'webkit_user_content_filter_unref' "$SRC" || fail "WebKit filter lifetime handling missing"
pass "native WebKit content-filter pipeline"

grep -rFq 'content-blocking.ini' "$SRC" || fail "normal per-site exception profile missing"
grep -rFq 'NION_MAX_CONTENT_BLOCKING_EXCEPTIONS 2048' "$SRC" || fail "exception entry bound missing"
grep -rFq 'NION_MAX_CONTENT_BLOCKING_FILE_BYTES (1024 * 1024)' "$SRC" || fail "exception file-size bound missing"
grep -rFq 'app->is_private || !app->content_blocking_file' "$SRC" || fail "Private Window persistence guard missing"
grep -rFq 'nion_write_key_file_atomic(key_file, app->content_blocking_file)' "$SRC" || fail "atomic exception write missing"
pass "normal persistent / private memory-only exceptions"

grep -rFq 'WebKitUserContentManager *content_manager = webkit_user_content_manager_new();' "$SRC" || fail "tab-local UserContentManager missing"
grep -rFq '"user-content-manager", content_manager' "$SRC" || fail "tab-local UserContentManager not attached"
grep -rFq '"related-view", related_view' "$SRC" || fail "related-view lifecycle regression"
grep -rFq '"network-session", app->network_session' "$SRC" || fail "normal tabs lost explicit Tor-backed NetworkSession"
pass "tab-local filter state with related-view/network-session invariants"

grep -rFq 'nion_apply_content_blocking(tab, uri);' "$SRC" || fail "content filter not applied before navigation"
grep -rFq 'site_info_content_blocking_switch' "$SRC" || fail "Site Information content-blocking switch missing"
grep -rFq 'Disabled for this site' "$SRC" || fail "per-site disable status missing"
grep -rFq 'Saved content-blocking exceptions' "$SRC" || fail "Browsing Data normal exception selector missing"
grep -rFq 'Private content-blocking exceptions' "$SRC" || fail "Browsing Data private exception selector missing"
pass "Site Information and Browsing Data integration"

grep -rFq 'nion_update_site_info_button(app);' "$SRC" || fail "lightweight Site Information button updater missing"
if grep -rF 'gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button)' "$SRC" >/dev/null; then
    fail "Site Information still replaces GtkMenuButton child while refreshing"
fi
pass "Site Information refresh remains decoupled from content-filter state"

printf 'NION 1.5.0 STAGE 1 CONTENT BLOCKING: PASS\n'
