#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
README="$ROOT/README.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "src/main.c missing"

grep -q '"context-menu", G_CALLBACK(on_webview_context_menu)' "$SRC" \
  || fail "WebView context-menu handler is not connected"
pass "page context-menu handler"

grep -q 'WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK_IN_NEW_WINDOW' "$SRC" \
  && grep -q 'Open Link in New Tab' "$SRC" \
  || fail "link new-window action is not adapted to a NiOn tab"
pass "Open Link in New Tab"

grep -q 'WEBKIT_CONTEXT_MENU_ACTION_OPEN_IMAGE_IN_NEW_WINDOW' "$SRC" \
  && grep -q 'Open Image in New Tab' "$SRC" \
  || fail "image new-window action is not adapted to a NiOn tab"
pass "Open Image in New Tab"

grep -q 'WEBKIT_CONTEXT_MENU_ACTION_DOWNLOAD_IMAGE_TO_DISK' "$SRC" \
  && grep -q '"Save Image"' "$SRC" \
  || fail "Save Image context action missing"
pass "Save Image via WebKit download action"

grep -q 'webkit_print_operation_new' "$SRC" \
  && grep -q 'webkit_print_operation_run_dialog' "$SRC" \
  || fail "WebKit print operation missing"
pass "WebKit print dialog"

grep -q '{ "print", action_print' "$SRC" \
  && grep -q '"<Primary>p"' "$SRC" \
  || fail "Ctrl+P action missing"
pass "Ctrl+P"

grep -q 'Print / Save as PDF…' "$SRC" \
  || fail "Print / Save as PDF label missing"
pass "Print / Save as PDF UI"

grep -q 'Stable release: 1.3.0' "$README" \
  || fail "README stable release marker missing"
grep -q 'Better web-page context menu' "$README" \
  || fail "README page context-menu documentation missing"
grep -q 'Print / Save as PDF' "$README" \
  || fail "README print/PDF documentation missing"
pass "release documentation"

echo "NION 1.2.0 STAGE 2 STATIC CHECK: PASS"
