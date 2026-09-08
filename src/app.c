/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <signal.h>

#include "types.h"
#include "network.h"
#include "per-site.h"
#include "session.h"
#include "downloads.h"
#include "bookmarks.h"
#include "tabs.h"
#include "tor-core.h"
#include "app.h"

/* UI chrome / status operations that still live in the UI layer (Phase 7,
 * NiOn 2.0.0). main.c registers implementations via nion_app_set_callbacks()
 * so this module never calls into the UI layer directly. */
static NionAppLifecycleCallbacks cb;

static void
nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

static void
nion_update_controls(NionApp *app)
{
    if (cb.update_controls)
        cb.update_controls(app);
}

static void
nion_refresh_home_pages(NionApp *app)
{
    if (cb.refresh_home_pages)
        cb.refresh_home_pages(app);
}

static void
nion_stop_all_web_activity(NionApp *app)
{
    if (cb.stop_all_web_activity)
        cb.stop_all_web_activity(app);
}

static void
nion_build_ui(NionApp *app)
{
    if (cb.build_ui)
        cb.build_ui(app);
}

void
nion_app_set_callbacks(const NionAppLifecycleCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

static void nion_sync_private_windows_tor(NionApp *app);
static void nion_request_close(NionApp *app);
static void nion_open_private_window(NionApp *source);

void nion_set_tor_ready(NionApp *app, gboolean ready)
{
    if (ready)
        app->tor_switching_identity = FALSE;
    app->tor_ready = ready;
    app->tor_failed = ready ? FALSE : app->tor_failed;

    if (ready) {
        if (app->tor_startup_timeout_id) {
            g_source_remove(app->tor_startup_timeout_id);
            app->tor_startup_timeout_id = 0;
        }
        app->tor_bootstrap_percent = 100;
        nion_set_status(app, "● TOR CONNECTED");
    } else if (!app->tor_failed) {
        gchar *status = g_strdup_printf("○ CONNECTING TO TOR… %d%%",
                                        app->tor_bootstrap_percent);
        nion_set_status(app, status);
        g_free(status);
    }

    nion_refresh_home_pages(app);
    if (ready)
        nion_start_pending_restores(app);
    nion_update_controls(app);
    nion_sync_private_windows_tor(app);
}

void nion_set_tor_progress(NionApp *app, gint percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    app->tor_failed = FALSE;
    app->tor_bootstrap_percent = percent;

    if (percent >= 100) {
        nion_set_tor_ready(app, TRUE);
        return;
    }

    app->tor_ready = FALSE;
    gchar *status = g_strdup_printf("○ CONNECTING TO TOR… %d%%", percent);
    nion_set_status(app, status);
    g_free(status);

    nion_refresh_home_pages(app);
    nion_update_controls(app);
    nion_sync_private_windows_tor(app);
}

void nion_set_tor_error(NionApp *app, const gchar *message)
{
    /* A failed rotation (or any Tor error) releases the switching guard so a
     * later New Identity request is allowed to try again. */
    app->tor_switching_identity = FALSE;
    app->tor_ready = FALSE;
    app->tor_failed = TRUE;

    /* Fail closed twice: UI/policy code blocks new navigation, and the
     * WebKit network session is moved away from the former Tor SOCKS port.
     * This prevents background fetches/subresources from continuing to use a
     * stale loopback endpoint after the Tor process has died. */
    g_free(app->tor_proxy_uri);
    app->tor_proxy_uri = g_strdup("socks://127.0.0.1:9");
    nion_apply_network_proxy(app);

    gchar *status = g_strdup_printf("○ TOR ERROR — %s",
                                    (message && *message) ? message : "Tor stopped unexpectedly");
    nion_set_status(app, status);
    g_free(status);

    nion_stop_all_web_activity(app);
    nion_cancel_active_downloads(app);
    nion_refresh_home_pages(app);
    nion_update_controls(app);
    nion_sync_private_windows_tor(app);
}

static void nion_private_clear_closed_tabs(NionApp *app)
{
    if (!app || !app->is_private || !app->closed_tabs)
        return;

    while (!g_queue_is_empty(app->closed_tabs))
        nion_closed_tab_free(g_queue_pop_head(app->closed_tabs));
}

void action_private_window(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_open_private_window(user_data);
}

/* ---- New Identity / circuit rotation (v2.1) ---- */

static void nion_rotate_tor_guard_state(NionApp *app)
{
    /* A fresh identity needs fresh entry guards. Tor keeps its guard list in
     * the DataDirectory `state` file; move it aside (never delete silently)
     * so the restarted Tor selects new guards and builds new circuits. */
    if (!app || !app->tor_dir)
        return;

    gchar *state_file = g_build_filename(app->tor_dir, "state", NULL);
    if (g_file_test(state_file, G_FILE_TEST_EXISTS)) {
        gchar *target = g_build_filename(app->tor_dir, "state.previous-identity", NULL);
        if (g_rename(state_file, target) != 0) {
            g_warning("New Identity: could not rotate Tor guard state %s: %s",
                      state_file, g_strerror(errno));
        } else {
            g_printerr("[NiOn] New Identity: rotated Tor guard state for fresh circuits.\n");
        }
        g_free(target);
    }
    g_free(state_file);

    /* A hard-killed Tor can leave a stale lock; it must not block the restart. */
    gchar *lock_file = g_build_filename(app->tor_dir, "lock", NULL);
    if (g_file_test(lock_file, G_FILE_TEST_EXISTS))
        g_unlink(lock_file);
    g_free(lock_file);
}

void action_new_identity(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    if (!app)
        return;

    /* Tor is owned by the normal window. Triggering from a Private Window
     * rotates the owner's circuit; every Private Window follows through the
     * existing tor-state sync in nion_set_tor_ready / _progress / _error. */
    if (app->is_private)
        app = app->owner;
    if (!app || app->shutting_down)
        return;
    if (app->tor_switching_identity) {
        nion_set_status(app, "○ NEW IDENTITY — circuit rotation already in progress…");
        return;
    }
    app->tor_switching_identity = TRUE;

    /* Fail closed for the whole rotation: stop in-flight loads/downloads and
     * move every window to "connecting" (tor_ready = FALSE blocks new nav). */
    nion_stop_all_web_activity(app);
    nion_cancel_active_downloads(app);
    nion_set_tor_progress(app, 0);
    nion_set_status(app, "○ NEW IDENTITY — resetting Tor circuit…");

    nion_stop_tor_gracefully(app);
    nion_rotate_tor_guard_state(app);

    if (!nion_choose_tor_port(app)) {
        app->tor_switching_identity = FALSE;
        return; /* error already surfaced via nion_set_tor_error */
    }
    nion_apply_network_proxy(app);
    if (!nion_start_tor(app)) {
        app->tor_switching_identity = FALSE;
        return; /* error already surfaced via nion_set_tor_error */
    }

    /* The switching guard is released by nion_set_tor_ready(TRUE) once the
     * new circuit finishes bootstrapping, or by nion_set_tor_error on failure. */
}

void action_exit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    if (app && app->is_private) {
        gtk_window_close(GTK_WINDOW(app->window));
        return;
    }
    nion_request_close(app);
}

