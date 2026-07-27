#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ "$NION_VERSION" == "$(tr -d '\r\n' < release/manifest/NION_VERSION)" ]] || fail "manifest loader version mismatch"
pass "manifest loader"

grep -Fq "version: files('release/manifest/NION_VERSION')" meson.build || fail "Meson is not reading canonical version file"
grep -Fq "fs.read('release/manifest/TOR_DAEMON_VERSION')" meson.build || fail "Meson is not reading canonical Tor daemon version"
grep -Fq "conf.set_quoted('NION_TOR_DAEMON_VERSION'" meson.build || fail "Tor daemon version is not generated into config.h"
grep -Fq "conf.set_quoted('NION_RELEASE_STATUS'" meson.build || fail "release status is not generated into config.h"
grep -Fq "fs.read('release/manifest/GTK_TESTED_VERSION')" meson.build || fail "GTK stable baseline is not manifest-driven"
grep -Fq "fs.read('release/manifest/WEBKITGTK_TESTED_VERSION')" meson.build || fail "WebKitGTK stable baseline is not manifest-driven"
grep -Fq "fs.read('release/manifest/GLIB_TESTED_VERSION')" meson.build || fail "GLib stable baseline is not manifest-driven"
grep -Fq "conf.set_quoted('NION_GTK_TESTED_VERSION'" meson.build || fail "GTK stable baseline is not generated into config.h"
pass "Meson/config.h integration"

grep -Fq '@NION_VERSION@' data/io.github.jeannesbryan.Nion.metainfo.xml.in || fail "AppStream version placeholder missing"
grep -Fq '@APPSTREAM_RELEASE_TYPE@' data/io.github.jeannesbryan.Nion.metainfo.xml.in || fail "AppStream release type placeholder missing"
pass "AppStream template"

for script in \
  scripts/build-appimage.sh \
  scripts/fetch-tor-runtime.sh \
  scripts/repair-tor-runtime.sh \
  scripts/run-dev.sh \
  scripts/make-source-archive.sh \
  scripts/release-preflight.sh \
  scripts/test-appimage.sh \
  scripts/test-appimage-containers.sh; do
  grep -Fq 'scripts/manifest.sh' "$script" || fail "$script does not consume manifest"
done
pass "release/runtime scripts consume manifest"

# Release-critical implementation files must not pin these values directly.
for literal_file in NION_VERSION TOR_BROWSER_VERSION TOR_DAEMON_VERSION TOR_SIGNING_FINGERPRINT APPIMAGE_ARCH \
  GTK_MIN_VERSION WEBKITGTK_MIN_VERSION GTK_TESTED_VERSION WEBKITGTK_TESTED_VERSION GLIB_TESTED_VERSION; do
  [[ -s "release/manifest/$literal_file" ]] || fail "missing release/manifest/$literal_file"
done

if grep -RInE --exclude='manifest.sh' --exclude='test-manifest-1.2.1.sh' \
    --exclude-dir=release/manifest \
    'VERSION="[0-9]+\.[0-9]+\.[0-9]+"|TOR_DAEMON_VERSION="[0-9]|TOR_BROWSER_VERSION="[0-9]|EXPECTED_TOR_VERSION="[0-9]' \
    scripts src packaging meson.build >/tmp/nion-manifest-hardcodes.$$ 2>/dev/null; then
  cat /tmp/nion-manifest-hardcodes.$$ >&2
  rm -f /tmp/nion-manifest-hardcodes.$$
  fail "release-critical hardcoded version remains outside manifest"
fi
rm -f /tmp/nion-manifest-hardcodes.$$
pass "no release-critical shell/C version pins outside manifest"

grep -Fq "dist/\${{ env.NION_APPIMAGE_BASENAME }}" .github/workflows/release.yml || fail "workflow asset name is not manifest-driven"
grep -Fq '"v$NION_VERSION"' .github/workflows/release.yml || fail "workflow tag guard missing"
pass "GitHub release workflow"

# Render the AppStream template exactly as Meson will conceptually render the two
# release fields, then verify XML syntax and current version presence.
python3 - "$NION_VERSION" "$NION_APPSTREAM_RELEASE_TYPE" <<'PY'
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
version, release_type = sys.argv[1:]
text = Path('data/io.github.jeannesbryan.Nion.metainfo.xml.in').read_text()
text = text.replace('@NION_VERSION@', version).replace('@APPSTREAM_RELEASE_TYPE@', release_type)
root = ET.fromstring(text)
ns = root.find('releases')
if ns is None or not any(r.attrib.get('version') == version and r.attrib.get('type') == release_type for r in ns):
    raise SystemExit('rendered AppStream release does not match manifest')
PY
pass "rendered AppStream metadata"

echo "NION 1.2.1 MANIFEST STATIC CHECK: PASS"
