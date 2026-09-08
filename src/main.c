/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <libsoup/soup.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "types.h"
#include "util.h"
#include "navigation.h"
#include "per-site.h"
#include "content-filter.h"
#include "permission.h"
#include "privacy.h"
#include "tor-core.h"
#include "network.h"
#include "session.h"
#include "tabs.h"
#include "webview.h"
#include "bookmarks.h"
#include "downloads.h"
#include "settings.h"
#include "site-data.h"
#include "app.h"
#include "ui.h"

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select);
static NionTab *nion_new_tab_internal(NionApp *app, const gchar *uri, gboolean select,
                                      WebKitWebView *related_view);
static void nion_detect_onion_location(NionTab *tab);
static void nion_prepare_content_filter(NionApp *app);

static NionTab *nion_current_tab(NionApp *app)
{
    gint page_num = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (page_num < 0)
        return NULL;

    GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page_num);
    if (!page)
        return NULL;

    return g_object_get_data(G_OBJECT(page), "nion-tab");
}

static void nion_set_status(NionApp *app, const gchar *text)
{
    if (!app->status_label)
        return;

    gtk_label_set_text(GTK_LABEL(app->status_label), text ? text : "");

    /* Keep status semantics obvious even on themes that ignore custom colors.
     * CSS classes only add subtle emphasis; the leading glyph/text remains the
     * authoritative state indicator. */
    gtk_widget_remove_css_class(app->status_label, "nion-status-connected");
    gtk_widget_remove_css_class(app->status_label, "nion-status-connecting");
    gtk_widget_remove_css_class(app->status_label, "nion-status-warning");
    gtk_widget_remove_css_class(app->status_label, "nion-status-error");

    if (!text)
        return;

    if (strstr(text, "MIXED CONTENT"))
        gtk_widget_add_css_class(app->status_label, "nion-status-warning");
    else if (g_str_has_prefix(text, "● TOR CONNECTED"))
        gtk_widget_add_css_class(app->status_label, "nion-status-connected");
    else if (strstr(text, "TOR ERROR") || strstr(text, "TOR OFFLINE"))
        gtk_widget_add_css_class(app->status_label, "nion-status-error");
    else if (strstr(text, "NOT READY") || strstr(text, "PORT CONFLICT") ||
             strstr(text, "STATE RECOVERY"))
        gtk_widget_add_css_class(app->status_label, "nion-status-warning");
    else
        gtk_widget_add_css_class(app->status_label, "nion-status-connecting");
}

static void nion_apply_content_filter_to_window(NionApp *app)
{
    if (!app || !app->notebook)
        return;
    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (tab && tab->web_view)
            nion_apply_content_blocking(tab, webkit_web_view_get_uri(tab->web_view));
    }
    nion_update_site_info_button(app);
}

