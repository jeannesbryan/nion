#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists glib-2.0 || fail "glib-2.0 dev files missing"

# 1. The pure logic is compiled from the production file and exercised by the
#    harness. This is the part that actually decides what the label reads.
cc -std=c11 -O1 -Wall -Wextra \
   $(pkg-config --cflags glib-2.0) \
   -o "$BUILD_DIR/find_label_test" \
   "$ROOT/tests/find_label_test.c" \
   "$SRC/find-label.c" \
   $(pkg-config --libs glib-2.0)

"$BUILD_DIR/find_label_test" | tail -3 || fail "find label logic tests failed"
pass "find label logic, compiled from src/find-label.c"

# 2. Wire-up rules. These cannot be checked by the harness because they are
#    about signal plumbing, so they are asserted against the source that does it.
python3 - "$SRC/ui.c" "$SRC/main.c" "$SRC/types.h" <<'PY' || exit 1
import re, sys

ui = open(sys.argv[1], encoding="utf-8").read()
main = open(sys.argv[2], encoding="utf-8").read()
types = open(sys.argv[3], encoding="utf-8").read()

problems = []

# The position must be tracked on the app, not guessed.
for field in ("find_total", "find_index", "find_new_search"):
    if field not in types:
        problems.append(f"{field} missing from NionApp")

# A fresh search has to be flagged, otherwise found-text fires for next/prev and
# every step would snap the counter back to 1.
run = re.search(r"static void nion_find_run\(NionApp \*app\)\s*\{", ui)
if not run:
    problems.append("nion_find_run not found")
else:
    body = ui[run.end():ui.find("\nstatic ", run.end()) if ui.find("\nstatic ", run.end()) > 0 else len(ui)]
    if "find_new_search = TRUE" not in body:
        problems.append("a fresh search does not set find_new_search")
    if "webkit_find_controller_search(" not in body:
        problems.append("nion_find_run no longer starts a search")

# on_find_found must consume the flag, and must not reset unconditionally.
found = re.search(r"void on_find_found\([^)]*\)\s*\{", ui)
if not found:
    problems.append("on_find_found not found")
else:
    body = ui[found.end():ui.find("\nvoid on_find_failed", found.end())]
    if "find_new_search" not in body:
        problems.append("on_find_found ignores find_new_search")
    if re.search(r"find_index\s*=\s*1\s*;", body) and "if (app->find_new_search)" not in body:
        problems.append("on_find_found resets the position unconditionally, so steps would not advance")

# Both step handlers must move the position through the shared helper, so the
# clamping rule lives in one place.
for name, direction in (("on_find_prev_clicked", "-1"), ("on_find_next_clicked", "1")):
    m = re.search(r"static void %s\([^)]*\)\s*\{" % name, ui)
    if not m:
        problems.append(f"{name} not found")
        continue
    body = ui[m.end():ui.find("\nstatic ", m.end())]
    if "nion_find_step_position" not in body:
        problems.append(f"{name} does not use nion_find_step_position")
    if direction not in body:
        problems.append(f"{name} passes the wrong direction")

# Closing the bar must clear the state, or reopening would show a stale count.
close = re.search(r"static void nion_find_close\(NionApp \*app\)\s*\{", ui)
if not close:
    problems.append("nion_find_close not found")
else:
    body = ui[close.end():ui.find("\nvoid action_find", close.end())]
    if "find_total = 0" not in body:
        problems.append("closing the find bar does not clear find_total")

# The old wording is gone. "3 matches" without a position was the complaint.
if re.search(r'"%u matches"', ui):
    problems.append('old label format "%u matches" still present')
if re.search(r'"1000\+ matches"', ui):
    problems.append('old label "1000+ matches" still present')

# found-text must still be connected, and failed-to-find-text too.
if '"found-text"' not in main:
    problems.append("found-text is no longer connected")
if '"failed-to-find-text"' not in main:
    problems.append("failed-to-find-text is no longer connected")

if problems:
    for p in problems:
        print(f"FAIL: {p}", file=sys.stderr)
    sys.exit(1)
print("PASS: find bar wiring")
PY

# 3. The new source file must be in the build, or the helper is linked from
#    nowhere and the browser would fail to build.
grep -q "'src/find-label.c'" "$ROOT/meson.build" || fail "find-label.c is not listed in meson.build"
pass "find-label.c listed in meson.build"

grep -q '#include "find-label.h"' "$SRC/ui.c" || fail "ui.c does not include find-label.h"
pass "ui.c includes find-label.h"

# 4. Single source of truth: ui.c must not re-implement the formatting.
if grep -qE 'g_strdup_printf\("%u of ' "$SRC/ui.c"; then
    fail "ui.c formats the label itself instead of calling the shared helper"
fi
pass "label formatting has a single home"

echo "ALL CHECKS PASSED"
