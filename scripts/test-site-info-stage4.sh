#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
README="$ROOT/README.md"
CHANGELOG="$ROOT/CHANGELOG.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -d "$SRC" ]] || fail "src/ C sources missing"

grep -rq 'site_info_button = gtk_' "$SRC" \
  && grep -rq 'nion_site_info_row("Host"' "$SRC" \
  && grep -rq 'nion_site_info_row("Connection"' "$SRC" \
  && grep -rq 'nion_site_info_row("Route"' "$SRC" \
  && grep -rq 'nion_site_info_row("Mixed content"' "$SRC" \
  && grep -rq 'nion_site_info_row("Address"' "$SRC" \
  || fail "site/connection information UI missing"
pass "site/connection information UI"

grep -rq 'webkit_web_view_get_tls_info(tab->web_view' "$SRC" \
  && grep -rq 'tab->connection_committed = TRUE' "$SRC" \
  || fail "TLS information is not tied to committed navigation"
pass "TLS information after navigation commit"

grep -rq 'webkit_network_session_set_tls_errors_policy' "$SRC" \
  && grep -rq 'WEBKIT_TLS_ERRORS_POLICY_FAIL' "$SRC" \
  || fail "explicit strict TLS policy missing"
pass "strict TLS error policy"

grep -rq 'g_signal_connect(tab->web_view, "insecure-content-detected"' "$SRC" \
  && grep -rq 'WEBKIT_INSECURE_CONTENT_RUN' "$SRC" \
  && grep -rq 'WEBKIT_INSECURE_CONTENT_DISPLAYED' "$SRC" \
  || fail "WebKit mixed-content signal handling missing"
pass "mixed-content signal handling"

grep -rq 'tab->mixed_content_displayed = FALSE' "$SRC" \
  && grep -rq 'tab->mixed_content_run = FALSE' "$SRC" \
  && grep -rq 'tab->mixed_content_other = FALSE' "$SRC" \
  || fail "mixed-content state reset missing"
pass "per-navigation mixed-content reset"

grep -rq 'dialog-warning-symbolic' "$SRC" \
  && grep -rq 'MIXED CONTENT DETECTED' "$SRC" \
  || fail "visible mixed-content warning missing"
pass "visible mixed-content warning"

grep -rq 'is_onion ? "Onion site information" : "Site information"' "$SRC" \
  && grep -rq 'Onion Service — end-to-end encrypted by Tor' "$SRC" \
  || fail "onion connection distinction missing"
pass "onion connection distinction"

grep -q '^### Site and connection information' "$README" \
  || fail "README site information documentation missing"
grep -q 'Stage 4: Site & Connection Information' "$CHANGELOG" \
  || fail "CHANGELOG Stage 4 entry missing"
pass "Stage 4 documentation"

echo "NION 1.2.0 STAGE 4 STATIC CHECK: PASS"