static void on_content_filter_saved(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    if (!app || app->shutting_down)
        return;

    GError *error = NULL;
    WebKitUserContentFilter *filter = webkit_user_content_filter_store_save_finish(
        WEBKIT_USER_CONTENT_FILTER_STORE(source), result, &error);
    if (!filter) {
        app->content_filter_failed = TRUE;
        app->content_filter_ready = FALSE;
        g_warning("NiOn lightweight content filter could not be compiled: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        nion_update_site_info_button(app);
        return;
    }

    if (app->content_filter)
        webkit_user_content_filter_unref(app->content_filter);
    app->content_filter = filter;
    app->content_filter_failed = FALSE;
    app->content_filter_ready = TRUE;
    nion_apply_content_filter_to_window(app);
    if (app->private_windows) {
        for (guint i = 0; i < app->private_windows->len; i++) {
            NionApp *private_app = g_ptr_array_index(app->private_windows, i);
            nion_apply_content_filter_to_window(private_app);
        }
    }
}

static void nion_prepare_content_filter(NionApp *app)
{
    if (!app || app->is_private || !app->content_filter_store_dir)
        return;

    if (g_mkdir_with_parents(app->content_filter_store_dir, 0700) != 0) {
        g_warning("Could not create content-filter store %s: %s",
                  app->content_filter_store_dir, g_strerror(errno));
        app->content_filter_failed = TRUE;
        return;
    }
    g_chmod(app->content_filter_store_dir, 0700);

    GError *error = NULL;
    GBytes *rules = g_resources_lookup_data(
        "/io/github/jeannesbryan/Nion/content-blocking.json",
        G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!rules) {
        g_warning("Could not load bundled NiOn content-blocking rules: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        app->content_filter_failed = TRUE;
        return;
    }

    app->content_filter_store = webkit_user_content_filter_store_new(
        app->content_filter_store_dir);
    if (!app->content_filter_store) {
        app->content_filter_failed = TRUE;
        g_bytes_unref(rules);
        return;
    }

    webkit_user_content_filter_store_save(app->content_filter_store,
                                          NION_CONTENT_FILTER_ID,
                                          rules,
                                          NULL,
                                          on_content_filter_saved,
                                          app);
    g_bytes_unref(rules);
}

static gboolean nion_is_valid_onion_location(NionTab *tab, const gchar *candidate)
{
    if (!tab || !candidate || !*candidate)
        return FALSE;

    const gchar *page_uri = webkit_web_view_get_uri(tab->web_view);
    if (!nion_uri_is_https_clearnet(page_uri))
        return FALSE;

    gchar *validation = NULL;
    gboolean ok = nion_uri_is_onion(candidate) && nion_validate_uri(candidate, &validation);
    g_free(validation);
    return ok;
}

static void nion_set_onion_location(NionTab *tab, const gchar *candidate)
{
    if (!tab)
        return;

    gchar *normalized = candidate ? g_strdup(candidate) : NULL;
    if (normalized)
        g_strstrip(normalized);
    if (!normalized || !*normalized || !nion_is_valid_onion_location(tab, normalized))
        g_clear_pointer(&normalized, g_free);

    /* A genuinely advertised .onion twin of the current clearnet page is
     * remembered per site (v2.1 #5) so future visits can offer the jump even
     * if the site stops sending Onion-Location. Only the fresh detection
     * path (valid candidate on an https clearnet page) persists; clearing
     * (normalized == NULL) never does. */
    if (normalized && tab->app && !tab->app->is_private && tab->web_view) {
        const gchar *page_uri = webkit_web_view_get_uri(tab->web_view);
        if (nion_uri_is_https_clearnet(page_uri)) {
            gchar *site_key = nion_site_key_for_uri(page_uri);
            if (site_key) {
                nion_remember_preferred_onion(tab->app, site_key, normalized);
                g_free(site_key);
            }
        }
    }

    if (g_strcmp0(tab->onion_location, normalized) == 0) {
        g_free(normalized);
        return;
    }

    g_free(tab->onion_location);
    tab->onion_location = normalized;
    nion_update_onion_button(tab->app);
}

static void on_onion_meta_evaluated(GObject *object, GAsyncResult *result, gpointer user_data)
{
    WebKitWebView *web_view = WEBKIT_WEB_VIEW(object);
    GtkWidget *page = GTK_WIDGET(user_data);
    NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;

    GError *error = NULL;
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(web_view, result, &error);
    if (value && tab && !tab->onion_location && jsc_value_is_string(value)) {
        gchar *candidate = jsc_value_to_string(value);
        if (candidate) {
            g_strstrip(candidate);
            if (*candidate)
                nion_set_onion_location(tab, candidate);
            g_free(candidate);
        }
    }

    g_clear_object(&value);
    g_clear_error(&error);
    if (page)
        g_object_unref(page);
}

static void nion_detect_onion_location(NionTab *tab)
{
    if (!tab)
        return;

    nion_set_onion_location(tab, NULL);

    const gchar *page_uri = webkit_web_view_get_uri(tab->web_view);
    if (!nion_uri_is_https_clearnet(page_uri))
        return;

    /* Prefer the standardized HTTP Onion-Location response header. */
    WebKitWebResource *resource = webkit_web_view_get_main_resource(tab->web_view);
    if (resource) {
        WebKitURIResponse *response = webkit_web_resource_get_response(resource);
        if (response) {
            SoupMessageHeaders *headers = webkit_uri_response_get_http_headers(response);
            const gchar *header = headers ? soup_message_headers_get_one(headers, "Onion-Location") : NULL;
            if (header && *header) {
                nion_set_onion_location(tab, header);
                if (tab->onion_location)
                    return;
            }
        }
    }

    /* Tor also documents the equivalent HTML meta form. Keep the script tiny
     * and only read one value from the already-loaded top-level document. */
    const gchar *script =
        "(() => {"
        " const m = document.querySelector('meta[http-equiv=\"onion-location\" i]');"
        " return m && m.content ? m.content.trim() : '';"
        "})()";
    webkit_web_view_evaluate_javascript(tab->web_view,
                                        script,
                                        -1,
                                        "nion-onion-location",
                                        NULL,
                                        NULL,
                                        on_onion_meta_evaluated,
                                        g_object_ref(tab->page));
}

static NionTab *nion_new_tab_internal(NionApp *app, const gchar *uri, gboolean select,
                                      WebKitWebView *related_view)
{
    NionTab *tab = g_new0(NionTab, 1);
    tab->app = app;

    WebKitUserContentManager *content_manager = webkit_user_content_manager_new();
    WebKitWebsitePolicies *default_policies = related_view
        ? g_object_ref(webkit_web_view_get_website_policies(related_view))
        : nion_website_policies_for_uri(app, uri);
    if (!default_policies)
        default_policies = nion_website_policies_for_uri(app, uri);
    if (related_view) {
        /* Keep popup/new-tab views related to the opener for WebKit lifecycle
         * and NetworkSession inheritance, while keeping mutable Settings and
         * UserContentManager tab-local. This is required for per-site JavaScript
         * and content-blocking exceptions to coexist in different tabs. */
        WebKitSettings *settings = webkit_settings_new();
        nion_apply_privacy_settings(app, settings);
        webkit_settings_set_enable_javascript(
            settings,
            webkit_settings_get_enable_javascript(webkit_web_view_get_settings(related_view)));
        tab->web_view = WEBKIT_WEB_VIEW(g_object_new(
            WEBKIT_TYPE_WEB_VIEW,
            "related-view", related_view,
            "settings", settings,
            "user-content-manager", content_manager,
            "website-policies", default_policies,
            NULL));
        g_object_unref(settings);
    } else {
        WebKitSettings *settings = webkit_settings_new();
        nion_apply_privacy_settings(app, settings);

        tab->web_view = WEBKIT_WEB_VIEW(g_object_new(
            WEBKIT_TYPE_WEB_VIEW,
            "network-session", app->network_session,
            "settings", settings,
            "user-content-manager", content_manager,
            "website-policies", default_policies,
            NULL));
        g_object_unref(settings);
    }
    g_object_unref(default_policies);
    g_object_unref(content_manager);

    tab->page = GTK_WIDGET(tab->web_view);
    g_object_set_data_full(G_OBJECT(tab->page), "nion-tab", tab, nion_tab_free);
    g_object_set_data(G_OBJECT(tab->web_view), "nion-tab-pointer", tab);
    nion_apply_content_blocking(tab, uri);

    GtkWidget *label = nion_make_tab_label(tab);
    gint page_num = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab->page, label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), tab->page, TRUE);

    WebKitFindController *find_controller = webkit_web_view_get_find_controller(tab->web_view);
    g_signal_connect(find_controller, "found-text", G_CALLBACK(on_find_found), tab);
    g_signal_connect(find_controller, "failed-to-find-text", G_CALLBACK(on_find_failed), tab);

    WebKitBackForwardList *history = webkit_web_view_get_back_forward_list(tab->web_view);
    g_signal_connect(history, "changed", G_CALLBACK(on_back_forward_list_changed), tab);

    g_signal_connect(tab->web_view, "notify::title", G_CALLBACK(on_webview_title_changed), tab);
    g_signal_connect(tab->web_view, "notify::favicon", G_CALLBACK(on_webview_favicon_changed), tab);
    g_object_bind_property(tab->web_view, "is-playing-audio",
                           tab->audio_button, "visible",
                           G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
    g_signal_connect(tab->web_view, "notify::is-muted",
                     G_CALLBACK(on_webview_muted_changed), tab);
    nion_update_tab_audio_button(tab);
    g_signal_connect(tab->web_view, "notify::uri", G_CALLBACK(on_webview_uri_changed), tab);
    g_signal_connect(tab->web_view, "notify::estimated-load-progress", G_CALLBACK(on_webview_progress_changed), tab);
    g_signal_connect(tab->web_view, "load-changed", G_CALLBACK(on_webview_load_changed), tab);
    g_signal_connect(tab->web_view, "load-failed", G_CALLBACK(on_webview_load_failed), tab);
    g_signal_connect(tab->web_view, "load-failed-with-tls-errors", G_CALLBACK(on_webview_tls_failed), tab);
    g_signal_connect(tab->web_view, "web-process-terminated",
                     G_CALLBACK(on_webview_web_process_terminated), tab);
    g_signal_connect(tab->web_view, "insecure-content-detected",
                     G_CALLBACK(on_webview_insecure_content_detected), tab);
    g_signal_connect(tab->web_view, "decide-policy", G_CALLBACK(on_webview_decide_policy), tab);
    g_signal_connect(tab->web_view, "permission-request", G_CALLBACK(on_permission_request), tab);
    g_signal_connect(tab->web_view, "context-menu", G_CALLBACK(on_webview_context_menu), tab);
    g_signal_connect(tab->web_view, "create", G_CALLBACK(on_webview_create), app);

    if (uri) {
        if (*uri) {
            tab->home_page = FALSE;
            tab->error_page = FALSE;
            nion_apply_site_javascript(tab, uri);
            webkit_web_view_load_uri(tab->web_view, uri);
        } else {
            /* Intentionally unloaded. Used for popup/new-window WebViews. */
            tab->home_page = FALSE;
            tab->error_page = FALSE;
        }
    } else {
        nion_load_home(tab);
    }

    if (select)
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_num);

    nion_tab_touch(tab);
    nion_update_controls(app);
    nion_schedule_session_save(app);
    return tab;
}

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return nion_new_tab_internal(app, uri, select, NULL);
}