void nion_prepare_dirs(NionApp *app)
{
    app->data_dir = g_build_filename(g_get_user_data_dir(), "nion", NULL);
    app->cache_dir = g_build_filename(g_get_user_cache_dir(), "nion", NULL);
    app->tor_dir = g_build_filename(app->data_dir, "tor", NULL);
    app->cookie_file = g_build_filename(app->data_dir, "cookies.sqlite", NULL);
    app->config_dir = g_build_filename(g_get_user_config_dir(), "nion", NULL);
    app->preferences_file = g_build_filename(app->config_dir, "preferences.ini", NULL);
    app->session_file = g_build_filename(app->data_dir, "session.ini", NULL);
    app->downloads_file = g_build_filename(app->data_dir, "downloads.ini", NULL);
    app->bookmarks_file = g_build_filename(app->data_dir, "bookmarks.ini", NULL);
    app->site_zoom_file = g_build_filename(app->config_dir, "site-zoom.ini", NULL);
    app->site_javascript_file = g_build_filename(app->config_dir, "site-javascript.ini", NULL);
    app->content_blocking_file = g_build_filename(app->config_dir, "content-blocking.ini", NULL);
    app->autoplay_file = g_build_filename(app->config_dir, "autoplay.ini", NULL);
    app->content_filter_store_dir = g_build_filename(app->cache_dir, "content-filters", NULL);
    app->tor_runtime_file = g_build_filename(app->data_dir, "tor-runtime.ini", NULL);

    const gchar *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    app->download_dir = (downloads && *downloads)
        ? g_strdup(downloads)
        : g_build_filename(g_get_home_dir(), "Downloads", NULL);

    const gchar *private_dirs[] = {
        app->data_dir,
        app->cache_dir,
        app->config_dir,
        app->tor_dir,
        NULL,
    };
    for (guint i = 0; private_dirs[i]; i++) {
        if (g_mkdir_with_parents(private_dirs[i], 0700) != 0)
            g_warning("Could not create NiOn private directory %s: %s", private_dirs[i], g_strerror(errno));
        else
            g_chmod(private_dirs[i], 0700);
    }

    if (g_mkdir_with_parents(app->download_dir, 0755) != 0)
        g_warning("Could not create download directory %s: %s", app->download_dir, g_strerror(errno));
}

