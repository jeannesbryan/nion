#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src/main.c"
README="$ROOT/README.md"
CHANGELOG="$ROOT/CHANGELOG.md"
PRIVACY="$ROOT/PRIVACY.md"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC" ]] || fail "src/main.c missing"

grep -q 'gchar \*source_uri;' "$SRC" \
  && grep -q 'webkit_download_get_request(download)' "$SRC" \
  && grep -q 'webkit_uri_request_get_uri(request)' "$SRC" \
  || fail "download source URI capture missing"
pass "download source URI capture"

grep -q '"source-uri"' "$SRC" \
  && grep -q 'NION_MAX_SAVED_URI_BYTES' "$SRC" \
  || fail "bounded persistent source URI missing"
pass "bounded source URI history"

grep -q 'gtk_menu_button_new()' "$SRC" \
  && grep -q 'Open File' "$SRC" \
  && grep -q 'Open Containing Folder' "$SRC" \
  && grep -q 'Copy Download Link' "$SRC" \
  && grep -q 'Retry Failed Download' "$SRC" \
  || fail "Stage 5 download actions menu missing"
pass "download actions menu"

grep -q 'g_app_info_launch_default_for_uri' "$SRC" \
  && grep -q 'g_filename_to_uri(item->destination' "$SRC" \
  && grep -q 'g_filename_to_uri(parent' "$SRC" \
  || fail "local Open File/Folder handling missing"
pass "local file/folder launch"

grep -q 'gdk_display_get_clipboard' "$SRC" \
  && grep -q 'gdk_clipboard_set_text(clipboard, item->source_uri)' "$SRC" \
  || fail "Copy Download Link clipboard path missing"
pass "Copy Download Link"

grep -q 'g_strcmp0(item->history_status, "Failed") == 0' "$SRC" \
  && grep -q 'nion_download_source_retryable(item->source_uri)' "$SRC" \
  && grep -q '!app->tor_ready || app->tor_failed' "$SRC" \
  && grep -q 'webkit_web_view_download_uri(tab->web_view, item->source_uri)' "$SRC" \
  || fail "fail-closed retry path missing"
pass "Tor-gated failed-download retry"

grep -q 'nion_validate_uri(uri, NULL)' "$SRC" \
  || fail "retry URL validation missing"
pass "retry URL validation"

grep -q 'Copy Download Link' "$README" \
  || fail "README download actions documentation missing"
grep -q 'Stage 5: Download Improvements' "$CHANGELOG" \
  || fail "CHANGELOG Stage 5 entry missing"
grep -q '^## Download source history' "$PRIVACY" \
  || fail "PRIVACY source-URI disclosure missing"
pass "download documentation"

echo "NION 1.2.0 STAGE 5 STATIC CHECK: PASS"
