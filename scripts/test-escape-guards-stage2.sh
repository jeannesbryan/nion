#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "ESCAPE GUARDS STAGE 2: FAIL: $*" >&2; exit 1; }
SRC=src

# These are the 2.1.0 feature guards. They must stay version-independent, so a
# release bump does not silently retire them; release-specific metadata
# assertions live in test-hardening-stage3-<version>.sh.
VERSION="$(tr -d "\r\n" < release/manifest/NION_VERSION)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "manifest version missing or malformed: $VERSION"
release_type="$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)"
release_status="$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)"
[[ "$release_type" == "development" || "$release_type" == "stable" ]] || fail 'invalid AppStream release type'
[[ -n "$release_status" ]] || fail 'release status missing'

grep -rFq 'webkit_navigation_action_is_user_gesture(action)' "$SRC" || fail 'navigation user-gesture gate missing'
grep -rFq 'WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION && !user_gesture' "$SRC" || fail 'new-window policy gate missing'
grep -rFq 'POPUP BLOCKED (NO USER GESTURE)' "$SRC" || fail 'popup block status missing'
grep -rFq '!webkit_navigation_action_is_user_gesture(navigation_action)' "$SRC" || fail 'create callback defense-in-depth missing'
grep -rFq 'webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE)' "$SRC" || fail 'automatic JS popup baseline was relaxed'

grep -rFq 'nion_external_protocol_scheme' "$SRC" || fail 'external protocol detector missing'
grep -rFq 'nion_show_external_protocol_prompt' "$SRC" || fail 'external protocol confirmation missing'
grep -rFq 'g_app_info_launch_default_for_uri_async' "$SRC" || fail 'async desktop handler launch missing'
grep -rFq 'Open Anyway' "$SRC" || fail 'explicit external-app confirmation action missing'
grep -rFq 'may access the network without Tor' "$SRC" || fail 'Tor-boundary warning missing'
grep -rFq 'EXTERNAL PROTOCOL BLOCKED (NO USER GESTURE)' "$SRC" || fail 'automatic external-protocol block missing'
grep -rFq 'external_protocol_prompt_open' "$SRC" || fail 'duplicate external prompt guard missing'

for scheme in file javascript data blob about nion; do
  grep -rFq "\"$scheme\"" "$SRC" || fail "hard non-delegable scheme missing: $scheme"
done

grep -rFq '"External URI handlers", "USER-GESTURE + CONFIRM"' "$SRC" || fail 'privacy audit external handler status missing'
grep -Eq "\*\*(Current development|Stable release): ${VERSION//./\\.}" README.md || fail "README release marker for $VERSION missing"

grep -Fq '### Escape guards' README.md || fail 'README escape guard section missing'
printf 'NION %s ESCAPE GUARDS STAGE 2: PASS\n' "$VERSION"
grep -Fq '## External application boundary' PRIVACY.md || fail 'privacy external boundary missing'

printf 'NION 2.1.0 ESCAPE GUARDS STAGE 2: PASS\n'
