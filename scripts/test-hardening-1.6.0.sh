#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "1.6.0 FINAL HARDENING: FAIL: $*" >&2; exit 1; }

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "1.6.0" ]] || fail 'manifest version is not 1.6.0'
[[ "$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)" == "Stable" ]] || fail 'release status is not Stable'
[[ "$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)" == "stable" ]] || fail 'AppStream release type is not stable'
grep -Fq '**Stable release: 1.6.0**' README.md || fail 'README stable marker missing'
grep -Fq 'Web process crash recovery' README.md || fail 'README crash recovery feature missing'
grep -Fq 'Forget This Site' README.md || fail 'README Forget This Site feature missing'
./scripts/test-web-process-recovery-1.6.0.sh >/dev/null
./scripts/test-forget-site-1.6.0.sh >/dev/null
./scripts/test-site-info-width-fix.sh >/dev/null

printf 'NION 1.6.0 FINAL HARDENING: PASS\n'
