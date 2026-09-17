#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

# NiOn 2.2.0 release guard: bridges, cross-distribution container testing and
# the C library floor that the 2.1.0 AppImage got wrong.

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ "$NION_VERSION" == "2.2.0" ]] || fail "expected NiOn 2.2.0"
[[ "$NION_RELEASE_STATUS" == "Stable" ]] || fail "2.2.0 must be Stable"
[[ "$NION_APPSTREAM_RELEASE_TYPE" == "stable" ]] || fail "AppStream release must be stable"
pass "stable release metadata"

# --- Pinned release environment ----------------------------------------------
[[ "$NION_GLIBC_FLOOR" == "2.39" ]] || fail "supported C library floor changed unexpectedly"
[[ "$NION_GTK_MIN_VERSION" == "4.10" ]] || fail "GTK minimum compatibility floor changed unexpectedly"
[[ "$NION_WEBKITGTK_MIN_VERSION" == "2.40" ]] || fail "WebKitGTK minimum compatibility floor changed unexpectedly"
[[ "$NION_GTK_TESTED_VERSION" == "4.14.5" ]] || fail "GTK release baseline mismatch"
[[ "$NION_WEBKITGTK_TESTED_VERSION" == "2.52.6" ]] || fail "WebKitGTK release baseline mismatch"
[[ "$NION_GLIB_TESTED_VERSION" == "2.80.0" ]] || fail "GLib release baseline mismatch"
pass "minimum floors vs pinned release baseline"

grep -Fq 'Stable release: 2.2.0' README.md || fail "README stable marker missing"
grep -Fq 'Stable GTK baseline       4.14.5' BUILDING.md || fail "BUILDING GTK release baseline missing"
grep -Fq 'Stable WebKitGTK baseline 2.52.6' BUILDING.md || fail "BUILDING WebKitGTK release baseline missing"
grep -Fq 'Stable GLib baseline      2.80.0' BUILDING.md || fail "BUILDING GLib release baseline missing"
grep -Fq 'glibc 2.39' README.md || fail "README does not state the supported C library floor"
grep -Fq 'glibc 2.39' BUILDING.md || fail "BUILDING does not state the supported C library floor"
grep -rFq 'Stable dependency baseline' src || fail "About stable baseline missing"
grep -rFq 'NION_GLIBC_FLOOR' src || fail "About does not surface the C library floor"
pass "dependency/floor provenance surfaces"

# --- The guard that turns the 2.1.0 loader failure into a build failure -------
[[ -x scripts/check-glibc-floor.sh ]] || fail "glibc floor checker missing"
grep -Fq 'check-glibc-floor.sh' scripts/build-appimage.sh || fail "build-appimage.sh does not enforce the glibc floor"
grep -Fq 'GLIBC-REQUIRED' scripts/build-appimage.sh || fail "build-appimage.sh does not record the required glibc"
grep -Fq 'GLIBC-REQUIRED' packaging/AppRun || fail "AppRun does not read the required glibc"
grep -Fq 'NiOn cannot start on this system' packaging/AppRun || fail "AppRun has no friendly incompatible-libc message"
grep -Fq 'GlibcFloor=' scripts/build-appimage.sh || fail "BUILD-INFO does not record the glibc floor"
pass "build-time glibc gate and runtime loader guard present"

# --- Container smoke test wiring ---------------------------------------------
grep -Fq 'test-appimage-containers.sh' scripts/release-preflight.sh || fail "container smoke test is not wired into preflight"
grep -Fq 'NION_REQUIRE_CONTAINER_TEST' scripts/release-preflight.sh || fail "preflight cannot make the container test mandatory"
grep -Fq 'NION_REQUIRE_CONTAINER_TEST' .github/workflows/release.yml || fail "CI does not require the container smoke test"
grep -Fq 'runs-on: ubuntu-24.04' .github/workflows/release.yml || fail "CI release runner is not the pinned environment"
grep -Fq 'Verify pinned release environment' .github/workflows/release.yml || fail "CI does not verify the runner C library"
grep -Fq 'test-bridges-stage1.sh' scripts/release-preflight.sh || fail "bridge regression is not in preflight"
grep -Fq "$(basename "$0")" scripts/release-preflight.sh || fail "this guard is not in preflight"
[[ -x scripts/build-release-container.sh ]] || fail "pinned container build script missing"
pass "container smoke test, container build and both guards are wired in"

# --- Bridge feature invariants (see test-bridges-stage1.sh for the detail) ----
grep -Fq 'UseBridges 1' src/bridge.c || fail "UseBridges missing"
grep -Fq 'ClientTransportPlugin' src/bridge.c || fail "ClientTransportPlugin missing"
grep -Fq 'nion_bridges_active' src/tor-core.c || fail "bridge mode is not reported at Tor start-up"
grep -rFq 'WEBKIT_NETWORK_PROXY_MODE_CUSTOM' src || fail "Tor custom proxy missing"
grep -rFq 'socks://127.0.0.1:9' src || fail "dead-SOCKS fail-closed guard missing"
grep -rFq 'webkit_settings_set_enable_webrtc(settings, FALSE)' src || fail "WebRTC hardening missing"
pass "2.2.0 security/fail-closed invariants"

# Obsolete / deprecated Tor and GTK/WebKit call sites cleaned up in 2.2.0.
# Exclude the test suite itself: this very assertion contains the string it
# searches for, so a self-match would make the check always fail.
if grep -Rqs --exclude='test-*.sh' 'WarnUnsafeSocks 1' src/ scripts/ packaging/; then
  fail "obsolete WarnUnsafeSocks option still written to the torrc"
fi
grep -Fq 'WarnUnsafeSocks was removed from this torrc' src/tor-core.c || fail "WarnUnsafeSocks removal is undocumented"
grep -Fq 'gtk_css_provider_load_from_string' src/ui.c || fail "GTK 4.12+ CSS provider API is not used"
grep -Fq 'GTK_CHECK_VERSION(4, 12, 0)' src/ui.c || fail "GTK 4.10 compatibility path for the CSS provider was dropped"
grep -Fq 'WEBKIT_CHECK_VERSION(2, 52, 0)' src/privacy.c || fail "deprecated hyperlink-auditing call is not version-guarded"
pass "obsolete Tor option and deprecated GTK/WebKit call sites cleaned up"

if grep -RInE --exclude='CHANGELOG.md' --exclude='TESTING.md' \
  --exclude='test-hardening-stage3-2.2.0.sh' --exclude='test-hardening-stage3-2.1.0.sh' \
  'Current development: 2\.[01]\.0|stable release: 2\.1\.0' README.md BUILDING.md PRIVACY.md SECURITY.md data release/manifest 2>/dev/null; then
  fail "stale development/release marker remains on release surfaces"
fi
pass "no stale release marker"

echo 'NION 2.2.0 FINAL HARDENING CHECK: PASS'
