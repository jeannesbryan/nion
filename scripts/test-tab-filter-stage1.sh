#!/usr/bin/env bash
# Filter the open-tab list (roadmap T2-6).
#
# Part 1 runs the real matching rules: tests/tab_filter_test.c is linked against
# src/tab-filter.c from the production tree.
#
# Part 2 asserts the wiring a unit test cannot see: the field must sit outside
# the rebuilt container, an empty result must say so, and opening the menu must
# start from the full list.
#
# Part 3 drives the real GTK window with xdotool. A filter is a thing the user
# types into, so the only honest proof is typing into it: the menu logs its row
# count under NION_TAB_DEBUG, and the test types a word and checks that the
# count actually narrowed. GTK criticals are treated as failures.
#
# Part 4 mutation-tests part 1, so a detector that stopped detecting fails loud.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

export TMPDIR="$BUILD_DIR/tmp"
mkdir -p "$TMPDIR"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists glib-2.0 || fail "glib dev files missing"
GLIB_CFLAGS=$(pkg-config --cflags glib-2.0)
GLIB_LIBS=$(pkg-config --libs glib-2.0)

build() {
    local dir="$1" out="$2"
    shift 2
    cc -std=c11 -O1 -Wall -Wextra \
       -I"$dir/src" \
       "$@" \
       $GLIB_CFLAGS \
       -o "$out" \
       "$ROOT/tests/tab_filter_test.c" \
       "$dir/src/tab-filter.c" \
       $GLIB_LIBS 2>"$BUILD_DIR/cc.log"
}

run_clean() {
    local label="$1" bin="$2"
    "$bin" >"$BUILD_DIR/run.log" 2>&1
    local rc=$?
    cat "$BUILD_DIR/run.log"
    [[ $rc -eq 0 ]] || { cat "$BUILD_DIR/cc.log" >&2; fail "$label failed (exit $rc)"; }
    grep -qE '[0-9]+ checks, 0 failed' "$BUILD_DIR/run.log" || \
        fail "$label did not report a clean run"
}

# ---------------------------------------------------------------- part 1
build "$ROOT" "$BUILD_DIR/tab_filter_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "tab filter tests did not compile"
}
run_clean "tab filter rules" "$BUILD_DIR/tab_filter_test"
pass "matching rules, compiled from src/tab-filter.c"

# The module must stay GTK-free: the rules are tested on their own, and that only
# works while nothing drags the widget layer in.
if grep -qE '#include\s*<(gtk|webkit)' "$SRC/tab-filter.c" "$SRC/tab-filter.h"; then
    fail "src/tab-filter.c or its header includes GTK/WebKit"
fi
pass "the matching module has no GTK dependency"

# ---------------------------------------------------------------- part 2
python3 - "$SRC" "$ROOT/meson.build" <<'PY' || exit 1
import os
import re
import sys

src = sys.argv[1]
meson = open(sys.argv[2], encoding="utf-8").read()

def read(name):
    with open(os.path.join(src, name), encoding="utf-8") as handle:
        return handle.read()

types = read("types.h")
tabs = read("tabs.c")
filter_h = read("tab-filter.h")

problems = []

# The mechanical switch, forgotten twice already during this release.
if "'src/tab-filter.c'" not in meson:
    problems.append("meson.build does not list src/tab-filter.c")

# The field must live outside the rebuilt container, or typing destroys it.
if "tab_filter_entry" not in types:
    problems.append("types.h has no place to keep the filter field")
build_fn = re.search(r"nion_build_tab_overflow_button\(NionApp \*app\)(.*?)\n\}\n", tabs, re.S)
if not build_fn:
    problems.append("tabs.c has no nion_build_tab_overflow_button body")
else:
    body = build_fn.group(1)
    if "gtk_search_entry_new" not in body:
        problems.append("the overflow menu does not offer a filter field")
    if "app->tab_filter_entry = filter;" not in body:
        problems.append("the filter field is not kept on the app")
    # The entry must be appended to the outer box, not the rebuilt one.
    if re.search(r"gtk_box_append\(GTK_BOX\(box\), filter\)", body):
        problems.append("the filter field is inside the rebuilt container and would be destroyed while typing")
    if "gtk_box_append(GTK_BOX(outer), filter)" not in body:
        problems.append("the filter field is not added next to the scroll area")

# Typing must rebuild, and a programmatic clear must not be mistaken for typing.
if "on_tab_filter_changed" not in tabs:
    problems.append("typing in the filter does not rebuild the rows")
