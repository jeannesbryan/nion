#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
README="$ROOT/README.md"
ROADMAP="$ROOT/ROADMAP.md"
CHANGELOG="$ROOT/CHANGELOG.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "src/main.c missing"

grep -q 'site-zoom.ini' "$SRC" \
  && grep -q 'NION_MAX_SITE_ZOOM_FILE_BYTES' "$SRC" \
  && grep -q 'NION_MAX_SITE_ZOOM_ENTRIES 2048' "$SRC" \
  || fail "bounded site zoom profile store missing"
pass "bounded site zoom profile store"

grep -q 'nion_write_key_file_atomic(key_file, app->site_zoom_file)' "$SRC" \
  || fail "site zoom store is not using atomic profile writes"
pass "atomic site zoom persistence"

grep -q 'nion_quarantine_profile_file(app->site_zoom_file, "site zoom")' "$SRC" \
  || fail "site zoom quarantine path missing"
pass "malformed site zoom quarantine"

grep -q 'nion_site_zoom_key_for_uri' "$SRC" \
  && grep -q 'default_port = is_https ? 443 : 80' "$SRC" \
  || fail "site-key normalization missing"
pass "HTTP/HTTPS host key normalization"

grep -q 'nion_apply_site_zoom(tab, committed_uri)' "$SRC" \
  || fail "remembered zoom is not applied at navigation commit"
pass "zoom applied on committed navigation"

grep -q 'percent == NION_ZOOM_DEFAULT_PERCENT' "$SRC" \
  && grep -q 'g_hash_table_remove(app->site_zoom, key)' "$SRC" \
  || fail "Ctrl+0/default zoom override removal path missing"
pass "100% removes remembered override"

home_resets=$(grep -c 'webkit_web_view_set_zoom_level(tab->web_view, 1.0)' "$SRC" || true)
(( home_resets >= 2 )) || fail "Home/error pages are not explicitly reset to 100%"
pass "internal pages reset to 100%"

grep -q 'Per-site zoom memory:' "$README" \
  || fail "README no longer documents Stage 3 per-site zoom"
grep -q 'Stage 3 — Per-Site Zoom Memory ✅' "$ROADMAP" \
  || fail "ROADMAP does not mark Stage 3 complete"
grep -q 'Stage 3: Per-Site Zoom Memory' "$CHANGELOG" \
  || fail "CHANGELOG Stage 3 entry missing"
pass "Stage 3 documentation"

echo "NION 1.2.0 STAGE 3 STATIC CHECK: PASS"
