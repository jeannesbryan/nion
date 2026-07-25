#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
source "$ROOT/scripts/manifest.sh"
VERSION="$NION_VERSION"
fail=0

pass() { printf 'PASS  %s\n' "$*"; }
warn() { printf 'WARN  %s\n' "$*"; }
failmsg() { printf 'FAIL  %s\n' "$*" >&2; fail=1; }

printf 'NiOn %s release preflight\n\n' "$VERSION"

for tool in bash grep awk sed find stat sha256sum python3; do
  command -v "$tool" >/dev/null 2>&1 && pass "tool: $tool" || failmsg "missing tool: $tool"
done

printf '\n== Canonical release manifest ==\n'
if ./scripts/test-manifest-1.2.1.sh >/dev/null; then
  pass '1.2.1 version/dependency manifest static check'
else
  failmsg '1.2.1 version/dependency manifest static check failed'
fi
[[ "$VERSION" == "$(tr -d '\r\n' < release/manifest/NION_VERSION)" ]] && pass 'NiOn version loaded from manifest' || failmsg 'NiOn manifest version mismatch'
[[ "$NION_APPIMAGE_BASENAME" == "NiOn-${VERSION}-${NION_APPIMAGE_ARCH}.AppImage" ]] && pass 'AppImage name derived from manifest' || failmsg 'AppImage name derivation mismatch'

printf '\n== Release metadata ==\n'
grep -Fq "version: files('release/manifest/NION_VERSION')" meson.build && pass 'Meson reads canonical version file' || failmsg 'Meson version source mismatch'
grep -Fq '@NION_VERSION@' data/io.github.jeannesbryan.Nion.metainfo.xml.in && pass 'AppStream version is generated' || failmsg 'AppStream version placeholder missing'
grep -Fq "Stable release: $VERSION" README.md && pass 'README stable version' || failmsg 'README stable version mismatch'
grep -q '<project_license>GPL-3.0-or-later</project_license>' data/io.github.jeannesbryan.Nion.metainfo.xml.in && pass 'AppStream project license' || failmsg 'AppStream project license missing'
[[ -s LICENSE ]] && pass 'LICENSE present' || failmsg 'LICENSE missing'
[[ -s .gitignore ]] && pass '.gitignore present' || failmsg '.gitignore missing'
[[ "$NION_RELEASE_STATUS" == 'Stable' ]] && pass 'release status is Stable' || failmsg 'release status is not Stable'
[[ "$NION_APPSTREAM_RELEASE_TYPE" == 'stable' ]] && pass 'AppStream release type is stable' || failmsg 'AppStream release type is not stable'
[[ -s BUILDING.md && -s TESTING.md && -s PRIVACY.md ]] && pass 'core documentation present' || failmsg 'core documentation missing'

