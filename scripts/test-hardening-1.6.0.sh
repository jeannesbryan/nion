#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "1.6.0 RELIABILITY/SITE-PRIVACY REGRESSION: FAIL: $*" >&2; exit 1; }

grep -Fq 'Web process crash recovery' README.md || fail 'README crash recovery feature missing'
grep -Fq 'Forget This Site' README.md || fail 'README Forget This Site feature missing'
./scripts/test-web-process-recovery-1.6.0.sh >/dev/null
./scripts/test-forget-site-1.6.0.sh >/dev/null
./scripts/test-site-info-width-fix.sh >/dev/null

printf 'NION 1.6.0 RELIABILITY/SITE-PRIVACY REGRESSION: PASS\n'
