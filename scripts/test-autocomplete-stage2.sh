#!/usr/bin/env bash
# Local-only address bar autocomplete (roadmap T1-2).
#
# Part 1 runs the real ranking code: tests/autocomplete_test.c is linked against
# src/autocomplete.c from the production tree, so the rules are exercised, not
# grepped. Part 1b repeats the same run under AddressSanitizer, because the
# module hands owned memory to its caller and a borrowing mistake would only
# show up as a stale read in a plain build. Part 2 asserts the wiring a harness
# cannot see (what must never fill the address bar directly, and how the list is
# built). Part 3 mutation-tests parts 1 and 1b so a detector that stopped
# detecting fails loud.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

# Keep any future use of g_dir_make_tmp inside BUILD_DIR, so a run that aborts
# mid-test (the mutation builds do exactly that) still leaves nothing in /tmp.
export TMPDIR="$BUILD_DIR/tmp"
mkdir -p "$TMPDIR"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists gtk4 || fail "gtk4 dev files missing"
pkg-config --exists webkitgtk-6.0 || fail "webkitgtk-6.0 dev files missing"

CFLAGS=$(pkg-config --cflags gtk4 webkitgtk-6.0)
LIBS=$(pkg-config --libs gtk4 webkitgtk-6.0)

# $1 = tree to compile from, $2 = output binary, $3.. = extra flags
build() {
    local dir="$1" out="$2"
    shift 2
    cc -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
       -I"$dir/src" -I"$ROOT/build" \
       "$@" \
       $CFLAGS \
       -o "$out" \
       "$ROOT/tests/autocomplete_test.c" \
       "$dir/src/autocomplete.c" \
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
build "$ROOT" "$BUILD_DIR/autocomplete_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "autocomplete tests did not compile"
}
run_clean "autocomplete behaviour" "$BUILD_DIR/autocomplete_test"
pass "ranking behaviour, compiled from src/autocomplete.c and src/history.c"

# ---------------------------------------------------------------- part 1b
# The ownership test is only real under a sanitizer: without it, reading freed
# memory usually "works", which is exactly how the bug got in. If the sanitizer
# cannot be built at all, that is a failure, not a skip: silently dropping the
# check would put us back where we started.
build "$ROOT" "$BUILD_DIR/autocomplete_asan" -fsanitize=address -g || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "autocomplete tests did not compile with -fsanitize=address"
}
ASAN_OPTIONS="detect_leaks=1:abort_on_error=0" \
    run_clean "autocomplete behaviour under AddressSanitizer" "$BUILD_DIR/autocomplete_asan"
pass "no use-after-free in the suggestion result (AddressSanitizer)"

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
ui_h = read("ui.h")
webview = read("webview.c")
main_c = read("main.c")
autocomplete = read("autocomplete.c")
bookmarks = read("bookmarks.c")
history = read("history.c")
downloads = read("downloads.c")
settings = read("settings.c")
site_data = read("site-data.c")
app_c = read("app.c")
per_site = read("per-site.c")
session_c = read("session.c")
tabs_c = read("tabs.c")
network_c = read("network.c")
bridge = read("bridge.c")

problems = []

# meson.build is the mechanical switch: a missing entry means an unbuilt module.
if "'src/autocomplete.c'" not in meson:
    problems.append("meson.build does not list src/autocomplete.c")

# The cap is a documented constant, not a magic number in the UI.
if not re.search(r"#define\s+NION_AUTOCOMPLETE_MAX_RESULTS\s+8\b", types):
    problems.append("NION_AUTOCOMPLETE_MAX_RESULTS is not 8 in src/types.h")

# The list must be a revealer plus a list box, never a popover: an overlay
# popover above an accelerated WebKitGTK view flickers on light X11 WMs.
if "GtkPopover" in ui and "autocomplete_popover" in ui:
    problems.append("autocomplete uses a GtkPopover")
