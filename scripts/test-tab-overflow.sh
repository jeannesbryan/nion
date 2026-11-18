#!/usr/bin/env bash
# Copyright (C) 2026 Jeannes Bryan

# Tab strip overflow (Priority+).
#
# A scrollable GtkNotebook keeps every tab reachable, but a tab scrolled out of
# the strip has no name and no list: reaching tab 12 of 14 means clicking the
# arrows until it appears. The strip therefore carries an overflow menu that
# names every open tab and jumps to it.
#
# The measurement is the fragile part, so it is asserted from the running binary
# instead of from the source: the count of hidden tabs is logged when the menu
# is built (NION_TAB_DEBUG), and the test drives the real window with xdotool.
# Two properties are asserted beyond the list itself:
#   * every tab appears exactly once (rows == tabs + 1, the heading is a row too);
#   * a tab that the strip cannot show is reachable from the menu, proven by
#     activating the tenth row and seeing tab 10 become current.
# GTK criticals are treated as failures: the first version of this feature lost
# its button to a second gtk_notebook_set_action_widget() call, which showed up
# only as a critical while the menu still built its rows.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

# ---- static structure ------------------------------------------------------
# gtk_notebook_set_action_widget() owns ONE slot per pack type: a second call
# replaces the first widget, which is destroyed. The overflow button and the new
# tab button must therefore share a single container.
# Count call sites, not mentions: the comment above that call names the function
# too, and counting it would make this assertion report a phantom second call.
action_widget_calls=$(grep -n 'gtk_notebook_set_action_widget' src/ui.c \
  | grep -vE '^[0-9]+:[[:space:]]*(\*|/\*|//)' \
  | grep -cE 'gtk_notebook_set_action_widget[[:space:]]*\(' || true)
[[ "$action_widget_calls" == "1" ]] || fail "ui.c calls gtk_notebook_set_action_widget ${action_widget_calls} times; a second call destroys the first widget (exactly one container must be set)"

grep -q 'nion_build_tab_overflow_button' src/ui.c || fail "the tab strip never builds its overflow button"
grep -q 'gtk_menu_button_new' src/tabs.c || fail "the overflow control must be a GtkMenuButton (a hand-parented popover has no surface to open against)"
grep -q 'gtk_menu_button_popup' src/tabs.c || fail "the Ctrl+Shift+A path must open the menu through the menu button"
grep -q 'gtk_widget_compute_bounds' src/tabs.c || fail "hidden tabs must be detected from measured geometry, not from a tab count"

# Two ways in: the shortcut calls nion_show_tab_overflow() which rebuilds first,
# the toolbar button lets GtkMenuButton pop up on its own and relies on the
# popover's "show" signal. Synthetic clicks do not reach this widget tree under
# Xvfb here (mouse events are dropped even for the new-tab button), so the button
# path cannot be driven end to end; this assertion is what keeps it wired.
grep -q 'G_CALLBACK(on_tab_overflow_popover_show), app' src/tabs.c \
  || fail "the popover does not rebuild when it opens, so the toolbar button would show a stale or empty list"
# Assert the property, not a literal call spelling: the handler was refactored to
# bind the app pointer to a local so it can also clear the filter, and matching
# the old text made a correct change look like a regression.
show_body=$(python3 - src/tabs.c <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
match = re.search(r"on_tab_overflow_popover_show\(GtkPopover \*popover, gpointer user_data\)\s*\{(.*?)\n\}", text, re.S)
if not match:
    print("MISSING")
else:
    body = match.group(1)
    print("REBUILDS" if "nion_rebuild_tab_overflow(" in body else "NO_REBUILD")
PY
)
[[ "$show_body" == "REBUILDS" ]] \
  || fail "the popover show handler no longer rebuilds the menu (got ${show_body})"

grep -q '{ "show-all-tabs", action_show_all_tabs' src/main.c || fail "win.show-all-tabs action is missing"
grep -q '"<Primary><Shift>a"' src/main.c || fail "Ctrl+Shift+A is not bound to the overflow menu"

# A duplicate accelerator silently loses to whichever was registered first, so
# the strip shortcut must appear exactly once.
dup=$(grep -c '"<Primary><Shift>a"' src/main.c || true)
[[ "$dup" == "1" ]] || fail "Ctrl+Shift+A is bound ${dup} times; a duplicate binding is silently dropped"

pass "static structure, single action-widget container, one shortcut binding"