static guint nion_count_nonblank_tabs(NionApp *app)
{
    if (!app || !app->notebook)
        return 0;

    guint count = 0;
    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || tab->home_page)
            continue;

        const gchar *uri = tab->display_uri_override ? tab->display_uri_override
                                                     : webkit_web_view_get_uri(tab->web_view);
        if (uri && *uri && !g_str_equal(uri, "about:blank"))
            count++;
    }
    return count;
}

static void nion_finish_close(NionApp *app)
{
    if (!app || app->shutting_down)
        return;

    if (app->session_save_source_id) {
        g_source_remove(app->session_save_source_id);
        app->session_save_source_id = 0;
    }
    nion_save_session(app, TRUE);
    nion_save_download_history(app);
    app->close_confirmed = TRUE;
    app->shutting_down = TRUE;
    g_application_quit(G_APPLICATION(app->application));
}

static void on_close_confirm_no(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    app->close_confirm_open = FALSE;
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}


static void on_close_confirm_yes(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    app->close_confirm_open = FALSE;
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
    nion_finish_close(app);
}

static gboolean on_close_confirm_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    NionApp *app = user_data;
    app->close_confirm_open = FALSE;
    return FALSE;
}

static void nion_request_close(NionApp *app)
{
    if (!app || app->shutting_down)
        return;

    guint nonblank = nion_count_nonblank_tabs(app);
    if (nonblank == 0) {
        nion_finish_close(app);
        return;
    }

    if (app->close_confirm_open)
        return;
    app->close_confirm_open = TRUE;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Close NiOn?");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 430, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Close NiOn?</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    gchar *message_text = g_strdup_printf(
        nonblank == 1
            ? "1 tab is still open on a website. Close NiOn?"
            : "%u tabs are still open on websites. Close NiOn?",
        nonblank);
    GtkWidget *message = gtk_label_new(message_text);
    g_free(message_text);
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *note = gtk_label_new(
        app->restore_session
            ? "Your tabs are saved and can be restored at the next start."
            : "Tab restore is disabled in Preferences.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *no = gtk_button_new_with_label("No");
    GtkWidget *yes = gtk_button_new_with_label("Yes");
    gtk_widget_add_css_class(yes, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), no);
    gtk_box_append(GTK_BOX(buttons), yes);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(no, "clicked", G_CALLBACK(on_close_confirm_no), app);
    g_signal_connect(yes, "clicked", G_CALLBACK(on_close_confirm_yes), app);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_confirm_request), app);
    gtk_window_present(GTK_WINDOW(window));
}

