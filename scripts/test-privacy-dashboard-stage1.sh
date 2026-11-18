#!/usr/bin/env bash
# Privacy dashboard on the start page (roadmap T2-8).
#
# Part 1 runs the real dashboard rules: tests/privacy_dashboard_test.c is linked
# against src/privacy-dashboard.c from the production tree.
#
# Part 2 asserts the wiring: the page must render the dashboard rather than the
# three hardcoded tiles it used to have, a private window must not be handed a
# figure it cannot have, and the honesty rule (no invented counts) must hold in
# the source that feeds it.
#
# Part 3 renders the real start page in a real window and checks the text that
# actually reached the page, because a tile built correctly and then dropped by a
# format-string mistake still leaves a page without a dashboard.
#
# Part 4 mutation-tests part 1.
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
       "$ROOT/tests/privacy_dashboard_test.c" \
       "$dir/src/privacy-dashboard.c" \
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
    if grep -q "AddressSanitizer" "$BUILD_DIR/run.log"; then
        fail "$label reported an AddressSanitizer finding"
    fi
}

# ---------------------------------------------------------------- part 1
build "$ROOT" "$BUILD_DIR/dash_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "privacy dashboard tests did not compile"
}
run_clean "privacy dashboard rules" "$BUILD_DIR/dash_test"
pass "dashboard rules, compiled from src/privacy-dashboard.c"

build "$ROOT" "$BUILD_DIR/dash_asan" -fsanitize=address -g || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "privacy dashboard tests did not compile with -fsanitize=address"
}
ASAN_OPTIONS="detect_leaks=1:abort_on_error=0" \
    run_clean "privacy dashboard under AddressSanitizer" "$BUILD_DIR/dash_asan"
pass "no memory error building the dashboard (AddressSanitizer)"

if grep -qE '#include\s*<(gtk|webkit)' "$SRC/privacy-dashboard.c" "$SRC/privacy-dashboard.h"; then
    fail "src/privacy-dashboard.c or its header includes GTK/WebKit"
fi
pass "the dashboard module has no GTK dependency"

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
ui = read("ui.c")
module = read("privacy-dashboard.c")

problems = []

if "'src/privacy-dashboard.c'" not in meson:
    problems.append("meson.build does not list src/privacy-dashboard.c")

home = re.search(r"static gchar \*nion_home_html\(NionApp \*app\)(.*?)\n\}\n", ui, re.S)
if not home:
    problems.append("ui.c has no nion_home_html body")
else:
    body = home.group(1)
    # The page must render the dashboard, not a hand-built set of tiles.
    if "nion_dashboard_build" not in body:
        problems.append("the start page does not build the dashboard")
    if "nion_dashboard_summary" not in body:
        problems.append("the start page does not show the dashboard summary")
    if "dashboard->tiles" not in body:
        problems.append("the start page does not render the dashboard tiles")
    if "g_markup_escape_text" not in body:
        problems.append("tile text is not escaped before going into the page")
    if "nion_dashboard_free" not in body:
        problems.append("the dashboard is never freed, so the page leaks each render")
    # The old three hardcoded tiles must be gone, or there would be two sources.
    if "<span>Discarded</span>" in body:
        problems.append("the old hardcoded tiles are still in the page")
    # A private window must not be given a count it cannot have.
    if "app->is_private\n        ? -1" not in body and "app->is_private ? -1" not in body:
        problems.append("a private window is handed a site-override count instead of -1")

# The honesty rule, in the module that decides what is shown.
if "not counted" not in module.lower():
    problems.append("the module does not say that blocked requests are not counted")
# Every count the module prints must come from the facts struct.
if "NionDashFacts" not in module:
    problems.append("the module does not take its values from the facts struct")
# A failure must be flagged, not smoothed over.
if "attention" not in module:
    problems.append("the module has no way to mark a problem")

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: dashboard wiring (start page renders it, text escaped, private window "
      "keeps no counts, unmeasured facts say so)")
PY

