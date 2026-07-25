#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
ROADMAP="$ROOT/ROADMAP.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "missing src/main.c"

grep -q '#define NION_MAX_CLOSED_TABS 10' "$SRC" || fail "closed-tab limit missing"
grep -q 'GQueue \*closed_tabs' "$SRC" || fail "closed-tab queue missing"
grep -q 'nion_remember_closed_tab' "$SRC" || fail "closed-tab recorder missing"
grep -q 'g_queue_push_tail(app->closed_tabs' "$SRC" || fail "closed-tab push missing"
grep -q 'g_queue_pop_tail(app->closed_tabs' "$SRC" || fail "closed-tab LIFO reopen missing"
grep -q 'g_str_equal(uri, "about:blank")' "$SRC" || fail "blank-tab exclusion missing"
grep -q '"reopen-closed-tab", action_reopen_closed_tab' "$SRC" || fail "reopen action missing"
grep -q '"<Primary><Shift>t"' "$SRC" || fail "Ctrl+Shift+T accelerator missing"

for item in \
  'Reload' \
  'Duplicate Tab' \
  'Mute Tab' \
  'Close Tab' \
  'Close Other Tabs' \
  'Close Tabs to the Right'; do
  grep -q "\"$item\"" "$SRC" || fail "tab context item missing: $item"
done

grep -q 'GDK_BUTTON_SECONDARY' "$SRC" || fail "secondary-click tab gesture missing"
grep -q 'gtk_popover_popup' "$SRC" || fail "tab context popover missing"
grep -q 'gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 1' "$SRC" || fail "last-tab keepalive guard missing"
grep -q 'nion_new_tab(app, NULL, TRUE)' "$SRC" || fail "last-tab replacement New Tab missing"
grep -q 'Stage 1 — Tab Recovery & Tab Context Menu ✅' "$ROADMAP" || fail "roadmap Stage 1 not complete"

pass "bounded recent closed-tab recovery"
pass "Ctrl+Shift+T action"
pass "tab right-click context menu"
pass "last-tab New Tab behavior retained"
echo "NION 1.2.0 STAGE 1 STATIC CHECK: PASS"
