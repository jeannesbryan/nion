#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

grep -rq 'audio-volume-high-symbolic' "$SRC" || fail "speaker icon missing"
grep -rq 'audio-volume-muted-symbolic' "$SRC" || fail "muted icon missing"
grep -rq '"is-playing-audio"' "$SRC" || fail "WebKit is-playing-audio binding missing"
grep -rq 'notify::is-muted' "$SRC" || fail "WebKit is-muted notification missing"
grep -rq 'webkit_web_view_get_is_muted' "$SRC" || fail "mute getter missing"
grep -rq 'webkit_web_view_set_is_muted' "$SRC" || fail "mute setter missing"
grep -rq 'g_key_file_set_boolean(session, group, "muted"' "$SRC" || fail "session mute persistence missing"
grep -rq 'g_key_file_has_key(session, group, "muted"' "$SRC" || fail "backward-compatible mute restore missing"
if grep -rq 'g_menu_append(menu, "Bookmark This Page"' "$SRC"; then
    fail "Bookmark This Page still present in hamburger menu"
fi
grep -rq '"bookmark-page", action_bookmark_page' "$SRC" || fail "Ctrl+D bookmark action unexpectedly removed"
grep -rEq '(app|win)\.bookmark-page' "$SRC" || fail "Ctrl+D accelerator target missing"

pass "tab audio indicator wiring"
pass "per-tab mute toggle"
pass "session mute persistence"
pass "Bookmark This Page removed from hamburger only"
echo "STAGE 4 STATIC CHECK: PASS"