changed = re.search(r"on_tab_filter_changed\(GtkEditable \*editable, gpointer user_data\)(.*?)\n\}\n", tabs, re.S)
if not changed:
    problems.append("on_tab_filter_changed has no body to inspect")
elif "tab_filter_updating" not in changed.group(1):
    problems.append("a programmatic clear would be handled as if the user had typed")

# Opening the menu starts from the full list.
show = re.search(r"on_tab_overflow_popover_show\(GtkPopover \*popover, gpointer user_data\)(.*?)\n\}\n", tabs, re.S)
if not show:
    problems.append("the popover show handler has no body to inspect")
else:
    body = show.group(1)
    if "nion_rebuild_tab_overflow(" not in body:
        problems.append("the popover does not rebuild when it opens")
    if "tab_filter_updating = TRUE" not in body:
        problems.append("opening the menu does not reset the filter")
    if 'gtk_editable_set_text(GTK_EDITABLE(app->tab_filter_entry), "")' not in body:
        problems.append("opening the menu does not clear the filter field")

# Rows must be filtered, and an empty result must say so.
rebuild = re.search(r"void nion_rebuild_tab_overflow\(NionApp \*app\)(.*?)\n\}\n", tabs, re.S)
if not rebuild:
    problems.append("tabs.c has no nion_rebuild_tab_overflow body")
else:
    body = rebuild.group(1)
    if "nion_tab_filter_matches(" not in body:
        problems.append("the rebuild never applies the filter")
    if "No tab matches this filter" not in body:
        problems.append("an empty filtered list says nothing, which reads as a bug")
    if "of %d tabs match" not in body:
        problems.append("the heading does not say how many tabs the filter kept")

# One source of truth for what a tab is called.
if "nion_tab_filter_fields" not in tabs:
    problems.append("the filter and the drawn row do not share one title/address source")

# The field must be reachable from the keyboard.
if "on_tab_filter_key" not in tabs:
    problems.append("the arrow keys cannot leave the filter field, so rows need the mouse")
key_fn = re.search(r"on_tab_filter_key\(GtkEventControllerKey \*controller.*?\n\}\n", tabs, re.S)
if not key_fn or "GDK_KEY_Down" not in key_fn.group(0):
    problems.append("Down from the filter field does not move into the list")

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: tab filter wiring (field outside the rebuilt box, reset on open, "
      "empty result explained, keyboard reachable)")
PY

