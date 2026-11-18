#!/usr/bin/env bash
# Reading list: pages saved for offline reading (roadmap T1-5).
#
# Part 1 runs the real store and parser: tests/reading_list_test.c is linked
# against src/reading-list.c, src/history.c and src/util.c from the production
# tree, so the rules are exercised, not grepped.
#
# Part 2 asserts the wiring a harness cannot see: the saved page must be text
# rendered by GTK rather than by WebKit, a Private Window must have no path, and
# nothing in this feature may reach the network.
#
# Part 3 mutation-tests part 1 so a detector that stopped detecting fails loud.
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
pkg-config --exists gtk4 || fail "gtk4 dev files missing"
pkg-config --exists webkitgtk-6.0 || fail "webkitgtk-6.0 dev files missing"
pkg-config --exists javascriptcoregtk-6.0 || fail "javascriptcoregtk-6.0 dev files missing"

PKGS="gtk4 webkitgtk-6.0 javascriptcoregtk-6.0"
CFLAGS=$(pkg-config --cflags $PKGS)
LIBS=$(pkg-config --libs $PKGS)

build() {
    local dir="$1" out="$2"
    shift 2
    cc -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
       -I"$dir/src" -I"$ROOT/build" \
       "$@" \
       $CFLAGS \
       -o "$out" \
       "$ROOT/tests/reading_list_test.c" \
       "$dir/src/reading-list.c" \
       "$dir/src/history.c" \
       "$dir/src/util.c" \
       $LIBS 2>"$BUILD_DIR/cc.log"
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
build "$ROOT" "$BUILD_DIR/reading_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "reading list tests did not compile"
}
run_clean "reading list behaviour" "$BUILD_DIR/reading_test"
pass "store, gate, thresholds and payload parsing, compiled from src/reading-list.c"

# The parser is the part a malformed page reaches, so the same run under the
# sanitizer is required rather than optional: a truncation bug would otherwise
# show up as a stale read that "works".
build "$ROOT" "$BUILD_DIR/reading_asan" -fsanitize=address -g || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "reading list tests did not compile with -fsanitize=address"
}
ASAN_OPTIONS="detect_leaks=1:abort_on_error=0" \
    run_clean "reading list behaviour under AddressSanitizer" "$BUILD_DIR/reading_asan"
pass "no memory error in the store or the parser (AddressSanitizer)"

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
module = read("reading-list.c")
header = read("reading-list.h")
app_c = read("app.c")
main_c = read("main.c")
ui = read("ui.c")

problems = []

# meson.build is the mechanical switch. This exact omission already cost a
# link-time failure once during T1-4.
if "'src/reading-list.c'" not in meson:
    problems.append("meson.build does not list src/reading-list.c")
if "javascriptcoregtk-6.0" not in meson:
    problems.append("meson.build does not declare the javascriptcoregtk dependency")

# Offline means offline: a saved page must be text, never a copy of the page.
if "webkit_web_view_new" in module:
    problems.append("reading-list.c creates a WebView: a saved page must not be a page")
if "gtk_text_view_new" not in module:
    problems.append("reading-list.c does not render saved text with a GtkTextView")
if 'webkit_web_view_evaluate_javascript' not in module:
    problems.append("reading-list.c does not extract the page content")
if "document.body" not in module or "cloneNode" not in module:
    problems.append("the extraction script does not read the page body")

# No network call anywhere in this module.
for needle in ("soup_session", "soup_session_new", "g_network", "curl"):
    if needle in module:
        problems.append(f"reading-list.c references {needle}: the reading list must stay local")

# The gate: only what NiOn will actually browse can be saved, and stored entries
# are re-checked on load so a hand-edited file cannot smuggle a scheme in.
if "nion_history_uri_is_recordable" not in module:
    problems.append("reading-list.c does not apply the history URI gate")
if module.count("nion_history_uri_is_recordable") < 3:
    problems.append(
        "the URI gate is applied fewer than three times; save, load and the "
        "extracted address each need it"
    )

# Private Window: no path, no list, and the save path refuses explicitly rather
# than relying only on a missing path.
if "app->is_private" not in module:
    problems.append("reading-list.c does not refuse a Private Window explicitly")
private_block = re.search(r"nion_reading_list_add\(NionApp \*app(.*?)\n\}\n", module, re.S)
if not private_block:
    problems.append("nion_reading_list_add has no body to inspect")
else:
    body = private_block.group(1)
    if "app->is_private" not in body:
        problems.append("nion_reading_list_add does not check is_private")
    if "!app->reading_list_file" not in body:
        problems.append("nion_reading_list_add does not require a store path")

if "app->reading_list_file = g_build_filename" not in app_c:
    problems.append("the normal window never builds a reading list path")
# The private window must not receive one. Check that the copy block mentions it
# as deliberately omitted rather than assigning it.
if re.search(r"app->reading_list_file = g_strdup\(owner->reading_list_file\)", app_c):
    problems.append("a Private Window copies the reading list path from the owner")
if "reading_list_file is left NULL" not in app_c:
    problems.append("app.c does not record why a private window has no reading list")

# Actions are reachable and the shortcuts do not collide with existing ones.
if '{ "reading-list", action_reading_list' not in main_c:
    problems.append("the reading-list action is not registered")
if '{ "save-for-later", action_save_for_later' not in main_c:
    problems.append("the save-for-later action is not registered")
if '"win.save-for-later"' not in main_c or '"win.reading-list"' not in main_c:
    problems.append("the new actions have no accelerators")