# ---------------------------------------------------------------- part 3
if [[ -x "$ROOT/build/nion" ]] && command -v xdotool >/dev/null 2>&1 \
   && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    UI_TMP="$BUILD_DIR/ui"
    mkdir -p "$UI_TMP"
    UI_LOG="$UI_TMP/nion.log"
    UI_PID=""

    FAKE_TOR="$UI_TMP/fake-tor.sh"
    {
        echo '#!/bin/sh'
        echo 'echo "Bootstrapped 100% (done): Done"'
        echo 'while :; do sleep 1; done'
    } > "$FAKE_TOR"
    chmod +x "$FAKE_TOR"

    ui_stop() {
        if [[ -n "$UI_PID" ]] && kill -0 "$UI_PID" 2>/dev/null; then
            kill -KILL "$UI_PID" 2>/dev/null || true
            wait "$UI_PID" 2>/dev/null || true
        fi
        UI_PID=""
    }

    # The start page is what a fresh profile shows, so no session is seeded: one
    # blank tab is exactly the surface this task is about.
    ui_launch() {
        local private="$1"
        export XDG_DATA_HOME="$UI_TMP/data" XDG_CONFIG_HOME="$UI_TMP/config" \
               XDG_CACHE_HOME="$UI_TMP/cache"
        rm -rf "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
        mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"

        local args=()
        [[ "$private" == "yes" ]] && args=(--private)

        NION_TAB_DEBUG=1 NION_TOR_BINARY="$FAKE_TOR" "$ROOT/build/nion" "${args[@]}" \
            >"$UI_LOG" 2>&1 &
        UI_PID=$!
        local wid=""
        for _ in $(seq 1 100); do
            wid=$(xdotool search --pid "$UI_PID" --onlyvisible 2>/dev/null | tail -1 || true)
            [[ -n "$wid" ]] && break
            sleep 0.25
        done
        [[ -n "$wid" ]] || { cat "$UI_LOG" >&2; ui_stop; fail "NiOn never mapped a window"; }
        xdotool windowactivate --sync "$wid" 2>/dev/null || true
        sleep 2.5
    }

    ui_launch "no"

    # The dashboard logs how many tiles reached the rendered page. That is the
    # only honest check: a page built correctly and then dropped by a
    # format-string mistake still leaves the user with no dashboard, and reading
    # the source would not notice.
    dash_line=""
    for _ in $(seq 1 60); do
        dash_line=$(grep 'home-dashboard tiles=' "$UI_LOG" 2>/dev/null | tail -1 || true)
        [[ -n "$dash_line" ]] && break
        sleep 0.25
    done
    [[ -n "$dash_line" ]] || {
        cat "$UI_LOG" >&2
        ui_stop
        fail "the start page never rendered a dashboard"
    }

    tiles=$(printf '%s' "$dash_line" | sed -n 's/.*tiles=\([0-9]*\).*/\1/p')
    [[ -n "$tiles" ]] || { ui_stop; fail "the dashboard line has no tile count: $dash_line"; }
    # Every tile the module builds must reach the page. A partial render would
    # mean a format argument was dropped.
    (( tiles >= 10 )) || { ui_stop; fail "only ${tiles} dashboard tiles reached the page"; }
    pass "the start page rendered ${tiles} dashboard tiles"

    # With Tor connected and nothing wrong, the summary must not invent a problem.
    [[ "$dash_line" == *"attention=0"* ]] \
        || { ui_stop; fail "a healthy start page reported something needing attention: $dash_line"; }
    pass "a healthy start page reports nothing needing attention"

    criticals=$(grep 'CRITICAL' "$UI_LOG" 2>/dev/null | grep -v 'Vulkan\|radv\|/dev/dri' || true)
    ui_stop
    [[ -z "$criticals" ]] || {
        printf '%s\n' "$criticals" | head -3 >&2
        fail "NiOn's widgets reported a critical while drawing the start page"
    }
    pass "no widget criticals while drawing the start page"
else
    echo "SKIP: the window test needs build/nion, xdotool and a display"
fi

# ---------------------------------------------------------------- part 4
mutate() {
    local label="$1" expr="$2"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' %' '--')"
    rm -rf "$dir"; mkdir -p "$dir/src"
    cp "$ROOT/src/privacy-dashboard.c" "$ROOT/src/privacy-dashboard.h" "$dir/src/"
    python3 - "$dir/src/privacy-dashboard.c" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
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

# The whole point: an unmeasured figure must never become a number.
mutate "blocked requests invented" \
    '    add_tile(dashboard, "Requests blocked",
             g_strdup("Not counted"),|||    add_tile(dashboard, "Requests blocked",
             g_strdup("0"),'

# A private window must not be told it has a store of zero entries.
mutate "private window reports a count" \
    '                 g_strdup("None kept"),|||                 g_strdup("0"),'

# A failed filter must not read as active.
mutate "failed filter reads as active" \
    '                 g_strdup("Not active"),|||                 g_strdup("Active"),'

# A failure must be flaggable, or the summary claims everything is fine.
# Reporting "connected" while still bootstrapping is the one lie that matters
# most on this page.
mutate "connecting reported as connected" \
    '        gchar *value = g_strdup_printf("Connecting — %d%%",|||        gchar *value = g_strdup_printf("Connected — %d%%",'

# A failure must be flaggable, or the summary tells the user everything is fine
# while the filter is dead.
mutate "failures never flagged" \
    '                 g_strdup("The filter list did not load, so filtering is not "
                          "running. The rest of the protections are unaffected."),
                 NION_DASH_KNOWN, TRUE);|||                 g_strdup("The filter list did not load, so filtering is not "
                          "running. The rest of the protections are unaffected."),
                 NION_DASH_KNOWN, FALSE);'

# A private window that claims no history is written must actually say so.
mutate "private window claims disk writes" \
    '                 g_strdup("Nothing from this window"),|||                 g_strdup("Nothing unusual"),'

echo "NiOn PRIVACY DASHBOARD (T2-8): PASS"
