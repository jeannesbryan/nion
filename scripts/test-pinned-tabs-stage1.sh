#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "missing src/main.c"

grep -q 'gboolean pinned;' "$SRC" || fail "per-tab pinned state missing"
grep -q '"pinned", tab->pinned' "$SRC" || fail "session pinned save missing"
grep -q 'g_key_file_has_key(session, group, "pinned"' "$SRC" || fail "backward-compatible pinned restore missing"
grep -q 'nion_set_tab_pinned(restored_tab, pinned, FALSE)' "$SRC" || fail "restored pinned state not applied"
grep -q 'nion_set_tab_pinned(tab, closed->pinned, TRUE)' "$SRC" || fail "closed-tab pin recovery missing"
grep -q 'gtk_notebook_reorder_child' "$SRC" || fail "pinned-left reorder enforcement missing"
grep -q 'normalizing_tab_order' "$SRC" || fail "reorder recursion guard missing"
grep -q '"Pin Tab"' "$SRC" || fail "Pin Tab context action missing"
grep -q '"Unpin Tab"' "$SRC" || fail "Unpin Tab context label missing"
[[ "$(grep -c 'other && !other->pinned' "$SRC")" -ge 2 ]] || fail "bulk-close pinned protection missing"
grep -q 'gtk_widget_set_visible(tab->tab_close_button, !tab->pinned)' "$SRC" || fail "pinned close-button suppression missing"
grep -q '📌' "$SRC" || fail "pinned marker missing"

pass "pin/unpin context action"
pass "pinned-left ordering guard"
pass "bulk-close protection"
pass "session + closed-tab recovery persistence"
echo "NION 1.3.0 STAGE 1 STATIC CHECK: PASS"