static gboolean nion_free_private_app_idle(gpointer user_data)
{
    NionApp *app = user_data;
    if (!app)
        return G_SOURCE_REMOVE;

    NionApp *owner = app->owner;
    if (owner && owner->private_windows)
        g_ptr_array_remove_fast(owner->private_windows, app);

    g_clear_object(&app->network_session);
    
    // Baris g_clear_object(&app->web_context); sudah dihapus di sini

    g_clear_pointer(&app->tor_proxy_uri, g_free);
    g_clear_pointer(&app->tor_last_log, g_free);
    g_clear_pointer(&app->search_engine, g_free);
    g_clear_pointer(&app->download_dir, g_free);
    g_clear_pointer(&app->preferences_file, g_free);
    g_clear_pointer(&app->bookmarks_file, g_free);
    
    if (app->bookmarks)
        g_ptr_array_unref(app->bookmarks);
    if (app->site_javascript_disabled)
        g_hash_table_unref(app->site_javascript_disabled);
    if (app->site_javascript_enabled)
        g_hash_table_unref(app->site_javascript_enabled);
    if (app->content_blocking_disabled)
        g_hash_table_unref(app->content_blocking_disabled);
    if (app->autoplay_allowed_sites)
        g_hash_table_unref(app->autoplay_allowed_sites);
    if (app->temporary_permissions)
        g_hash_table_unref(app->temporary_permissions);
    if (app->closed_tabs)
        g_queue_free_full(app->closed_tabs, nion_closed_tab_free);
        
    // Hentikan dan bersihkan proses Tor langsung di sini
    if (app->tor_process) {
        g_subprocess_send_signal(app->tor_process, SIGTERM);
        g_clear_object(&app->tor_process);
    }

    g_free(app);
    return G_SOURCE_REMOVE;
}

gboolean on_window_close_request(GtkWindow *window, gpointer user_data)
{
    NionApp *app = user_data;
    if (app->is_private) {
        if (app->shutting_down)
            return FALSE;
        app->shutting_down = TRUE;
        if (app->private_tab_close_confirm_window) {
            gtk_window_destroy(GTK_WINDOW(app->private_tab_close_confirm_window));
            app->private_tab_close_confirm_window = NULL;
            app->private_tab_close_confirm_open = FALSE;
        }
        nion_cancel_active_downloads(app);
        nion_private_cleanup_partial_downloads(app);
        nion_private_clear_closed_tabs(app);
        nion_stop_all_web_activity(app);
        if (app->downloads_window) {
            gtk_window_destroy(GTK_WINDOW(app->downloads_window));
            app->downloads_window = NULL;
            app->downloads_list = NULL;
            app->downloads_empty_label = NULL;
        }
        gtk_window_destroy(window);
        g_idle_add(nion_free_private_app_idle, app);
        return TRUE;
    }

    if (app->shutting_down || app->close_confirmed)
        return FALSE;

    nion_request_close(app);
    return TRUE;
}

static void nion_private_sync_from_owner(NionApp *private_app, NionApp *owner)
{
    if (!private_app || !owner)
        return;

    private_app->tor_ready = owner->tor_ready;
    private_app->tor_failed = owner->tor_failed;
    private_app->tor_bootstrap_percent = owner->tor_bootstrap_percent;
    private_app->tor_socks_port = owner->tor_socks_port;
    g_free(private_app->tor_proxy_uri);
    private_app->tor_proxy_uri = g_strdup(owner->tor_proxy_uri ? owner->tor_proxy_uri
                                                               : "socks://127.0.0.1:9");
    g_free(private_app->tor_last_log);
    private_app->tor_last_log = g_strdup(owner->tor_last_log);
    nion_apply_network_proxy(private_app);
    if (!private_app->tor_ready) {
        nion_stop_all_web_activity(private_app);
        nion_cancel_active_downloads(private_app);
    }

    if (private_app->window) {
        if (private_app->tor_ready)
            nion_set_status(private_app, "● TOR CONNECTED — PRIVATE");
        else if (private_app->tor_failed)
            nion_set_status(private_app, "○ TOR ERROR — PRIVATE BROWSING BLOCKED");
        else {
            gchar *status = g_strdup_printf("○ CONNECTING TO TOR… %d%% — PRIVATE",
                                            private_app->tor_bootstrap_percent);
            nion_set_status(private_app, status);
            g_free(status);
        }
        nion_refresh_home_pages(private_app);
        nion_update_controls(private_app);
    }
}

static void nion_sync_private_windows_tor(NionApp *app)
{
    if (!app || app->is_private || !app->private_windows)
        return;
    for (guint i = 0; i < app->private_windows->len; i++) {
        NionApp *private_app = g_ptr_array_index(app->private_windows, i);
        nion_private_sync_from_owner(private_app, app);
    }
}