printf '\n== Script/XML sanity ==\n'
for script in scripts/*.sh packaging/AppRun; do
  if bash -n "$script"; then
    pass "shell syntax: $script"
  else
    failmsg "shell syntax: $script"
  fi
done
if python3 - "$VERSION" "$NION_APPSTREAM_RELEASE_TYPE" <<'PY' >/dev/null 2>&1
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
version, release_type = sys.argv[1:]
text = Path('data/io.github.jeannesbryan.Nion.metainfo.xml.in').read_text()
text = text.replace('@NION_VERSION@', version).replace('@APPSTREAM_RELEASE_TYPE@', release_type)
ET.fromstring(text)
ET.parse('data/nion.gresource.xml')
PY
then
  pass 'XML/templates parse'
else
  failmsg 'XML/template parse failed'
fi

printf '\n== Privacy/fail-closed invariants ==\n'
if grep -Rqs --exclude='*.md' --exclude='release-preflight.sh' 'ControlPort' src scripts packaging; then
  failmsg 'unexpected Tor ControlPort code remains'
else
  pass 'no Tor ControlPort runtime code'
fi
grep -q 'WEBKIT_NETWORK_PROXY_MODE_CUSTOM' src/main.c && pass 'WebKit custom proxy mode present' || failmsg 'custom proxy mode missing'
grep -q 'socks://127.0.0.1:9' src/main.c && pass 'dead SOCKS proxy on Tor failure present' || failmsg 'dead proxy fail-closed guard missing'
grep -q 'TOR OFFLINE — navigation blocked' src/main.c && pass 'policy-level offline navigation block present' || failmsg 'offline navigation policy guard missing'
grep -q 'ClientRejectInternalAddresses 1' src/main.c && pass 'Tor internal-address rejection present' || failmsg 'Tor internal-address rejection missing'
grep -q 'ClientDNSRejectInternalAddresses 1' src/main.c && pass 'Tor DNS internal-address rejection present' || failmsg 'Tor DNS internal-address rejection missing'
grep -q 'webkit_settings_set_enable_webrtc(settings, FALSE)' src/main.c && pass 'WebRTC disabled' || failmsg 'WebRTC hardening missing'

printf '\n== Profile resilience / 1.2 regressions ==\n'
grep -q 'nion_quarantine_profile_file' src/main.c && pass 'profile quarantine path present' || failmsg 'profile quarantine missing'
grep -q 'NION_MAX_SESSION_FILE_BYTES' src/main.c && pass 'session size bound present' || failmsg 'session size bound missing'
grep -q 'NION_MAX_DOWNLOAD_HISTORY 500' src/main.c && pass 'download history bound present' || failmsg 'download history bound missing'
grep -q 'SQLite header' src/main.c && pass 'cookie SQLite sanity check present' || failmsg 'cookie DB sanity check missing'
for test in \
  scripts/test-zoom-stage3.sh \
  scripts/test-site-info-stage4.sh \
  scripts/test-downloads-stage5.sh \
  scripts/test-bookmark-search-stage6.sh \
  scripts/test-pinned-tabs-stage1.sh \
  scripts/test-private-window-stage2.sh \
  scripts/test-private-downloads-stage3.sh \
  scripts/test-private-session-audit-stage4.sh \
  scripts/test-new-window-related-view.sh \
  scripts/test-context-menu-ownership.sh \
  scripts/test-context-menu-gtk4-signature.sh \
  scripts/test-docs-stage5.sh; do
  if "$test" >/dev/null; then pass "$(basename "$test")"; else failmsg "$(basename "$test") failed"; fi
done

printf '\n== Bundled Tor ==\n'
if [[ -x runtime/tor/tor ]]; then
  tor_version="$(runtime/tor/tor --version 2>&1 | head -n1 || true)"
  if [[ "$tor_version" == *"$NION_TOR_DAEMON_VERSION"* ]]; then
    pass "$tor_version"
  else
    failmsg "unexpected bundled Tor: ${tor_version:-no output}; manifest expects $NION_TOR_DAEMON_VERSION"
  fi
  if [[ -f runtime/tor/MANIFEST.ini ]] && \
     grep -Fqx "tor-browser-version=$NION_TOR_BROWSER_VERSION" runtime/tor/MANIFEST.ini && \
     grep -Fqx "tor-version=$NION_TOR_DAEMON_VERSION" runtime/tor/MANIFEST.ini && \
     grep -Fqx "runtime-layout=$NION_TOR_RUNTIME_LAYOUT" runtime/tor/MANIFEST.ini; then
    pass 'bundled Tor MANIFEST.ini matches canonical release manifest'
  else
    failmsg 'bundled Tor MANIFEST.ini does not match canonical release manifest'
  fi
else
  warn 'Tor runtime not prepared yet; run ./scripts/fetch-tor-runtime.sh'
fi

printf '\n== Native build ==\n'
if [[ -x build/nion ]]; then
  if ldd build/nion 2>/dev/null | grep -q 'not found'; then
    failmsg 'native binary has unresolved libraries'
    ldd build/nion | grep 'not found' >&2 || true
  else
    pass 'native binary has no unresolved ldd dependencies'
  fi
else
  warn 'build/nion not present; run ./scripts/run-dev.sh or meson compile first'
fi

printf '\n== AppImage ==\n'
appimage="dist/$NION_APPIMAGE_BASENAME"
if [[ -x "$appimage" ]]; then
  if ./scripts/test-appimage.sh "$appimage"; then
    pass 'AppImage diagnostic/profile replacement test'
  else
    failmsg 'AppImage test failed'
  fi
else
  warn "$appimage not built yet; run ./scripts/build-appimage.sh"
fi

printf '\n== Existing profile permissions ==\n'
for dir in "${XDG_DATA_HOME:-$HOME/.local/share}/nion" \
           "${XDG_CONFIG_HOME:-$HOME/.config}/nion" \
           "${XDG_CACHE_HOME:-$HOME/.cache}/nion"; do
  if [[ -d "$dir" ]]; then
    mode="$(stat -c '%a' "$dir" 2>/dev/null || true)"
    if [[ "$mode" == '700' ]]; then
      pass "$dir mode 700"
    else
      warn "$dir mode is ${mode:-unknown}; NiOn $VERSION will attempt to normalize it to 700 on start"
    fi
  fi
done

printf '\n'
if (( fail )); then
  echo 'RELEASE PREFLIGHT: FAIL'
  exit 1
fi
echo 'RELEASE PREFLIGHT: PASS'
echo 'Continue with the runtime scenarios in TESTING.md; preflight does not replace live Tor/privacy testing.'
