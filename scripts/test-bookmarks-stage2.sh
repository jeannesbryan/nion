#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
README="$ROOT/README.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

grep -rq 'bookmarks.ini' "$SRC" || fail "persistent bookmarks file missing"
grep -rq '"bookmark-page", action_bookmark_page' "$SRC" || fail "bookmark-page action missing"
grep -rq '"bookmarks", action_bookmarks' "$SRC" || fail "bookmarks manager action missing"
grep -rq '"<Primary>d"' "$SRC" || fail "Ctrl+D accelerator missing"
grep -rq 'gtk_list_box_append' "$SRC" || fail "bookmarks list UI missing"
grep -rq 'on_bookmark_rename_clicked' "$SRC" || fail "rename action missing"
grep -rq 'on_bookmark_delete_clicked' "$SRC" || fail "delete action missing"
grep -rq 'nion_bookmark_uri_exists' "$SRC" || fail "duplicate URL check missing"
grep -rq 'NION_MAX_BOOKMARKS_FILE_BYTES' "$SRC" || fail "bookmark profile size bound missing"
grep -rq 'nion_quarantine_profile_file(app->bookmarks_file, "bookmarks")' "$SRC" || fail "bookmark corruption quarantine missing"
grep -q '^### Bookmarks' "$README" || fail "README bookmarks section missing"

pass "Simple Bookmarks source invariants"
echo "STAGE 2 STATIC CHECK: PASS"
