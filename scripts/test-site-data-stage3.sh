#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
README="$ROOT/README.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

# Bookmark toolbar improvement.
grep -rq 'GtkWidget \*bookmark_button;' "$SRC" || fail "bookmark toolbar button missing"
grep -rq 'non-starred-symbolic' "$SRC" || fail "unbookmarked icon state missing"
grep -rq 'starred-symbolic' "$SRC" || fail "bookmarked icon state missing"
grep -rq 'nion-bookmark-active' "$SRC" || fail "bookmarked visual state missing"
grep -rq 'nion_update_bookmark_button' "$SRC" || fail "bookmark state synchronizer missing"
grep -rq 'nion_toggle_current_bookmark' "$SRC" || fail "bookmark toolbar toggle missing"
grep -rq 'tab->home_page || tab->error_page' "$SRC" || fail "blank/error bookmark guard missing"
python3 - "$SRC" <<'PY'
from pathlib import Path
import sys
s = '\n'.join(p.read_text() for p in sorted(Path(sys.argv[1]).glob('*')))
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
grep -rq '"clear-site-data", action_clear_site_data' "$SRC" || fail "clear-site-data action missing"
grep -rq 'Clear Data for This Site…' "$SRC" || fail "clear-site-data menu entry missing"
grep -rq 'webkit_website_data_manager_fetch' "$SRC" || fail "website-data fetch missing"
grep -rq 'webkit_website_data_get_name' "$SRC" || fail "website-data host matching missing"
grep -rq 'webkit_website_data_manager_remove' "$SRC" || fail "website-data remove missing"
grep -rq 'WEBKIT_WEBSITE_DATA_ALL' "$SRC" || fail "site data type coverage missing"
grep -rq 'webkit_web_view_reload_bypass_cache' "$SRC" || fail "post-clear cache-bypass reload missing"
grep -rq 'win.clear-data' "$SRC" || fail "global browsing-data action was lost"

grep -q 'Clear Data for This Site' "$README" || fail "README lost Clear Data for This Site documentation"

pass "Bookmark toolbar + per-site data clearing source invariants"
echo "STAGE 3 STATIC CHECK: PASS"
