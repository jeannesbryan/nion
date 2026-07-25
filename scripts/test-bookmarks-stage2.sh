#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
README="$ROOT/README.md"
ROADMAP="$ROOT/ROADMAP.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "src/main.c missing"

grep -q 'bookmarks.ini' "$SRC" || fail "persistent bookmarks file missing"
grep -q '"bookmark-page", action_bookmark_page' "$SRC" || fail "bookmark-page action missing"
grep -q '"bookmarks", action_bookmarks' "$SRC" || fail "bookmarks manager action missing"
grep -q '"<Primary>d"' "$SRC" || fail "Ctrl+D accelerator missing"
grep -q 'gtk_list_box_append' "$SRC" || fail "bookmarks list UI missing"
grep -q 'on_bookmark_rename_clicked' "$SRC" || fail "rename action missing"
grep -q 'on_bookmark_delete_clicked' "$SRC" || fail "delete action missing"
grep -q 'nion_bookmark_uri_exists' "$SRC" || fail "duplicate URL check missing"
grep -q 'NION_MAX_BOOKMARKS_FILE_BYTES' "$SRC" || fail "bookmark profile size bound missing"
grep -q 'nion_quarantine_profile_file(app->bookmarks_file, "bookmarks")' "$SRC" || fail "bookmark corruption quarantine missing"
grep -q '^## Bookmarks' "$README" || fail "README bookmarks section missing"
grep -q 'Stage 2 — Simple Bookmarks ✅' "$ROADMAP" || fail "ROADMAP Stage 2 not complete"

pass "Simple Bookmarks source invariants"
echo "STAGE 2 STATIC CHECK: PASS"
