/* Copyright (C) 2026 Jeannes Bryan */

/* session (extracted from src/main.c, Phase 1, NiOn 2.0.0). */

#include "session.h"
#include "config.h"
#include "types.h"
#include "util.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib/gstdio.h>

void nion_save_session(NionApp *app, gboolean clean_shutdown)
{
    if (!app || app->is_private)
        return;
    if (!app->session_file)
        return;

    if (!app->restore_session) {
        g_unlink(app->session_file);
        return;
    }

    if (!app->notebook)
        return;

    GKeyFile *session = g_key_file_new();
    gint tab_count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    if (tab_count > NION_MAX_SESSION_TABS)
        tab_count = NION_MAX_SESSION_TABS;
    gint active_tab = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (active_tab < 0 || active_tab >= tab_count)
        active_tab = 0;

    g_key_file_set_integer(session, "Session", "format", NION_SESSION_FORMAT);
    g_key_file_set_string(session, "Session", "nion-version", NION_VERSION);
    g_key_file_set_integer(session, "Session", "tab-count", tab_count);
    g_key_file_set_integer(session, "Session", "active-tab", MAX(active_tab, 0));
    g_key_file_set_boolean(session, "Session", "clean-shutdown", clean_shutdown);

    /* Keep crash-recovery snapshots bounded. URLs are always preserved; opaque
     * WebKit history state is included only while it fits within the RC
     * profile budget. */
    gsize state_budget = NION_MAX_SESSION_FILE_BYTES - (1024 * 1024);

    for (gint i = 0; i < tab_count; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab)
            continue;

        gchar *group = g_strdup_printf("Tab-%d", i);
        g_key_file_set_boolean(session, group, "home", tab->home_page);
        g_key_file_set_boolean(session, group, "muted",
                               webkit_web_view_get_is_muted(tab->web_view));
        g_key_file_set_boolean(session, group, "pinned", tab->pinned);

        const gchar *uri = tab->display_uri_override;
        if (!uri || !*uri)
            uri = webkit_web_view_get_uri(tab->web_view);
        if (uri && *uri && !g_str_equal(uri, "about:blank") &&
            strlen(uri) <= NION_MAX_SAVED_URI_BYTES)
            g_key_file_set_string(session, group, "uri", uri);

        /* Never persist the synthetic WebProcess recovery page as opaque
         * WebKit history state. The original URL above is enough to recover. */
        if (!tab->home_page && !tab->web_process_terminated) {
            WebKitWebViewSessionState *state = webkit_web_view_get_session_state(tab->web_view);
            if (state) {
                GBytes *bytes = webkit_web_view_session_state_serialize(state);
                if (bytes) {
                    gsize data_length = 0;
                    gconstpointer data = g_bytes_get_data(bytes, &data_length);
                    if (data && data_length > 0) {
                        gchar *base64 = g_base64_encode(data, data_length);
                        gsize base64_len = base64 ? strlen(base64) : 0;
                        if (base64_len > 0 &&
                            base64_len <= NION_MAX_TAB_STATE_BASE64_BYTES &&
                            base64_len <= state_budget) {
                            g_key_file_set_string(session, group, "state", base64);
                            state_budget -= base64_len;
                        }
                        g_free(base64);
                    }
                    g_bytes_unref(bytes);
                }
                webkit_web_view_session_state_unref(state);
            }
        }

        g_free(group);
    }

    nion_write_key_file_atomic(session, app->session_file);
    g_key_file_free(session);
}

static gboolean nion_session_save_timeout(gpointer user_data)
{
    NionApp *app = user_data;
    app->session_save_source_id = 0;
    if (!app->shutting_down)
        nion_save_session(app, FALSE);
    return G_SOURCE_REMOVE;
}

void nion_schedule_session_save(NionApp *app)
{
    if (!app || app->is_private)
        return;
    if (!app->restore_session || app->shutting_down || !app->notebook)
        return;

    if (app->session_save_source_id)
        g_source_remove(app->session_save_source_id);

    app->session_save_source_id = g_timeout_add(NION_SESSION_SAVE_DELAY_MS,
                                                 nion_session_save_timeout,
                                                 app);
}