accels = re.findall(r'const gchar \*(\w+_accels)\[\] = \{ ([^}]*)\}', main_c)
seen = {}
for name, body in accels:
    for combo in re.findall(r'"([^"]+)"', body):
        seen.setdefault(combo.lower(), []).append(name)
dupes = {c: names for c, names in seen.items() if len(names) > 1}
if dupes:
    problems.append("accelerator collision: " + ", ".join(
        f"{c} used by {'/'.join(n)}" for c, n in sorted(dupes.items())))

# The menu must not offer the feature in a window that cannot use it.
if 'if (!app->is_private) {' not in ui:
    problems.append("ui.c does not keep the history/reading entries out of a Private Window")
if '"win.save-for-later"' not in ui or '"win.reading-list"' not in ui:
    problems.append("the menu does not offer the reading list actions")

# Destructive clearing must default to Cancel.
if "gtk_alert_dialog_set_default_button(dialog, 1)" not in module:
    problems.append("the clear-list dialog does not default to Cancel")
if "gtk_alert_dialog_set_cancel_button(dialog, 1)" not in module:
    problems.append("the clear-list dialog does not wire a cancel button")

# Bounds exist and are used.
for constant in ("NION_MAX_READING_LIST", "NION_MAX_READING_TEXT_BYTES",
                 "NION_MIN_READABLE_CHARS", "NION_READING_FORMAT"):
    if f"#define {constant}" not in types:
        problems.append(f"{constant} is not defined in types.h")
if "nion_reading_prune" not in module:
    problems.append("the list is never pruned to its cap")

# The saved page must say what it is not, so nobody expects images offline.
if "images are not kept" not in module:
    problems.append("the offline reader does not tell the user that images are not kept")

# Fail-closed phrasing: no promise of a direct connection.
for phrase in ("fall back to direct", "bypass tor", "connect directly"):
    if phrase in module.lower():
        problems.append(f"reading-list.c contains bypass phrasing: {phrase}")

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: reading list wiring (text-only offline reader, private window has "
      "no store, gate applied, shortcuts distinct)")
PY

# ---------------------------------------------------------------- part 3
mutate() {
    local label="$1" file="$2" expr="$3" sanitize="${4:-}"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' %' '--')"
    rm -rf "$dir"; mkdir -p "$dir/src"
    cp "$ROOT/src/reading-list.c" "$ROOT/src/reading-list.h" \
       "$ROOT/src/history.c" "$ROOT/src/history.h" \
       "$ROOT/src/util.c" "$ROOT/src/util.h" "$ROOT/src/types.h" "$dir/src/"
    python3 - "$dir/src/$file" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
import sys
path, expr = sys.argv[1], sys.argv[2]
old, new = expr.split("|||")
text = open(path, encoding="utf-8").read()
if old not in text:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
PY
    local flags=()
    [[ -n "$sanitize" ]] && flags=(-fsanitize=address -g)
    build "$dir" "$dir/test" "${flags[@]}" || fail "mutation '$label' did not compile"
    if ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:exitcode=1" \
        "$dir/test" >"$dir/run.log" 2>&1; then
        cat "$dir/run.log" >&2
        fail "mutation '$label' survived: the harness does not catch it"
    fi
    pass "mutation caught: $label"
}

# A Private Window that saves would keep the trail the window exists to avoid.
mutate "private window saves anyway" "reading-list.c" \
    "    if (!app || app->is_private || !app->reading_list_file || !app->reading_list)
        return FALSE;|||    if (!app || !app->reading_list_file || !app->reading_list)
        return FALSE;"

# Without the gate, a saved page could carry a scheme NiOn refuses to browse.
mutate "save gate dropped" "reading-list.c" \
    "    if (!nion_history_uri_is_recordable(uri))
        return FALSE;|||    if (FALSE)
        return FALSE;"

# And the same gate on load, so a hand-edited file cannot smuggle one in.
mutate "load gate dropped" "reading-list.c" \
    "        if (nion_history_uri_is_recordable(uri) && text && *text) {|||        if (text && *text) {"

# A login wall or an error page must not become reading material.
mutate "threshold dropped" "reading-list.c" \
    "    if (strlen(text) < NION_MIN_READABLE_CHARS)
        return FALSE;|||    if (FALSE)
        return FALSE;"

# Saving twice must refresh, not duplicate.
mutate "duplicate rows allowed" "reading-list.c" \
    "    if (existing >= 0) {|||    if (FALSE) {"

# Newest first is the order the window and the prune both assume.
mutate "new saves appended last" "reading-list.c" \
    "    g_ptr_array_insert(app->reading_list, 0, entry);|||    g_ptr_array_add(app->reading_list, entry);"

# The cap keeps the file rewritable in one go.
mutate "list never pruned" "reading-list.c" \
    "    while (entries->len > max_entries) {|||    while (FALSE) {"

# A length-prefixed payload that ignores its own lengths reads past the buffer
# it was given. That is undefined behaviour, not a wrong return value, so the
# mutation is judged under the sanitizer: without the length check, strndup
# reads outside the payload and AddressSanitizer stops the run.
mutate "payload lengths not enforced" "reading-list.c" \
    "        if ((guint64)length > available)
            goto fail;|||        if (FALSE)
            goto fail;" \
    "asan"

# Truncation must land on a UTF-8 boundary or the saved page starts with half a
# character.
mutate "truncation splits UTF-8" "reading-list.c" \
    "        while (cut > 0 && ((guchar)out->str[cut] & 0xC0) == 0x80)
            cut--;|||        (void)0;"

echo "NiOn READING LIST (T1-5): PASS"