# ---------------------------------------------------------------- part 3
# Real window, real typing. Skipped only when the display or tools are genuinely
# absent; a missing precondition is reported as a skip, never as a pass.
if [[ -x "$ROOT/build/nion" ]] && command -v xdotool >/dev/null 2>&1 \
   && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    UI_TMP="$BUILD_DIR/ui"
    mkdir -p "$UI_TMP"
    UI_LOG="$UI_TMP/nion.log"
    UI_PID=""
    WID=""

    ui_stop() {
        if [[ -n "$UI_PID" ]] && kill -0 "$UI_PID" 2>/dev/null; then
            kill -KILL "$UI_PID" 2>/dev/null || true
            wait "$UI_PID" 2>/dev/null || true
        fi
        UI_PID=""
    }

    # Tabs whose addresses are distinguishable, so a typed word can match exactly
    # one of them and the assertion means something.
    ui_seed() {
        export XDG_DATA_HOME="$UI_TMP/data" XDG_CONFIG_HOME="$UI_TMP/config" \
               XDG_CACHE_HOME="$UI_TMP/cache"
        mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"
        printf '[General]\nrestore-session=true\n' > "$XDG_CONFIG_HOME/nion/preferences.ini"
        UI_SESSION="$XDG_DATA_HOME/nion/session.ini"
        local uris=(
            "https://mail.example.com/"
            "https://torproject.org/"
            "https://documents.example.org/"
            "https://news.example.net/"
            "https://photos.example.org/"
            "https://recipes.example.com/"
        )
        {
            echo "[Session]"
            echo "format=1"
            echo "nion-version=2.3.4"
            echo "tab-count=${#uris[@]}"
            echo "active-tab=0"
            echo "clean-shutdown=true"
            local i=0
            for uri in "${uris[@]}"; do
                echo
                echo "[Tab-$i]"
                echo "home=false"
                echo "muted=false"
                echo "pinned=false"
                echo "uri=$uri"
                i=$((i + 1))
            done
        } > "$UI_SESSION"
    }

    ui_launch() {
        ui_seed
        NION_TAB_DEBUG=1 NION_TOR_BINARY=/bin/false "$ROOT/build/nion" >"$UI_LOG" 2>&1 &
        UI_PID=$!

        local wid=""
        for _ in $(seq 1 100); do
            wid=$(xdotool search --pid "$UI_PID" --onlyvisible 2>/dev/null | tail -1 || true)
            [[ -n "$wid" ]] && break
            sleep 0.25
        done
        [[ -n "$wid" ]] || { cat "$UI_LOG" >&2; ui_stop; fail "NiOn never mapped a window"; }
        WID="$wid"
        xdotool windowactivate --sync "$wid" 2>/dev/null || true
        sleep 1
    }

    # Open the menu and wait for the log line that describes this opening.
    ui_open_menu() {
        local before
        before=$(grep -c 'tab-overflow-menu' "$UI_LOG" 2>/dev/null || true)
        xdotool key --clearmodifiers ctrl+shift+a || true
        for _ in $(seq 1 80); do
            local now
            now=$(grep -c 'tab-overflow-menu' "$UI_LOG" 2>/dev/null || true)
            if [[ "${now:-0}" -gt "${before:-0}" ]]; then
                return 0
            fi
            sleep 0.25
        done
        cat "$UI_LOG" >&2
        return 1
    }

    last_line() { grep 'tab-overflow-menu' "$UI_LOG" | tail -1; }
    field() { printf '%s' "$1" | sed -n "s/.*[ ]$2=\([^ ]*\).*/\1/p"; }

    ui_launch
    ui_open_menu || { ui_stop; fail "the overflow menu never opened"; }
    baseline=$(last_line)
    base_rows=$(field "$baseline" rows)
    base_tabs=$(field "$baseline" tabs)
    base_filtered=$(field "$baseline" filtered)
    [[ "$base_filtered" == "0" ]] || { ui_stop; fail "the menu opened with a filter already applied: $baseline"; }
    pass "menu opens unfiltered (${base_rows} rows for ${base_tabs} tabs)"

    # Type a word that only one seeded address contains.
    xdotool type --clearmodifiers --delay 80 "torproject" || true
    # Poll for the settled result, not the first filtered line: typing happens one
    # character at a time, so an early snapshot would catch a prefix of the word
    # ("tor" matches two seeded addresses) and assert against a state the user
    # never asked for.
    filtered_line=""
    for _ in $(seq 1 80); do
        candidate=$(last_line)
        if [[ "$(field "$candidate" filtered)" == "1" \
              && "$(field "$candidate" shown)" == "1" ]]; then
            filtered_line="$candidate"
            break
        fi
        sleep 0.25
    done
    [[ -n "$filtered_line" ]] || {
        cat "$UI_LOG" >&2
        ui_stop
        fail "typing into the filter field never produced a filtered menu"
    }
    filtered_rows=$(field "$filtered_line" rows)
    filtered_shown=$(field "$filtered_line" shown)
    (( filtered_shown == 1 )) || { ui_stop; fail "expected 1 matching tab, the menu reports ${filtered_shown}: $filtered_line"; }
    # heading + 1 row
    (( filtered_rows == 2 )) || { ui_stop; fail "expected 2 rows (heading plus one match), the menu reports ${filtered_rows}: $filtered_line"; }
    pass "typing narrowed the list to ${filtered_shown} of ${base_tabs} tabs"

    # Activate the filtered row with the keyboard while a row still exists.
    # Down leaves the field for the first row, Return opens that tab.
    #
    # This has to happen before the no-match query: with nothing matching there
    # is no row left to activate, and the test would be asserting on an empty
    # menu while pretending to test a row.
    active_before=$(sed -n 's/^active-tab=//p' "$UI_SESSION" 2>/dev/null | head -1)
    xdotool key --clearmodifiers Down || true
    sleep 0.4
    xdotool key --clearmodifiers Return || true
    # The session file is written on a short debounce, so poll for the change
    # rather than sleeping a guessed amount and reading too early.
    active_after=""
    for _ in $(seq 1 40); do
        active_after=$(sed -n 's/^active-tab=//p' "$UI_SESSION" 2>/dev/null | head -1)
        if [[ -n "$active_after" && "$active_after" != "$active_before" ]]; then
            break
        fi
        sleep 0.25
    done
    [[ -n "$active_after" && "$active_after" != "$active_before" ]] || {
        ui_stop
        fail "a row activated from a filtered list did not switch tabs (still on tab ${active_before:-?})"
    }
    pass "a filtered row still switches tabs (tab ${active_before} -> ${active_after})"

    # Reopening must start from the full list again, and the filter still held
    # text when the popover closed, so this is the reset being exercised.
    ui_open_menu || { ui_stop; fail "the overflow menu could not be reopened"; }
    reopened=$(last_line)
    [[ "$(field "$reopened" filtered)" == "0" ]] || {
        ui_stop
        fail "reopening kept the previous filter, which would silently hide tabs: $reopened"
    }
    [[ "$(field "$reopened" rows)" == "$base_rows" ]] || {
        ui_stop
        fail "reopening did not restore the full list ($(field "$reopened" rows) rows, expected ${base_rows}): $reopened"
    }
    pass "reopening the menu restores the full list"

    # A word nothing contains must produce the explained empty list.
    xdotool type --clearmodifiers --delay 60 "zzzznomatch" || true
    empty_line=""
    for _ in $(seq 1 80); do
        candidate=$(last_line)
        if [[ "$(field "$candidate" filtered)" == "1" \
              && "$(field "$candidate" shown)" == "0" ]]; then
            empty_line="$candidate"
            break
        fi
        sleep 0.25
    done
    [[ -n "$empty_line" ]] || {
        cat "$UI_LOG" >&2
        ui_stop
        fail "a query matching nothing never reported an empty result"
    }
    pass "a query matching nothing leaves an explained empty list"

    # Escape empties the field first, then closes the menu: a search field that
    # swallowed Escape would leave the menu looking stuck open.
    xdotool key --clearmodifiers Escape || true
    cleared=""
    for _ in $(seq 1 40); do
        candidate=$(last_line)
        if [[ "$(field "$candidate" filtered)" == "0" ]]; then
            cleared="$candidate"
            break
        fi
        sleep 0.25
    done
    [[ -n "$cleared" ]] || { ui_stop; fail "Escape did not clear the filter field"; }
    pass "Escape clears the filter field"

    criticals=$(grep -c 'CRITICAL' "$UI_LOG" || true)
    ui_stop
    [[ "${criticals:-0}" == "0" ]] || {
        grep -E 'CRITICAL' "$UI_LOG" | head -3 >&2
        fail "GTK reported ${criticals} critical(s) while the filter was used"
    }
    pass "no GTK criticals while filtering"
