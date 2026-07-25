#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRC="$ROOT/src/main.c"

fail() { printf 'NEW WINDOW RELATED-VIEW CHECK: FAIL: %s\n' "$*" >&2; exit 1; }

# WebKitGTK 6 removed webkit_web_view_new_with_related_view(). The supported
# construction path is the construct-only "related-view" GObject property.
grep -q '"related-view", related_view' "$SRC" || \
    fail 'popup/new-window WebView is not constructed with related-view'
if grep -q 'webkit_web_view_new_with_related_view' "$SRC"; then
    fail 'removed WebKitGTK 6 constructor webkit_web_view_new_with_related_view is still used'
fi

grep -q '"settings", webkit_web_view_get_settings(related_view)' "$SRC" || \
    fail 'popup does not preserve opener WebKit settings'
grep -q '"user-content-manager", webkit_web_view_get_user_content_manager(related_view)' "$SRC" || \
    fail 'popup does not preserve opener user-content manager'
grep -q '"website-policies", webkit_web_view_get_website_policies(related_view)' "$SRC" || \
    fail 'popup does not preserve opener website policies'
grep -q 'nion_new_tab_internal(app, "", TRUE, web_view)' "$SRC" || \
    fail 'WebView::create does not pass the opener as related_view'

# Guard against the original crash-prone regression: ::create must not route
# through the generic unrelated-tab constructor.
CREATE_BODY="$(sed -n '/static WebKitWebView \*on_webview_create/,/^}/p' "$SRC")"
if grep -q 'nion_new_tab(app, "", TRUE)' <<<"$CREATE_BODY"; then
    fail 'WebView::create still creates an unrelated generic tab'
fi

grep -q '"network-session", app->network_session' "$SRC" || \
    fail 'normal tabs lost the explicit NiOn network session'
grep -q 'g_signal_connect(tab->web_view, "create"' "$SRC" || \
    fail 'WebView::create signal is no longer connected'

printf 'NEW WINDOW RELATED-VIEW CHECK: PASS\n'
