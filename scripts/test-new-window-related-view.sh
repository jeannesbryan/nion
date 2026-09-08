#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRC="$ROOT/src"

fail() { printf 'NEW WINDOW RELATED-VIEW CHECK: FAIL: %s\n' "$*" >&2; exit 1; }

# WebKitGTK 6 removed webkit_web_view_new_with_related_view(). The supported
# construction path is the construct-only "related-view" GObject property.
grep -rq '"related-view", related_view' "$SRC" || \
    fail 'popup/new-window WebView is not constructed with related-view'
if grep -rq 'webkit_web_view_new_with_related_view' "$SRC"; then
    fail 'removed WebKitGTK 6 constructor webkit_web_view_new_with_related_view is still used'
fi

# Per-site JavaScript and content-blocking exceptions require mutable settings
# and the UserContentManager to be tab-local. Keep the related-view lifecycle /
# NetworkSession relationship, but attach independent hardened page controls.
grep -rq 'WebKitSettings \*settings = webkit_settings_new();' "$SRC" || \
    fail 'popup does not create independent hardened WebKit settings'
grep -rq 'webkit_settings_get_enable_javascript(webkit_web_view_get_settings(related_view))' "$SRC" || \
    fail 'popup does not seed JavaScript state from opener'
grep -rq '"settings", settings' "$SRC" || \
    fail 'popup independent settings are not attached'
grep -rq 'WebKitUserContentManager \*content_manager = webkit_user_content_manager_new();' "$SRC" || \
    fail 'popup/tab does not create an independent user-content manager'
grep -rq '"user-content-manager", content_manager' "$SRC" || \
    fail 'popup independent user-content manager is not attached'
if grep -rq '"website-policies", webkit_web_view_get_website_policies(related_view)' "$SRC"; then
    :
elif grep -rq 'g_object_ref(webkit_web_view_get_website_policies(related_view))' "$SRC" && \
     grep -rq '"website-policies", default_policies' "$SRC"; then
    :
else
    fail 'popup does not preserve opener website policies'
fi
grep -rq 'nion_new_tab_internal(app, "", TRUE, web_view)' "$SRC" || \
    fail 'WebView::create does not pass the opener as related_view'

# Guard against the original crash-prone regression: ::create must not route
# through the generic unrelated-tab constructor.
CREATE_BODY="$(sed -n '/static WebKitWebView \*on_webview_create/,/^}/p' src/*.c)"
if grep -q 'nion_new_tab(app, "", TRUE)' <<<"$CREATE_BODY"; then
    fail 'WebView::create still creates an unrelated generic tab'
fi

grep -rq '"network-session", app->network_session' "$SRC" || \
    fail 'normal tabs lost the explicit NiOn network session'
grep -rq 'g_signal_connect(tab->web_view, "create"' "$SRC" || \
    fail 'WebView::create signal is no longer connected'

printf 'NEW WINDOW RELATED-VIEW CHECK: PASS\n'
