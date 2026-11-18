#!/usr/bin/env bash
# Local browsing history (roadmap T1-1).
#
# Part 1 runs the real store code: tests/history_test.c is linked against
# src/history.c and src/util.c from the production tree, so the rules are
# exercised, not grepped. Part 2 asserts the wiring that the harness cannot see
# (where a visit is recorded, when it is refused, what New Identity deletes).
# Part 3 mutation-tests part 1 so a detector that stopped detecting fails loud.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists gtk4 || fail "gtk4 dev files missing"
pkg-config --exists webkitgtk-6.0 || fail "webkitgtk-6.0 dev files missing"

CFLAGS=$(pkg-config --cflags gtk4 webkitgtk-6.0)
LIBS=$(pkg-config --libs gtk4 webkitgtk-6.0)

build_and_run() {
    local dir="$1" out="$2"
    cc -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
       -I"$dir/src" -I"$ROOT/build" \
       $CFLAGS \
       -o "$out" \
       "$ROOT/tests/history_test.c" \
       "$dir/src/history.c" \
       "$dir/src/util.c" \
       $LIBS 2>"$BUILD_DIR/cc.log"
}

# ---------------------------------------------------------------- part 1
build_and_run "$ROOT" "$BUILD_DIR/history_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "history tests did not compile"
}

"$BUILD_DIR/history_test" >"$BUILD_DIR/run.log" 2>&1
RC=$?
cat "$BUILD_DIR/run.log"
[[ $RC -eq 0 ]] || fail "history behaviour tests failed (exit $RC)"
grep -q '^0 failed$' "$BUILD_DIR/run.log" || \
    grep -qE '[0-9]+ checks, 0 failed' "$BUILD_DIR/run.log" || \
    fail "harness did not report a clean run"
pass "history store behaviour, compiled from src/history.c and src/util.c"

# ---------------------------------------------------------------- part 2
python3 - "$SRC" "$ROOT/meson.build" <<'PY' || exit 1
import os
import re
import sys

src = sys.argv[1]
meson = open(sys.argv[2], encoding="utf-8").read()

def read(name):
    return open(os.path.join(src, name), encoding="utf-8").read()

types = read("types.h")
main_c = read("main.c")
webview = read("webview.c")
app_c = read("app.c")
ui_c = read("ui.c")
site_data = read("site-data.c")
history = read("history.c")

problems = []

# meson.build is the mechanical switch: a missing entry means an unbuilt module.
if "'src/history.c'" not in meson:
    problems.append("meson.build does not list src/history.c")

# The limits the rules depend on must exist and be the values documented.
for define, value in (
    ("NION_MAX_HISTORY", "2000"),
    ("NION_HISTORY_RETENTION_DAYS", "90"),
    ("NION_MAX_HISTORY_FILE_BYTES", None),
):
    if define not in types:
        problems.append(f"{define} missing from src/types.h")
    elif value and not re.search(rf"#define\s+{define}\s+{value}\b", types):
        problems.append(f"{define} is not {value} in src/types.h")

# Action, accelerator, load call, callback registration.
if not re.search(r'\{\s*"history"\s*,\s*action_history', main_c):
    problems.append("main.c does not register the win.history action")
if 'win.history' not in main_c or '<Primary>h' not in main_c:
    problems.append("main.c does not bind Ctrl+H to win.history")
if not re.search(r"nion_history_load\s*\(\s*app\s*\)", main_c):
    problems.append("main.c never loads the history store at startup")
if not re.search(r"nion_history_set_callbacks\s*\(\s*&", main_c):
    problems.append("main.c does not register NionHistoryCallbacks")

# A visit is recorded at LOAD_FINISHED, and only for a page that really loaded.
finished = re.search(r"case WEBKIT_LOAD_FINISHED:(.*?)break;", webview, re.S)
if not finished:
    problems.append("webview.c has no WEBKIT_LOAD_FINISHED branch")