static void nion_open_private_window(NionApp *source)
{
    if (!source)
        return;
    NionApp *owner = source->is_private ? source->owner : source;
    if (!owner)
        return;

    NionApp *app = g_new0(NionApp, 1);
    app->application = owner->application;
    app->is_private = TRUE;
    app->owner = owner;
    app->restore_session = FALSE;
    app->block_third_party_cookies = owner->block_third_party_cookies;
    app->security_level = owner->security_level;
    app->search_engine = g_strdup(owner->search_engine ? owner->search_engine : "duckduckgo");
    app->preferences_file = g_strdup(owner->preferences_file);
    app->bookmarks_file = g_strdup(owner->bookmarks_file);
    app->download_dir = g_strdup(owner->download_dir);
    app->bookmarks = owner->bookmarks ? g_ptr_array_ref(owner->bookmarks)
                                     : g_ptr_array_new_with_free_func(nion_bookmark_free);
    app->closed_tabs = g_queue_new();
    nion_load_site_javascript(app);
    nion_load_content_blocking(app);
    nion_load_autoplay(app);

    nion_private_sync_from_owner(app, owner);
    if (!nion_prepare_network(app)) {
        nion_set_status(owner, owner->tor_ready
            ? "● TOR CONNECTED — PRIVATE WINDOW BLOCKED: EPHEMERAL SESSION UNAVAILABLE"
            : "○ TOR NOT READY — PRIVATE WINDOW BLOCKED: EPHEMERAL SESSION UNAVAILABLE");
        nion_free_private_app_idle(app);
        return;
    }

    if (!owner->private_windows)
        owner->private_windows = g_ptr_array_new();
    g_ptr_array_add(owner->private_windows, app);

    /* Downloads stay in main.c for now; the network module no longer connects
     * this signal (it would create a network -> main.c coupling). */
    g_signal_connect(app->network_session, "download-started",
                     G_CALLBACK(on_download_started), app);

    nion_build_ui(app);
    nion_private_sync_from_owner(app, owner);
}

