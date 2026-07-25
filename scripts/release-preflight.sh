#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="1.1.0"
fail=0

pass() { printf 'PASS  %s\n' "$*"; }
warn() { printf 'WARN  %s\n' "$*"; }
failmsg() { printf 'FAIL  %s\n' "$*" >&2; fail=1; }

printf 'NiOn %s stable release preflight\n\n' "$VERSION"

for tool in bash grep awk sed find stat sha256sum; do
  command -v "$tool" >/dev/null 2>&1 && pass "tool: $tool" || failmsg "missing tool: $tool"
done

printf '\n== Release metadata ==\n'
grep -q "version: '$VERSION'" meson.build && pass 'Meson version' || failmsg 'Meson version mismatch'
grep -q "release version=\"$VERSION\"" data/io.github.jeannesbryan.Nion.metainfo.xml && pass 'AppStream version' || failmsg 'AppStream version mismatch'
grep -q "VERSION=\"$VERSION\"" scripts/build-appimage.sh && pass 'AppImage builder version' || failmsg 'AppImage builder version mismatch'
grep -Fq "Current release: $VERSION Stable" README.md && pass 'README version' || failmsg 'README version mismatch'
grep -q '<project_license>GPL-3.0-or-later</project_license>' data/io.github.jeannesbryan.Nion.metainfo.xml && pass 'AppStream project license' || failmsg 'AppStream project license missing'
[[ -s LICENSE ]] && pass 'LICENSE present' || failmsg 'LICENSE missing'
[[ -s .gitignore ]] && pass '.gitignore present' || failmsg '.gitignore missing'
[[ -s GITHUB_RELEASE.md ]] && pass 'GitHub release notes present' || failmsg 'GitHub release notes missing'
[[ -s UPGRADING.md ]] && pass 'upgrade documentation present' || failmsg 'upgrade documentation missing'

printf '\n== Script/XML sanity ==\n'
for script in scripts/*.sh packaging/AppRun; do
  if bash -n "$script"; then
    pass "shell syntax: $script"
  else
    failmsg "shell syntax: $script"
  fi
done
if python3 - <<'PY' >/dev/null 2>&1
import xml.etree.ElementTree as ET
ET.parse('data/io.github.jeannesbryan.Nion.metainfo.xml')
ET.parse('data/nion.gresource.xml')
PY
then
  pass 'XML parses'
else
  failmsg 'XML parse failed'
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

printf '\n== Profile resilience ==\n'
grep -q 'nion_quarantine_profile_file' src/main.c && pass 'profile quarantine path present' || failmsg 'profile quarantine missing'
grep -q 'NION_MAX_SESSION_FILE_BYTES' src/main.c && pass 'session size bound present' || failmsg 'session size bound missing'
grep -q 'NION_MAX_DOWNLOAD_HISTORY 500' src/main.c && pass 'download history bound present' || failmsg 'download history bound missing'
grep -q 'SQLite header' src/main.c && pass 'cookie SQLite sanity check present' || failmsg 'cookie DB sanity check missing'

printf '\n== Bundled Tor ==\n'
if [[ -x runtime/tor/tor ]]; then
  tor_version="$(runtime/tor/tor --version 2>&1 | head -n1 || true)"
  if [[ "$tor_version" == *'0.4.9.11'* ]]; then
    pass "$tor_version"
  else
    failmsg "unexpected bundled Tor: ${tor_version:-no output}"
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
appimage="dist/NiOn-${VERSION}-x86_64.AppImage"
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
      warn "$dir mode is ${mode:-unknown}; NiOn 1.1.0 will attempt to normalize it to 700 on start"
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
