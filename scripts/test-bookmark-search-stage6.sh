#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SRC=src/main.c
README=README.md
ROADMAP=ROADMAP.md

fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
pass() { printf 'PASS: %s\n' "$*"; }

grep -q 'GtkWidget \*bookmarks_search_entry;' "$SRC" || fail 'bookmark search entry state missing'
grep -q 'GtkWidget \*bookmarks_result_label;' "$SRC" || fail 'bookmark result counter missing'
grep -q 'gtk_search_entry_new' "$SRC" || fail 'GtkSearchEntry UI missing'
grep -q 'Search bookmarks by title or URL' "$SRC" || fail 'search placeholder missing'
grep -q 'gtk_search_entry_set_key_capture_widget' "$SRC" || fail 'type-to-search key capture missing'
grep -q '"search-changed"' "$SRC" || fail 'delayed search-changed signal missing'
grep -q 'nion_bookmark_matches_search' "$SRC" || fail 'bookmark search matcher missing'
grep -q 'g_utf8_casefold' "$SRC" || fail 'Unicode case-insensitive search missing'
grep -q 'g_strsplit_set(query_fold, " \\t\\r\\n", -1)' "$SRC" || fail 'multi-term search tokenization missing'
grep -q 'No bookmarks match your search.' "$SRC" || fail 'no-results state missing'
grep -q 'live title/URL search' "$README" || fail 'README Bookmark Search documentation missing'
grep -q 'Stage 6 — Bookmark Search ✅' "$ROADMAP" || fail 'ROADMAP Stage 6 not complete'
grep -q 'Search by title or URL' "$README" || fail 'README bookmark search documentation missing'
grep -q 'Bookmark search (1.2.0 Stage 6)' PRIVACY.md || fail 'PRIVACY local-search disclosure missing'

pass 'bookmark title/URL live search UI'
pass 'case-insensitive multi-term matcher'
pass 'search result/empty-state feedback'
pass 'Stage 6 documentation'
printf 'NION 1.2.0 STAGE 6 STATIC CHECK: PASS\n'