else:
    body = finished.group(1)
    if "nion_history_record" not in body:
        problems.append("webview.c does not record history on LOAD_FINISHED")
    else:
        # The call must sit behind the same guards as the rest of the branch:
        # a failed load, an error page or the internal home page is not a visit.
        guard = re.search(
            r"if\s*\(\s*!tab->load_failed\s*&&\s*!tab->error_page\s*&&\s*!tab->home_page\s*\)\s*\{?\s*\n?\s*nion_history_record",
            body,
        )
        if not guard:
            problems.append(
                "nion_history_record is not guarded by "
                "!load_failed && !error_page && !home_page"
            )

# A late title may fill a placeholder, never overwrite a good title.
if "nion_history_upgrade_title" not in webview:
    problems.append("webview.c never upgrades a placeholder title")

# New Identity must wipe the trail.
identity = re.search(r"void action_new_identity\(.*?\n\}", app_c, re.S)
if not identity:
    problems.append("app.c has no action_new_identity to inspect")
elif "nion_history_clear" not in identity.group(0):
    problems.append("New Identity does not clear the local history")

# A Private Window must not receive the history path. Copying it is exactly the
# mistake that would turn private browsing into a recorded trail.
if re.search(r"app->history_file\s*=\s*g_strdup\s*\(\s*owner->history_file", app_c):
    problems.append("private window copies the history file path (must stay NULL)")

# The private browser must not be able to open the normal window's store.
if "app->is_private" not in ui_c or "win.history" not in ui_c:
    problems.append("ui.c does not gate the History entry on a non-private window")
history_window = re.search(r"static void\s*\n?nion_show_history\(NionApp \*app\)(.*?)\n\}\n", history, re.S)
if not history_window:
    problems.append("history.c has no nion_show_history to inspect")
elif "app->is_private" not in history_window.group(1):
    problems.append("nion_show_history does not refuse a Private Window")

# "Forget this site" must forget the addresses too.
forget = re.search(r"static void nion_forget_site_local_state\(.*?\n\}", site_data, re.S)
if not forget:
    problems.append("site-data.c has no nion_forget_site_local_state to inspect")
elif "nion_history_forget_site" not in forget.group(0):
    problems.append("Forget This Site leaves the URL in the history")

# The store itself must refuse private writes at two levels: the guard and the
# missing path. Both are asserted in code, not only in the harness.
record_fn = re.search(r"void\s*\nnion_history_record\(NionApp \*app.*?\n\}", history, re.S)
if not record_fn:
    problems.append("history.c has no nion_history_record to inspect")
else:
    body = record_fn.group(0)
    if "app->is_private" not in body:
        problems.append("nion_history_record has no private-window guard")
    if "app->history_file" not in body:
        problems.append("nion_history_record does not require a history path")

if problems:
    for p in problems:
        print("FAIL: " + p)
    sys.exit(1)

print("PASS: history wiring (menu, Ctrl+H, LOAD_FINISHED guard, New Identity "
      "wipe, private-window refusals, Forget This Site)")
PY

# ---------------------------------------------------------------- part 3
# Mutation testing: each mutation must make the harness fail. A mutation that
# survives means the assertion protecting that rule is not real.
mutate() {
    local label="$1" file="$2" expr="$3"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' ' '-')"
    rm -rf "$dir"; mkdir -p "$dir"
    cp -r "$ROOT/src" "$dir/src"
    python3 - "$dir/src/$file" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
import sys
path, expr = sys.argv[1], sys.argv[2]
old, new = expr.split("|||")
text = open(path, encoding="utf-8").read()
if old not in text:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
PY
    build_and_run "$dir" "$dir/history_test" || fail "mutation '$label' did not compile"
    if "$dir/history_test" >"$dir/run.log" 2>&1; then
        cat "$dir/run.log" >&2
        fail "mutation '$label' survived: the harness does not catch it"
    fi
    pass "mutation caught: $label"
}

mutate "private window starts recording" "history.c" \
    "if (!app || app->is_private || !app->history_file)|||if (!app || !app->history_file)"

mutate "expired entries never dropped" "history.c" \
    "now > 0 && entry->last_visit + retention < now|||FALSE"

mutate "clear leaves the file on disk" "history.c" \
    "if (g_unlink(app->history_file) != 0)|||if (FALSE)"

mutate "non-http URIs accepted" "history.c" \
    '(g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0)|||(scheme != NULL)'

echo "NiOn LOCAL HISTORY (T1-1): PASS"
