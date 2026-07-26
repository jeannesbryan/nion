#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

expected=(
  BUILDING.md
  CHANGELOG.md
  PRIVACY.md
  README.md
  SECURITY.md
  TESTING.md
  THIRD_PARTY_NOTICES.md
)

mapfile -t actual < <(find . -maxdepth 1 -type f -name '*.md' -printf '%f\n' | sort)
mapfile -t wanted < <(printf '%s\n' "${expected[@]}" | sort)
[[ "${actual[*]}" == "${wanted[*]}" ]] || {
  printf 'Expected root Markdown:\n%s\n' "${wanted[*]}" >&2
  printf 'Actual root Markdown:\n%s\n' "${actual[*]}" >&2
  fail "root Markdown set is not consolidated"
}
pass "root Markdown consolidated"

for removed in APPIMAGE.md GITHUB_RELEASE.md REPOSITORY.md ROADMAP.md TOR_RUNTIME.md UPGRADING.md; do
  [[ ! -e "$removed" ]] || fail "obsolete/duplicate document remains: $removed"
done
pass "obsolete/duplicate root documentation removed"

grep -Fq '## Main features' README.md || fail "README feature section missing"
grep -Fq '## Strengths' README.md || fail "README strengths section missing"
grep -Fq '## Limitations' README.md || fail "README limitations section missing"
grep -Fq '## Keyboard shortcuts' README.md || fail "README shortcuts section missing"
grep -Fq '## Build from source' README.md || fail "README concise build section missing"
pass "README is product-focused"

grep -Fq '## Build the AppImage' BUILDING.md || fail "BUILDING AppImage procedure missing"
grep -Fq '## Manual GitHub release' BUILDING.md || fail "BUILDING manual release procedure missing"
grep -Fq '## Bundled Tor' BUILDING.md || fail "BUILDING Tor packaging notes missing"
pass "technical build/release guidance consolidated"

grep -Fq '## Private Window' PRIVACY.md || fail "PRIVACY Private Window boundary missing"
grep -Fq '## Release gate' TESTING.md || fail "TESTING release gate missing"
pass "privacy/testing documentation retained"

if [[ -e release/manifest/README.md ]]; then
  fail "release/manifest/README.md should be consolidated into BUILDING.md"
fi
pass "manifest documentation consolidated"

echo "NION DOCUMENTATION CONSOLIDATION CHECK: PASS"
