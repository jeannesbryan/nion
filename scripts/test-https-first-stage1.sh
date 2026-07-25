#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail=0
pass() { printf 'PASS  %s\n' "$*"; }
failmsg() { printf 'FAIL  %s\n' "$*" >&2; fail=1; }

printf 'NiOn 1.1.0 Stage 1 HTTPS-First static checks\n\n'

grep -q "version: '1.1.0'" meson.build && pass 'development version is 1.1.0' || failmsg 'Meson version mismatch'
grep -q 'onion_host ? "http://" : "https://"' src/main.c && pass 'schemeless clearnet prefers HTTPS; onion prefers HTTP' || failmsg 'address resolution invariant missing'
grep -q 'nion_uri_is_http_clearnet' src/main.c && pass 'plain HTTP clearnet detector present' || failmsg 'HTTP detector missing'
grep -q 'Unencrypted clearnet connection' src/main.c && pass 'warning UI present' || failmsg 'warning UI missing'
grep -q 'tab->http_warning_decision = g_object_ref(decision)' src/main.c && pass 'policy decision retained for async confirmation' || failmsg 'async policy retention missing'
grep -q 'webkit_policy_decision_use(tab->http_warning_decision)' src/main.c && pass 'Continue resumes original navigation decision' || failmsg 'Continue policy use missing'
grep -q 'webkit_policy_decision_ignore(tab->http_warning_decision)' src/main.c && pass 'Cancel blocks pending HTTP navigation' || failmsg 'Cancel policy ignore missing'
grep -q 'if (!uri || !\*uri || nion_uri_is_onion(uri))' src/main.c && pass '.onion excluded from clearnet HTTP warning' || failmsg '.onion exclusion missing'
grep -q 'http_allowed_origin' src/main.c && pass 'per-tab temporary HTTP-origin allowance present' || failmsg 'temporary HTTP-origin allowance missing'

if (( fail )); then
  printf '\nSTAGE 1 STATIC CHECK: FAIL\n'
  exit 1
fi
printf '\nSTAGE 1 STATIC CHECK: PASS\n'
printf 'Run ./scripts/run-dev.sh and complete the runtime cases at the top of TESTING.md.\n'
