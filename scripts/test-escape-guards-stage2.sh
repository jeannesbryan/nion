#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "2.0.0 ESCAPE GUARDS STAGE 2: FAIL: $*" >&2; exit 1; }
SRC=src

[[ "$(tr -d '\r\n' < release/manifest/NION_VERSION)" == "2.0.0" ]] || fail 'manifest version is not 2.0.0'
release_type="$(tr -d '\r\n' < release/manifest/APPSTREAM_RELEASE_TYPE)"
release_status="$(tr -d '\r\n' < release/manifest/RELEASE_STATUS)"
[[ "$release_type" == "development" || "$release_type" == "stable" ]] || fail 'invalid AppStream release type'
[[ "$release_status" == 'Stable' || "$release_status" == 'Security Levels & Escape Guards development — Stage 2' ]] || fail 'Stage 2/final release status missing'

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
grep -rFq '"Popup / new-window escape", "USER-GESTURE ONLY"' "$SRC" || fail 'privacy audit popup status missing'

grep -Eq '\\*\\*(Current development: 2\.0\.0 — Stage 2 \(Escape Guards\)|Stable release: 2\.0\.0)' README.md || fail 'README Stage 2/final marker missing'
grep -Fq '### Escape guards' README.md || fail 'README escape guard section missing'
grep -Fq '## NiOn 2.0.0 Stage 2 — Escape Guards' TESTING.md || fail 'Stage 2 runtime checklist missing'
grep -Fq '## External application boundary' PRIVACY.md || fail 'privacy external boundary missing'

printf 'NION 2.0.0 ESCAPE GUARDS STAGE 2: PASS\n'
