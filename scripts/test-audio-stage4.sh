#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "missing src/main.c"

grep -q 'audio-volume-high-symbolic' "$SRC" || fail "speaker icon missing"
grep -q 'audio-volume-muted-symbolic' "$SRC" || fail "muted icon missing"
grep -q '"is-playing-audio"' "$SRC" || fail "WebKit is-playing-audio binding missing"
grep -q 'notify::is-muted' "$SRC" || fail "WebKit is-muted notification missing"
grep -q 'webkit_web_view_get_is_muted' "$SRC" || fail "mute getter missing"
grep -q 'webkit_web_view_set_is_muted' "$SRC" || fail "mute setter missing"
grep -q 'g_key_file_set_boolean(session, group, "muted"' "$SRC" || fail "session mute persistence missing"
grep -q 'g_key_file_has_key(session, group, "muted"' "$SRC" || fail "backward-compatible mute restore missing"
if grep -q 'g_menu_append(menu, "Bookmark This Page"' "$SRC"; then
    fail "Bookmark This Page still present in hamburger menu"
fi
grep -q '"bookmark-page", action_bookmark_page' "$SRC" || fail "Ctrl+D bookmark action unexpectedly removed"
grep -Eq '(app|win)\.bookmark-page' "$SRC" || fail "Ctrl+D accelerator target missing"

pass "tab audio indicator wiring"
pass "per-tab mute toggle"
pass "session mute persistence"
pass "Bookmark This Page removed from hamburger only"
echo "STAGE 4 STATIC CHECK: PASS"
