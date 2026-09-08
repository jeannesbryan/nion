#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
fail(){ echo "FAIL: $*" >&2; exit 1; }
pass(){ echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

if grep -rq 'g_list_free(items)' "$SRC"; then
  fail "WebKit-owned context-menu list must never be freed"
fi
pass "does not free WebKit-owned context-menu GList"

grep -rq 'webkit_context_menu_get_n_items(context_menu)' "$SRC" \
  && grep -rq 'webkit_context_menu_get_item_at_position(context_menu, position)' "$SRC" \
  || fail "indexed context-menu traversal missing"
pass "uses ownership-safe indexed context-menu traversal"

grep -rq 'g_object_ref_sink(replacement)' "$SRC" \
  && grep -rq 'g_object_ref_sink(separator)' "$SRC" \
  && grep -rq 'g_object_ref_sink(print_item)' "$SRC" \
  || fail "floating ContextMenuItem references are not explicitly sunk"
pass "ContextMenuItem floating refs are handled explicitly"

grep -rq 'webkit_context_menu_remove(context_menu, item)' "$SRC" \
  && grep -rq 'webkit_context_menu_insert(context_menu, replacement, (gint)position)' "$SRC" \
  || fail "stock action relabel replacement path missing"
pass "stock item relabel preserves menu position"

echo "CONTEXT MENU OWNERSHIP CHECK: PASS"
