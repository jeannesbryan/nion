#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
README="$ROOT/README.md"
ROADMAP="$ROOT/ROADMAP.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "src/main.c missing"

# Bookmark toolbar improvement.
grep -q 'GtkWidget \*bookmark_button;' "$SRC" || fail "bookmark toolbar button missing"
grep -q 'non-starred-symbolic' "$SRC" || fail "unbookmarked icon state missing"
grep -q 'starred-symbolic' "$SRC" || fail "bookmarked icon state missing"
grep -q 'nion-bookmark-active' "$SRC" || fail "bookmarked visual state missing"
grep -q 'nion_update_bookmark_button' "$SRC" || fail "bookmark state synchronizer missing"
grep -q 'nion_toggle_current_bookmark' "$SRC" || fail "bookmark toolbar toggle missing"
grep -q 'tab->home_page || tab->error_page' "$SRC" || fail "blank/error bookmark guard missing"
python3 - "$SRC" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text()
order = [
    'gtk_box_append(GTK_BOX(toolbar), app->address);',
    'gtk_box_append(GTK_BOX(toolbar), app->bookmark_button);',
    'gtk_box_append(GTK_BOX(toolbar), app->menu_button);',
]
pos = [s.find(x) for x in order]
if any(x < 0 for x in pos) or pos != sorted(pos):
    raise SystemExit('bookmark button is not between address bar and hamburger menu')
PY

# Per-site website-data removal.
grep -q '"clear-site-data", action_clear_site_data' "$SRC" || fail "clear-site-data action missing"
grep -q 'Clear Data for This Site…' "$SRC" || fail "clear-site-data menu entry missing"
grep -q 'webkit_website_data_manager_fetch' "$SRC" || fail "website-data fetch missing"
grep -q 'webkit_website_data_get_name' "$SRC" || fail "website-data host matching missing"
grep -q 'webkit_website_data_manager_remove' "$SRC" || fail "website-data remove missing"
grep -q 'WEBKIT_WEBSITE_DATA_ALL' "$SRC" || fail "site data type coverage missing"
grep -q 'webkit_web_view_reload_bypass_cache' "$SRC" || fail "post-clear cache-bypass reload missing"
grep -q 'Clear Browsing Data…' "$SRC" || fail "global clear-data action was lost"

grep -Eq '(Current development: 1\.2\.[01]|Current release: 1\.1\.0 Stable|Stage [34] \((Clear Data for This Site|Tab Audio Indicator & Mute)\))' "$README" || fail "README does not identify a Stage 3+ / stable 1.1.0 build"
grep -q 'Stage 3 — Clear Data for This Site ✅' "$ROADMAP" || fail "ROADMAP Stage 3 not complete"

pass "Bookmark toolbar + per-site data clearing source invariants"
echo "STAGE 3 STATIC CHECK: PASS"
