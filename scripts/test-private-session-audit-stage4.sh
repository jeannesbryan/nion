#!/usr/bin/env bash
set -euo pipefail
SRC="${1:-src/main.c}"
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

# The private WebKit session must be ephemeral at runtime, not merely by intent.
grep -q 'webkit_network_session_new_ephemeral()' "$SRC" || fail "ephemeral NetworkSession constructor missing"
grep -q 'webkit_network_session_is_ephemeral(app->network_session)' "$SRC" || fail "runtime ephemeral-session verification missing"
grep -q 'PRIVATE WINDOW BLOCKED: EPHEMERAL SESSION UNAVAILABLE' "$SRC" || fail "private window fail-closed status missing"
grep -q 'webkit_network_session_get_persistent_credential_storage_enabled(app->network_session)' "$SRC" || fail "persistent credential verification missing"
pass "runtime ephemeral/credential invariants"

# Normal profile stores must reject private state even if a future call path reaches them.
grep -q 'if (!app || app->is_private)' "$SRC" || fail "generic private persistence guard missing"
grep -q 'if (!app || app->is_private || !app->preferences_file)' "$SRC" || fail "private preference-write guard missing"
grep -q 'if (!app || app->is_private || !app->site_zoom_file' "$SRC" || fail "private site-zoom write guard missing"
grep -q 'if (!app || app->is_private || !app->downloads_file' "$SRC" || fail "private download-history write guard missing"
grep -q 'if (!app || app->is_private)' "$SRC" || fail "private session guard missing"
pass "persistent profile write guards"

# Private window construction must not gain normal profile backing paths.
private_block="$(sed -n '/static void nion_open_private_window/,/^}/p' "$SRC")"
for token in 'session_file =' 'downloads_file =' 'site_zoom_file =' 'cookie_file =' 'data_dir =' 'cache_dir ='; do
  if grep -Fq "$token" <<<"$private_block"; then
    fail "private window unexpectedly assigns persistent profile field: $token"
  fi
done
grep -Fq 'app->bookmarks = owner->bookmarks' <<<"$private_block" || fail "intentional global bookmark sharing missing"
grep -Fq 'app->search_engine = g_strdup(owner->search_engine' <<<"$private_block" || fail "intentional global search preference sharing missing"
pass "private profile separation and intentional globals"

# Closed-tab URLs may exist only during the live private window and must be erased on close.
grep -q 'static void nion_private_clear_closed_tabs' "$SRC" || fail "private closed-tab memory purge helper missing"
[[ "$(grep -c 'nion_private_clear_closed_tabs' "$SRC")" -ge 3 ]] || fail "private closed-tab purge not wired into close/shutdown paths"
pass "private closed-tab recovery is memory-only"

# Stage 3 download privacy remains enforced.
grep -q 'if (!item->app->is_private)' "$SRC" || fail "private desktop notification suppression missing"
grep -q 'nion_private_cleanup_partial_downloads' "$SRC" || fail "private partial download cleanup missing"
grep -q 'nion_cancel_active_downloads(private_app);' "$SRC" || fail "Tor-failure private download cancellation missing"
pass "private download audit invariants"

# Private audit must disclose both the enforced boundaries and the intentional escape hatches.
grep -q '"Private WebKit session", ephemeral ? "EPHEMERAL" : "UNSAFE"' "$SRC" || fail "runtime private-session audit row missing"
grep -q '"Closed-tab recovery", "MEMORY-ONLY"' "$SRC" || fail "closed-tab audit disclosure missing"
grep -q '"Bookmarks / search preference", "GLOBAL BY DESIGN"' "$SRC" || fail "global-data disclosure missing"
grep -q '"User-exported artifacts", "LIMITATION"' "$SRC" || fail "private export limitation disclosure missing"
pass "private-session audit UI disclosures"

echo "NION 1.3.0 STAGE 4 PRIVATE-SESSION AUDIT: PASS"