# ---- behavioural -----------------------------------------------------------
[[ -x build/nion ]] || { echo 'build/nion is required. Run ./scripts/run-dev.sh once, close NiOn, then rerun this script.' >&2; exit 1; }
command -v xdotool >/dev/null || { echo 'xdotool is required to drive the real window.' >&2; exit 1; }
[[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || { echo 'This test drives the real GTK window and needs a display.' >&2; exit 1; }

TMP="$(mktemp -d)"
PID=""
cleanup() {
  if [[ -n "$PID" ]] && kill -0 "$PID" 2>/dev/null; then
    kill -KILL "$PID" 2>/dev/null || true
  fi
  rm -rf "$TMP"
}
trap cleanup EXIT

seed_profile() {
  local dir="$1" tabs="$2"
  export XDG_DATA_HOME="$dir/data" XDG_CONFIG_HOME="$dir/config" XDG_CACHE_HOME="$dir/cache"
  mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"
  printf '[General]\nrestore-session=true\n' > "$XDG_CONFIG_HOME/nion/preferences.ini"
  SESSION="$XDG_DATA_HOME/nion/session.ini"
  {
    echo "[Session]"
    echo "format=1"
    echo "nion-version=2.2.5"
    echo "tab-count=$tabs"
    echo "active-tab=0"
    echo "clean-shutdown=true"
    for ((i = 0; i < tabs; i++)); do
      echo
      echo "[Tab-$i]"
      echo "home=false"
      echo "muted=false"
      echo "pinned=false"
      echo "uri=https://example.com/p-$i"
    done
  } > "$SESSION"
}

# Launch with Tor disabled: restored tabs never navigate, which is exactly the
# state the strip overflows in, and nothing reaches the public network.
launch_with() {
  local tabs="$1" log="$2"
  seed_profile "$TMP/run-$tabs" "$tabs"
  NION_TAB_DEBUG=1 NION_TOR_BINARY=/bin/false ./build/nion >"$log" 2>&1 &
  PID=$!

  # Polling must survive a failing probe: under `set -e` a bare `xdotool | grep`
  # in the loop body aborts the whole script on the first not-yet-true iteration.
  local wid=""
  for _ in $(seq 1 100); do
    wid=$(xdotool search --pid "$PID" --onlyvisible 2>/dev/null | tail -1 || true)
    if [[ -n "$wid" ]]; then
      break
    fi
    sleep 0.25
  done
  [[ -n "$wid" ]] || { cat "$log" >&2; fail "NiOn never mapped a window"; }

  xdotool windowactivate --sync "$wid" 2>/dev/null || true
  sleep 1
  xdotool key --clearmodifiers ctrl+shift+a || true

  for _ in $(seq 1 60); do
    if grep -q 'tab-overflow-menu' "$log"; then
      return 0
    fi
    sleep 0.25
  done
  cat "$log" >&2
  fail "the overflow menu never opened for $tabs tabs"
}

stop_app() {
  kill -KILL "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
  PID=""
}

# --- few tabs: nothing overflows, and the menu still lists every tab ---------
launch_with 3 "$TMP/few.log"
line=$(grep -m1 'tab-overflow-menu' "$TMP/few.log")
stop_app
rows=$(printf '%s' "$line" | sed 's/.*rows=\([0-9]*\).*/\1/')
tabs=$(printf '%s' "$line" | sed 's/.*tabs=\([0-9]*\).*/\1/')
hidden=$(printf '%s' "$line" | sed 's/.*hidden=\([0-9]*\).*/\1/')
[[ "$rows" == "4" ]] || fail "3 tabs produced $rows rows, expected 4 (one heading plus one row per tab): $line"
[[ "$tabs" == "3" ]] || fail "menu reports $tabs tabs, expected 3: $line"
[[ "$hidden" == "0" ]] || fail "3 tabs cannot overflow a full-width strip, but $hidden were reported hidden: $line"
pass "three tabs: listed once each, nothing reported hidden"

# --- many tabs: overflow happens, and the hidden ones stay reachable ---------
launch_with 14 "$TMP/many.log"
line=$(grep -m1 'tab-overflow-menu' "$TMP/many.log")
rows=$(printf '%s' "$line" | sed 's/.*rows=\([0-9]*\).*/\1/')
tabs=$(printf '%s' "$line" | sed 's/.*tabs=\([0-9]*\).*/\1/')
hidden=$(printf '%s' "$line" | sed 's/.*hidden=\([0-9]*\).*/\1/')
[[ "$tabs" == "14" ]] || fail "menu reports $tabs tabs, expected 14: $line"
[[ "$rows" == "15" ]] || fail "14 tabs produced $rows rows, expected 15 (heading plus one row each): $line"
(( hidden >= 1 )) || fail "14 tabs in a 1100px window must overflow, but 0 were reported hidden: $line"
(( hidden <= 13 )) || fail "every tab reported hidden, which means the strip was not measured: $line"

# Walk deep into the list and activate a row. Which index the focus lands on
# depends on whether the first Down starts at the heading or the first row, so
# the assertion is the property that matters: the tab that became current is one
# the strip cannot show (index >= tabs - hidden). Reaching such a tab proves the
# menu is a way in, not just a list.
first_hidden=$(( tabs - hidden ))
xdotool key --clearmodifiers Down Down Down Down Down Down Down Down Down
xdotool key --clearmodifiers Return
active_after=""
for _ in $(seq 1 60); do
  active_after=$(sed -n 's/^active-tab=//p' "$SESSION" 2>/dev/null | head -1)
  if [[ -n "$active_after" && "$active_after" != "0" ]]; then
    break
  fi
  sleep 0.25
done
criticals=$(grep -c 'CRITICAL' "$TMP/many.log" || true)
[[ -n "$active_after" ]] || fail "could not read active-tab from the session file"
[[ "$active_after" != "0" ]] || fail "activating a menu row left the current tab at 0"
(( active_after >= first_hidden )) || fail "the menu only reached tab ${active_after}, but tabs ${first_hidden}..$((tabs - 1)) are the ones the strip cannot show; the menu must reach those too"
[[ "$criticals" == "0" ]] || fail "GTK reported ${criticals} critical(s) while the overflow menu was used:
$(grep -E 'CRITICAL' "$TMP/many.log" | head -3)"
stop_app

pass "fourteen tabs: ${hidden} reported hidden, all listed, tenth tab reachable from the menu"
pass "no GTK criticals while the overflow menu is built and used"
echo "NION TAB OVERFLOW: PASS"
