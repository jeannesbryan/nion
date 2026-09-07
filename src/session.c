/* Copyright (C) 2026 Jeannes Bryan */

/* Session save/restore & crash recovery (extracted from src/main.c, Phase 4,
 * NiOn 2.0.0).

 * UI/tab operations go through registered callbacks (see session.h) so this
 * module never calls into main.c UI code directly. */

#include "config.h"
#include "session.h"
#include "types.h"
#include "util.h"
#include "navigation.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

static NionSessionCallbacks s_session_callbacks;

void nion_session_set_callbacks(const NionSessionCallbacks *callbacks)
{
    if (callbacks)
        s_session_callbacks = *callbacks;
}

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return s_session_callbacks.new_tab
        ? s_session_callbacks.new_tab(app, uri, select) : NULL;
}

static void nion_set_tab_pinned(NionTab *tab, gboolean pinned, gboolean schedule_save)
{
    if (s_session_callbacks.set_tab_pinned)
        s_session_callbacks.set_tab_pinned(tab, pinned, schedule_save);
}

static void nion_load_home(NionTab *tab)
{
    if (s_session_callbacks.load_home)
        s_session_callbacks.load_home(tab);
}

static void nion_load_uri(NionTab *tab, const gchar *uri)
{
    if (s_session_callbacks.load_uri)
        s_session_callbacks.load_uri(tab, uri);
}

static void nion_prepare_normal_navigation(NionTab *tab)
{
    if (s_session_callbacks.prepare_normal_navigation)
        s_session_callbacks.prepare_normal_navigation(tab);
}

static void nion_update_controls(NionApp *app)
{
    if (s_session_callbacks.update_controls)
        s_session_callbacks.update_controls(app);
}

static void nion_set_status(NionApp *app, const gchar *text)
{
    if (s_session_callbacks.set_status)
        s_session_callbacks.set_status(app, text);
}

static void nion_closed_tab_free(gpointer data)
{
    if (s_session_callbacks.closed_tab_free)
        s_session_callbacks.closed_tab_free(data);
}

/* ---- Session logic (source order preserved) ---- */

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

static gboolean nion_restore_tab_state(NionTab *tab, const gchar *base64)
{
    if (!tab || !nion_base64_state_looks_valid(base64))
        return FALSE;

    gsize decoded_length = 0;
    guchar *decoded = g_base64_decode(base64, &decoded_length);
    if (!decoded || decoded_length == 0) {
        g_free(decoded);
        return FALSE;
    }

    GBytes *bytes = g_bytes_new_take(decoded, decoded_length);
    WebKitWebViewSessionState *state = webkit_web_view_session_state_new(bytes);
    g_bytes_unref(bytes);
    if (!state)
        return FALSE;

    webkit_web_view_restore_session_state(tab->web_view, state);
    webkit_web_view_session_state_unref(state);
    return TRUE;
}

gboolean nion_restore_saved_session(NionApp *app)
{
    if (!app || app->is_private)
        return FALSE;
    if (!app->restore_session || !g_file_test(app->session_file, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    if (!nion_profile_file_within_limit(app->session_file, NION_MAX_SESSION_FILE_BYTES)) {
        nion_quarantine_profile_file(app->session_file, "session");
        return FALSE;
    }

    GKeyFile *session = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(session, app->session_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn session: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(session);
        nion_quarantine_profile_file(app->session_file, "session");
        return FALSE;
    }

    error = NULL;
    gint format = g_key_file_get_integer(session, "Session", "format", &error);
    if (error || format != NION_SESSION_FORMAT) {
        g_clear_error(&error);
        g_key_file_free(session);
        nion_quarantine_profile_file(app->session_file, "session");
        return FALSE;
    }

    error = NULL;
    gint tab_count = g_key_file_get_integer(session, "Session", "tab-count", &error);
    if (error || tab_count < 1 || tab_count > NION_MAX_SESSION_TABS_INPUT) {
        g_clear_error(&error);
        g_key_file_free(session);
        nion_quarantine_profile_file(app->session_file, "session");
        return FALSE;
    }
    if (tab_count > NION_MAX_SESSION_TABS)
        tab_count = NION_MAX_SESSION_TABS;

    app->previous_shutdown_clean = g_key_file_get_boolean(session, "Session", "clean-shutdown", NULL);
    app->crash_recovery_decision_pending = !app->previous_shutdown_clean;
    gint active_tab = g_key_file_get_integer(session, "Session", "active-tab", NULL);
    gint restored = 0;

    for (gint i = 0; i < tab_count; i++) {
        gchar *group = g_strdup_printf("Tab-%d", i);
        gboolean home = g_key_file_get_boolean(session, group, "home", NULL);
        gboolean muted = g_key_file_has_key(session, group, "muted", NULL)
            ? g_key_file_get_boolean(session, group, "muted", NULL)
            : FALSE;
        gboolean pinned = g_key_file_has_key(session, group, "pinned", NULL)
            ? g_key_file_get_boolean(session, group, "pinned", NULL)
            : FALSE;
        gchar *uri = g_key_file_get_string(session, group, "uri", NULL);
        gchar *state = g_key_file_get_string(session, group, "state", NULL);

        if (uri && *uri) {
            gchar *validation = NULL;
            if (strlen(uri) > NION_MAX_SAVED_URI_BYTES || !nion_validate_uri(uri, &validation)) {
                g_warning("Ignoring invalid restored URI in %s: %s",
                          group, validation ? validation : "invalid address");
                g_clear_pointer(&uri, g_free);
            }
            g_free(validation);
        }

        /* After an unclean shutdown recover URLs/pinned/mute state first, but
         * deliberately skip opaque WebKit history snapshots. A corrupted or
         * crash-triggering page-state blob must not create an automatic startup
         * crash loop before the user can choose Start Fresh. */
        gboolean state_valid = app->previous_shutdown_clean && state && *state &&
            nion_base64_state_looks_valid(state);
        if (state && *state && app->previous_shutdown_clean && !state_valid)
            g_warning("Ignoring malformed WebKit session state in %s", group);

        NionTab *restored_tab = NULL;
        if (home || ((!uri || !*uri) && !state_valid)) {
            restored_tab = nion_new_tab(app, NULL, FALSE);
        } else {
            restored_tab = nion_new_tab(app, "", FALSE);
            if (restored_tab) {
                restored_tab->home_page = FALSE;
                restored_tab->error_page = FALSE;
                restored_tab->restore_pending = TRUE;
                restored_tab->restore_uri = g_strdup(uri);
                if (state_valid)
                    nion_restore_tab_state(restored_tab, state);
            }
        }

        if (restored_tab) {
            webkit_web_view_set_is_muted(restored_tab->web_view, muted);
            nion_set_tab_pinned(restored_tab, pinned, FALSE);
            restored++;
        }

        g_free(state);
        g_free(uri);
        g_free(group);
    }

    g_key_file_free(session);

    if (restored <= 0) {
        nion_quarantine_profile_file(app->session_file, "session");
        return FALSE;
    }

    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    if (active_tab < 0 || active_tab >= pages)
        active_tab = 0;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), active_tab);
    app->restored_previous_session = TRUE;
    return TRUE;
}

