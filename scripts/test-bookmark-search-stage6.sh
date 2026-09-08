#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SRC=src
README=README.md

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
pass() { printf 'PASS: %s\n' "$*"; }

grep -rq 'GtkWidget \*bookmarks_search_entry;' "$SRC" || fail 'bookmark search entry state missing'
grep -rq 'GtkWidget \*bookmarks_result_label;' "$SRC" || fail 'bookmark result counter missing'
grep -rq 'gtk_search_entry_new' "$SRC" || fail 'GtkSearchEntry UI missing'
grep -rq 'Search bookmarks by title or URL' "$SRC" || fail 'search placeholder missing'
grep -rq 'gtk_search_entry_set_key_capture_widget' "$SRC" || fail 'type-to-search key capture missing'
grep -rq '"search-changed"' "$SRC" || fail 'delayed search-changed signal missing'
grep -rq 'nion_bookmark_matches_search' "$SRC" || fail 'bookmark search matcher missing'
grep -rq 'g_utf8_casefold' "$SRC" || fail 'Unicode case-insensitive search missing'
grep -rq 'g_strsplit_set(query_fold, " \\t\\r\\n", -1)' "$SRC" || fail 'multi-term search tokenization missing'
grep -rq 'No bookmarks match your search.' "$SRC" || fail 'no-results state missing'
grep -q 'Live search by title and URL' "$README" || fail 'README Bookmark Search documentation missing'
grep -q 'Bookmark search is performed locally/in memory' PRIVACY.md || fail 'PRIVACY local-search disclosure missing'

pass 'bookmark title/URL live search UI'
pass 'case-insensitive multi-term matcher'
pass 'search result/empty-state feedback'
pass 'bookmark search documentation'
printf 'NION 1.2.0 STAGE 6 STATIC CHECK: PASS\n'