else
    echo "SKIP: the window test needs build/nion, xdotool and a display"
fi

# ---------------------------------------------------------------- part 4
mutate() {
    local label="$1" expr="$2"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' %' '--')"
    rm -rf "$dir"; mkdir -p "$dir/src"
    cp "$ROOT/src/tab-filter.c" "$ROOT/src/tab-filter.h" "$dir/src/"
    python3 - "$dir/src/tab-filter.c" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
import sys
path, expr = sys.argv[1], sys.argv[2]
old, new = expr.split("|||")
text = open(path, encoding="utf-8").read()
if old not in text:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
PY
    build "$dir" "$dir/test" || fail "mutation '$label' did not compile"
    if "$dir/test" >"$dir/run.log" 2>&1; then
        cat "$dir/run.log" >&2
        fail "mutation '$label' survived: the harness does not catch it"
    fi
    pass "mutation caught: $label"
}

# An active query must filter at all.
mutate "every query matches everything" \
    "    if (!nion_tab_filter_is_active(query))
        return TRUE;|||    if (TRUE)
        return TRUE;"

# One missing term must drop the row, otherwise typing widens the list.
mutate "any term is enough" \
    "        if (!in_title && !in_uri)
            return FALSE;|||        if (!in_title && !in_uri)
            continue;"

# Case folding must be real.
mutate "case sensitive matching" \
    "    gchar *folded_hay = g_utf8_casefold(haystack, -1);|||    gchar *folded_hay = g_strdup(haystack);"

# The address must be searched, not only the title.
mutate "address ignored" \
    "        gboolean in_uri = !in_title && folded_contains(uri, term);|||        gboolean in_uri = FALSE;"

# A whitespace-only query must not narrow the list.
mutate "whitespace query is active" \
    "        if (!g_ascii_isspace(*p))
            return TRUE;|||        if (!g_ascii_isspace(*p))
            return FALSE;"

echo "NiOn TAB FILTER (T2-6): PASS"
