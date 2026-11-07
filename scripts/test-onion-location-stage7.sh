#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
CHANGELOG="$ROOT/CHANGELOG.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

# 1. The Onion-Location advert must be gated on a successful document response.
#    A 404 or 500 page that still carries the header must not offer the jump.
grep -rq 'webkit_uri_response_get_status_code' "$SRC" \
  && grep -rq 'status >= 200 && status < 300' "$SRC" \
  || fail "Onion-Location is not gated on a 2xx document response"
pass "Onion-Location requires a 2xx document response"

# 2. The gate must be established before the header is read, and it must also
#    block the HTML meta fallback. The meta path runs after the page_ok check,
#    so a page that failed the gate can never reach the JavaScript evaluation.
python3 - "$SRC/main.c" <<'PY' || exit 1
import re, sys

path = sys.argv[1]
src = open(path, encoding="utf-8").read()

# Match the definition (followed by a brace), not the forward declaration at
# the top of the file. Searching for the bare name finds the prototype first
# and silently measures an empty body.
m = re.search(r"static void\s+nion_detect_onion_location\s*\([^)]*\)\s*\{", src)
if not m:
    print("FAIL: nion_detect_onion_location definition not found", file=sys.stderr)
    sys.exit(1)
start = m.start()
end = src.find("\nstatic ", m.end())
body = src[start:end if end > 0 else len(src)]

problems = []

status_at = body.find("webkit_uri_response_get_status_code")
header_at = body.find('soup_message_headers_get_one')
meta_at = body.find("evaluate_javascript")
gate_at = body.find("if (!page_ok)")

if status_at < 0:
    problems.append("no status code read")
if header_at < 0:
    problems.append("no header read")
if status_at > header_at >= 0:
    problems.append("header is read before the status is checked")
if meta_at < 0:
    problems.append("meta fallback missing entirely")
if gate_at < 0:
    problems.append("meta fallback is not gated on page_ok")
elif gate_at > meta_at:
    problems.append("page_ok gate sits after the meta evaluation")

if problems:
    print("FAIL: " + "; ".join(problems), file=sys.stderr)
    sys.exit(1)
print("PASS: status gate precedes the header read and gates the meta fallback")
PY

# 3. The badge carries a drawn onion mark rather than a bare text label.
grep -rq 'draw_onion_mark' "$SRC" \
  && grep -rq 'gtk_drawing_area_set_draw_func' "$SRC" \
  || fail "onion mark is not drawn onto the badge"
pass "onion mark drawn on the badge"

if grep -rq 'gtk_button_new_with_label("Onion")' "$SRC"; then
  fail "onion button is still a bare text label"
fi
grep -rq 'gtk_label_new("Onion")' "$SRC" \
  && grep -rq 'gtk_button_set_child(GTK_BUTTON(app->onion_button)' "$SRC" \
  || fail "onion button no longer carries the mark plus label"
pass "onion button shows mark plus label"

# 4. The existing safety rules must survive the change: only an https clearnet
#    page may advertise, and the candidate has to be a valid .onion URI.
grep -rq 'nion_uri_is_https_clearnet(page_uri)' "$SRC" \
  && grep -rq 'nion_uri_is_onion(candidate)' "$SRC" \
  && grep -rq 'nion_validate_uri(candidate, &validation)' "$SRC" \
  || fail "Onion-Location validation rules were weakened"
pass "clearnet-only and valid .onion validation preserved"

# 5. The persistent memory of a preferred onion must still be written only from
#    the fresh detection path, never from a clear.
grep -rq 'nion_remember_preferred_onion(tab->app, site_key, normalized)' "$SRC" \
  && grep -rq 'if (normalized && tab->app && !tab->app->is_private' "$SRC" \
  || fail "preferred-onion persistence rules changed"
pass "preferred-onion persistence unchanged"

echo "ALL CHECKS PASSED"
