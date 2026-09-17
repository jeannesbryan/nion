#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

# This file is the 2.1.0 *feature* regression guard. It is deliberately
# version-independent: the release-specific metadata assertions for the current
# release live in test-hardening-stage3-2.2.0.sh, so a version bump does not
# silently retire these invariants.
[[ "$NION_RELEASE_STATUS" == "Stable" ]] || fail "NiOn must be released as Stable"
[[ "$NION_APPSTREAM_RELEASE_TYPE" == "stable" ]] || fail "AppStream release must be stable"
[[ "$NION_GTK_MIN_VERSION" == "4.10" ]] || fail "GTK minimum compatibility floor changed unexpectedly"
[[ "$NION_WEBKITGTK_MIN_VERSION" == "2.40" ]] || fail "WebKitGTK minimum compatibility floor changed unexpectedly"
pass "release status and compatibility floors"

grep -rFq 'Stable dependency baseline' src || fail "About stable baseline missing"
grep -Fq 'GTKStableBaseline=$NION_GTK_TESTED_VERSION' scripts/build-appimage.sh || fail "AppImage BUILD-INFO GTK stable baseline missing"
grep -Fq 'outside the preferred stable' scripts/build-appimage.sh || fail "AppImage unstable dependency warning missing"
pass "dependency provenance surfaces"

grep -rFq 'webkit_navigation_action_is_user_gesture' src || fail "Escape Guard user-gesture check missing"
grep -rFq 'NION_SECURITY_SAFEST' src || fail "Security Level Safest implementation missing"
grep -rFq 'WEBKIT_NETWORK_PROXY_MODE_CUSTOM' src || fail "Tor custom proxy missing"
grep -rFq 'socks://127.0.0.1:9' src || fail "dead-SOCKS fail-closed guard missing"
grep -rFq 'webkit_settings_set_enable_webrtc(settings, FALSE)' src || fail "WebRTC hardening missing"
pass "2.1.0 security/fail-closed invariants"

if grep -RInE --exclude='CHANGELOG.md' --exclude='TESTING.md' --exclude='test-hardening-stage3-2.1.0.sh' \
  'Current development: 2\.0\.0|development — Stage [12]|Development Stage [12]' README.md BUILDING.md PRIVACY.md SECURITY.md data release/manifest 2>/dev/null; then
  fail "stale 2.1.0 development marker remains on release surfaces"
fi
pass "no stale development release marker"

echo 'NION 2.1.0 FINAL HARDENING CHECK: PASS'
