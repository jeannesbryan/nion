#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ "$NION_VERSION" == "1.7.0" ]] || fail "expected NiOn 1.7.0"
[[ "$NION_RELEASE_STATUS" == "Stable" ]] || fail "1.7.0 must be Stable"
[[ "$NION_APPSTREAM_RELEASE_TYPE" == "stable" ]] || fail "AppStream release must be stable"
pass "stable release metadata"

[[ "$NION_GTK_MIN_VERSION" == "4.10" ]] || fail "GTK minimum compatibility floor changed unexpectedly"
[[ "$NION_WEBKITGTK_MIN_VERSION" == "2.40" ]] || fail "WebKitGTK minimum compatibility floor changed unexpectedly"
[[ "$NION_GTK_TESTED_VERSION" == "4.22.4" ]] || fail "GTK stable baseline mismatch"
[[ "$NION_WEBKITGTK_TESTED_VERSION" == "2.52.5" ]] || fail "WebKitGTK stable baseline mismatch"
[[ "$NION_GLIB_TESTED_VERSION" == "2.88.2" ]] || fail "GLib stable baseline mismatch"
pass "minimum vs stable-baseline dependency metadata"

grep -Fq 'Stable release: 1.7.0' README.md || fail "README stable marker missing"
grep -Fq 'Stable GTK baseline       4.22.4' BUILDING.md || fail "BUILDING GTK stable baseline missing"
grep -Fq 'Stable WebKitGTK baseline 2.52.5' BUILDING.md || fail "BUILDING WebKitGTK stable baseline missing"
grep -Fq 'Stable GLib baseline      2.88.2' BUILDING.md || fail "BUILDING GLib stable baseline missing"
grep -Fq 'Stable dependency baseline' src/main.c || fail "About stable baseline missing"
grep -Fq 'GTKStableBaseline=$NION_GTK_TESTED_VERSION' scripts/build-appimage.sh || fail "AppImage BUILD-INFO GTK stable baseline missing"
grep -Fq 'outside the preferred stable' scripts/build-appimage.sh || fail "AppImage unstable dependency warning missing"
pass "dependency provenance surfaces"

grep -Fq 'webkit_navigation_action_is_user_gesture' src/main.c || fail "Escape Guard user-gesture check missing"
grep -Fq 'NION_SECURITY_SAFEST' src/main.c || fail "Security Level Safest implementation missing"
grep -Fq 'WEBKIT_NETWORK_PROXY_MODE_CUSTOM' src/main.c || fail "Tor custom proxy missing"
grep -Fq 'socks://127.0.0.1:9' src/main.c || fail "dead-SOCKS fail-closed guard missing"
grep -Fq 'webkit_settings_set_enable_webrtc(settings, FALSE)' src/main.c || fail "WebRTC hardening missing"
pass "1.7.0 security/fail-closed invariants"

if grep -RInE --exclude='CHANGELOG.md' --exclude='TESTING.md' --exclude='test-hardening-stage3-1.7.0.sh' \
  'Current development: 1\.7\.0|development — Stage [12]|Development Stage [12]' README.md BUILDING.md PRIVACY.md SECURITY.md data release/manifest 2>/dev/null; then
  fail "stale 1.7.0 development marker remains on release surfaces"
fi
pass "no stale development release marker"

echo 'NION 1.7.0 FINAL HARDENING CHECK: PASS'
