#!/usr/bin/env bash
set -euo pipefail
SRC="${1:-src/main.c}"
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

grep -q 'webkit_network_session_new_ephemeral()' "$SRC" || fail "ephemeral WebKit network session missing"
grep -q 'app->is_private || !app->downloads_file' "$SRC" || fail "private download persistence isolation missing"
grep -q 'if (!app || app->is_private)' "$SRC" || fail "private session-save guard missing"
grep -q 'app->is_private || !nion_restore_saved_session(app)' "$SRC" || fail "private restore bypass missing"
grep -q '!tab->app->is_private && tab->app->site_zoom' "$SRC" || fail "private site-zoom read isolation missing"
grep -q 'app->is_private || !app->site_zoom_file' "$SRC" || fail "private site-zoom write isolation missing"
grep -q 'g_ptr_array_ref(owner->bookmarks)' "$SRC" || fail "global bookmarks sharing missing"
grep -q 'g_strdup(owner->search_engine' "$SRC" || fail "global search preference inheritance missing"
grep -q 'G_ACTION_MAP(app->window)' "$SRC" || fail "window-scoped actions missing"
grep -q '"win.private-window"' "$SRC" || fail "private window action/accelerator missing"
grep -q 'nion_sync_private_windows_tor(app)' "$SRC" || fail "Tor state propagation to private windows missing"
grep -q 'if (!private_app->tor_ready)' "$SRC" || fail "private fail-closed load stop missing"
grep -q '"related-view", related_view' "$SRC" || fail "private popup related-view inheritance regression"
grep -q 'webkit_network_session_set_persistent_credential_storage_enabled(app->network_session,' "$SRC" || fail "credential persistence control missing"
grep -q '!app->is_private);' "$SRC" || fail "private persistent credentials not disabled"

pass "ephemeral private WebKit session"
pass "no private session restore or site-zoom persistence"
pass "bookmarks/search preference sharing"
pass "window-scoped actions for normal/private windows"
pass "private download persistence isolation"
pass "Tor fail-closed state propagation"
echo "NION 1.3.0 STAGE 2 STATIC CHECK: PASS"
