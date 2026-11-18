/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "types.h"
#include "ui.h"
#include "data-saver.h"

/* Data saver (v2.3.7, roadmap T2-10).
 *
 * The rules are a resource-type filter rather than a stylesheet: an image hidden
 * by CSS has already been downloaded, so hiding saves nothing. Blocking the
 * request is what stops the bytes, which is the only honest reading of "data
 * saver".
 */

/* Kept as one literal so the test can read exactly what is used, and so there is
 * no second place where the rule set could drift. */
static const gchar *const data_saver_rules =
    "["
    "  {"
    "    \"trigger\": {"
    "      \"url-filter\": \".*\","
    "      \"resource-type\": [\"image\", \"media\", \"font\"]"
    "    },"
    "    \"action\": { \"type\": \"block\" }"
    "  }"
    "]";

const gchar *
nion_data_saver_rules_json(void)
{
    return data_saver_rules;
}

WebKitUserContentFilter *
nion_data_saver_filter_for_app(NionApp *app)
{
    if (!app)
        return NULL;
    /* A private window borrows the filter from its owner, exactly as the content
     * blocker does: the rules are not a per-window secret. */
    NionApp *owner = app->is_private ? app->owner : app;
    return owner ? owner->data_saver_filter : NULL;
}

gboolean
nion_data_saver_has_failed(NionApp *app)
{
    if (!app)
        return TRUE;
    NionApp *owner = app->is_private ? app->owner : app;
    return !owner || owner->data_saver_failed;
}

gboolean
nion_data_saver_is_on(NionTab *tab)
{
    return tab ? tab->data_saver : FALSE;
}

void
nion_data_saver_apply(NionTab *tab)
{
    if (!tab || !tab->app || !tab->web_view)
        return;

    WebKitUserContentManager *manager =
        webkit_web_view_get_user_content_manager(tab->web_view);
    if (!manager)
        return;

    /* An internal page has nothing to fetch, so the filter is pointless there and
     * leaving it off keeps the start page rendering its own tiles. */
    gboolean wanted = tab->data_saver && !tab->home_page && !tab->error_page;
    WebKitUserContentFilter *filter = nion_data_saver_filter_for_app(tab->app);

    if (tab->data_saver_applied) {
        webkit_user_content_manager_remove_filter_by_id(manager,
                                                       NION_DATA_SAVER_FILTER_ID);
        tab->data_saver_applied = FALSE;
    }

    /* If the rules never compiled, the tab must not be left believing it is
     * saving data. The caller reports the failure; here the state is simply not
     * applied, so no false claim survives. */
    if (wanted && filter) {
        webkit_user_content_manager_add_filter(manager, filter);
        tab->data_saver_applied = TRUE;
    }
}

gboolean
nion_data_saver_toggle(NionTab *tab)
{
    if (!tab || !tab->app)
        return FALSE;

    NionApp *app = tab->app;

    /* Refusing loudly beats a toggle that appears to work and does nothing. */
    if (nion_data_saver_has_failed(app) ||
        !nion_data_saver_filter_for_app(app)) {
        nion_show_status(app,
            "Data saver is unavailable: its filter rules could not be loaded");
        return FALSE;
    }

    if (tab->home_page || tab->error_page) {
        nion_show_status(app,
            "Data saver applies to a web page, and this tab is not showing one");
        return FALSE;
    }

    tab->data_saver = !tab->data_saver;
    nion_data_saver_apply(tab);

    if (tab->web_view) {
        /* A filter only affects requests made after it is applied, so the page
         * has to be fetched again for the change to mean anything. */
        webkit_web_view_reload(tab->web_view);
    }

    {
        gchar *message = tab->data_saver
            ? g_strdup("Data saver on for this tab: images, media and fonts are "
                       "not downloaded. Reloading.")
            : g_strdup("Data saver off for this tab: loading everything again.");
        nion_show_status(app, message);
        g_free(message);
    }

    if (getenv("NION_TAB_DEBUG")) {
        g_printerr("data-saver enabled=%d applied=%d\n",
                   tab->data_saver ? 1 : 0, tab->data_saver_applied ? 1 : 0);
    }

    return tab->data_saver;
}
