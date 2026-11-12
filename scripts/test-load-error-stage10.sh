#!/usr/bin/env bash
# Guard for the "false error page" bug fixed in NiOn 2.2.5.
#
# Symptom the user reported: open https://jeannesbryan.my.id/, click the
# ./nostr link, and NiOn shows "Website unavailable through Tor / Frame load
# interrupted"; only a manual reload opens the page.
#
# Cause: the strict-HTTPS upgrade ignores the http:// policy decision and loads
# the https:// twin. WebKitGTK 2.52 reports the dropped frame with
# WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE, and NiOn only
# treated WEBKIT_NETWORK_ERROR_CANCELLED as an interruption, so the healthy
# https load was replaced by an error page.
#
# The classification lives in src/load-error.c and is compiled from that exact
# file here, so this test cannot pass against a copy of the logic.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists webkitgtk-6.0 || fail "webkitgtk-6.0 dev files missing"

echo "false error page guard (stage 10)"

# --- 1. the classification itself, from the production source ---
cc -std=c11 -O1 -Wall -Wextra -Werror \
   $(pkg-config --cflags webkitgtk-6.0 glib-2.0) \
   -o "$BUILD_DIR/load_error_test" \
   "$ROOT/tests/load_error_test.c" \
   "$SRC/load-error.c" \
   $(pkg-config --libs webkitgtk-6.0 glib-2.0) \
   || fail "test harness did not compile"
"$BUILD_DIR/load_error_test" | tail -4 || fail "load error classification failed"
pass "classification compiled from src/load-error.c"

# --- 2. wiring rules that a compiled unit test cannot see ---
python3 - "$SRC/webview.c" "$SRC/load-error.c" "$ROOT/meson.build" <<'PY' || exit 1
import re, sys

webview = open(sys.argv[1], encoding="utf-8").read()
load_error = open(sys.argv[2], encoding="utf-8").read()
meson = open(sys.argv[3], encoding="utf-8").read()

problems = []

# The header must exist as its own module so it can be unit tested.
if "src/load-error.c" not in meson:
    problems.append("src/load-error.c not registered in meson.build")

# The unit must actually reference both codes; a helper that only knows one of
# them would silently reintroduce the bug.
for needle in ("WEBKIT_NETWORK_ERROR_CANCELLED",
               "WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE"):
    if needle not in load_error:
        problems.append(f"{needle} missing from src/load-error.c")

if "nion_load_error_is_navigation_interruption" not in webview:
    problems.append("load-failed handler does not call the classifier")

# The classifier must be consulted BEFORE the generic error page is built.
fn = webview.find("on_webview_load_failed")
if fn == -1:
    problems.append("on_webview_load_failed not found")
else:
    body = webview[fn:]
    end = body.find("\ngboolean on_webview_tls_failed")
    body = body[:end] if end != -1 else body
    # The classifier must be called with the real error object. A call with
    # anything else (NULL, a fresh error, a different variable) compiles fine
    # and always classifies as "page failure", silently restoring the bug.
    call_match = re.search(r"nion_load_error_is_navigation_interruption\s*\(\s*error\s*\)", body)
    call = call_match.start() if call_match else -1
    error_page = body.find("Website unavailable through Tor")
    if not call_match:
        problems.append("classifier is not called with the load error itself")
    elif error_page != -1 and call > error_page:
        problems.append("classifier runs after the generic error page is built")
    # The old duplicated CANCELLED branch must be gone: one decision point only.
    if "WEBKIT_NETWORK_ERROR_CANCELLED" in body:
        problems.append("legacy CANCELLED branch still present in webview.c")

# No diagnostic scaffolding may ship.
if "TEMP-DIAG" in webview:
    problems.append("TEMP-DIAG scaffolding left in src/webview.c")

if problems:
    for p in problems:
        print(f"FAIL  {p}", file=sys.stderr)
    sys.exit(1)

print("PASS  wiring: classifier consulted before any error page is built")
print("PASS  wiring: single decision point, no leftover diagnostics")
PY

echo
echo "STAGE 10 GUARD: PASS"