for needle in ("gtk_revealer_new", "gtk_list_box_new",
               "gtk_revealer_set_child", "gtk_box_reorder_child_after"):
    if needle not in ui:
        problems.append(f"ui.c does not build the suggestion list with {needle}")
if "app->autocomplete_revealer, toolbar" not in ui:
    problems.append("the suggestion list is not placed under the toolbar")

# One keystroke rebuilds the list, and activation navigates.
if '"changed", G_CALLBACK(on_autocomplete_entry_changed)' not in ui:
    problems.append("typing in the address bar does not refresh the list")
if '"row-activated"' not in ui:
    problems.append("list rows are not activatable")
if "nion_go_to_address(app)" not in ui:
    problems.append("activating a row does not navigate")

# The row must own its URI: the item it was built from dies with the result.
row_fn = re.search(r"nion_autocomplete_build_row\(NionAutocompleteItem \*item\)(.*?)\n\}\n", ui, re.S)
if not row_fn:
    problems.append("ui.c has no nion_autocomplete_build_row to inspect")
elif 'g_object_set_data_full(G_OBJECT(row), "nion-uri", g_strdup(item->uri), g_free)' not in row_fn.group(1):
    problems.append("a suggestion row does not keep its own copy of the URI")

# THE TRAP: programmatic fills must not open the list. Every write to the
# address bar goes through one setter, and no other file may touch it directly.
if not re.search(r"void\s+nion_set_address_text\(NionApp \*app, const gchar \*text\)", ui):
    problems.append("ui.c has no nion_set_address_text")
setter = re.search(r"void\s+nion_set_address_text\(NionApp \*app, const gchar \*text\)(.*?)\n\}\n", ui, re.S)
if not setter:
    problems.append("nion_set_address_text has no body to inspect")
else:
    body = setter.group(1)
    if "app->address_updating = TRUE" not in body or "app->address_updating = FALSE" not in body:
        problems.append("nion_set_address_text does not bracket the write with address_updating")
    if "nion_autocomplete_hide" not in body:
        problems.append("nion_set_address_text does not dismiss the list")
if "void nion_set_address_text(NionApp *app, const gchar *text);" not in ui_h:
    problems.append("nion_set_address_text is not declared in src/ui.h")

# Exactly one write may exist in the whole tree, and it must be the one inside
# the setter. Anywhere else it would silently reopen the suggestion list (or,
# worse, leave the flag raised).
write_re = re.compile(r"gtk_editable_set_text\s*\(\s*GTK_EDITABLE\(\s*app->address\s*\)")
writes = {}
for name in os.listdir(src):
    if not name.endswith(".c"):
        continue
    hits = write_re.findall(read(name))
    if hits:
        writes[name] = len(hits)

stray = {name: count for name, count in writes.items() if name != "ui.c" or count != 1}
if stray:
    problems.append(
        "address bar written outside nion_set_address_text: "
        + ", ".join(f"{name} x{count}" for name, count in sorted(stray.items()))
    )
if "ui.c" not in writes:
    problems.append("nion_set_address_text does not actually write the address bar")
elif setter and write_re.search(setter.group(1)) is None:
    problems.append("the only address bar write is not inside nion_set_address_text")

# The refresh itself must refuse to work while the bar is filled from code.
refresh = re.search(r"nion_autocomplete_refresh\(NionApp \*app\)(.*?)\n\}\n", ui, re.S)
if not refresh:
    problems.append("ui.c has no nion_autocomplete_refresh to inspect")
else:
    body = refresh.group(1)
    if "app->address_updating" not in body:
        problems.append("the refresh does not check address_updating")
    if "app->tor_ready" not in body:
        problems.append("the refresh does not check tor_ready")
    if "nion_autocomplete_suggest" not in body:
        problems.append("the refresh does not use the tested ranking model")

# webview.c updates the bar on every URI change; it must go through the hook so
# the suppression applies without webview.c depending on ui.h.
if "set_address_text" not in webview:
    problems.append("webview.c still fills the address bar without the setter hook")