void nion_start_pending_restores(NionApp *app)
{
    if (!app->tor_ready || !app->notebook || app->crash_recovery_decision_pending)
        return;

    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || !tab->restore_pending)
            continue;

        tab->restore_pending = FALSE;
        WebKitBackForwardList *list = webkit_web_view_get_back_forward_list(tab->web_view);
        WebKitBackForwardListItem *current = list ? webkit_back_forward_list_get_current_item(list) : NULL;

        if (current) {
            nion_prepare_normal_navigation(tab);
            webkit_web_view_go_to_back_forward_list_item(tab->web_view, current);
        } else if (tab->restore_uri && *tab->restore_uri) {
            nion_load_uri(tab, tab->restore_uri);
        } else {
            nion_load_home(tab);
        }
    }

    nion_update_controls(app);
}

static void nion_crash_recovery_close_prompt(NionApp *app)
{
    if (!app || !app->crash_recovery_window)
        return;
    GtkWidget *window = app->crash_recovery_window;
    app->crash_recovery_window = NULL;
    gtk_window_destroy(GTK_WINDOW(window));
}

static void on_crash_recovery_restore_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app || !app->crash_recovery_decision_pending)
        return;

    app->crash_recovery_decision_pending = FALSE;
    nion_crash_recovery_close_prompt(app);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — RESTORING TABS AFTER UNCLEAN SHUTDOWN"
        : "○ CONNECTING TO TOR — TABS WILL RESTORE AFTER RECOVERY");
    if (app->tor_ready)
        nion_start_pending_restores(app);
}

static void on_crash_recovery_start_fresh_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app || !app->crash_recovery_decision_pending || !app->notebook)
        return;

    app->crash_recovery_decision_pending = FALSE;
    nion_crash_recovery_close_prompt(app);

    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    while (gtk_notebook_get_n_pages(notebook) > 0)
        gtk_notebook_remove_page(notebook, 0);

    if (app->closed_tabs)
        g_queue_clear_full(app->closed_tabs, nion_closed_tab_free);

    app->restored_previous_session = FALSE;
    nion_new_tab(app, NULL, TRUE);
    nion_save_session(app, FALSE);
    nion_update_controls(app);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — STARTED FRESH AFTER UNCLEAN SHUTDOWN"
        : "○ CONNECTING TO TOR — STARTED FRESH AFTER UNCLEAN SHUTDOWN");
}

void nion_show_crash_recovery_prompt(NionApp *app)
{
    if (!app || app->is_private || !app->crash_recovery_decision_pending ||
        app->crash_recovery_window || !app->window)
        return;

    GtkWidget *window = gtk_window_new();
    app->crash_recovery_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Recover NiOn tabs");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>NiOn did not shut down normally.</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *message = gtk_label_new(
        "Your last normal-window tabs were saved in the crash-recovery snapshot. "
        "Choose Restore Tabs to continue, or Start Fresh if a restored page may have caused the crash. "
        "NiOn never restores an unclean session automatically.");
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *fresh = gtk_button_new_with_label("Start Fresh");
    GtkWidget *restore = gtk_button_new_with_label("Restore Tabs");
    gtk_widget_add_css_class(restore, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), fresh);
    gtk_box_append(GTK_BOX(buttons), restore);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(fresh, "clicked", G_CALLBACK(on_crash_recovery_start_fresh_clicked), app);
    g_signal_connect(restore, "clicked", G_CALLBACK(on_crash_recovery_restore_clicked), app);
    gtk_window_present(GTK_WINDOW(window));
}