/* Adapter for the Start Page "New Identity" button: webview.c dispatches
 * nion://new-identity here through NionWebviewCallbacks; activating the
 * window action routes through the same orchestration as the menu item and
 * Ctrl+Shift+U. */
static void nion_activate_new_identity(NionApp *app)
{
    if (app && app->window)
        g_action_group_activate_action(G_ACTION_GROUP(app->window),
                                       "new-identity", NULL);
}

static void nion_install_actions(NionApp *app)
{
    const GActionEntry actions[] = {
        { "new-tab", action_new_tab, NULL, NULL, NULL, {0} },
        { "private-window", action_private_window, NULL, NULL, NULL, {0} },
        { "new-identity", action_new_identity, NULL, NULL, NULL, {0} },
        { "close-tab", action_close_tab, NULL, NULL, NULL, {0} },
        { "reopen-closed-tab", action_reopen_closed_tab, NULL, NULL, NULL, {0} },
        { "focus-location", action_focus_location, NULL, NULL, NULL, {0} },
        { "reload", action_reload, NULL, NULL, NULL, {0} },
        { "hard-reload", action_hard_reload, NULL, NULL, NULL, {0} },
        { "find", action_find, NULL, NULL, NULL, {0} },
        { "zoom-in", action_zoom_in, NULL, NULL, NULL, {0} },
        { "zoom-out", action_zoom_out, NULL, NULL, NULL, {0} },
        { "zoom-reset", action_zoom_reset, NULL, NULL, NULL, {0} },
        { "print", action_print, NULL, NULL, NULL, {0} },
        { "fullscreen", action_fullscreen, NULL, NULL, NULL, {0} },
        { "back", action_back, NULL, NULL, NULL, {0} },
        { "forward", action_forward, NULL, NULL, NULL, {0} },
        { "next-tab", action_next_tab, NULL, NULL, NULL, {0} },
        { "previous-tab", action_previous_tab, NULL, NULL, NULL, {0} },
        { "downloads", action_downloads, NULL, NULL, NULL, {0} },
        { "bookmark-page", action_bookmark_page, NULL, NULL, NULL, {0} },
        { "bookmarks", action_bookmarks, NULL, NULL, NULL, {0} },
        { "preferences", action_preferences, NULL, NULL, NULL, {0} },
        { "privacy-audit", action_privacy_audit, NULL, NULL, NULL, {0} },
        { "clear-site-data", action_clear_site_data, NULL, NULL, NULL, {0} },
        { "forget-site", action_forget_site, NULL, NULL, NULL, {0} },
        { "clear-data", action_clear_data, NULL, NULL, NULL, {0} },
        { "about", action_about, NULL, NULL, NULL, {0} },
        { "exit", action_exit, NULL, NULL, NULL, {0} },
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app->window),
                                    actions, G_N_ELEMENTS(actions), app);

    const gchar *new_tab_accels[] = { "<Primary>t", NULL };
    const gchar *private_window_accels[] = { "<Primary><Shift>p", NULL };
    const gchar *new_identity_accels[] = { "<Primary><Shift>u", NULL };
    const gchar *close_tab_accels[] = { "<Primary>w", NULL };
    const gchar *reopen_closed_tab_accels[] = { "<Primary><Shift>t", NULL };
    const gchar *focus_accels[] = { "<Primary>l", "F6", NULL };
    const gchar *reload_accels[] = { "<Primary>r", "F5", NULL };
    const gchar *hard_reload_accels[] = { "<Primary><Shift>r", NULL };
    const gchar *find_accels[] = { "<Primary>f", NULL };
    const gchar *zoom_in_accels[] = { "<Primary>plus", "<Primary>equal", "<Primary>KP_Add", NULL };
    const gchar *zoom_out_accels[] = { "<Primary>minus", "<Primary>KP_Subtract", NULL };
    const gchar *zoom_reset_accels[] = { "<Primary>0", "<Primary>KP_0", NULL };
    const gchar *print_accels[] = { "<Primary>p", NULL };
    const gchar *fullscreen_accels[] = { "F11", NULL };
    const gchar *back_accels[] = { "<Alt>Left", NULL };
    const gchar *forward_accels[] = { "<Alt>Right", NULL };
    const gchar *next_tab_accels[] = { "<Primary>Tab", "<Primary>Page_Down", NULL };
    const gchar *previous_tab_accels[] = { "<Primary><Shift>Tab", "<Primary>Page_Up", NULL };
    const gchar *downloads_accels[] = { "<Primary>j", NULL };
    const gchar *bookmark_accels[] = { "<Primary>d", NULL };

    gtk_application_set_accels_for_action(app->application, "win.new-tab", new_tab_accels);
    gtk_application_set_accels_for_action(app->application, "win.private-window", private_window_accels);
    gtk_application_set_accels_for_action(app->application, "win.new-identity", new_identity_accels);
    gtk_application_set_accels_for_action(app->application, "win.close-tab", close_tab_accels);
    gtk_application_set_accels_for_action(app->application, "win.reopen-closed-tab", reopen_closed_tab_accels);
    gtk_application_set_accels_for_action(app->application, "win.focus-location", focus_accels);
    gtk_application_set_accels_for_action(app->application, "win.reload", reload_accels);
    gtk_application_set_accels_for_action(app->application, "win.hard-reload", hard_reload_accels);
    gtk_application_set_accels_for_action(app->application, "win.find", find_accels);
    gtk_application_set_accels_for_action(app->application, "win.zoom-in", zoom_in_accels);
    gtk_application_set_accels_for_action(app->application, "win.zoom-out", zoom_out_accels);
    gtk_application_set_accels_for_action(app->application, "win.zoom-reset", zoom_reset_accels);
    gtk_application_set_accels_for_action(app->application, "win.print", print_accels);
    gtk_application_set_accels_for_action(app->application, "win.fullscreen", fullscreen_accels);
    gtk_application_set_accels_for_action(app->application, "win.back", back_accels);
    gtk_application_set_accels_for_action(app->application, "win.forward", forward_accels);
    gtk_application_set_accels_for_action(app->application, "win.next-tab", next_tab_accels);
    gtk_application_set_accels_for_action(app->application, "win.previous-tab", previous_tab_accels);
    gtk_application_set_accels_for_action(app->application, "win.downloads", downloads_accels);
    gtk_application_set_accels_for_action(app->application, "win.bookmark-page", bookmark_accels);
}

