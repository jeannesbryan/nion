#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

grep -rq '#define NION_MAX_CLOSED_TABS 10' "$SRC" || fail "closed-tab limit missing"
grep -rq 'GQueue \*closed_tabs' "$SRC" || fail "closed-tab queue missing"
grep -rq 'nion_remember_closed_tab' "$SRC" || fail "closed-tab recorder missing"
grep -rq 'g_queue_push_tail(app->closed_tabs' "$SRC" || fail "closed-tab push missing"
grep -rq 'g_queue_pop_tail(app->closed_tabs' "$SRC" || fail "closed-tab LIFO reopen missing"
grep -rq 'g_str_equal(uri, "about:blank")' "$SRC" || fail "blank-tab exclusion missing"
grep -rq '"reopen-closed-tab", action_reopen_closed_tab' "$SRC" || fail "reopen action missing"
grep -rq '"<Primary><Shift>t"' "$SRC" || fail "Ctrl+Shift+T accelerator missing"

for item in \
  'Reload' \
  'Duplicate Tab' \
  'Mute Tab' \
  'Close Tab' \
  'Close Other Tabs' \
  'Close Tabs to the Right'; do
  grep -rq "\"$item\"" "$SRC" || fail "tab context item missing: $item"
done

grep -rq 'GDK_BUTTON_SECONDARY' "$SRC" || fail "secondary-click tab gesture missing"
grep -rq 'gtk_popover_popup' "$SRC" || fail "tab context popover missing"
grep -rq 'gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 1' "$SRC" || fail "last-tab keepalive guard missing"
grep -rq 'nion_new_tab(app, NULL, TRUE)' "$SRC" || fail "last-tab replacement New Tab missing"

pass "bounded recent closed-tab recovery"
pass "Ctrl+Shift+T action"
pass "tab right-click context menu"
pass "last-tab New Tab behavior retained"
echo "NION 1.2.0 STAGE 1 STATIC CHECK: PASS"