if "nion_set_address_text(app, uri)" not in webview:
    problems.append("webview.c does not use the address setter for URI changes")
if ".set_address_text = nion_set_address_text" not in main_c:
    problems.append("main.c never registers the webview set_address_text hook")

# Local only: the model must not reach the network in any way.
for needle in ("webkit", "soup", "curl", "http_get", "g_network"):
    if needle in autocomplete.lower().replace("http://", "").replace("https://", ""):
        problems.append(f"src/autocomplete.c mentions {needle}: suggestions must stay local")

# The URI gate is the one the history store owns, so a suggestion can never
# offer a scheme NiOn refuses to browse.
if "nion_history_uri_is_recordable" not in autocomplete:
    problems.append("the candidate collector does not apply the history URI gate")
gate_hits = autocomplete.count("nion_history_uri_is_recordable")
if gate_hits < 3:
    problems.append(
        f"the URI gate is applied {gate_hits} time(s); history, bookmarks and open "
        "tabs must each be filtered"
    )

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: autocomplete wiring (revealer list, one address setter, "
      "programmatic fill suppression, local-only collector, URI gate)")
PY

# ---------------------------------------------------------------- part 3
# Each mutation must make a harness fail. A mutation that survives means the
# assertion protecting that rule is not real.
mutate() {
    local label="$1" file="$2" expr="$3" sanitize="${4:-}"
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
    local flags=()
    [[ -n "$sanitize" ]] && flags=(-fsanitize=address -g)
    build "$dir" "$dir/test" "${flags[@]}" || fail "mutation '$label' did not compile"
    if ASAN_OPTIONS="detect_leaks=0:abort_on_error=0" \
        "$dir/test" >"$dir/run.log" 2>&1; then
        cat "$dir/run.log" >&2
        fail "mutation '$label' survived: the harness does not catch it"
    fi
    pass "mutation caught: $label"
}

# One row per address. Without the dedupe every source shows up separately and
# a single page can bury the rest of the list.
mutate "duplicate rows per address" "autocomplete.c" \
    "if (!duplicate)|||if (TRUE)"

# The address-prefix tier. Without it a title match can outrank the address the
# user is literally in the middle of typing.
mutate "prefix tier flattened" "autocomplete.c" \
    "    if (g_str_has_prefix(body, query_fold))
        score = 0;|||    if (g_str_has_prefix(body, query_fold))
        score = 1;"

# Every title term must match, not just one of them.
mutate "title matches on a single term" "autocomplete.c" \
    "if (any && all)|||if (any)"

# Only browsable schemes may be suggested.
mutate "history gate dropped" "autocomplete.c" \
    "if (!entry || !nion_history_uri_is_recordable(entry->uri))|||if (!entry)"

mutate "bookmark gate dropped" "autocomplete.c" \
    "if (!bookmark || !nion_history_uri_is_recordable(bookmark->uri))|||if (!bookmark)"

mutate "open tab gate dropped" "autocomplete.c" \
    "if (!item || !nion_history_uri_is_recordable(item->uri))|||if (!item)"

# Recency and source order inside one tier.
mutate "recency order reversed" "autocomplete.c" \
    "return left->item->recency > right->item->recency ? -1 : 1;|||return left->item->recency > right->item->recency ? 1 : -1;"

mutate "source priority reversed" "autocomplete.c" \
    "return left->item->source < right->item->source ? -1 : 1;|||return left->item->source < right->item->source ? 1 : -1;"

# The ownership contract, checked with the sanitizer: returning the ranked view
# of a collection that is about to be freed is a use-after-free.
mutate "result borrowed from a freed collection" "autocomplete.c" \
    "    GPtrArray *owned = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);|||    g_ptr_array_unref(collected);
    return ranked;
    GPtrArray *owned = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);" \
    "asan"

echo "NiOn ADDRESS BAR AUTOCOMPLETE (T1-2): PASS"