static void on_activate(GtkApplication *application, gpointer user_data)
{
    NionApp *app = user_data;
    app->application = application;

    if (app->window) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }

    /* The UI layer owns status/chrome; register its implementations so app.c
     * can drive fail-closed Tor state and private-window lifecycle without
     * calling into the UI layer directly. */
    static const NionAppLifecycleCallbacks app_callbacks = {
        .set_status = nion_set_status,
        .update_controls = nion_update_controls,
        .refresh_home_pages = nion_refresh_home_pages,
        .stop_all_web_activity = nion_stop_all_web_activity,
        .build_ui = nion_build_ui,
    };
    nion_app_set_callbacks(&app_callbacks);

    /* ui.c owns the chrome; register main.c's tab-creation hub / current-tab /
     * status / action-table implementations so ui.c never calls in here. */
    static const NionUiCallbacks ui_callbacks = {
        .new_tab = nion_new_tab,
        .current_tab = nion_current_tab,
        .set_status = nion_set_status,
        .install_actions = nion_install_actions,
    };
    nion_ui_set_callbacks(&ui_callbacks);

    /* The UI owns status/Tor-state reporting; register its implementations
     * so tor-core.c can notify the UI without calling into main.c directly. */
    static const NionTorCallbacks tor_callbacks = {
        .set_tor_ready = nion_set_tor_ready,
        .set_tor_progress = nion_set_tor_progress,
        .set_tor_error = nion_set_tor_error,
        .set_status = nion_set_status,
    };
    nion_tor_set_callbacks(&tor_callbacks);

    /* Session restoration calls tab/UI operations that still live in main.c. */
    static const NionSessionCallbacks session_callbacks = {
        .new_tab = nion_new_tab,
        .set_tab_pinned = nion_set_tab_pinned,
        .load_home = nion_load_home,
        .load_uri = nion_load_uri,
        .prepare_normal_navigation = nion_prepare_normal_navigation,
        .update_controls = nion_update_controls,
        .set_status = nion_set_status,
        .closed_tab_free = nion_closed_tab_free,
    };
    nion_session_set_callbacks(&session_callbacks);

    /* Tab model calls tab-creation / status / navigation operations that still
     * live in main.c (the webview-creation hub stays here until Phase 5b). */
    static const NionTabCallbacks tab_callbacks = {
        .new_tab = nion_new_tab,
        .set_status = nion_set_status,
        .update_controls = nion_update_controls,
        .clear_retry = nion_clear_retry,
        .load_home = nion_load_home,
        .load_uri = nion_load_uri,
        .reload_crashed_tab = nion_reload_crashed_tab,
    };
    nion_tabs_set_callbacks(&tab_callbacks);

    /* WebView signal handlers (in webview.c) call UI/navigation operations
     * that still live in main.c; register them here. */
    static const NionWebviewCallbacks webview_callbacks = {
        .new_tab_internal = nion_new_tab_internal,
        .current_tab = nion_current_tab,
        .set_status = nion_set_status,
        .update_controls = nion_update_controls,
        .update_window_title = nion_update_window_title,
        .update_progress = nion_update_progress,
        .update_site_info = nion_update_site_info,
        .update_bookmark_button = nion_update_bookmark_button,
        .clear_retry = nion_clear_retry,
        .reload_crashed_tab = nion_reload_crashed_tab,
        .tab_fallback_title = nion_tab_fallback_title,
        .tab_has_mixed_content = nion_tab_has_mixed_content,
        .show_error_page = nion_show_error_page,
        .detect_onion_location = nion_detect_onion_location,
        .set_onion_location = nion_set_onion_location,
        .http_origin_key = nion_http_origin_key,
        .close_http_warning = nion_close_http_warning,
        .show_http_warning = nion_show_http_warning,
        .show_external_protocol_prompt = nion_show_external_protocol_prompt,
        .request_new_identity = nion_activate_new_identity,
    };
    nion_webview_set_callbacks(&webview_callbacks);

    nion_apply_css();
    nion_prepare_appimage_webkit_sandbox();
    nion_prepare_dirs(app);
    nion_validate_cookie_store(app);
    nion_cleanup_stale_tor(app);
    nion_load_preferences(app);
    nion_load_bookmarks(app);
    nion_load_site_zoom(app);
    nion_load_site_javascript(app);
    nion_load_content_blocking(app);
    nion_load_autoplay(app);
    nion_load_preferred_onion(app);
    nion_prepare_content_filter(app);
    gboolean tor_port_ok = nion_choose_tor_port(app);
    if (!nion_prepare_network(app)) {
        g_critical("NiOn could not create its WebKit network session");
        return;
    }
    /* Downloads stay in main.c for now; the network module no longer connects
     * this signal (it would create a network -> main.c coupling). */
    g_signal_connect(app->network_session, "download-started",
                     G_CALLBACK(on_download_started), app);
    nion_build_ui(app);
    if (tor_port_ok)
        nion_start_tor(app);
    else
        nion_set_tor_error(app, "No free Tor SOCKS port was found in NiOn runtime range");
}

int main(int argc, char **argv)
{
    NionApp app = {0};
    app.private_windows = g_ptr_array_new();
    GtkApplication *application = gtk_application_new(NION_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_application_set_resource_base_path(G_APPLICATION(application), "/io/github/jeannesbryan/Nion");

    g_signal_connect(application, "activate", G_CALLBACK(on_activate), &app);
    g_signal_connect(application, "shutdown", G_CALLBACK(on_shutdown), &app);

    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
