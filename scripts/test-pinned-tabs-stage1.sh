#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

grep -rq 'gboolean pinned;' "$SRC" || fail "per-tab pinned state missing"
grep -rq '"pinned", tab->pinned' "$SRC" || fail "session pinned save missing"
grep -rq 'g_key_file_has_key(session, group, "pinned"' "$SRC" || fail "backward-compatible pinned restore missing"
grep -rq 'nion_set_tab_pinned(restored_tab, pinned, FALSE)' "$SRC" || fail "restored pinned state not applied"
grep -rq 'nion_set_tab_pinned(tab, closed->pinned, TRUE)' "$SRC" || fail "closed-tab pin recovery missing"
grep -rq 'gtk_notebook_reorder_child' "$SRC" || fail "pinned-left reorder enforcement missing"
grep -rq 'normalizing_tab_order' "$SRC" || fail "reorder recursion guard missing"
grep -rq '"Pin Tab"' "$SRC" || fail "Pin Tab context action missing"
grep -rq '"Unpin Tab"' "$SRC" || fail "Unpin Tab context label missing"
[[ "$(grep -rh 'other && !other->pinned' "$SRC" | wc -l)" -ge 2 ]] || fail "bulk-close pinned protection missing"
grep -rq 'gtk_widget_set_visible(tab->tab_close_button, !tab->pinned)' "$SRC" || fail "pinned close-button suppression missing"
grep -rq '📌' "$SRC" || fail "pinned marker missing"

pass "pin/unpin context action"
pass "pinned-left ordering guard"
pass "bulk-close protection"
pass "session + closed-tab recovery persistence"
echo "NION 1.3.0 STAGE 1 STATIC CHECK: PASS"