static void nion_cleanup(NionApp *app)
{
    if (!app->shutting_down)
        nion_save_session(app, TRUE);
    nion_save_download_history(app);
    nion_save_bookmarks(app);

    if (app->session_save_source_id) {
        g_source_remove(app->session_save_source_id);
        app->session_save_source_id = 0;
    }

    app->shutting_down = TRUE;
    if (app->crash_recovery_window) {
        GtkWidget *recovery = app->crash_recovery_window;
        app->crash_recovery_window = NULL;
        gtk_window_destroy(GTK_WINDOW(recovery));
    }
    if (app->private_windows) {
        while (app->private_windows->len > 0) {
            NionApp *private_app = g_ptr_array_index(app->private_windows, app->private_windows->len - 1);
            if (private_app) {
                private_app->shutting_down = TRUE;
                nion_cancel_active_downloads(private_app);
                nion_private_cleanup_partial_downloads(private_app);
                nion_private_clear_closed_tabs(private_app);
            }
            if (private_app && private_app->downloads_window)
                gtk_window_destroy(GTK_WINDOW(private_app->downloads_window));
            if (private_app && private_app->window)
                gtk_window_destroy(GTK_WINDOW(private_app->window));
            g_ptr_array_remove_index_fast(app->private_windows, app->private_windows->len - 1);
            if (private_app) {
                g_clear_object(&private_app->network_session);
                g_clear_pointer(&private_app->tor_proxy_uri, g_free);
                g_clear_pointer(&private_app->tor_last_log, g_free);
                g_clear_pointer(&private_app->search_engine, g_free);
                g_clear_pointer(&private_app->download_dir, g_free);
                g_clear_pointer(&private_app->preferences_file, g_free);
                g_clear_pointer(&private_app->bookmarks_file, g_free);
                if (private_app->bookmarks) g_ptr_array_unref(private_app->bookmarks);
                if (private_app->site_javascript_disabled) g_hash_table_unref(private_app->site_javascript_disabled);
                if (private_app->site_javascript_enabled) g_hash_table_unref(private_app->site_javascript_enabled);
                if (private_app->content_blocking_disabled) g_hash_table_unref(private_app->content_blocking_disabled);
                if (private_app->autoplay_allowed_sites) g_hash_table_unref(private_app->autoplay_allowed_sites);
                if (private_app->temporary_permissions) g_hash_table_unref(private_app->temporary_permissions);
                if (private_app->closed_tabs) g_queue_free_full(private_app->closed_tabs, nion_closed_tab_free);
                g_free(private_app);
            }
        }
        g_ptr_array_unref(app->private_windows);
        app->private_windows = NULL;
    }
    nion_cancel_active_downloads(app);
    nion_stop_tor_gracefully(app);

    g_clear_object(&app->tor_output);
    g_clear_object(&app->tor_process);
    g_clear_object(&app->network_session);

    g_clear_pointer(&app->data_dir, g_free);
    g_clear_pointer(&app->cache_dir, g_free);
    g_clear_pointer(&app->tor_dir, g_free);
    g_clear_pointer(&app->cookie_file, g_free);
    g_clear_pointer(&app->download_dir, g_free);
    g_clear_pointer(&app->config_dir, g_free);
    g_clear_pointer(&app->preferences_file, g_free);
    g_clear_pointer(&app->session_file, g_free);
    g_clear_pointer(&app->downloads_file, g_free);
    g_clear_pointer(&app->bookmarks_file, g_free);
    g_clear_pointer(&app->site_zoom_file, g_free);
    g_clear_pointer(&app->site_javascript_file, g_free);
    g_clear_pointer(&app->content_blocking_file, g_free);
    g_clear_pointer(&app->autoplay_file, g_free);
    g_clear_pointer(&app->content_filter_store_dir, g_free);
    g_clear_pointer(&app->tor_runtime_file, g_free);
    g_clear_pointer(&app->tor_proxy_uri, g_free);
    g_clear_pointer(&app->tor_binary_path, g_free);
    g_clear_pointer(&app->tor_last_log, g_free);
    g_clear_pointer(&app->search_engine, g_free);
    if (app->bookmarks) {
        g_ptr_array_unref(app->bookmarks);
        app->bookmarks = NULL;
    }
    if (app->site_zoom) {
        g_hash_table_unref(app->site_zoom);
        app->site_zoom = NULL;
    }
    if (app->site_javascript_disabled) {
        g_hash_table_unref(app->site_javascript_disabled);
        app->site_javascript_disabled = NULL;
    }
    if (app->site_javascript_enabled) {
        g_hash_table_unref(app->site_javascript_enabled);
        app->site_javascript_enabled = NULL;
    }
    if (app->content_blocking_disabled) {
        g_hash_table_unref(app->content_blocking_disabled);
        app->content_blocking_disabled = NULL;
    }
    if (app->autoplay_allowed_sites) {
        g_hash_table_unref(app->autoplay_allowed_sites);
        app->autoplay_allowed_sites = NULL;
    }
    if (app->content_filter) {
        webkit_user_content_filter_unref(app->content_filter);
        app->content_filter = NULL;
    }
    g_clear_object(&app->content_filter_store);
    if (app->temporary_permissions) {
        g_hash_table_unref(app->temporary_permissions);
        app->temporary_permissions = NULL;
    }
    if (app->closed_tabs) {
        g_queue_free_full(app->closed_tabs, nion_closed_tab_free);
        app->closed_tabs = NULL;
    }
}

void nion_prepare_appimage_webkit_sandbox(void)
{
    /* --- LANGKAH 3: PENGHEMAT RAM MULAI DI SINI --- */
    WebKitWebContext *context = webkit_web_context_get_default();
    
    // Set model cache agar super irit RAM
    webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    /* --- LANGKAH 3 SELESAI --- */

    const gchar *appdir = g_getenv("APPDIR");
    if (!appdir || !g_path_is_absolute(appdir) ||
        !g_file_test(appdir, G_FILE_TEST_IS_DIR))
        return;

    /* WebKitGTK 6 keeps its WebProcess sandbox mandatory. The AppImage mount
     * is outside the normal system prefixes, so explicitly make the read-only
     * AppDir visible before any WebKit subprocess can be created. */
    webkit_web_context_add_path_to_sandbox(context, appdir, TRUE);
}

void on_shutdown(GApplication *application, gpointer user_data)
{
    (void)application;
    nion_cleanup(user_data);
}
