/* Copyright (C) 2026 Jeannes Bryan */

/* content-filter (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

#include "content-filter.h"
#include "per-site.h"
#include "types.h"
#include "util.h"

WebKitUserContentFilter *nion_content_filter_for_app(NionApp *app)
{
    if (!app)
        return NULL;
    NionApp *owner = app->is_private ? app->owner : app;
    return owner ? owner->content_filter : NULL;
}

gboolean nion_content_filter_is_ready(NionApp *app)
{
    if (!app)
        return FALSE;
    NionApp *owner = app->is_private ? app->owner : app;
    return owner && owner->content_filter_ready && owner->content_filter;
}

gboolean nion_content_filter_has_failed(NionApp *app)
{
    if (!app)
        return TRUE;
    NionApp *owner = app->is_private ? app->owner : app;
    return !owner || owner->content_filter_failed;
}

void nion_apply_content_blocking(NionTab *tab, const gchar *uri)
{
    if (!tab || !tab->app || !tab->web_view)
        return;

    WebKitUserContentManager *manager =
        webkit_web_view_get_user_content_manager(tab->web_view);
    if (!manager)
        return;

    gboolean enabled = !tab->home_page && !tab->error_page &&
        nion_content_blocking_enabled_for_uri(tab->app, uri);
    WebKitUserContentFilter *filter = nion_content_filter_for_app(tab->app);

    if (tab->content_filter_applied) {
        webkit_user_content_manager_remove_filter_by_id(manager, NION_CONTENT_FILTER_ID);
        tab->content_filter_applied = FALSE;
    }

    if (enabled && filter) {
        webkit_user_content_manager_add_filter(manager, filter);
        tab->content_filter_applied = TRUE;
    }
}
