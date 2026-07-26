#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
fail(){ printf 'SITE INFO WIDTH FIX CHECK: FAIL: %s\n' "$*" >&2; exit 1; }

grep -Fq 'gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(site_info_scroller), FALSE);' "$SRC" || fail 'natural width propagation is not disabled'
grep -Fq 'gtk_label_set_wrap_mode(GTK_LABEL(value), PANGO_WRAP_WORD_CHAR);' "$SRC" || fail 'generic Site Information values do not use character-fallback wrapping'
grep -Fq 'gtk_label_set_width_chars(GTK_LABEL(value), 1);' "$SRC" || fail 'generic Site Information values still have an unbounded minimum width'
grep -Fq 'gtk_label_set_max_width_chars(GTK_LABEL(value), 48);' "$SRC" || fail 'generic Site Information values have no bounded natural width'
grep -Fq 'gtk_label_set_wrap_mode(GTK_LABEL(app->site_info_uri_label), PANGO_WRAP_CHAR);' "$SRC" || fail 'Address does not hard-wrap long URLs/.onion addresses'
grep -Fq 'gtk_widget_set_size_request(site_info_box, -1, -1);' "$SRC" || fail 'Site Information box still forces a fixed child width'
if grep -Fq 'gtk_widget_set_size_request(site_info_box, 390, -1);' "$SRC"; then
  fail 'legacy fixed-width Site Information box remains'
fi
printf 'SITE INFO WIDTH FIX CHECK: PASS\n'
