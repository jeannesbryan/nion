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

#define NION_APP_ID "io.github.jeannesbryan.Nion"
#define NION_TOR_HOST "127.0.0.1"
#define NION_TOR_PORT_FIRST 19050
#define NION_TOR_PORT_LAST 19069
#define NION_REPOSITORY_URL "https://github.com/jeannesbryan/nion"
#define NION_TOR_STARTUP_TIMEOUT_SECONDS 120
#define NION_TOR_GRACEFUL_SHUTDOWN_MS 3000
#define NION_ONION_RETRY_DELAY_MS 350
#define NION_SESSION_SAVE_DELAY_MS 750
#define NION_SESSION_FORMAT 1
#define NION_MAX_SESSION_TABS 256
#define NION_MAX_SESSION_TABS_INPUT 1024
#define NION_MAX_SESSION_FILE_BYTES (16 * 1024 * 1024)
#define NION_MAX_TAB_STATE_BASE64_BYTES (8 * 1024 * 1024)
#define NION_MAX_SAVED_URI_BYTES (16 * 1024)
#define NION_MAX_PREFERENCES_FILE_BYTES (1024 * 1024)
#define NION_MAX_DOWNLOADS_FILE_BYTES (4 * 1024 * 1024)
#define NION_MAX_DOWNLOAD_HISTORY 500
#define NION_MAX_DOWNLOAD_HISTORY_INPUT 5000
#define NION_MAX_BOOKMARKS_FILE_BYTES (2 * 1024 * 1024)
#define NION_MAX_BOOKMARKS 1000
#define NION_MAX_BOOKMARKS_INPUT 5000
#define NION_MAX_BOOKMARK_TITLE_CHARS 240
#define NION_MAX_CLOSED_TABS 10
#define NION_MAX_SITE_ZOOM_FILE_BYTES (1024 * 1024)
#define NION_MAX_SITE_ZOOM_ENTRIES 2048
#define NION_SITE_ZOOM_FORMAT 1
#define NION_ZOOM_MIN_PERCENT 50
#define NION_ZOOM_MAX_PERCENT 200
#define NION_ZOOM_DEFAULT_PERCENT 100

typedef struct _NionApp NionApp;
typedef struct _NionTab NionTab;
typedef struct _NionDownload NionDownload;
typedef struct _NionBookmark NionBookmark;
typedef struct _NionClearSiteRequest NionClearSiteRequest;
typedef struct _NionClosedTab NionClosedTab;

struct _NionTab {
    NionApp *app;
    GtkWidget *page;
    WebKitWebView *web_view;
    GtkWidget *title_label;
    GtkWidget *favicon_picture;
    GtkWidget *audio_button;
    GtkWidget *tab_label_box;
    GtkWidget *tab_menu_popover;
    GtkWidget *tab_menu_mute_button;
    GtkWidget *tab_menu_close_others_button;
    GtkWidget *tab_menu_close_right_button;

    gboolean home_page;
    gboolean error_page;
    gboolean load_failed;
    gboolean connection_committed;
    gboolean mixed_content_displayed;
    gboolean mixed_content_run;
    gboolean mixed_content_other;

    guint onion_cancel_retries;
    guint retry_source_id;
    gchar *retry_uri;
    gchar *display_uri_override;
    gchar *onion_location;

    GtkWidget *http_warning_window;
    gchar *http_warning_uri;
    WebKitPolicyDecision *http_warning_decision;
    gchar *http_allowed_origin;

    gboolean restore_pending;
    gchar *restore_uri;
};

struct _NionBookmark {
    gchar *title;
    gchar *uri;
};

struct _NionClosedTab {
    gchar *uri;
    gboolean muted;
};

struct _NionClearSiteRequest {
    NionApp *app;
    gchar *host;
    gchar *uri;
    WebKitWebView *web_view;
    GList *website_data;
};

struct _NionApp {
    GtkApplication *application;
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *address;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *reload_button;
    GtkWidget *home_button;
    GtkWidget *onion_button;
    GtkWidget *site_info_button;
    GtkWidget *site_info_popover;
    GtkWidget *site_info_title_label;
    GtkWidget *site_info_host_label;
    GtkWidget *site_info_connection_label;
    GtkWidget *site_info_route_label;
    GtkWidget *site_info_mixed_label;
    GtkWidget *site_info_uri_label;
    GtkWidget *bookmark_button;
    GtkWidget *new_tab_button;
    GtkWidget *progress_bar;
    GtkWidget *status_label;
    GtkWidget *downloads_panel;
    GtkWidget *downloads_list;
    GtkWidget *downloads_window;
    GtkWidget *downloads_empty_label;
    GtkWidget *bookmarks_window;
    GtkWidget *bookmarks_list;
    GtkWidget *bookmarks_empty_label;
    GtkWidget *bookmarks_search_entry;
    GtkWidget *bookmarks_result_label;
    GtkWidget *menu_button;
    GtkWidget *preferences_window;
    GtkWidget *toolbar;
    GtkWidget *find_bar;
    GtkWidget *find_entry;
    GtkWidget *find_match_label;
    GtkWidget *find_prev_button;
    GtkWidget *find_next_button;
    GtkWidget *find_close_button;

    WebKitNetworkSession *network_session;

    GSubprocess *tor_process;
    GDataInputStream *tor_output;
    guint16 tor_socks_port;
    gchar *tor_proxy_uri;
    gchar *tor_binary_path;
    gchar *tor_runtime_file;
    guint tor_startup_timeout_id;
    gboolean tor_recovery_attempted;
    guint tor_port_retry_count;
    gboolean tor_saw_port_conflict;
    gboolean tor_saw_corruption;
    gboolean tor_ready;
    gboolean tor_failed;
    gint tor_bootstrap_percent;
    gboolean shutting_down;
    gboolean close_confirm_open;
    gboolean close_confirmed;
    gchar *tor_last_log;

    gboolean restore_session;
    gboolean block_third_party_cookies;
    gchar *search_engine;
    gboolean previous_shutdown_clean;
    gboolean restored_previous_session;
    guint session_save_source_id;

    gchar *data_dir;
    gchar *cache_dir;
    gchar *tor_dir;
    gchar *cookie_file;
    gchar *download_dir;
    gchar *config_dir;
    gchar *preferences_file;
    gchar *session_file;
    gchar *downloads_file;
    gchar *bookmarks_file;
    gchar *site_zoom_file;
    GPtrArray *bookmarks;
    GHashTable *site_zoom;
    GQueue *closed_tabs;
};

struct _NionDownload {
    NionApp *app;
    WebKitDownload *download;
    GtkWidget *row;
    GtkWidget *name_label;
    GtkWidget *detail_label;
    GtkWidget *progress_bar;
    GtkWidget *action_button;
    GtkWidget *more_button;
    GtkWidget *open_button;
    GtkWidget *folder_button;
    GtkWidget *copy_link_button;
    GtkWidget *retry_button;

    gchar *destination;
    gchar *filename;
    gchar *source_uri;
    gboolean failed;
    gboolean finished;
    gboolean cancel_requested;
    gchar *history_id;
    gchar *history_status;
    gchar *history_detail;
    gint64 history_time;
};

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select);
static void nion_update_controls(NionApp *app);
static void nion_load_home(NionTab *tab);
static void nion_refresh_home_pages(NionApp *app);
static void on_download_started(WebKitNetworkSession *session, WebKitDownload *download, gpointer user_data);
static void nion_cancel_active_downloads(NionApp *app);
static void nion_schedule_session_save(NionApp *app);
static void nion_save_session(NionApp *app, gboolean clean_shutdown);
static void nion_apply_cookie_policy(NionApp *app);
static gboolean nion_restore_saved_session(NionApp *app);
static void nion_start_pending_restores(NionApp *app);
static void nion_prepare_normal_navigation(NionTab *tab);
static void nion_load_uri(NionTab *tab, const gchar *uri);
static void nion_stop_all_web_activity(NionApp *app);
static void nion_apply_privacy_settings(WebKitSettings *settings);
static gboolean nion_start_tor(NionApp *app);
static void nion_prepare_network(NionApp *app);
static void nion_apply_network_proxy(NionApp *app);
static void nion_store_tor_log(NionApp *app, const gchar *line);
static void nion_update_onion_button(NionApp *app);
static void nion_update_site_info(NionApp *app);
static void nion_detect_onion_location(NionTab *tab);
static void nion_show_downloads(NionApp *app);
static void nion_save_download_history(NionApp *app);
static void nion_load_download_history(NionApp *app);
static void nion_load_bookmarks(NionApp *app);
static void nion_save_bookmarks(NionApp *app);
static void nion_show_bookmarks(NionApp *app);
static void nion_refresh_bookmarks_window(NionApp *app);
static void nion_add_current_bookmark(NionApp *app);
static void nion_update_bookmark_button(NionApp *app);
static void nion_toggle_current_bookmark(NionApp *app);
static void nion_update_tab_audio_button(NionTab *tab);
static void nion_reopen_closed_tab(NionApp *app);
static void nion_update_tab_context_menu(NionTab *tab);
static void nion_tab_context_popdown(NionTab *tab);
static void nion_print_tab(NionTab *tab);
static gboolean on_webview_context_menu(WebKitWebView *web_view,
                                        WebKitContextMenu *context_menu,
                                        GdkEvent *event,
                                        WebKitHitTestResult *hit_test_result,
                                        gpointer user_data);
static void nion_request_close(NionApp *app);
static void nion_find_open(NionApp *app);
static void nion_find_close(NionApp *app);
static void nion_find_run(NionApp *app);
static void on_find_found(WebKitFindController *controller, guint match_count, gpointer user_data);
static void on_find_failed(WebKitFindController *controller, gpointer user_data);
static void nion_set_zoom(NionApp *app, gdouble zoom);
static void nion_load_site_zoom(NionApp *app);
static void nion_save_site_zoom(NionApp *app);
static void nion_apply_site_zoom(NionTab *tab, const gchar *uri);
static const gchar *nion_search_template(const NionApp *app);
static gboolean nion_validate_uri(const gchar *uri, gchar **message);
static gboolean nion_uri_is_http_clearnet(const gchar *uri);
static gchar *nion_http_origin_key(const gchar *uri);
static void nion_show_http_warning(NionTab *tab, const gchar *uri);

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

static gboolean nion_profile_file_within_limit(const gchar *path, goffset max_bytes)
{
    if (!path || max_bytes <= 0 || !g_file_test(path, G_FILE_TEST_EXISTS))
        return TRUE;

    GStatBuf st = {0};
    if (g_stat(path, &st) != 0)
        return FALSE;

    return S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= max_bytes;
}

static void nion_quarantine_profile_file(const gchar *path, const gchar *label)
{
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
        return;

    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = now ? g_date_time_format(now, "%Y%m%d-%H%M%S") : g_strdup("unknown-time");
    gint64 nonce = g_get_real_time();
    gchar *target = g_strdup_printf("%s.corrupt-%s-%" G_GINT64_FORMAT, path, stamp, nonce);

    if (g_rename(path, target) == 0) {
        g_chmod(target, 0600);
        g_warning("Quarantined invalid NiOn %s file as %s",
                  label ? label : "profile", target);
    } else {
        g_warning("Could not quarantine invalid NiOn %s file %s",
                  label ? label : "profile", path);
    }

    g_free(target);
    g_free(stamp);
    if (now)
        g_date_time_unref(now);
}

static gboolean nion_base64_state_looks_valid(const gchar *base64)
{
    if (!base64 || !*base64)
        return FALSE;

    gsize len = strlen(base64);
    if (len > NION_MAX_TAB_STATE_BASE64_BYTES || (len % 4) != 0)
        return FALSE;

    gboolean padding_seen = FALSE;
    guint padding = 0;
    for (gsize i = 0; i < len; i++) {
        const guchar c = (guchar)base64[i];
        if (c == '=') {
            padding_seen = TRUE;
            padding++;
            if (padding > 2 || i + 2 < len)
                return FALSE;
            continue;
        }
        if (padding_seen)
            return FALSE;
        if (!(g_ascii_isalnum(c) || c == '+' || c == '/'))
            return FALSE;
    }

    return TRUE;
}

static gboolean nion_write_key_file_atomic(GKeyFile *key_file, const gchar *path)
{
    if (!key_file || !path)
        return FALSE;

    gsize length = 0;
    GError *error = NULL;
    gchar *contents = g_key_file_to_data(key_file, &length, &error);
    if (!contents) {
        g_warning("Could not serialize NiOn state: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        return FALSE;
    }

    gchar *tmp = g_strdup_printf("%s.tmp", path);
    gboolean ok = g_file_set_contents(tmp, contents, (gssize)length, &error);
    g_free(contents);

    if (!ok) {
        g_warning("Could not write %s: %s", tmp, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_unlink(tmp);
        g_free(tmp);
        return FALSE;
    }

    g_chmod(tmp, 0600);
    if (g_rename(tmp, path) != 0) {
        g_warning("Could not replace %s", path);
        g_unlink(tmp);
        g_free(tmp);
        return FALSE;
    }

    g_free(tmp);
    return TRUE;
}

static void nion_load_preferences(NionApp *app)
{
    app->restore_session = TRUE;
    app->block_third_party_cookies = FALSE;
    g_clear_pointer(&app->search_engine, g_free);
    app->search_engine = g_strdup("duckduckgo");

    if (g_file_test(app->preferences_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->preferences_file,
                                        NION_MAX_PREFERENCES_FILE_BYTES)) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->preferences_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn preferences: %s", error->message);
            nion_quarantine_profile_file(app->preferences_file, "preferences");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    gboolean valid = TRUE;
    if (g_key_file_has_key(key_file, "General", "restore-session", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "General", "restore-session", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->restore_session = value;
        }
    }

    if (g_key_file_has_key(key_file, "Privacy", "block-third-party-cookies", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "Privacy", "block-third-party-cookies", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->block_third_party_cookies = value;
        }
    }

    if (g_key_file_has_key(key_file, "Search", "engine", NULL)) {
        error = NULL;
        gchar *engine = g_key_file_get_string(key_file, "Search", "engine", &error);
        if (error || !engine || !(g_str_equal(engine, "duckduckgo") ||
                                  g_str_equal(engine, "brave") ||
                                  g_str_equal(engine, "startpage"))) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(engine);
        } else {
            g_free(app->search_engine);
            app->search_engine = engine;
        }
    }

    g_key_file_free(key_file);

    if (!valid) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        app->restore_session = TRUE;
        app->block_third_party_cookies = FALSE;
        g_clear_pointer(&app->search_engine, g_free);
        app->search_engine = g_strdup("duckduckgo");
    }
}

static void nion_save_preferences(NionApp *app)
{
    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_boolean(key_file, "General", "restore-session", app->restore_session);
    g_key_file_set_boolean(key_file, "Privacy", "block-third-party-cookies", app->block_third_party_cookies);
    g_key_file_set_string(key_file, "Search", "engine",
                          app->search_engine ? app->search_engine : "duckduckgo");
    nion_write_key_file_atomic(key_file, app->preferences_file);
    g_key_file_free(key_file);
}

static gint nion_zoom_percent(gdouble zoom)
{
    gint percent = (gint)(zoom * 100.0 + 0.5);
    return CLAMP(percent, NION_ZOOM_MIN_PERCENT, NION_ZOOM_MAX_PERCENT);
}

static gchar *nion_site_zoom_key_for_uri(const gchar *uri)
{
    if (!uri || !*uri)
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean is_http = scheme && g_ascii_strcasecmp(scheme, "http") == 0;
    gboolean is_https = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    if ((!is_http && !is_https) || !host || !*host) {
        g_uri_unref(parsed);
        return NULL;
    }

    gchar *lower_host = g_ascii_strdown(host, -1);
    gint port = g_uri_get_port(parsed);
    gint default_port = is_https ? 443 : 80;
    gchar *key = NULL;

    /* Zoom is remembered by site rather than scheme: http://example.com and
     * https://example.com intentionally share a value. Explicit non-default
     * ports stay separate because they can represent a different web app. */
    if (port < 0 || port == default_port) {
        key = g_strdup(lower_host);
    } else if (strchr(lower_host, ':')) {
        key = g_strdup_printf("[%s]:%d", lower_host, port);
    } else {
        key = g_strdup_printf("%s:%d", lower_host, port);
    }

    g_free(lower_host);
    g_uri_unref(parsed);
    return key;
}

static void nion_save_site_zoom(NionApp *app)
{
    if (!app || !app->site_zoom_file || !app->site_zoom)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_SITE_ZOOM_FORMAT);

    GList *keys = g_hash_table_get_keys(app->site_zoom);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    guint index = 0;
    for (GList *node = keys; node && index < NION_MAX_SITE_ZOOM_ENTRIES; node = node->next) {
        const gchar *key = node->data;
        gint percent = GPOINTER_TO_INT(g_hash_table_lookup(app->site_zoom, key));
        if (!key || !*key || percent < NION_ZOOM_MIN_PERCENT ||
            percent > NION_ZOOM_MAX_PERCENT || percent == NION_ZOOM_DEFAULT_PERCENT)
            continue;

        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_integer(key_file, group, "percent", percent);
        g_free(group);
    }
    g_list_free(keys);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);

    nion_write_key_file_atomic(key_file, app->site_zoom_file);
    g_key_file_free(key_file);
}

static void nion_load_site_zoom(NionApp *app)
{
    if (!app)
        return;

    if (app->site_zoom)
        g_hash_table_unref(app->site_zoom);
    app->site_zoom = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (!app->site_zoom_file)
        return;
    if (!g_file_test(app->site_zoom_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->site_zoom_file,
                                        NION_MAX_SITE_ZOOM_FILE_BYTES)) {
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->site_zoom_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn site zoom: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
        return;
    }

    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_SITE_ZOOM_FORMAT) {
        valid = FALSE;
        g_clear_error(&error);
    }

    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_SITE_ZOOM_ENTRIES) {
            valid = FALSE;
            g_clear_error(&error);
        }
    }

    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *key = g_key_file_get_string(key_file, group, "key", &error);
        if (error || !key || !*key || strlen(key) > 1024) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        gint percent = g_key_file_get_integer(key_file, group, "percent", &error);
        if (error || percent < NION_ZOOM_MIN_PERCENT ||
            percent > NION_ZOOM_MAX_PERCENT ||
            g_hash_table_contains(app->site_zoom, key)) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        if (percent != NION_ZOOM_DEFAULT_PERCENT)
            g_hash_table_insert(app->site_zoom, key, GINT_TO_POINTER(percent));
        else
            g_free(key);
        g_free(group);
    }

    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->site_zoom);
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
    } else {
        g_chmod(app->site_zoom_file, 0600);
    }
}

static gboolean nion_remember_site_zoom(NionApp *app, const gchar *key, gint percent)
{
    if (!app || !app->site_zoom || !key || !*key)
        return FALSE;

    percent = CLAMP(percent, NION_ZOOM_MIN_PERCENT, NION_ZOOM_MAX_PERCENT);
    if (percent == NION_ZOOM_DEFAULT_PERCENT)
        return g_hash_table_remove(app->site_zoom, key);

    gpointer current = g_hash_table_lookup(app->site_zoom, key);
    if (current && GPOINTER_TO_INT(current) == percent)
        return FALSE;

    if (!current && g_hash_table_size(app->site_zoom) >= NION_MAX_SITE_ZOOM_ENTRIES) {
        g_warning("NiOn site zoom limit reached; not persisting zoom for %s", key);
        return FALSE;
    }

    g_hash_table_replace(app->site_zoom, g_strdup(key), GINT_TO_POINTER(percent));
    return TRUE;
}

static void nion_apply_site_zoom(NionTab *tab, const gchar *uri)
{
    if (!tab || !tab->web_view)
        return;

    gint percent = NION_ZOOM_DEFAULT_PERCENT;
    if (!tab->home_page && !tab->error_page) {
        gchar *key = nion_site_zoom_key_for_uri(uri);
        if (key && tab->app && tab->app->site_zoom) {
            gpointer stored = g_hash_table_lookup(tab->app->site_zoom, key);
            if (stored)
                percent = GPOINTER_TO_INT(stored);
        }
        g_free(key);
    }

    webkit_web_view_set_zoom_level(tab->web_view, (gdouble)percent / 100.0);
}

static void nion_apply_cookie_policy(NionApp *app)
{
    if (!app->network_session)
        return;

    WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(app->network_session);
    webkit_cookie_manager_set_accept_policy(
        cookies,
        app->block_third_party_cookies
            ? WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY
            : WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
}

static void nion_save_session(NionApp *app, gboolean clean_shutdown)
{
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

        const gchar *uri = tab->display_uri_override;
        if (!uri || !*uri)
            uri = webkit_web_view_get_uri(tab->web_view);
        if (uri && *uri && !g_str_equal(uri, "about:blank") &&
            strlen(uri) <= NION_MAX_SAVED_URI_BYTES)
            g_key_file_set_string(session, group, "uri", uri);

        if (!tab->home_page) {
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

static void nion_schedule_session_save(NionApp *app)
{
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

static gboolean nion_restore_saved_session(NionApp *app)
{
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
    gint active_tab = g_key_file_get_integer(session, "Session", "active-tab", NULL);
    gint restored = 0;

    for (gint i = 0; i < tab_count; i++) {
        gchar *group = g_strdup_printf("Tab-%d", i);
        gboolean home = g_key_file_get_boolean(session, group, "home", NULL);
        gboolean muted = g_key_file_has_key(session, group, "muted", NULL)
            ? g_key_file_get_boolean(session, group, "muted", NULL)
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

        gboolean state_valid = state && *state && nion_base64_state_looks_valid(state);
        if (state && *state && !state_valid)
            g_warning("Ignoring malformed WebKit session state in %s", group);

        if (home || ((!uri || !*uri) && !state_valid)) {
            nion_new_tab(app, NULL, FALSE);
            restored++;
        } else {
            NionTab *tab = nion_new_tab(app, "", FALSE);
            if (tab) {
                tab->home_page = FALSE;
                tab->error_page = FALSE;
                tab->restore_pending = TRUE;
                tab->restore_uri = g_strdup(uri);
                webkit_web_view_set_is_muted(tab->web_view, muted);
                if (state_valid)
                    nion_restore_tab_state(tab, state);
                restored++;
            }
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

static void nion_start_pending_restores(NionApp *app)
{
    if (!app->tor_ready || !app->notebook)
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

static gboolean nion_looks_like_host_port(const gchar *text)
{
    if (!text || !*text || strpbrk(text, " \t\r\n"))
        return FALSE;

    const gchar *colon = strchr(text, ':');
    if (!colon || colon == text)
        return FALSE;

    /* Keep common host:port input such as localhost:8080 and
     * example.com:8443 from being mistaken for a URI scheme. */
    gchar *host = g_strndup(text, (gsize)(colon - text));
    gboolean hostish = g_ascii_strcasecmp(host, "localhost") == 0 || strchr(host, '.') != NULL;
    g_free(host);
    if (!hostish)
        return FALSE;

    const gchar *p = colon + 1;
    if (!g_ascii_isdigit(*p))
        return FALSE;
    while (g_ascii_isdigit(*p))
        p++;

    return *p == '\0' || *p == '/' || *p == '?' || *p == '#';
}

static gboolean nion_string_has_scheme(const gchar *text)
{
    if (!text || !g_ascii_isalpha(text[0]) || nion_looks_like_host_port(text))
        return FALSE;

    for (const gchar *p = text + 1; *p; p++) {
        if (*p == ':')
            return TRUE;
        if (!(g_ascii_isalnum(*p) || *p == '+' || *p == '-' || *p == '.'))
            return FALSE;
    }

    return FALSE;
}


static gboolean nion_ipv4_bytes_are_private(const guint8 *bytes)
{
    if (!bytes)
        return FALSE;

    /* Unspecified/current-network, RFC1918, loopback, carrier-grade NAT,
     * link-local, benchmarking, multicast and reserved space. NiOn is a
     * Tor-only web browser, not a local-network browser. */
    if (bytes[0] == 0 ||
        bytes[0] == 10 ||
        bytes[0] == 127 ||
        (bytes[0] == 100 && bytes[1] >= 64 && bytes[1] <= 127) ||
        (bytes[0] == 169 && bytes[1] == 254) ||
        (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
        (bytes[0] == 192 && bytes[1] == 168) ||
        (bytes[0] == 198 && (bytes[1] == 18 || bytes[1] == 19)) ||
        bytes[0] >= 224)
        return TRUE;

    return FALSE;
}

static gboolean nion_ipv6_bytes_are_private(const guint8 *bytes)
{
    if (!bytes)
        return FALSE;

    gboolean all_zero = TRUE;
    for (guint i = 0; i < 16; i++) {
        if (bytes[i] != 0) {
            all_zero = FALSE;
            break;
        }
    }
    if (all_zero)
        return TRUE;

    /* ::1 */
    gboolean loopback = TRUE;
    for (guint i = 0; i < 15; i++) {
        if (bytes[i] != 0) {
            loopback = FALSE;
            break;
        }
    }
    if (loopback && bytes[15] == 1)
        return TRUE;

    /* fc00::/7 (ULA), fe80::/10 (link-local), ff00::/8 (multicast). */
    if ((bytes[0] & 0xfe) == 0xfc ||
        (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
        bytes[0] == 0xff)
        return TRUE;

    /* IPv4-mapped IPv6 ::ffff:a.b.c.d */
    gboolean mapped = TRUE;
    for (guint i = 0; i < 10; i++) {
        if (bytes[i] != 0) {
            mapped = FALSE;
            break;
        }
    }
    if (mapped && bytes[10] == 0xff && bytes[11] == 0xff)
        return nion_ipv4_bytes_are_private(bytes + 12);

    return FALSE;
}

static gboolean nion_host_is_local_or_private(const gchar *host)
{
    if (!host || !*host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    gboolean local_name =
        g_str_equal(lower, "localhost") ||
        g_str_has_suffix(lower, ".localhost") ||
        g_str_has_suffix(lower, ".local") ||
        g_str_has_suffix(lower, ".lan") ||
        g_str_equal(lower, "home.arpa") ||
        g_str_has_suffix(lower, ".home.arpa");

    if (local_name) {
        g_free(lower);
        return TRUE;
    }

    GInetAddress *address = g_inet_address_new_from_string(lower);
    g_free(lower);
    if (!address)
        return FALSE;

    const guint8 *bytes = g_inet_address_to_bytes(address);
    gboolean blocked = g_inet_address_get_family(address) == G_SOCKET_FAMILY_IPV4
        ? nion_ipv4_bytes_are_private(bytes)
        : nion_ipv6_bytes_are_private(bytes);

    g_object_unref(address);
    return blocked;
}

static gboolean nion_host_is_onion(const gchar *host)
{
    if (!host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    gboolean result = g_str_has_suffix(lower, ".onion");
    g_free(lower);
    return result;
}

static gboolean nion_is_valid_v3_onion_host(const gchar *host)
{
    if (!host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    if (!g_str_has_suffix(lower, ".onion")) {
        g_free(lower);
        return FALSE;
    }

    gsize host_len = strlen(lower);
    if (host_len <= 6) {
        g_free(lower);
        return FALSE;
    }

    gchar *suffix = lower + host_len - 6; /* points to .onion */
    gchar *label_start = suffix;
    while (label_start > lower && *(label_start - 1) != '.')
        label_start--;

    gsize label_len = (gsize)(suffix - label_start);
    if (label_len != 56) {
        g_free(lower);
        return FALSE;
    }

    for (gsize i = 0; i < label_len; i++) {
        gchar c = label_start[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) {
            g_free(lower);
            return FALSE;
        }
    }

    g_free(lower);
    return TRUE;
}

static gboolean nion_uri_is_onion(const gchar *uri)
{
    if (!uri)
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    gboolean result = nion_host_is_onion(g_uri_get_host(parsed));
    g_uri_unref(parsed);
    return result;
}

static gboolean nion_validate_uri(const gchar *uri, gchar **message);


static gboolean nion_uri_is_http_clearnet(const gchar *uri)
{
    if (!uri || !*uri || nion_uri_is_onion(uri))
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean ok = scheme && host && *host && g_ascii_strcasecmp(scheme, "http") == 0;
    g_uri_unref(parsed);
    return ok;
}

static gchar *nion_http_origin_key(const gchar *uri)
{
    if (!nion_uri_is_http_clearnet(uri))
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *host = g_uri_get_host(parsed);
    gint port = g_uri_get_port(parsed);
    gchar *lower_host = host ? g_ascii_strdown(host, -1) : NULL;
    gchar *key = NULL;
    if (lower_host && *lower_host)
        key = g_strdup_printf("%s:%d", lower_host, port);

    g_free(lower_host);
    g_uri_unref(parsed);
    return key;
}

static gboolean nion_uri_is_https_clearnet(const gchar *uri)
{
    if (!uri || !*uri || nion_uri_is_onion(uri))
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    gboolean ok = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    g_uri_unref(parsed);
    return ok;
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

static gboolean nion_validate_uri(const gchar *uri, gchar **message)
{
    if (message)
        *message = NULL;

    if (!uri || !*uri) {
        if (message)
            *message = g_strdup("The address is empty.");
        return FALSE;
    }

    if (g_str_equal(uri, "about:blank"))
        return TRUE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        if (message)
            *message = g_strdup_printf("Invalid address: %s",
                                       error ? error->message : "could not parse URI");
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);

    if (!scheme || !(g_ascii_strcasecmp(scheme, "http") == 0 ||
                     g_ascii_strcasecmp(scheme, "https") == 0)) {
        if (message)
            *message = g_strdup("NiOn only opens http:// and https:// web addresses.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (!host || !*host) {
        if (message)
            *message = g_strdup("The web address does not contain a valid hostname.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (nion_host_is_local_or_private(host)) {
        if (message)
            *message = g_strdup(
                "Local, private, link-local, multicast, and reserved network addresses are blocked "
                "by NiOn's Tor-only privacy policy.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (nion_host_is_onion(host) && !nion_is_valid_v3_onion_host(host)) {
        if (message)
            *message = g_strdup(
                "Invalid .onion address. NiOn accepts Tor v3 onion addresses "
                "with a 56-character base32 service label before .onion.");
        g_uri_unref(parsed);
        return FALSE;
    }

    g_uri_unref(parsed);
    return TRUE;
}

static const gchar *nion_search_template(const NionApp *app)
{
    const gchar *engine = (app && app->search_engine) ? app->search_engine : "duckduckgo";
    if (g_str_equal(engine, "brave"))
        return "https://search.brave.com/search?q=%s";
    if (g_str_equal(engine, "startpage"))
        return "https://www.startpage.com/sp/search?query=%s";
    return "https://duckduckgo.com/?q=%s";
}

static gchar *nion_resolve_address(NionApp *app, const gchar *input, gchar **message)
{
    if (message)
        *message = NULL;

    if (!input)
        return NULL;

    gchar *text = g_strdup(input);
    g_strstrip(text);

    if (!*text)
        return text;

    if (g_str_has_prefix(text, "about:")) {
        if (g_str_equal(text, "about:blank"))
            return text;

        if (message)
            *message = g_strdup("Only about:blank is supported as an internal about: address.");
        g_free(text);
        return NULL;
    }

    if (nion_string_has_scheme(text)) {
        if (!(g_ascii_strncasecmp(text, "http://", 7) == 0 ||
              g_ascii_strncasecmp(text, "https://", 8) == 0)) {
            if (message)
                *message = g_strdup("Unsupported URL scheme. NiOn only opens HTTP and HTTPS websites.");
            g_free(text);
            return NULL;
        }

        gchar *validation = NULL;
        if (!nion_validate_uri(text, &validation)) {
            if (message)
                *message = validation;
            else
                g_free(validation);
            g_free(text);
            return NULL;
        }
        return text;
    }

    gboolean contains_space = strpbrk(text, " \t\r\n") != NULL;
    gboolean hostish = FALSE;
    gboolean onion_host = FALSE;

    if (!contains_space) {
        /* Parse a no-scheme candidate to inspect the actual host. This avoids
         * treating '.onion' in a path/query as an onion hostname and makes
         * inputs like linux/appimage fall through to search. */
        gchar *candidate = g_strdup_printf("https://%s", text);
        GError *candidate_error = NULL;
        GUri *parsed = g_uri_parse(candidate, G_URI_FLAGS_PARSE_RELAXED, &candidate_error);
        if (parsed) {
            const gchar *host = g_uri_get_host(parsed);
            if (host && *host) {
                onion_host = nion_host_is_onion(host);
                hostish = onion_host || strchr(host, '.') != NULL ||
                           g_ascii_strcasecmp(host, "localhost") == 0;
            }
            g_uri_unref(parsed);
        }
        g_clear_error(&candidate_error);
        g_free(candidate);
    }

    if (hostish) {
        gchar *uri = g_strdup_printf("%s%s", onion_host ? "http://" : "https://", text);
        gchar *validation = NULL;
        if (!nion_validate_uri(uri, &validation)) {
            if (message)
                *message = validation;
            else
                g_free(validation);
            g_free(uri);
            g_free(text);
            return NULL;
        }
        g_free(text);
        return uri;
    }

    gchar *escaped = g_uri_escape_string(text, NULL, TRUE);
    gchar *uri = g_strdup_printf(nion_search_template(app), escaped ? escaped : "");
    g_free(escaped);
    g_free(text);
    return uri;
}

static gchar *nion_tab_fallback_title(NionTab *tab)
{
    if (tab->home_page)
        return g_strdup("NiOn");
    if (tab->error_page)
        return g_strdup("Error");

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    if ((!uri || !*uri) && tab->restore_uri && *tab->restore_uri)
        uri = tab->restore_uri;
    if (!uri || !*uri)
        return g_strdup("New Tab");

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return g_strdup("New Tab");
    }

    const gchar *host = g_uri_get_host(parsed);
    gchar *title = g_strdup((host && *host) ? host : "New Tab");
    g_uri_unref(parsed);
    return title;
}

static gchar *nion_home_html(NionApp *app)
{
    gchar *state = NULL;
    gchar *detail = NULL;
    const gchar *state_class = "connecting";
    const gchar *state_glyph = "○";

    if (app->tor_ready) {
        state = g_strdup("Tor connected");
        state_class = "connected";
        state_glyph = "●";
        detail = g_strdup(
            "Open a clearnet website, a Tor v3 .onion address, or type a search query. "
            "NiOn routes browsing through Tor.");
    } else if (app->tor_failed) {
        state = g_strdup("Tor unavailable");
        state_class = "error";
        state_glyph = "×";
        gchar *escaped = g_markup_escape_text(
            (app->tor_last_log && *app->tor_last_log) ? app->tor_last_log : "Tor stopped unexpectedly.",
            -1);
        detail = g_strdup_printf(
            "Browsing is blocked. Last Tor message:<br><code>%s</code>", escaped);
        g_free(escaped);
    } else {
        state = g_strdup_printf("Connecting to Tor — %d%%", app->tor_bootstrap_percent);
        detail = g_strdup("Browsing unlocks automatically when Tor reaches 100% bootstrap.");
    }

    gchar *html = g_strdup_printf(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta name='color-scheme' content='light dark'>"
        "<title>NiOn</title>"
        "<style>"
        ":root{color-scheme:light dark;--bg:#f5f5f5;--card:#fff;--fg:#171717;--muted:#666;"
        "--border:#d9d9d9;--soft:#ededed}"
        "@media(prefers-color-scheme:dark){:root{--bg:#101010;--card:#181818;--fg:#eee;"
        "--muted:#aaa;--border:#333;--soft:#222}}"
        "*{box-sizing:border-box}html,body{height:100%%;margin:0;font-family:system-ui,sans-serif}"
        "body{display:grid;place-items:center;background:var(--bg);color:var(--fg);padding:24px}"
        "main{width:min(620px,100%%);text-align:center}"
        ".brand{display:flex;align-items:center;justify-content:center;gap:14px;margin-bottom:24px}"
        ".mark{width:54px;height:54px;border:3px solid currentColor;border-radius:16px;display:grid;"
        "place-items:center;font-size:25px;font-weight:800;letter-spacing:-.08em}"
        ".name{text-align:left}.name h1{font-size:2.25rem;line-height:1;margin:0}.name p{margin:.35rem 0 0;color:var(--muted)}"
        ".status{border:1px solid var(--border);background:var(--card);border-radius:14px;padding:18px;text-align:left}"
        ".status-line{display:flex;gap:10px;align-items:center;font-weight:700}.dot{font-size:1.15rem}"
        ".status.connected .dot{color:#258a46}.status.error .dot{color:#ba3a3a}"
        ".status p{margin:.75rem 0 0;line-height:1.55;color:var(--muted)}"
        "code{overflow-wrap:anywhere;background:var(--soft);padding:.2rem .4rem;border-radius:5px;color:var(--fg)}"
        ".shortcuts{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:14px}"
        ".shortcut{border:1px solid var(--border);border-radius:10px;padding:11px;background:var(--card);color:var(--muted);font-size:.86rem}"
        "kbd{font:inherit;font-weight:700;color:var(--fg)}footer{margin-top:18px;color:var(--muted);font-size:.78rem}"
        "@media(max-width:560px){body{padding:16px}.brand{justify-content:flex-start}.shortcuts{grid-template-columns:1fr}.name h1{font-size:1.9rem}}"
        "</style></head><body><main>"
        "<div class='brand'><div class='mark'>N</div><div class='name'><h1>NiOn</h1>"
        "<p>Minimal Onion · browser over Tor</p></div></div>"
        "<section class='status %s'><div class='status-line'><span class='dot'>%s</span><span>%s</span></div>"
        "<p>%s</p></section>"
        "<div class='shortcuts'><div class='shortcut'><kbd>Ctrl+L</kbd><br>Focus address</div>"
        "<div class='shortcut'><kbd>Ctrl+T</kbd><br>New tab</div>"
        "<div class='shortcut'><kbd>Ctrl+Tab</kbd><br>Next tab</div></div>"
        "<footer>NiOn %s · Open websites. Open onions. Everything through Tor.</footer>"
        "</main></body></html>",
        state_class, state_glyph, state, detail, NION_VERSION);

    g_free(state);
    g_free(detail);
    return html;
}

static gchar *nion_error_html(const gchar *category,
                              const gchar *heading,
                              const gchar *detail,
                              const gchar *uri)
{
    gchar *safe_category = g_markup_escape_text(category ? category : "Browsing error", -1);
    gchar *safe_heading = g_markup_escape_text(heading ? heading : "Page could not be loaded", -1);
    gchar *safe_detail = g_markup_escape_text(detail ? detail : "Unknown error", -1);
    gchar *safe_uri = g_markup_escape_text(uri ? uri : "", -1);

    gchar *html = g_strdup_printf(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta name='color-scheme' content='light dark'>"
        "<title>%s — NiOn</title>"
        "<style>"
        ":root{color-scheme:light dark;--bg:#f5f5f5;--card:#fff;--fg:#171717;--muted:#666;--border:#d9d9d9;--soft:#ededed}"
        "@media(prefers-color-scheme:dark){:root{--bg:#101010;--card:#181818;--fg:#eee;--muted:#aaa;--border:#333;--soft:#222}}"
        "*{box-sizing:border-box}html,body{height:100%%;margin:0;font-family:system-ui,sans-serif}"
        "body{display:grid;place-items:center;background:var(--bg);color:var(--fg);padding:24px}"
        "main{width:min(680px,100%%);border:1px solid var(--border);background:var(--card);border-radius:14px;padding:24px}"
        ".tag{text-transform:uppercase;letter-spacing:.08em;font-size:.72rem;color:var(--muted);font-weight:700}"
        "h1{font-size:1.8rem;margin:.55rem 0 1rem}p{line-height:1.6;color:var(--muted)}"
        "code{display:block;overflow-wrap:anywhere;padding:.65rem .75rem;border-radius:8px;background:var(--soft);color:var(--fg)}"
        ".hint{margin:1.3rem 0 0;padding-top:1rem;border-top:1px solid var(--border);font-size:.9rem}"
        "</style></head><body><main>"
        "<div class='tag'>%s</div><h1>%s</h1>"
        "<p><code>%s</code></p><p>%s</p>"
        "<p class='hint'>Reload to try again, or edit the address above.</p>"
        "</main></body></html>",
        safe_heading, safe_category, safe_heading, safe_uri, safe_detail);

    g_free(safe_category);
    g_free(safe_heading);
    g_free(safe_detail);
    g_free(safe_uri);
    return html;
}

static void nion_clear_retry(NionTab *tab)
{
    if (tab->retry_source_id) {
        g_source_remove(tab->retry_source_id);
        tab->retry_source_id = 0;
    }
    g_clear_pointer(&tab->retry_uri, g_free);
}

static void nion_prepare_normal_navigation(NionTab *tab)
{
    nion_clear_retry(tab);
    tab->home_page = FALSE;
    tab->error_page = FALSE;
    tab->load_failed = FALSE;
    tab->connection_committed = FALSE;
    tab->mixed_content_displayed = FALSE;
    tab->mixed_content_run = FALSE;
    tab->mixed_content_other = FALSE;
    tab->onion_cancel_retries = 0;
    g_clear_pointer(&tab->display_uri_override, g_free);
}

static void nion_show_error_page(NionTab *tab,
                                 const gchar *category,
                                 const gchar *heading,
                                 const gchar *detail,
                                 const gchar *failing_uri,
                                 gboolean preserve_failing_uri)
{
    gchar *html = nion_error_html(category, heading, detail, failing_uri);

    tab->home_page = FALSE;
    tab->error_page = TRUE;
    tab->load_failed = TRUE;
    tab->connection_committed = FALSE;
    tab->mixed_content_displayed = FALSE;
    tab->mixed_content_run = FALSE;
    tab->mixed_content_other = FALSE;

    if (!preserve_failing_uri) {
        g_free(tab->display_uri_override);
        tab->display_uri_override = g_strdup(failing_uri ? failing_uri : "");
    }

    const gchar *content_uri = preserve_failing_uri && failing_uri && *failing_uri
        ? failing_uri
        : "about:blank";
    webkit_web_view_set_zoom_level(tab->web_view, 1.0);
    webkit_web_view_load_alternate_html(tab->web_view, html, content_uri, NULL);
    g_free(html);
}

static void nion_load_home(NionTab *tab)
{
    nion_clear_retry(tab);
    g_clear_pointer(&tab->display_uri_override, g_free);
    tab->home_page = TRUE;
    tab->error_page = FALSE;
    tab->load_failed = FALSE;
    tab->connection_committed = FALSE;
    tab->mixed_content_displayed = FALSE;
    tab->mixed_content_run = FALSE;
    tab->mixed_content_other = FALSE;
    tab->onion_cancel_retries = 0;

    gchar *html = nion_home_html(tab->app);
    webkit_web_view_set_zoom_level(tab->web_view, 1.0);
    webkit_web_view_load_html(tab->web_view, html, "about:blank");
    g_free(html);
}

static void nion_refresh_home_pages(NionApp *app)
{
    if (!app->notebook)
        return;

    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (tab && tab->home_page)
            nion_load_home(tab);
    }
}

static void nion_set_tor_ready(NionApp *app, gboolean ready)
{
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
}

static void nion_set_tor_progress(NionApp *app, gint percent)
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
}


static void nion_stop_all_web_activity(NionApp *app)
{
    if (!app || !app->notebook)
        return;

    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab)
            continue;

        nion_clear_retry(tab);
        webkit_web_view_stop_loading(tab->web_view);
    }
}

static void nion_set_tor_error(NionApp *app, const gchar *message)
{
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
}

static void nion_load_uri(NionTab *tab, const gchar *uri)
{
    if (!tab || !uri || !*uri)
        return;

    nion_prepare_normal_navigation(tab);
    webkit_web_view_load_uri(tab->web_view, uri);
}

static void nion_go_to_address(NionApp *app)
{
    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — request blocked");
        return;
    }

    NionTab *tab = nion_current_tab(app);
    if (!tab)
        return;

    const gchar *raw = gtk_editable_get_text(GTK_EDITABLE(app->address));
    gchar *message = NULL;
    gchar *uri = nion_resolve_address(app, raw, &message);

    if (!uri || !*uri) {
        if (message && *message) {
            nion_show_error_page(tab,
                                 "Address error",
                                 "NiOn cannot open this address",
                                 message,
                                 raw,
                                 FALSE);
            nion_set_status(app, "● TOR CONNECTED — ADDRESS ERROR");
        }
        g_free(message);
        g_free(uri);
        nion_update_controls(app);
        return;
    }

    nion_load_uri(tab, uri);
    g_free(message);
    g_free(uri);
}

static void on_address_activate(GtkEntry *entry, gpointer user_data)
{
    (void)entry;
    nion_go_to_address(user_data);
}

static void on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_clear_retry(tab);
        tab->home_page = FALSE;
        tab->error_page = FALSE;
        tab->load_failed = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
        webkit_web_view_go_back(tab->web_view);
    }
}

static void on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_clear_retry(tab);
        tab->home_page = FALSE;
        tab->error_page = FALSE;
        tab->load_failed = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
        webkit_web_view_go_forward(tab->web_view);
    }
}

static void on_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_clear_retry(tab);
        tab->load_failed = FALSE;
        tab->error_page = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
        webkit_web_view_reload(tab->web_view);
    }
}

static void on_home_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (!tab)
        return;

    nion_clear_retry(tab);
    nion_load_home(tab);
    nion_update_controls(app);
    nion_schedule_session_save(app);
}

static void nion_closed_tab_free(gpointer data)
{
    NionClosedTab *closed = data;
    if (!closed)
        return;

    g_free(closed->uri);
    g_free(closed);
}

static const gchar *nion_tab_reopenable_uri(NionTab *tab)
{
    if (!tab || tab->home_page)
        return NULL;

    const gchar *uri = NULL;
    if (tab->display_uri_override && *tab->display_uri_override)
        uri = tab->display_uri_override;
    else
        uri = webkit_web_view_get_uri(tab->web_view);

    if ((!uri || !*uri || g_str_equal(uri, "about:blank")) &&
        tab->restore_uri && *tab->restore_uri)
        uri = tab->restore_uri;

    if (!uri || !*uri || g_str_equal(uri, "about:blank") ||
        strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return NULL;

    gchar *validation = NULL;
    gboolean valid = nion_validate_uri(uri, &validation);
    g_free(validation);
    return valid ? uri : NULL;
}

static void nion_remember_closed_tab(NionTab *tab)
{
    if (!tab || !tab->app)
        return;

    const gchar *uri = nion_tab_reopenable_uri(tab);
    if (!uri)
        return;

    NionApp *app = tab->app;
    if (!app->closed_tabs)
        app->closed_tabs = g_queue_new();

    NionClosedTab *closed = g_new0(NionClosedTab, 1);
    closed->uri = g_strdup(uri);
    closed->muted = webkit_web_view_get_is_muted(tab->web_view);
    g_queue_push_tail(app->closed_tabs, closed);

    while (g_queue_get_length(app->closed_tabs) > NION_MAX_CLOSED_TABS)
        nion_closed_tab_free(g_queue_pop_head(app->closed_tabs));
}

static void nion_reopen_closed_tab(NionApp *app)
{
    if (!app || !app->closed_tabs || g_queue_is_empty(app->closed_tabs)) {
        if (app)
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — NO CLOSED TAB TO REOPEN"
                : "○ TOR NOT READY — NO CLOSED TAB TO REOPEN");
        return;
    }

    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — CLOSED TAB KEPT FOR LATER");
        return;
    }

    NionClosedTab *closed = g_queue_pop_tail(app->closed_tabs);

    /* If closing the last website left NiOn with its required replacement
     * New Tab, reuse that slot instead of leaving an unnecessary blank tab
     * beside the reopened page. */
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_n_pages(notebook) == 1) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, 0);
        NionTab *only = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (only && only->home_page)
            gtk_notebook_remove_page(notebook, 0);
    }

    NionTab *tab = nion_new_tab(app, closed->uri, TRUE);
    if (tab && closed->muted)
        webkit_web_view_set_is_muted(tab->web_view, TRUE);

    nion_set_status(app, "● TOR CONNECTED — REOPENED CLOSED TAB");
    nion_closed_tab_free(closed);
}

static void nion_close_tab(NionTab *tab)
{
    NionApp *app = tab->app;
    gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->page);
    if (page_num < 0)
        return;

    nion_remember_closed_tab(tab);

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 1) {
        /* Closing the last tab keeps NiOn alive, just like a normal browser.
         * Remove the WebView entirely so the replacement blank tab has no
         * inherited Back/Forward history. */
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
        nion_new_tab(app, NULL, TRUE);
        nion_schedule_session_save(app);
        return;
    }

    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
    nion_update_controls(app);
    nion_schedule_session_save(app);
}

static void on_tab_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_close_tab(user_data);
}

static void nion_tab_free(gpointer data)
{
    NionTab *tab = data;
    nion_clear_retry(tab);
    if (tab->http_warning_decision) {
        webkit_policy_decision_ignore(tab->http_warning_decision);
        g_clear_object(&tab->http_warning_decision);
    }
    if (tab->http_warning_window) {
        GtkWidget *warning = tab->http_warning_window;
        tab->http_warning_window = NULL;
        gtk_window_destroy(GTK_WINDOW(warning));
    }
    g_clear_pointer(&tab->display_uri_override, g_free);
    g_clear_pointer(&tab->onion_location, g_free);
    g_clear_pointer(&tab->http_warning_uri, g_free);
    g_clear_pointer(&tab->http_allowed_origin, g_free);
    g_clear_pointer(&tab->restore_uri, g_free);
    g_free(tab);
}

static void nion_update_tab_audio_button(NionTab *tab)
{
    if (!tab || !tab->audio_button || !tab->web_view)
        return;

    gboolean muted = webkit_web_view_get_is_muted(tab->web_view);
    gtk_button_set_icon_name(GTK_BUTTON(tab->audio_button),
                             muted ? "audio-volume-muted-symbolic"
                                   : "audio-volume-high-symbolic");
    gtk_widget_set_tooltip_text(tab->audio_button,
                                muted ? "Unmute tab" : "Mute tab");
}

static void on_tab_audio_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->web_view)
        return;
    nion_tab_context_popdown(tab);

    webkit_web_view_set_is_muted(tab->web_view,
                                 !webkit_web_view_get_is_muted(tab->web_view));
    nion_schedule_session_save(tab->app);
}

static void on_webview_muted_changed(WebKitWebView *web_view,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
    (void)web_view;
    (void)pspec;
    NionTab *tab = user_data;
    nion_update_tab_audio_button(tab);
    nion_update_tab_context_menu(tab);
}

static void nion_tab_context_popdown(NionTab *tab)
{
    if (tab && tab->tab_menu_popover)
        gtk_popover_popdown(GTK_POPOVER(tab->tab_menu_popover));
}

static void on_tab_context_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app || !tab->app->tor_ready)
        return;
    nion_tab_context_popdown(tab);

    if (tab->home_page) {
        nion_load_home(tab);
        return;
    }

    nion_clear_retry(tab);
    tab->load_failed = FALSE;
    tab->error_page = FALSE;
    g_clear_pointer(&tab->display_uri_override, g_free);
    webkit_web_view_reload(tab->web_view);
}

static void on_tab_context_duplicate_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);
    if (!tab->app->tor_ready) {
        nion_set_status(tab->app, "○ TOR NOT READY — DUPLICATE TAB BLOCKED");
        return;
    }

    const gchar *uri = nion_tab_reopenable_uri(tab);
    NionTab *duplicate = nion_new_tab(tab->app, uri, TRUE);
    if (duplicate && uri)
        webkit_web_view_set_zoom_level(duplicate->web_view,
                                       webkit_web_view_get_zoom_level(tab->web_view));
}

static void on_tab_context_mute_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->web_view)
        return;
    nion_tab_context_popdown(tab);

    webkit_web_view_set_is_muted(tab->web_view,
                                 !webkit_web_view_get_is_muted(tab->web_view));
    nion_update_tab_context_menu(tab);
    nion_schedule_session_save(tab->app);
}

static void on_tab_context_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_close_tab(user_data);
}

static void on_tab_context_close_others_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);

    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    for (gint i = gtk_notebook_get_n_pages(notebook) - 1; i >= 0; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || page == tab->page)
            continue;
        NionTab *other = g_object_get_data(G_OBJECT(page), "nion-tab");
        if (other)
            nion_close_tab(other);
    }

    gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, tab->page));
    nion_update_controls(tab->app);
}

static void on_tab_context_close_right_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);

    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint tab_index = gtk_notebook_page_num(notebook, tab->page);
    if (tab_index < 0)
        return;

    for (gint i = gtk_notebook_get_n_pages(notebook) - 1; i > tab_index; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *other = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (other)
            nion_close_tab(other);
    }

    nion_update_controls(tab->app);
}

static GtkWidget *nion_tab_menu_button(const gchar *label,
                                       GCallback callback,
                                       NionTab *tab)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(button, "flat");
    g_signal_connect(button, "clicked", callback, tab);
    return button;
}

static void nion_update_tab_context_menu(NionTab *tab)
{
    if (!tab || !tab->app)
        return;

    if (tab->tab_menu_mute_button) {
        gtk_button_set_label(GTK_BUTTON(tab->tab_menu_mute_button),
                             webkit_web_view_get_is_muted(tab->web_view)
                                ? "Unmute Tab" : "Mute Tab");
    }

    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    gint index = gtk_notebook_page_num(notebook, tab->page);
    if (tab->tab_menu_close_others_button)
        gtk_widget_set_sensitive(tab->tab_menu_close_others_button, pages > 1);
    if (tab->tab_menu_close_right_button)
        gtk_widget_set_sensitive(tab->tab_menu_close_right_button,
                                 index >= 0 && index < pages - 1);
}

static GtkWidget *nion_create_tab_context_menu(NionTab *tab)
{
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *reload = nion_tab_menu_button("Reload", G_CALLBACK(on_tab_context_reload_clicked), tab);
    GtkWidget *duplicate = nion_tab_menu_button("Duplicate Tab", G_CALLBACK(on_tab_context_duplicate_clicked), tab);
    GtkWidget *mute = nion_tab_menu_button("Mute Tab", G_CALLBACK(on_tab_context_mute_clicked), tab);
    GtkWidget *close = nion_tab_menu_button("Close Tab", G_CALLBACK(on_tab_context_close_clicked), tab);
    GtkWidget *close_others = nion_tab_menu_button("Close Other Tabs", G_CALLBACK(on_tab_context_close_others_clicked), tab);
    GtkWidget *close_right = nion_tab_menu_button("Close Tabs to the Right", G_CALLBACK(on_tab_context_close_right_clicked), tab);

    gtk_box_append(GTK_BOX(box), reload);
    gtk_box_append(GTK_BOX(box), duplicate);
    gtk_box_append(GTK_BOX(box), mute);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), close);
    gtk_box_append(GTK_BOX(box), close_others);
    gtk_box_append(GTK_BOX(box), close_right);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    tab->tab_menu_popover = popover;
    tab->tab_menu_mute_button = mute;
    tab->tab_menu_close_others_button = close_others;
    tab->tab_menu_close_right_button = close_right;
    return popover;
}

static void on_tab_context_pressed(GtkGestureClick *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data)
{
    (void)n_press;
    NionTab *tab = user_data;
    if (!tab || !tab->tab_menu_popover)
        return;

    nion_update_tab_context_menu(tab);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(tab->tab_menu_popover), &rect);
    gtk_popover_popup(GTK_POPOVER(tab->tab_menu_popover));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static GtkWidget *nion_make_tab_label(NionTab *tab)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "nion-tab-label");
    GtkWidget *favicon = gtk_picture_new();
    GtkWidget *label = gtk_label_new("New Tab");
    GtkWidget *audio = gtk_button_new_from_icon_name("audio-volume-high-symbolic");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");

    gtk_widget_set_size_request(favicon, 16, 16);
    gtk_picture_set_can_shrink(GTK_PICTURE(favicon), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(favicon), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_visible(favicon, FALSE);

    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 14);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 22);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_tooltip_text(audio, "Mute tab");
    gtk_widget_add_css_class(audio, "flat");
    gtk_widget_add_css_class(audio, "nion-tab-audio");
    gtk_widget_set_focus_on_click(audio, FALSE);
    gtk_widget_set_visible(audio, FALSE);

    gtk_widget_set_tooltip_text(close, "Close tab");
    gtk_widget_add_css_class(close, "flat");
    gtk_widget_add_css_class(close, "nion-tab-close");

    gtk_box_append(GTK_BOX(box), favicon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), audio);
    gtk_box_append(GTK_BOX(box), close);

    tab->title_label = label;
    tab->favicon_picture = favicon;
    tab->audio_button = audio;
    tab->tab_label_box = box;
    g_signal_connect(audio, "clicked", G_CALLBACK(on_tab_audio_clicked), tab);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close_clicked), tab);

    GtkWidget *popover = nion_create_tab_context_menu(tab);
    gtk_widget_set_parent(popover, box);
    GtkGesture *context_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(context_click, "pressed", G_CALLBACK(on_tab_context_pressed), tab);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(context_click));

    return box;
}

static void nion_update_window_title(NionApp *app, NionTab *tab)
{
    if (!tab) {
        gtk_window_set_title(GTK_WINDOW(app->window), "NiOn");
        return;
    }

    const gchar *title = webkit_web_view_get_title(tab->web_view);
    gchar *fallback = NULL;
    if (!title || !*title) {
        fallback = nion_tab_fallback_title(tab);
        title = fallback;
    }

    if (g_str_equal(title, "NiOn")) {
        gtk_window_set_title(GTK_WINDOW(app->window), "NiOn");
    } else {
        gchar *window_title = g_strdup_printf("%s — NiOn", title);
        gtk_window_set_title(GTK_WINDOW(app->window), window_title);
        g_free(window_title);
    }
    g_free(fallback);
}

static void nion_update_progress(NionApp *app, NionTab *tab)
{
    if (app->window && gtk_window_is_fullscreen(GTK_WINDOW(app->window))) {
        gtk_widget_set_visible(app->progress_bar, FALSE);
        return;
    }

    if (!tab || tab->home_page || tab->error_page || !webkit_web_view_is_loading(tab->web_view)) {
        gtk_widget_set_visible(app->progress_bar, FALSE);
        return;
    }

    gdouble progress = webkit_web_view_get_estimated_load_progress(tab->web_view);
    if (progress < 0.0)
        progress = 0.0;
    if (progress > 1.0)
        progress = 1.0;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar), progress);
    gtk_widget_set_visible(app->progress_bar, TRUE);
}

static gboolean nion_tab_has_mixed_content(const NionTab *tab)
{
    return tab && (tab->mixed_content_displayed || tab->mixed_content_run ||
                   tab->mixed_content_other);
}

static GtkWidget *nion_site_info_row(const gchar *heading, GtkWidget **value_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(heading);
    GtkWidget *value = gtk_label_new("");

    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(value), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(value), TRUE);
    gtk_widget_add_css_class(title, "nion-site-info-key");
    gtk_widget_add_css_class(value, "nion-site-info-value");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), value);
    if (value_out)
        *value_out = value;
    return box;
}

static void nion_update_site_info(NionApp *app)
{
    if (!app || !app->site_info_button)
        return;

    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gboolean usable = tab && !tab->home_page && !tab->error_page && uri && *uri &&
                      !g_str_equal(uri, "about:blank");

    gtk_widget_set_sensitive(app->site_info_button, usable);
    gtk_widget_remove_css_class(app->site_info_button, "nion-site-info-warning");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button),
                                  "dialog-information-symbolic");

    if (!usable) {
        if (app->site_info_title_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_title_label), "Site information");
        if (app->site_info_host_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_host_label), "No website loaded");
        if (app->site_info_connection_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_connection_label), "—");
        if (app->site_info_route_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_route_label),
                               app->tor_ready ? "Bundled Tor — connected" : "Bundled Tor — not ready");
        if (app->site_info_mixed_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_mixed_label), "—");
        if (app->site_info_uri_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_uri_label), "—");
        gtk_widget_set_tooltip_text(app->site_info_button, "No website connection information");
        return;
    }

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        gtk_widget_set_sensitive(app->site_info_button, FALSE);
        gtk_widget_set_tooltip_text(app->site_info_button, "Connection information unavailable");
        return;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gint port = g_uri_get_port(parsed);
    gboolean is_https = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    gboolean is_http = scheme && g_ascii_strcasecmp(scheme, "http") == 0;
    gboolean is_onion = nion_host_is_onion(host);
    gboolean default_port = port < 0 || (is_https && port == 443) || (is_http && port == 80);

    gchar *host_text = NULL;
    if (!host || !*host) {
        host_text = g_strdup("Unknown host");
    } else if (!default_port) {
        host_text = strchr(host, ':')
            ? g_strdup_printf("[%s]:%d", host, port)
            : g_strdup_printf("%s:%d", host, port);
    } else {
        host_text = g_strdup(host);
    }

    GTlsCertificate *certificate = NULL;
    GTlsCertificateFlags tls_errors = 0;
    gboolean has_tls = FALSE;
    if (is_https && tab->connection_committed)
        has_tls = webkit_web_view_get_tls_info(tab->web_view, &certificate, &tls_errors);
    (void)certificate;

    gchar *connection = NULL;
    if (is_onion && is_https) {
        if (!tab->connection_committed)
            connection = g_strdup("Onion Service + HTTPS — connecting");
        else if (has_tls && tls_errors == 0)
            connection = g_strdup("Onion Service + HTTPS — TLS verified");
        else
            connection = g_strdup("Onion Service + HTTPS — TLS information unavailable");
    } else if (is_onion) {
        connection = g_strdup("Onion Service — end-to-end encrypted by Tor");
    } else if (is_https) {
        if (!tab->connection_committed)
            connection = g_strdup("HTTPS — connecting");
        else if (has_tls && tls_errors == 0)
            connection = g_strdup("HTTPS — TLS verified");
        else
            connection = g_strdup("HTTPS — TLS information unavailable");
    } else if (is_http) {
        connection = g_strdup("HTTP — no TLS to the website");
    } else {
        connection = g_strdup("Unknown web connection");
    }

    const gchar *route = app->tor_ready
        ? "Bundled Tor — connected; browsing routed through SOCKS"
        : "Tor unavailable — new browsing is fail-closed";

    const gchar *mixed = NULL;
    if (tab->mixed_content_run && tab->mixed_content_displayed)
        mixed = "Warning — insecure active and display content detected";
    else if (tab->mixed_content_run)
        mixed = "Warning — insecure active content detected";
    else if (tab->mixed_content_displayed)
        mixed = "Warning — insecure display content detected";
    else if (tab->mixed_content_other)
        mixed = "Warning — insecure content event detected";
    else if (is_https)
        mixed = tab->connection_committed ? "None detected" : "Waiting for secure connection";
    else if (is_onion)
        mixed = "HTTPS mixed-content check not applicable to this HTTP onion page";
    else
        mixed = "Not applicable to this HTTP page";

    if (app->site_info_title_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_title_label),
                           is_onion ? "Onion site information" : "Site information");
    if (app->site_info_host_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_host_label), host_text);
    if (app->site_info_connection_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_connection_label), connection);
    if (app->site_info_route_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_route_label), route);
    if (app->site_info_mixed_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_mixed_label), mixed);
    if (app->site_info_uri_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_uri_label), uri);

    if (nion_tab_has_mixed_content(tab)) {
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button),
                                      "dialog-warning-symbolic");
        gtk_widget_add_css_class(app->site_info_button, "nion-site-info-warning");
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "Mixed content detected — open site information");
    } else if (is_onion) {
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "Onion service connection information");
    } else if (is_https) {
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "HTTPS connection information");
    } else {
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "HTTP connection information");
    }

    g_free(connection);
    g_free(host_text);
    g_uri_unref(parsed);
}

static void nion_update_controls(NionApp *app)
{
    if (!app->notebook)
        return;

    NionTab *tab = nion_current_tab(app);
    gboolean usable = tab != NULL && app->tor_ready;

    gtk_widget_set_sensitive(app->address, usable);
    gtk_widget_set_sensitive(app->reload_button, usable);
    gtk_widget_set_sensitive(app->back_button,
        usable && webkit_web_view_can_go_back(tab->web_view));
    gtk_widget_set_sensitive(app->forward_button,
        usable && webkit_web_view_can_go_forward(tab->web_view));

    if (tab) {
        if (tab->display_uri_override) {
            gtk_editable_set_text(GTK_EDITABLE(app->address), tab->display_uri_override);
        } else {
            const gchar *uri = webkit_web_view_get_uri(tab->web_view);
            if (uri && !tab->home_page && !g_str_equal(uri, "about:blank"))
                gtk_editable_set_text(GTK_EDITABLE(app->address), uri);
            else if (tab->home_page || !uri || g_str_equal(uri, "about:blank"))
                gtk_editable_set_text(GTK_EDITABLE(app->address), "");
        }
    }

    nion_update_window_title(app, tab);
    nion_update_progress(app, tab);
    nion_update_onion_button(app);
    nion_update_site_info(app);
    nion_update_bookmark_button(app);
}

static void nion_update_onion_button(NionApp *app)
{
    if (!app || !app->onion_button)
        return;

    NionTab *tab = nion_current_tab(app);
    gboolean visible = tab && tab->onion_location && *tab->onion_location;
    gtk_widget_set_visible(app->onion_button, visible);
    gtk_widget_set_sensitive(app->onion_button, visible && app->tor_ready);
    gtk_widget_set_tooltip_text(app->onion_button,
        visible ? tab->onion_location : "No Onion-Location advertised by this page");
}

static void on_onion_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (!tab || !tab->onion_location || !app->tor_ready)
        return;

    nion_new_tab(app, tab->onion_location, TRUE);
}

static void on_webview_title_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    NionTab *tab = user_data;
    const gchar *title = webkit_web_view_get_title(WEBKIT_WEB_VIEW(object));

    gchar *fallback = NULL;
    if (!title || !*title) {
        fallback = nion_tab_fallback_title(tab);
        title = fallback;
    }

    gtk_label_set_text(GTK_LABEL(tab->title_label), title);
    gtk_widget_set_tooltip_text(tab->title_label, title);
    g_free(fallback);

    if (nion_current_tab(tab->app) == tab)
        nion_update_window_title(tab->app, tab);
}

static void on_webview_favicon_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    NionTab *tab = user_data;
    GdkTexture *favicon = webkit_web_view_get_favicon(WEBKIT_WEB_VIEW(object));

    if (favicon) {
        gtk_picture_set_paintable(GTK_PICTURE(tab->favicon_picture), GDK_PAINTABLE(favicon));
        gtk_widget_set_visible(tab->favicon_picture, TRUE);
    } else {
        gtk_picture_set_paintable(GTK_PICTURE(tab->favicon_picture), NULL);
        gtk_widget_set_visible(tab->favicon_picture, FALSE);
    }
}

static void on_webview_uri_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    NionTab *tab = user_data;
    NionApp *app = tab->app;

    nion_schedule_session_save(app);
    if (nion_current_tab(app) != tab)
        return;

    if (tab->display_uri_override) {
        gtk_editable_set_text(GTK_EDITABLE(app->address), tab->display_uri_override);
        nion_update_bookmark_button(app);
        return;
    }

    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(object));
    if (uri && !tab->home_page && !g_str_equal(uri, "about:blank"))
        gtk_editable_set_text(GTK_EDITABLE(app->address), uri);
    nion_update_bookmark_button(app);
}

static void on_back_forward_list_changed(WebKitBackForwardList *list,
                                         WebKitBackForwardListItem *item_added,
                                         GList *items_removed,
                                         gpointer user_data)
{
    (void)list;
    (void)item_added;
    (void)items_removed;
    NionTab *tab = user_data;

    if (nion_current_tab(tab->app) == tab)
        nion_update_controls(tab->app);
    nion_schedule_session_save(tab->app);
}

static void on_webview_progress_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object;
    (void)pspec;
    NionTab *tab = user_data;
    NionApp *app = tab->app;

    if (nion_current_tab(app) != tab || tab->home_page || tab->error_page)
        return;

    nion_update_progress(app, tab);

    if (app->tor_ready && webkit_web_view_is_loading(tab->web_view)) {
        gint percent = (gint)(webkit_web_view_get_estimated_load_progress(tab->web_view) * 100.0 + 0.5);
        gchar *status = g_strdup_printf("● TOR CONNECTED — loading %d%%", percent);
        nion_set_status(app, status);
        g_free(status);
    }
}

static gboolean nion_retry_cancelled_onion(gpointer user_data)
{
    NionTab *tab = user_data;
    NionApp *app = tab->app;
    tab->retry_source_id = 0;

    if (!tab->retry_uri || !app->tor_ready || app->shutting_down)
        goto out;

    if (webkit_web_view_is_loading(tab->web_view))
        goto out;

    gchar *uri = g_strdup(tab->retry_uri);
    g_clear_pointer(&tab->retry_uri, g_free);
    tab->home_page = FALSE;
    tab->error_page = FALSE;
    tab->load_failed = FALSE;
    g_clear_pointer(&tab->display_uri_override, g_free);

    nion_set_status(app, "● TOR CONNECTED — retrying onion…");
    webkit_web_view_load_uri(tab->web_view, uri);
    g_free(uri);
    return G_SOURCE_REMOVE;

out:
    g_clear_pointer(&tab->retry_uri, g_free);
    return G_SOURCE_REMOVE;
}

static gboolean nion_schedule_onion_retry(NionTab *tab, const gchar *uri)
{
    if (!uri || !*uri || tab->onion_cancel_retries >= 1 || tab->retry_source_id)
        return FALSE;

    tab->onion_cancel_retries++;
    g_free(tab->retry_uri);
    tab->retry_uri = g_strdup(uri);
    tab->retry_source_id = g_timeout_add(NION_ONION_RETRY_DELAY_MS,
                                         nion_retry_cancelled_onion,
                                         tab);
    return TRUE;
}

static void on_webview_insecure_content_detected(WebKitWebView *web_view,
                                                   WebKitInsecureContentEvent event,
                                                   gpointer user_data)
{
    (void)web_view;
    NionTab *tab = user_data;
    if (!tab)
        return;

    if (event == WEBKIT_INSECURE_CONTENT_RUN)
        tab->mixed_content_run = TRUE;
    else if (event == WEBKIT_INSECURE_CONTENT_DISPLAYED)
        tab->mixed_content_displayed = TRUE;
    else
        tab->mixed_content_other = TRUE;

    if (nion_current_tab(tab->app) == tab) {
        nion_update_site_info(tab->app);
        if (tab->app->tor_ready)
            nion_set_status(tab->app, "● TOR CONNECTED — MIXED CONTENT DETECTED");
    }
}

static void on_webview_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data)
{
    NionTab *tab = user_data;
    NionApp *app = tab->app;

    switch (event) {
    case WEBKIT_LOAD_STARTED:
        nion_set_onion_location(tab, NULL);
        tab->connection_committed = FALSE;
        tab->mixed_content_displayed = FALSE;
        tab->mixed_content_run = FALSE;
        tab->mixed_content_other = FALSE;
        if (!tab->home_page && !tab->error_page) {
            tab->load_failed = FALSE;
            if (nion_current_tab(app) == tab)
                nion_set_status(app, "● TOR CONNECTED — loading 0%");
        }
        break;
    case WEBKIT_LOAD_REDIRECTED:
        break;
    case WEBKIT_LOAD_COMMITTED: {
        tab->onion_cancel_retries = 0;
        tab->connection_committed = TRUE;
        const gchar *committed_uri = webkit_web_view_get_uri(web_view);
        if (!nion_uri_is_http_clearnet(committed_uri))
            g_clear_pointer(&tab->http_allowed_origin, g_free);
        nion_apply_site_zoom(tab, committed_uri);
        nion_schedule_session_save(app);
        break;
    }
    case WEBKIT_LOAD_FINISHED:
        nion_schedule_session_save(app);
        if (!tab->load_failed && !tab->error_page && !tab->home_page)
            nion_detect_onion_location(tab);
        if (nion_current_tab(app) == tab) {
            gtk_widget_set_visible(app->progress_bar, FALSE);
            if (app->tor_ready && !tab->load_failed && !tab->error_page)
                nion_set_status(app, nion_tab_has_mixed_content(tab)
                    ? "● TOR CONNECTED — MIXED CONTENT DETECTED"
                    : "● TOR CONNECTED");
        }
        break;
    }

    (void)web_view;
    nion_update_controls(app);
}

static gboolean on_webview_load_failed(WebKitWebView *web_view,
                                       WebKitLoadEvent load_event,
                                       const gchar *failing_uri,
                                       GError *error,
                                       gpointer user_data)
{
    (void)web_view;
    NionTab *tab = user_data;
    NionApp *app = tab->app;

    if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) {
        if (load_event == WEBKIT_LOAD_STARTED && app->tor_ready &&
            nion_uri_is_onion(failing_uri) && nion_schedule_onion_retry(tab, failing_uri)) {
            /* load-failed is followed by LOAD_FINISHED. Mark this load as
             * failed so LOAD_FINISHED does not overwrite the retry status. */
            tab->load_failed = TRUE;
            if (nion_current_tab(app) == tab)
                nion_set_status(app, "● TOR CONNECTED — onion navigation interrupted, retrying…");
        }
        /* Cancellation is usually caused by another navigation. Never show
         * WebKit's raw "Operation was cancelled" page to the user. */
        return TRUE;
    }

    tab->load_failed = TRUE;

    if (!app->tor_ready || app->tor_failed) {
        nion_show_error_page(tab,
                             "Tor error",
                             "Tor is not available",
                             app->tor_last_log ? app->tor_last_log : "NiOn cannot reach the Tor SOCKS proxy.",
                             failing_uri,
                             TRUE);
        nion_set_status(app, "○ TOR ERROR — browsing blocked");
        return TRUE;
    }

    if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL)) {
        nion_show_error_page(tab,
                             "Address error",
                             "Unsupported protocol",
                             "NiOn only opens HTTP and HTTPS websites.",
                             failing_uri,
                             TRUE);
        nion_set_status(app, "● TOR CONNECTED — ADDRESS ERROR");
        return TRUE;
    }

    if (nion_uri_is_onion(failing_uri)) {
        gchar *detail = g_strdup_printf(
            "Tor is connected, but the onion service could not be loaded. %s",
            error && error->message ? error->message : "The service may be offline or temporarily unreachable.");
        nion_show_error_page(tab,
                             "Onion error",
                             "Onion service unavailable",
                             detail,
                             failing_uri,
                             TRUE);
        g_free(detail);
        nion_set_status(app, "● TOR CONNECTED — ONION ERROR");
        return TRUE;
    }

    gchar *detail = g_strdup_printf(
        "Tor is connected, but the website could not be loaded. %s",
        error && error->message ? error->message : "The website may be unavailable.");
    nion_show_error_page(tab,
                         "Website error",
                         "Website unavailable through Tor",
                         detail,
                         failing_uri,
                         TRUE);
    g_free(detail);
    nion_set_status(app, "● TOR CONNECTED — WEBSITE ERROR");
    return TRUE;
}

static gboolean on_webview_tls_failed(WebKitWebView *web_view,
                                      const gchar *failing_uri,
                                      GTlsCertificate *certificate,
                                      GTlsCertificateFlags errors,
                                      gpointer user_data)
{
    (void)web_view;
    (void)certificate;
    (void)errors;

    NionTab *tab = user_data;
    NionApp *app = tab->app;
    tab->load_failed = TRUE;

    nion_show_error_page(tab,
                         "TLS error",
                         "Secure connection could not be verified",
                         "NiOn refused the site's TLS certificate. The certificate is invalid, expired, untrusted, or does not match the website.",
                         failing_uri,
                         TRUE);
    nion_set_status(app, "● TOR CONNECTED — TLS ERROR");
    return TRUE;
}

static gchar *nion_format_bytes(guint64 bytes)
{
    const gchar *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    gdouble value = (gdouble)bytes;
    guint unit = 0;

    while (value >= 1024.0 && unit + 1 < G_N_ELEMENTS(units)) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
        return g_strdup_printf("%" G_GUINT64_FORMAT " %s", bytes, units[unit]);
    return g_strdup_printf("%.1f %s", value, units[unit]);
}

static gchar *nion_safe_download_filename(const gchar *suggested)
{
    const gchar *source = (suggested && *suggested) ? suggested : "download";
    gchar *base = g_path_get_basename(source);

    if (!base || !*base || g_str_equal(base, ".") || g_str_equal(base, "..")) {
        g_free(base);
        return g_strdup("download");
    }

    for (gchar *p = base; *p; p++) {
        if (*p == '/' || *p == '\\' || ((guchar)*p < 0x20) || *p == 0x7f)
            *p = '_';
    }

    return base;
}

static gchar *nion_unique_download_path(NionApp *app, const gchar *suggested)
{
    gchar *filename = nion_safe_download_filename(suggested);
    gchar *candidate = g_build_filename(app->download_dir, filename, NULL);

    if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        g_free(filename);
        return candidate;
    }

    const gchar *dot = strrchr(filename, '.');
    gchar *stem = NULL;
    gchar *extension = NULL;

    if (dot && dot != filename) {
        stem = g_strndup(filename, (gsize)(dot - filename));
        extension = g_strdup(dot);
    } else {
        stem = g_strdup(filename);
        extension = g_strdup("");
    }

    g_free(candidate);
    candidate = NULL;

    for (guint i = 1; i < G_MAXUINT; i++) {
        gchar *numbered = g_strdup_printf("%s (%u)%s", stem, i, extension);
        candidate = g_build_filename(app->download_dir, numbered, NULL);
        g_free(numbered);
        if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
            break;
        g_clear_pointer(&candidate, g_free);
    }

    g_free(stem);
    g_free(extension);
    g_free(filename);
    return candidate;
}

static gchar *nion_download_time_text(gint64 unix_time)
{
    if (unix_time <= 0)
        return g_strdup("");

    GDateTime *dt = g_date_time_new_from_unix_local(unix_time);
    if (!dt)
        return g_strdup("");
    gchar *text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return text;
}

static void nion_download_update_panel_visibility(NionApp *app)
{
    if (!app || !app->downloads_list)
        return;

    gboolean has_rows = gtk_widget_get_first_child(app->downloads_list) != NULL;
    if (app->downloads_empty_label)
        gtk_widget_set_visible(app->downloads_empty_label, !has_rows);
}

static void nion_download_free(gpointer data)
{
    NionDownload *item = data;
    if (!item)
        return;

    if (item->download) {
        g_signal_handlers_disconnect_by_data(item->download, item);
        g_object_unref(item->download);
    }

    g_clear_pointer(&item->destination, g_free);
    g_clear_pointer(&item->filename, g_free);
    g_clear_pointer(&item->source_uri, g_free);
    g_clear_pointer(&item->history_id, g_free);
    g_clear_pointer(&item->history_status, g_free);
    g_clear_pointer(&item->history_detail, g_free);
    g_free(item);
}

static void nion_download_set_history(NionDownload *item,
                                      const gchar *status,
                                      const gchar *detail)
{
    if (!item)
        return;
    g_free(item->history_status);
    item->history_status = g_strdup(status ? status : "Unknown");
    g_free(item->history_detail);
    item->history_detail = g_strdup(detail ? detail : "");
    item->history_time = g_get_real_time() / G_USEC_PER_SEC;
}

static void nion_download_refresh_history_detail(NionDownload *item)
{
    if (!item || !item->detail_label)
        return;

    const gchar *detail = item->history_detail ? item->history_detail : "";
    gchar *when = nion_download_time_text(item->history_time);
    gchar *shown = (*when && *detail)
        ? g_strdup_printf("%s · %s", detail, when)
        : g_strdup((*detail) ? detail : when);
    gtk_label_set_text(GTK_LABEL(item->detail_label), shown);
    g_free(shown);
    g_free(when);
}

static gboolean nion_download_source_retryable(const gchar *uri)
{
    if (!uri || !*uri || strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    gboolean web_scheme = scheme &&
        (g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0);
    g_uri_unref(parsed);

    return web_scheme && nion_validate_uri(uri, NULL);
}

static gboolean nion_download_parent_directory_exists(const gchar *destination)
{
    if (!destination || !*destination)
        return FALSE;

    gchar *parent = g_path_get_dirname(destination);
    gboolean exists = parent && g_file_test(parent, G_FILE_TEST_IS_DIR);
    g_free(parent);
    return exists;
}

static void nion_download_update_actions(NionDownload *item)
{
    if (!item || !item->more_button)
        return;

    gboolean file_ready = item->finished && item->destination &&
        g_file_test(item->destination, G_FILE_TEST_IS_REGULAR);
    gboolean folder_ready = nion_download_parent_directory_exists(item->destination);
    gboolean link_ready = item->source_uri && *item->source_uri;
    gboolean retry_ready = item->failed &&
        g_strcmp0(item->history_status, "Failed") == 0 &&
        nion_download_source_retryable(item->source_uri);

    gtk_widget_set_visible(item->open_button, file_ready);
    gtk_widget_set_visible(item->folder_button, folder_ready);
    gtk_widget_set_visible(item->copy_link_button, link_ready);
    gtk_widget_set_visible(item->retry_button, retry_ready);
    gtk_widget_set_visible(item->more_button,
                           file_ready || folder_ready || link_ready || retry_ready);
}

static void nion_download_action_status(NionDownload *item, const gchar *detail)
{
    if (!item || !item->app || !detail)
        return;

    const gchar *tor_state = item->app->tor_ready && !item->app->tor_failed
        ? "● TOR CONNECTED"
        : "○ TOR OFFLINE";
    gchar *message = g_strdup_printf("%s — %s", tor_state, detail);
    nion_set_status(item->app, message);
    g_free(message);
}

static gboolean nion_download_launch_uri(NionDownload *item,
                                         const gchar *uri,
                                         const gchar *success_detail)
{
    if (!item || !item->app || !uri || !*uri)
        return FALSE;

    GError *error = NULL;
    gboolean launched = g_app_info_launch_default_for_uri(uri, NULL, &error);
    if (!launched) {
        gchar *detail = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                        (error && error->message) ? error->message : "could not launch application");
        nion_download_action_status(item, detail);
        g_free(detail);
        g_clear_error(&error);
        return FALSE;
    }

    if (success_detail)
        nion_download_action_status(item, success_detail);
    return TRUE;
}

static void on_download_open_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->finished || !item->destination ||
        !g_file_test(item->destination, G_FILE_TEST_IS_REGULAR)) {
        if (item && item->app)
            nion_download_action_status(item, "DOWNLOADED FILE NOT FOUND");
        nion_download_update_actions(item);
        return;
    }

    GError *error = NULL;
    gchar *uri = g_filename_to_uri(item->destination, NULL, &error);
    if (!uri) {
        gchar *message = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                         (error && error->message) ? error->message : "invalid file path");
        nion_download_action_status(item, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    nion_download_launch_uri(item, uri, "OPENED DOWNLOADED FILE");
    g_free(uri);
}

static void on_download_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->destination)
        return;

    gchar *parent = g_path_get_dirname(item->destination);
    if (!parent || !g_file_test(parent, G_FILE_TEST_IS_DIR)) {
        if (item->app)
            nion_download_action_status(item, "DOWNLOAD FOLDER NOT FOUND");
        g_free(parent);
        nion_download_update_actions(item);
        return;
    }

    GError *error = NULL;
    gchar *uri = g_filename_to_uri(parent, NULL, &error);
    g_free(parent);
    if (!uri) {
        gchar *message = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                         (error && error->message) ? error->message : "invalid folder path");
        nion_download_action_status(item, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    nion_download_launch_uri(item, uri, "OPENED DOWNLOAD FOLDER");
    g_free(uri);
}

static void on_download_copy_link_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->source_uri || !*item->source_uri)
        return;

    GdkDisplay *display = gtk_widget_get_display(item->app->window);
    if (!display)
        return;

    GdkClipboard *clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, item->source_uri);
    nion_download_action_status(item, "DOWNLOAD LINK COPIED");
}

static void on_download_retry_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->app || !item->failed ||
        g_strcmp0(item->history_status, "Failed") != 0 ||
        !nion_download_source_retryable(item->source_uri))
        return;

    NionApp *app = item->app;
    if (!app->tor_ready || app->tor_failed) {
        nion_set_status(app, "○ TOR OFFLINE — DOWNLOAD RETRY BLOCKED");
        return;
    }

    NionTab *tab = nion_current_tab(app);
    if (!tab || !tab->web_view) {
        nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY FAILED: no active tab");
        return;
    }

    WebKitDownload *retry = webkit_web_view_download_uri(tab->web_view, item->source_uri);
    if (!retry) {
        nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY FAILED");
        return;
    }

    g_object_unref(retry);
    nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY STARTED (Ctrl+J)");
}

static GtkWidget *nion_download_create_row(NionDownload *item,
                                           const gchar *name,
                                           const gchar *detail,
                                           gboolean active)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name_label = gtk_label_new(name ? name : "Download");
    GtkWidget *detail_label = gtk_label_new(detail ? detail : "");
    GtkWidget *progress = gtk_progress_bar_new();
    GtkWidget *action = gtk_button_new_with_label(active ? "Cancel" : "Remove");
    GtkWidget *more = gtk_menu_button_new();
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *open = gtk_button_new_with_label("Open File");
    GtkWidget *folder = gtk_button_new_with_label("Open Containing Folder");
    GtkWidget *copy_link = gtk_button_new_with_label("Copy Download Link");
    GtkWidget *retry = gtk_button_new_with_label("Retry Failed Download");

    gtk_widget_add_css_class(row, "nion-download-row");
    gtk_widget_add_css_class(detail_label, "nion-download-detail");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(detail_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_ellipsize(GTK_LABEL(detail_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_widget_set_hexpand(progress, TRUE);
    gtk_widget_set_size_request(progress, 180, -1);
    gtk_widget_set_visible(progress, active);

    gtk_widget_set_tooltip_text(action, active ? "Cancel download" : "Remove from download history");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(more), "view-more-symbolic");
    gtk_widget_set_tooltip_text(more, "Download actions");
    gtk_widget_set_halign(more, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(more, GTK_ALIGN_CENTER);

    gtk_widget_set_margin_top(actions, 6);
    gtk_widget_set_margin_bottom(actions, 6);
    gtk_widget_set_margin_start(actions, 6);
    gtk_widget_set_margin_end(actions, 6);
    gtk_box_append(GTK_BOX(actions), open);
    gtk_box_append(GTK_BOX(actions), folder);
    gtk_box_append(GTK_BOX(actions), copy_link);
    gtk_box_append(GTK_BOX(actions), retry);
    gtk_popover_set_child(GTK_POPOVER(popover), actions);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(more), popover);

    gtk_box_append(GTK_BOX(labels), name_label);
    gtk_box_append(GTK_BOX(labels), detail_label);
    gtk_box_append(GTK_BOX(row), labels);
    gtk_box_append(GTK_BOX(row), progress);
    gtk_box_append(GTK_BOX(row), action);
    gtk_box_append(GTK_BOX(row), more);

    item->row = row;
    item->name_label = name_label;
    item->detail_label = detail_label;
    item->progress_bar = progress;
    item->action_button = action;
    item->more_button = more;
    item->open_button = open;
    item->folder_button = folder;
    item->copy_link_button = copy_link;
    item->retry_button = retry;

    g_signal_connect(open, "clicked", G_CALLBACK(on_download_open_clicked), item);
    g_signal_connect(folder, "clicked", G_CALLBACK(on_download_folder_clicked), item);
    g_signal_connect(copy_link, "clicked", G_CALLBACK(on_download_copy_link_clicked), item);
    g_signal_connect(retry, "clicked", G_CALLBACK(on_download_retry_clicked), item);

    nion_download_update_actions(item);
    g_object_set_data_full(G_OBJECT(row), "nion-download", item, nion_download_free);
    return row;
}

static void nion_save_download_history(NionApp *app)
{
    if (!app || !app->downloads_file || !app->downloads_list)
        return;

    GKeyFile *key_file = g_key_file_new();
    guint index = 0;
    for (GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (!item || (!item->finished && !item->failed))
            continue;
        if (index >= NION_MAX_DOWNLOAD_HISTORY)
            break;

        gchar *group = g_strdup_printf("Download-%u", index++);
        g_key_file_set_string(key_file, group, "name",
                              (item->filename && *item->filename) ? item->filename : "Download");
        g_key_file_set_string(key_file, group, "destination",
                              item->destination ? item->destination : "");
        g_key_file_set_string(key_file, group, "source-uri",
                              item->source_uri ? item->source_uri : "");
        g_key_file_set_string(key_file, group, "status",
                              item->history_status ? item->history_status : (item->finished ? "Completed" : "Failed"));
        g_key_file_set_string(key_file, group, "detail",
                              item->history_detail ? item->history_detail : "");
        g_key_file_set_int64(key_file, group, "time", item->history_time);
        g_free(group);
    }

    g_key_file_set_integer(key_file, "History", "count", (gint)index);
    nion_write_key_file_atomic(key_file, app->downloads_file);
    g_key_file_free(key_file);
}

static void nion_download_remove_row(NionDownload *item)
{
    if (!item || !item->row)
        return;

    NionApp *app = item->app;
    GtkWidget *row = item->row;
    item->row = NULL;
    gtk_box_remove(GTK_BOX(app->downloads_list), row);
    nion_download_update_panel_visibility(app);
    nion_save_download_history(app);
}

static void on_download_action_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item)
        return;

    if (!item->finished && !item->failed && item->download) {
        item->cancel_requested = TRUE;
        gtk_widget_set_sensitive(item->action_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(item->action_button), "Cancelling…");
        webkit_download_cancel(item->download);
        return;
    }

    nion_download_remove_row(item);
}

static gboolean on_download_decide_destination(WebKitDownload *download,
                                               const gchar *suggested_filename,
                                               gpointer user_data)
{
    NionDownload *item = user_data;
    NionApp *app = item->app;

    if (!app->tor_ready) {
        item->cancel_requested = TRUE;
        webkit_download_cancel(download);
        return TRUE;
    }

    gchar *destination = nion_unique_download_path(app, suggested_filename);
    if (!destination) {
        webkit_download_cancel(download);
        return TRUE;
    }

    g_free(item->destination);
    item->destination = destination;
    g_free(item->filename);
    item->filename = g_path_get_basename(destination);

    gtk_label_set_text(GTK_LABEL(item->name_label), item->filename);
    gtk_widget_set_tooltip_text(item->name_label, destination);
    nion_download_update_actions(item);
    webkit_download_set_allow_overwrite(download, FALSE);
    webkit_download_set_destination(download, destination);
    return TRUE;
}

static void on_download_progress_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    (void)pspec;
    NionDownload *item = user_data;
    WebKitDownload *download = WEBKIT_DOWNLOAD(object);

    gdouble progress = webkit_download_get_estimated_progress(download);
    if (progress < 0.0)
        progress = 0.0;
    if (progress > 1.0)
        progress = 1.0;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress_bar), progress);

    guint64 received = webkit_download_get_received_data_length(download);
    gchar *received_text = nion_format_bytes(received);
    gchar *detail = g_strdup_printf("%d%% · %s received",
                                    (gint)(progress * 100.0 + 0.5), received_text);
    gtk_label_set_text(GTK_LABEL(item->detail_label), detail);
    g_free(detail);
    g_free(received_text);
}

static void on_download_received_data(WebKitDownload *download,
                                      guint64 data_length,
                                      gpointer user_data)
{
    (void)data_length;
    NionDownload *item = user_data;
    on_download_progress_changed(G_OBJECT(download), NULL, item);
}

static void on_download_failed(WebKitDownload *download,
                               GError *error,
                               gpointer user_data)
{
    (void)download;
    NionDownload *item = user_data;
    item->failed = TRUE;

    if (item->destination && g_file_test(item->destination, G_FILE_TEST_IS_REGULAR))
        g_remove(item->destination);

    gtk_widget_set_sensitive(item->action_button, TRUE);
    gtk_button_set_label(GTK_BUTTON(item->action_button), "Remove");
    gtk_widget_set_visible(item->progress_bar, FALSE);

    if (item->cancel_requested) {
        nion_download_set_history(item, "Cancelled", "Cancelled");
        if (item->app->tor_ready)
            nion_set_status(item->app, "● TOR CONNECTED — DOWNLOAD CANCELLED");
    } else {
        gchar *detail = g_strdup_printf("Failed — %s",
                                        (error && error->message) ? error->message : "download interrupted");
        nion_download_set_history(item, "Failed", detail);
        g_free(detail);
        if (item->app->tor_ready)
            nion_set_status(item->app, "● TOR CONNECTED — DOWNLOAD FAILED");
    }
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    nion_save_download_history(item->app);
}

static void on_download_finished(WebKitDownload *download, gpointer user_data)
{
    (void)download;
    NionDownload *item = user_data;

    if (item->failed)
        return;

    item->finished = TRUE;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress_bar), 1.0);
    gtk_widget_set_visible(item->progress_bar, FALSE);
    gtk_widget_set_sensitive(item->action_button, TRUE);
    gtk_button_set_label(GTK_BUTTON(item->action_button), "Remove");

    guint64 received = webkit_download_get_received_data_length(item->download);
    gchar *size = nion_format_bytes(received);
    gchar *detail = g_strdup_printf("Completed · %s", size);
    nion_download_set_history(item, "Completed", detail);
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    g_free(detail);
    g_free(size);
    nion_save_download_history(item->app);

    const gchar *filename = (item->filename && *item->filename) ? item->filename : "Download";
    if (item->app->tor_ready) {
        gchar *status = g_strdup_printf("● TOR CONNECTED — DOWNLOAD COMPLETE: %s", filename);
        nion_set_status(item->app, status);
        g_free(status);
    }

    GNotification *notification = g_notification_new("NiOn download complete");
    g_notification_set_body(notification, filename);
    g_application_send_notification(G_APPLICATION(item->app->application), NULL, notification);
    g_object_unref(notification);
}

static void on_download_started(WebKitNetworkSession *session,
                                WebKitDownload *download,
                                gpointer user_data)
{
    (void)session;
    NionApp *app = user_data;

    if (!app->tor_ready) {
        webkit_download_cancel(download);
        nion_set_status(app, "○ TOR OFFLINE — DOWNLOAD BLOCKED");
        return;
    }

    NionDownload *item = g_new0(NionDownload, 1);
    item->app = app;
    item->download = g_object_ref(download);

    WebKitURIRequest *request = webkit_download_get_request(download);
    const gchar *request_uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (request_uri && *request_uri && strlen(request_uri) <= NION_MAX_SAVED_URI_BYTES)
        item->source_uri = g_strdup(request_uri);

    GtkWidget *row = nion_download_create_row(item, "Preparing download…", "Waiting for destination…", TRUE);

    g_signal_connect(item->action_button, "clicked", G_CALLBACK(on_download_action_clicked), item);
    g_signal_connect(download, "decide-destination", G_CALLBACK(on_download_decide_destination), item);
    g_signal_connect(download, "notify::estimated-progress", G_CALLBACK(on_download_progress_changed), item);
    g_signal_connect(download, "received-data", G_CALLBACK(on_download_received_data), item);
    g_signal_connect(download, "failed", G_CALLBACK(on_download_failed), item);
    g_signal_connect(download, "finished", G_CALLBACK(on_download_finished), item);

    gtk_box_prepend(GTK_BOX(app->downloads_list), row);
    nion_download_update_panel_visibility(app);
    nion_set_status(app, "● TOR CONNECTED — DOWNLOAD STARTED (Ctrl+J)");
}

static void nion_download_add_history_row(NionApp *app,
                                          const gchar *name,
                                          const gchar *destination,
                                          const gchar *source_uri,
                                          const gchar *status,
                                          const gchar *detail,
                                          gint64 timestamp)
{
    NionDownload *item = g_new0(NionDownload, 1);
    item->app = app;
    item->filename = g_strdup((name && *name) ? name : "Download");
    item->destination = g_strdup(destination ? destination : "");
    if (source_uri && *source_uri && strlen(source_uri) <= NION_MAX_SAVED_URI_BYTES)
        item->source_uri = g_strdup(source_uri);
    item->history_status = g_strdup(status ? status : "Unknown");
    item->history_detail = g_strdup(detail ? detail : "");
    item->history_time = timestamp;
    item->finished = g_strcmp0(status, "Completed") == 0;
    item->failed = !item->finished;

    GtkWidget *row = nion_download_create_row(item, item->filename, "", FALSE);
    if (item->destination && *item->destination)
        gtk_widget_set_tooltip_text(item->name_label, item->destination);
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    g_signal_connect(item->action_button, "clicked", G_CALLBACK(on_download_action_clicked), item);
    gtk_box_append(GTK_BOX(app->downloads_list), row);
}

static void nion_load_download_history(NionApp *app)
{
    if (!app || !app->downloads_file || !app->downloads_list)
        return;

    if (g_file_test(app->downloads_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->downloads_file,
                                        NION_MAX_DOWNLOADS_FILE_BYTES)) {
        nion_quarantine_profile_file(app->downloads_file, "download history");
        nion_download_update_panel_visibility(app);
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->downloads_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn download history: %s", error->message);
            nion_quarantine_profile_file(app->downloads_file, "download history");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_download_update_panel_visibility(app);
        return;
    }

    error = NULL;
    gint count = g_key_file_get_integer(key_file, "History", "count", &error);
    if (error || count < 0 || count > NION_MAX_DOWNLOAD_HISTORY_INPUT) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->downloads_file, "download history");
        nion_download_update_panel_visibility(app);
        return;
    }
    if (count > NION_MAX_DOWNLOAD_HISTORY)
        count = NION_MAX_DOWNLOAD_HISTORY;

    for (gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Download-%d", i);
        gchar *name = g_key_file_get_string(key_file, group, "name", NULL);
        gchar *destination = g_key_file_get_string(key_file, group, "destination", NULL);
        gchar *source_uri = g_key_file_get_string(key_file, group, "source-uri", NULL);
        gchar *status = g_key_file_get_string(key_file, group, "status", NULL);
        gchar *detail = g_key_file_get_string(key_file, group, "detail", NULL);
        gint64 timestamp = g_key_file_get_int64(key_file, group, "time", NULL);
        if (name && status)
            nion_download_add_history_row(app, name, destination, source_uri, status, detail, timestamp);
        g_free(name);
        g_free(destination);
        g_free(source_uri);
        g_free(status);
        g_free(detail);
        g_free(group);
    }
    g_key_file_free(key_file);
    nion_download_update_panel_visibility(app);
}

static void on_clear_downloads_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app || !app->downloads_list)
        return;

    GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
    while (row) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (item && (item->finished || item->failed)) {
            item->row = NULL;
            gtk_box_remove(GTK_BOX(app->downloads_list), row);
        }
        row = next;
    }
    nion_download_update_panel_visibility(app);
    nion_save_download_history(app);
}

static gboolean on_downloads_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void nion_show_downloads(NionApp *app)
{
    if (!app || !app->downloads_window)
        return;
    gtk_window_present(GTK_WINDOW(app->downloads_window));
}

static void nion_bookmark_free(gpointer data)
{
    NionBookmark *bookmark = data;
    if (!bookmark)
        return;
    g_free(bookmark->title);
    g_free(bookmark->uri);
    g_free(bookmark);
}

static gchar *nion_bookmark_title_normalize(const gchar *title, const gchar *uri)
{
    const gchar *source = (title && *title) ? title : uri;
    gchar *clean = g_strdup(source && *source ? source : "Bookmark");
    g_strstrip(clean);
    if (!*clean) {
        g_free(clean);
        clean = g_strdup(uri && *uri ? uri : "Bookmark");
    }

    if (g_utf8_validate(clean, -1, NULL)) {
        glong chars = g_utf8_strlen(clean, -1);
        if (chars > NION_MAX_BOOKMARK_TITLE_CHARS) {
            gchar *shortened = g_utf8_substring(clean, 0, NION_MAX_BOOKMARK_TITLE_CHARS);
            g_free(clean);
            clean = shortened;
        }
    } else if (strlen(clean) > 1024) {
        gchar *shortened = g_strndup(clean, 1024);
        g_free(clean);
        clean = shortened;
    }
    return clean;
}

static gboolean nion_bookmark_uri_exists(NionApp *app, const gchar *uri)
{
    if (!app || !app->bookmarks || !uri)
        return FALSE;
    for (guint i = 0; i < app->bookmarks->len; i++) {
        NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
        if (bookmark && g_strcmp0(bookmark->uri, uri) == 0)
            return TRUE;
    }
    return FALSE;
}

static gint nion_bookmark_index(NionApp *app, NionBookmark *bookmark)
{
    if (!app || !app->bookmarks || !bookmark)
        return -1;
    for (guint i = 0; i < app->bookmarks->len; i++) {
        if (g_ptr_array_index(app->bookmarks, i) == bookmark)
            return (gint)i;
    }
    return -1;
}

static void nion_save_bookmarks(NionApp *app)
{
    if (!app || !app->bookmarks_file || !app->bookmarks)
        return;

    GKeyFile *key_file = g_key_file_new();
    guint count = MIN(app->bookmarks->len, (guint)NION_MAX_BOOKMARKS);
    g_key_file_set_integer(key_file, "Bookmarks", "count", (gint)count);

    for (guint i = 0; i < count; i++) {
        NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
        if (!bookmark || !bookmark->uri)
            continue;
        gchar *group = g_strdup_printf("Bookmark-%u", i);
        g_key_file_set_string(key_file, group, "title",
                              bookmark->title ? bookmark->title : bookmark->uri);
        g_key_file_set_string(key_file, group, "uri", bookmark->uri);
        g_free(group);
    }

    nion_write_key_file_atomic(key_file, app->bookmarks_file);
    g_key_file_free(key_file);
}

static void nion_load_bookmarks(NionApp *app)
{
    if (!app)
        return;

    if (app->bookmarks)
        g_ptr_array_unref(app->bookmarks);
    app->bookmarks = g_ptr_array_new_with_free_func(nion_bookmark_free);

    if (!app->bookmarks_file)
        return;

    if (g_file_test(app->bookmarks_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->bookmarks_file,
                                        NION_MAX_BOOKMARKS_FILE_BYTES)) {
        nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->bookmarks_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn bookmarks: %s", error->message);
            nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    error = NULL;
    gint count = g_key_file_get_integer(key_file, "Bookmarks", "count", &error);
    if (error || count < 0 || count > NION_MAX_BOOKMARKS_INPUT) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        return;
    }
    if (count > NION_MAX_BOOKMARKS)
        count = NION_MAX_BOOKMARKS;

    for (gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Bookmark-%d", i);
        gchar *title = g_key_file_get_string(key_file, group, "title", NULL);
        gchar *uri = g_key_file_get_string(key_file, group, "uri", NULL);
        g_free(group);

        gchar *validation = NULL;
        gboolean valid = uri && *uri && strlen(uri) <= NION_MAX_SAVED_URI_BYTES &&
                         nion_validate_uri(uri, &validation);
        g_free(validation);
        if (valid && !nion_bookmark_uri_exists(app, uri)) {
            NionBookmark *bookmark = g_new0(NionBookmark, 1);
            bookmark->uri = g_strdup(uri);
            bookmark->title = nion_bookmark_title_normalize(title, uri);
            g_ptr_array_add(app->bookmarks, bookmark);
        }
        g_free(title);
        g_free(uri);
    }

    g_key_file_free(key_file);
}

static void nion_open_bookmark(NionApp *app, NionBookmark *bookmark)
{
    if (!app || !bookmark || !bookmark->uri)
        return;
    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — BOOKMARK OPEN BLOCKED");
        return;
    }
    nion_new_tab(app, bookmark->uri, TRUE);
}

static void on_bookmark_open_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    nion_open_bookmark(app, bookmark);
}

static void on_bookmark_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(row), "nion-bookmark");
    nion_open_bookmark(user_data, bookmark);
}

static void on_bookmark_rename_cancel(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void on_bookmark_rename_save(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!root || !GTK_IS_WINDOW(root))
        return;

    GtkWindow *window = GTK_WINDOW(root);
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(window), "nion-bookmark");
    GtkEntry *entry = g_object_get_data(G_OBJECT(window), "nion-title-entry");
    if (!bookmark || !entry || nion_bookmark_index(app, bookmark) < 0) {
        gtk_window_destroy(window);
        return;
    }

    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    gchar *title = nion_bookmark_title_normalize(text, bookmark->uri);
    g_free(bookmark->title);
    bookmark->title = title;
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK RENAMED");
    gtk_window_destroy(window);
}

static void on_bookmark_rename_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    if (!bookmark || nion_bookmark_index(app, bookmark) < 0)
        return;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Rename Bookmark");
    gtk_window_set_transient_for(GTK_WINDOW(window),
        app->bookmarks_window ? GTK_WINDOW(app->bookmarks_window) : GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 460, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Rename bookmark</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), bookmark->title ? bookmark->title : bookmark->uri);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *uri = gtk_label_new(bookmark->uri);
    gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(uri, "nion-muted");
    gtk_box_append(GTK_BOX(box), uri);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data(G_OBJECT(window), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(window), "nion-title-entry", entry);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_bookmark_rename_cancel), app);
    g_signal_connect(save, "clicked", G_CALLBACK(on_bookmark_rename_save), app);
    g_signal_connect_swapped(entry, "activate", G_CALLBACK(gtk_widget_activate), save);

    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
}

static void on_bookmark_delete_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    gint index = nion_bookmark_index(app, bookmark);
    if (index < 0)
        return;

    g_ptr_array_remove_index(app->bookmarks, (guint)index);
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    nion_update_bookmark_button(app);
    if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK REMOVED");
}

static gchar *nion_bookmark_search_fold(const gchar *text)
{
    if (!text || !*text)
        return g_strdup("");
    if (g_utf8_validate(text, -1, NULL))
        return g_utf8_casefold(text, -1);
    return g_ascii_strdown(text, -1);
}

static gboolean nion_bookmark_matches_search(NionBookmark *bookmark, const gchar *query)
{
    if (!bookmark)
        return FALSE;
    if (!query || !*query)
        return TRUE;

    gchar *query_copy = g_strdup(query);
    g_strstrip(query_copy);
    if (!*query_copy) {
        g_free(query_copy);
        return TRUE;
    }

    gchar *query_fold = nion_bookmark_search_fold(query_copy);
    gchar *title_fold = nion_bookmark_search_fold(bookmark->title);
    gchar *uri_fold = nion_bookmark_search_fold(bookmark->uri);
    gchar **terms = g_strsplit_set(query_fold, " \t\r\n", -1);
    gboolean matches = TRUE;

    for (guint i = 0; terms && terms[i]; i++) {
        const gchar *term = terms[i];
        if (!term || !*term)
            continue;
        if (!g_strstr_len(title_fold, -1, term) &&
            !g_strstr_len(uri_fold, -1, term)) {
            matches = FALSE;
            break;
        }
    }

    g_strfreev(terms);
    g_free(uri_fold);
    g_free(title_fold);
    g_free(query_fold);
    g_free(query_copy);
    return matches;
}

static GtkWidget *nion_bookmark_row_new(NionApp *app, NionBookmark *bookmark)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);

    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(labels, TRUE);
    GtkWidget *title = gtk_label_new(bookmark->title ? bookmark->title : bookmark->uri);
    GtkWidget *uri = gtk_label_new(bookmark->uri ? bookmark->uri : "");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(title, "heading");
    gtk_widget_add_css_class(uri, "nion-muted");
    gtk_box_append(GTK_BOX(labels), title);
    gtk_box_append(GTK_BOX(labels), uri);

    GtkWidget *open = gtk_button_new_with_label("Open");
    GtkWidget *rename = gtk_button_new_with_label("Rename");
    GtkWidget *remove = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(remove, "destructive-action");
    gtk_widget_set_valign(open, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(rename, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);

    g_object_set_data(G_OBJECT(open), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(rename), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(remove), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(row), "nion-bookmark", bookmark);
    g_signal_connect(open, "clicked", G_CALLBACK(on_bookmark_open_clicked), app);
    g_signal_connect(rename, "clicked", G_CALLBACK(on_bookmark_rename_clicked), app);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_bookmark_delete_clicked), app);

    gtk_box_append(GTK_BOX(content), labels);
    gtk_box_append(GTK_BOX(content), open);
    gtk_box_append(GTK_BOX(content), rename);
    gtk_box_append(GTK_BOX(content), remove);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

static void nion_refresh_bookmarks_window(NionApp *app)
{
    if (!app || !app->bookmarks_list)
        return;

    const gchar *query_text = app->bookmarks_search_entry
        ? gtk_editable_get_text(GTK_EDITABLE(app->bookmarks_search_entry))
        : "";
    gchar *query = g_strdup(query_text ? query_text : "");
    g_strstrip(query);
    gboolean searching = *query != '\0';
    guint total = app->bookmarks ? app->bookmarks->len : 0;
    guint matches = 0;

    GtkWidget *row = gtk_widget_get_first_child(app->bookmarks_list);
    while (row) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        gtk_list_box_remove(GTK_LIST_BOX(app->bookmarks_list), row);
        row = next;
    }

    if (app->bookmarks) {
        for (guint i = 0; i < app->bookmarks->len; i++) {
            NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
            if (bookmark && nion_bookmark_matches_search(bookmark, query)) {
                gtk_list_box_append(GTK_LIST_BOX(app->bookmarks_list),
                                    nion_bookmark_row_new(app, bookmark));
                matches++;
            }
        }
    }

    if (app->bookmarks_empty_label) {
        if (total == 0)
            gtk_label_set_text(GTK_LABEL(app->bookmarks_empty_label),
                               "No bookmarks yet. Press Ctrl+D on a website to add one.");
        else if (searching && matches == 0)
            gtk_label_set_text(GTK_LABEL(app->bookmarks_empty_label),
                               "No bookmarks match your search.");
        gtk_widget_set_visible(app->bookmarks_empty_label, matches == 0);
    }

    if (app->bookmarks_result_label) {
        gchar *summary = NULL;
        if (searching)
            summary = g_strdup_printf("%u of %u", matches, total);
        else
            summary = g_strdup_printf("%u bookmark%s", total, total == 1 ? "" : "s");
        gtk_label_set_text(GTK_LABEL(app->bookmarks_result_label), summary);
        g_free(summary);
    }
    g_free(query);
}

static void on_bookmarks_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    (void)entry;
    nion_refresh_bookmarks_window(user_data);
}

static gboolean on_bookmarks_window_close_request(GtkWindow *window, gpointer user_data)
{
    NionApp *app = user_data;
    if (app && app->bookmarks_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->bookmarks_search_entry), "");
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void on_bookmarks_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (app && app->bookmarks_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->bookmarks_search_entry), "");
    if (app && app->bookmarks_window)
        gtk_widget_set_visible(app->bookmarks_window, FALSE);
}

static const gchar *nion_current_bookmarkable_uri(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page)
        return NULL;

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    if (!uri || !*uri || g_str_equal(uri, "about:blank") ||
        strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return NULL;

    gchar *validation = NULL;
    gboolean valid = nion_validate_uri(uri, &validation);
    g_free(validation);
    return valid ? uri : NULL;
}

static void nion_update_bookmark_button(NionApp *app)
{
    if (!app || !app->bookmark_button)
        return;

    const gchar *uri = nion_current_bookmarkable_uri(app);
    gboolean bookmarkable = uri != NULL;
    gboolean bookmarked = bookmarkable && nion_bookmark_uri_exists(app, uri);

    gtk_widget_set_sensitive(app->bookmark_button, bookmarkable);
    gtk_button_set_icon_name(GTK_BUTTON(app->bookmark_button),
                             bookmarked ? "starred-symbolic" : "non-starred-symbolic");

    gtk_widget_remove_css_class(app->bookmark_button, "nion-bookmark-active");
    if (bookmarked)
        gtk_widget_add_css_class(app->bookmark_button, "nion-bookmark-active");

    if (!bookmarkable)
        gtk_widget_set_tooltip_text(app->bookmark_button, "This page cannot be bookmarked");
    else if (bookmarked)
        gtk_widget_set_tooltip_text(app->bookmark_button, "Remove this page from bookmarks");
    else
        gtk_widget_set_tooltip_text(app->bookmark_button, "Bookmark this page (Ctrl+D)");
}

static void nion_toggle_current_bookmark(NionApp *app)
{
    const gchar *uri = nion_current_bookmarkable_uri(app);
    if (!uri) {
        nion_update_bookmark_button(app);
        return;
    }

    if (nion_bookmark_uri_exists(app, uri)) {
        for (guint i = 0; app->bookmarks && i < app->bookmarks->len; i++) {
            NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
            if (bookmark && g_strcmp0(bookmark->uri, uri) == 0) {
                g_ptr_array_remove_index(app->bookmarks, i);
                nion_save_bookmarks(app);
                nion_refresh_bookmarks_window(app);
                nion_set_status(app, app->tor_ready
                    ? "● TOR CONNECTED — BOOKMARK REMOVED"
                    : "○ TOR NOT READY — BOOKMARK REMOVED");
                nion_update_bookmark_button(app);
                return;
            }
        }
    }

    nion_add_current_bookmark(app);
    nion_update_bookmark_button(app);
}

static void on_bookmark_toolbar_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_toggle_current_bookmark(user_data);
}

static void nion_add_current_bookmark(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NOTHING TO BOOKMARK"
            : "○ TOR NOT READY — NOTHING TO BOOKMARK");
        return;
    }

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    gchar *validation = NULL;
    if (!uri || !*uri || strlen(uri) > NION_MAX_SAVED_URI_BYTES ||
        !nion_validate_uri(uri, &validation)) {
        g_free(validation);
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — THIS PAGE CANNOT BE BOOKMARKED"
            : "○ TOR NOT READY — THIS PAGE CANNOT BE BOOKMARKED");
        return;
    }
    g_free(validation);

    if (!app->bookmarks)
        app->bookmarks = g_ptr_array_new_with_free_func(nion_bookmark_free);
    if (nion_bookmark_uri_exists(app, uri)) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — ALREADY BOOKMARKED"
            : "○ TOR NOT READY — ALREADY BOOKMARKED");
        return;
    }
    if (app->bookmarks->len >= NION_MAX_BOOKMARKS) {
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK LIMIT REACHED");
        return;
    }

    NionBookmark *bookmark = g_new0(NionBookmark, 1);
    bookmark->uri = g_strdup(uri);
    bookmark->title = nion_bookmark_title_normalize(
        webkit_web_view_get_title(tab->web_view), uri);
    g_ptr_array_add(app->bookmarks, bookmark);
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    nion_update_bookmark_button(app);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — BOOKMARK ADDED"
        : "○ TOR NOT READY — BOOKMARK ADDED");
}

static void on_bookmarks_add_current_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_add_current_bookmark(user_data);
}

static void nion_show_bookmarks(NionApp *app)
{
    if (!app)
        return;

    if (!app->bookmarks_window) {
        GtkWidget *window = gtk_window_new();
        app->bookmarks_window = window;
        gtk_window_set_title(GTK_WINDOW(window), "NiOn Bookmarks");
        gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
        gtk_window_set_default_size(GTK_WINDOW(window), 720, 520);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(root, 16);
        gtk_widget_set_margin_bottom(root, 16);
        gtk_widget_set_margin_start(root, 16);
        gtk_widget_set_margin_end(root, 16);
        gtk_window_set_child(GTK_WINDOW(window), root);

        GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *heading = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(heading), "<b>Bookmarks</b>");
        gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
        gtk_widget_set_hexpand(heading, TRUE);
        GtkWidget *add = gtk_button_new_with_label("Bookmark Current Page");
        gtk_box_append(GTK_BOX(header), heading);
        gtk_box_append(GTK_BOX(header), add);
        gtk_box_append(GTK_BOX(root), header);

        GtkWidget *note = gtk_label_new(
            "Bookmarks are stored locally in your NiOn profile. Double-click a row or use Open to launch it in a new tab.");
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_add_css_class(note, "nion-muted");
        gtk_box_append(GTK_BOX(root), note);

        GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        app->bookmarks_search_entry = gtk_search_entry_new();
        g_object_set(app->bookmarks_search_entry,
                     "placeholder-text", "Search bookmarks by title or URL",
                     NULL);
        gtk_widget_set_hexpand(app->bookmarks_search_entry, TRUE);
        gtk_search_entry_set_key_capture_widget(
            GTK_SEARCH_ENTRY(app->bookmarks_search_entry), window);
        app->bookmarks_result_label = gtk_label_new("0 bookmarks");
        gtk_widget_add_css_class(app->bookmarks_result_label, "nion-muted");
        gtk_widget_set_valign(app->bookmarks_result_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(search_row), app->bookmarks_search_entry);
        gtk_box_append(GTK_BOX(search_row), app->bookmarks_result_label);
        gtk_box_append(GTK_BOX(root), search_row);

        app->bookmarks_empty_label = gtk_label_new("No bookmarks yet. Press Ctrl+D on a website to add one.");
        gtk_widget_set_margin_top(app->bookmarks_empty_label, 28);
        gtk_widget_set_margin_bottom(app->bookmarks_empty_label, 28);
        gtk_widget_add_css_class(app->bookmarks_empty_label, "nion-muted");

        app->bookmarks_list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->bookmarks_list), GTK_SELECTION_SINGLE);
        gtk_list_box_set_show_separators(GTK_LIST_BOX(app->bookmarks_list), TRUE);
        gtk_list_box_set_placeholder(GTK_LIST_BOX(app->bookmarks_list), app->bookmarks_empty_label);
        gtk_widget_add_css_class(app->bookmarks_list, "boxed-list");

        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->bookmarks_list);
        gtk_box_append(GTK_BOX(root), scroll);

        GtkWidget *close = gtk_button_new_with_label("Close");
        gtk_widget_set_halign(close, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(root), close);

        g_signal_connect(add, "clicked", G_CALLBACK(on_bookmarks_add_current_clicked), app);
        g_signal_connect(close, "clicked", G_CALLBACK(on_bookmarks_close_clicked), app);
        g_signal_connect(app->bookmarks_search_entry, "search-changed",
                         G_CALLBACK(on_bookmarks_search_changed), app);
        g_signal_connect(app->bookmarks_list, "row-activated", G_CALLBACK(on_bookmark_row_activated), app);
        g_signal_connect(window, "close-request", G_CALLBACK(on_bookmarks_window_close_request), app);
    }

    nion_refresh_bookmarks_window(app);
    gtk_window_present(GTK_WINDOW(app->bookmarks_window));
}

static void nion_cancel_active_downloads(NionApp *app)
{
    if (!app->downloads_list)
        return;

    for (GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (!item || item->finished || item->failed || !item->download)
            continue;

        item->cancel_requested = TRUE;
        gtk_widget_set_sensitive(item->action_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(item->action_button), "Cancelling…");
        webkit_download_cancel(item->download);
    }
}

static void nion_close_http_warning(NionTab *tab)
{
    if (!tab || !tab->http_warning_window)
        return;

    GtkWidget *window = tab->http_warning_window;
    tab->http_warning_window = NULL;
    gtk_window_destroy(GTK_WINDOW(window));
}

static void nion_cancel_http_warning_decision(NionTab *tab)
{
    if (!tab || !tab->http_warning_decision)
        return;

    webkit_policy_decision_ignore(tab->http_warning_decision);
    g_clear_object(&tab->http_warning_decision);
}

static void on_http_warning_cancel(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab)
        return;

    nion_cancel_http_warning_decision(tab);
    g_clear_pointer(&tab->http_warning_uri, g_free);
    nion_close_http_warning(tab);
    if (tab->app && tab->app->tor_ready)
        nion_set_status(tab->app, "● TOR CONNECTED — HTTP navigation cancelled");
}

static void on_http_warning_continue(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->http_warning_decision || !tab->http_warning_uri)
        return;

    gchar *origin = nion_http_origin_key(tab->http_warning_uri);
    g_free(tab->http_allowed_origin);
    tab->http_allowed_origin = origin;

    webkit_policy_decision_use(tab->http_warning_decision);
    g_clear_object(&tab->http_warning_decision);
    g_clear_pointer(&tab->http_warning_uri, g_free);
    nion_close_http_warning(tab);

    if (tab->app && tab->app->tor_ready)
        nion_set_status(tab->app, "● TOR CONNECTED — plain HTTP allowed for this tab");
}

static gboolean on_http_warning_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    NionTab *tab = user_data;
    if (tab) {
        tab->http_warning_window = NULL;
        nion_cancel_http_warning_decision(tab);
        g_clear_pointer(&tab->http_warning_uri, g_free);
        if (tab->app && tab->app->tor_ready)
            nion_set_status(tab->app, "● TOR CONNECTED — HTTP navigation cancelled");
    }
    return FALSE;
}

static void nion_show_http_warning(NionTab *tab, const gchar *uri)
{
    if (!tab || !tab->app || !uri || !*uri)
        return;

    if (tab->http_warning_window) {
        gtk_window_present(GTK_WINDOW(tab->http_warning_window));
        return;
    }

    g_free(tab->http_warning_uri);
    tab->http_warning_uri = g_strdup(uri);

    GError *parse_error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &parse_error);
    const gchar *host = parsed ? g_uri_get_host(parsed) : NULL;

    GtkWidget *window = gtk_window_new();
    tab->http_warning_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Unencrypted clearnet connection");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(tab->app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Unencrypted clearnet connection</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    gchar *message_text = g_strdup_printf(
        "NiOn is about to open %s over plain HTTP. Tor still carries the request to an exit relay, "
        "but the connection between the exit relay and this clearnet website is not protected by HTTPS.",
        (host && *host) ? host : "this clearnet website");
    GtkWidget *message = gtk_label_new(message_text);
    g_free(message_text);
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *url_label = gtk_label_new(uri);
    gtk_label_set_wrap(GTK_LABEL(url_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(url_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(url_label), 0.0f);
    gtk_widget_add_css_class(url_label, "nion-muted");
    gtk_box_append(GTK_BOX(box), url_label);

    GtkWidget *note = gtk_label_new(
        "Continue allows plain HTTP for this clearnet site in the current tab until you leave it. "
        "HTTP remains allowed for .onion services without this warning.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *continue_button = gtk_button_new_with_label("Continue");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), continue_button);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_http_warning_cancel), tab);
    g_signal_connect(continue_button, "clicked", G_CALLBACK(on_http_warning_continue), tab);
    g_signal_connect(window, "close-request", G_CALLBACK(on_http_warning_close_request), tab);

    if (parsed)
        g_uri_unref(parsed);
    g_clear_error(&parse_error);

    nion_set_status(tab->app, "● TOR CONNECTED — HTTP WARNING");
    gtk_window_present(GTK_WINDOW(window));
}

static gboolean on_webview_decide_policy(WebKitWebView *web_view,
                                         WebKitPolicyDecision *decision,
                                         WebKitPolicyDecisionType decision_type,
                                         gpointer user_data)
{
    (void)web_view;
    NionTab *tab = user_data;
    NionApp *app = tab->app;

    if (decision_type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *response_decision = WEBKIT_RESPONSE_POLICY_DECISION(decision);

        if (!webkit_response_policy_decision_is_mime_type_supported(response_decision)) {
            if (!app->tor_ready) {
                webkit_policy_decision_ignore(decision);
                nion_set_status(app, "○ TOR OFFLINE — DOWNLOAD BLOCKED");
                return TRUE;
            }

            webkit_policy_decision_download(decision);
            return TRUE;
        }

        return FALSE;
    }

    if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        decision_type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
        return FALSE;

    WebKitNavigationAction *action = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    WebKitURIRequest *request = action ? webkit_navigation_action_get_request(action) : NULL;
    const gchar *uri = request ? webkit_uri_request_get_uri(request) : NULL;

    if (!uri || g_str_equal(uri, "about:blank"))
        return FALSE;

    if (!app->tor_ready) {
        webkit_policy_decision_ignore(decision);
        nion_show_error_page(tab,
                             "Tor error",
                             "Tor is not available",
                             app->tor_last_log ? app->tor_last_log
                                               : "NiOn blocks web navigation until Tor is connected.",
                             uri,
                             TRUE);
        nion_set_status(app, "○ TOR OFFLINE — navigation blocked");
        nion_update_controls(app);
        return TRUE;
    }

    /* A real navigation after a provisional onion cancellation supersedes
     * the pending retry. This prevents a stale timer from pulling the tab
     * back to the old onion URL. */
    if (tab->retry_source_id && g_strcmp0(uri, tab->retry_uri) != 0)
        nion_clear_retry(tab);

    gchar *validation = NULL;
    if (!nion_validate_uri(uri, &validation)) {
        webkit_policy_decision_ignore(decision);
        nion_show_error_page(tab,
                             "Address error",
                             nion_uri_is_onion(uri) ? "Invalid .onion address" : "Unsupported address",
                             validation ? validation : "NiOn cannot open this address.",
                             uri,
                             FALSE);
        nion_set_status(app, "● TOR CONNECTED — ADDRESS ERROR");
        g_free(validation);
        nion_update_controls(app);
        return TRUE;
    }

    if (decision_type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        nion_uri_is_http_clearnet(uri)) {
        gchar *origin = nion_http_origin_key(uri);
        gboolean already_allowed = origin && tab->http_allowed_origin &&
            g_strcmp0(origin, tab->http_allowed_origin) == 0;
        g_free(origin);

        if (!already_allowed) {
            if (tab->http_warning_decision) {
                webkit_policy_decision_ignore(decision);
                nion_update_controls(app);
                return TRUE;
            }

            tab->http_warning_decision = g_object_ref(decision);
            nion_show_http_warning(tab, uri);
            nion_update_controls(app);
            return TRUE;
        }
    }

    if (tab->error_page) {
        tab->error_page = FALSE;
        tab->load_failed = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
    }

    return FALSE;
}


static void nion_set_boolean_setting_if_present(WebKitSettings *settings,
                                                const gchar *property_name,
                                                gboolean value)
{
    if (!settings || !property_name)
        return;

    GParamSpec *property = g_object_class_find_property(
        G_OBJECT_GET_CLASS(settings), property_name);
    if (property && G_IS_PARAM_SPEC_BOOLEAN(property))
        g_object_set(settings, property_name, value, NULL);
}

static void nion_apply_privacy_settings(WebKitSettings *settings)
{
    if (!settings)
        return;

    /* Keep normal JavaScript and HTML5 storage for real-world site/login
     * compatibility, but remove high-risk APIs and prefetch paths. */
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);

    /* Network / leak surface. */
    webkit_settings_set_enable_webrtc(settings, FALSE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    webkit_settings_set_enable_dns_prefetching(settings, FALSE);

    /* Fingerprinting / device surface. Canvas 2D itself cannot currently be
     * disabled with a stable WebKitGTK setting without breaking broad web
     * compatibility; GPU-backed surfaces are reduced here instead. */
    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-accelerated-2d-canvas", FALSE);

    /* Explicit user/device access restrictions. */
    webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);

    /* Smaller passive/legacy attack surface. Some legacy properties were
     * removed from newer GTK4/WebKitGTK API surfaces, so probe them at
     * runtime instead of depending on removed C setters. */
    webkit_settings_set_enable_hyperlink_auditing(settings, FALSE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_enable_encrypted_media(settings, FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-plugins", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-java", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-offline-web-application-cache", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-mock-capture-devices", FALSE);
    nion_set_boolean_setting_if_present(settings, "allow-top-navigation-to-data-urls", FALSE);
    nion_set_boolean_setting_if_present(settings, "disable-web-security", FALSE);
}

static void nion_print_tab(NionTab *tab)
{
    if (!tab || !tab->app || !tab->web_view)
        return;

    NionApp *app = tab->app;
    if (tab->home_page) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NEW TAB HAS NOTHING TO PRINT"
            : "○ TOR NOT READY — NEW TAB HAS NOTHING TO PRINT");
        return;
    }

    WebKitPrintOperation *operation = webkit_print_operation_new(tab->web_view);
    WebKitPrintOperationResponse response = webkit_print_operation_run_dialog(
        operation, GTK_WINDOW(app->window));

    if (response == WEBKIT_PRINT_OPERATION_RESPONSE_PRINT)
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — PRINT / PDF JOB STARTED"
            : "○ TOR NOT READY — PRINT / PDF JOB STARTED");
    else if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — PRINT CANCELLED");

    g_object_unref(operation);
}

static void nion_context_menu_relabel_stock(WebKitContextMenu *context_menu,
                                             WebKitContextMenuAction stock_action,
                                             const gchar *label)
{
    GList *items = webkit_context_menu_get_items(context_menu);
    gint position = 0;

    for (GList *node = items; node; node = node->next, position++) {
        WebKitContextMenuItem *item = node->data;
        if (webkit_context_menu_item_get_stock_action(item) != stock_action)
            continue;

        WebKitContextMenuItem *replacement =
            webkit_context_menu_item_new_from_stock_action_with_label(stock_action, label);
        webkit_context_menu_remove(context_menu, item);
        webkit_context_menu_insert(context_menu, replacement, position);
        g_object_unref(replacement);
        break;
    }

    g_list_free(items);
}

static gboolean on_webview_context_menu(WebKitWebView *web_view,
                                        WebKitContextMenu *context_menu,
                                        GdkEvent *event,
                                        WebKitHitTestResult *hit_test_result,
                                        gpointer user_data)
{
    (void)web_view;
    (void)event;
    (void)hit_test_result;
    NionTab *tab = user_data;

    /* Keep WebKit's context-sensitive actions (editing, media controls,
     * link/image downloads, spelling, etc.) and only adapt browser-facing
     * wording to NiOn's tab model. The stock "new window" actions route
     * through WebView::create, which NiOn already maps to a new tab. */
    nion_context_menu_relabel_stock(context_menu,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK_IN_NEW_WINDOW,
        "Open Link in New Tab");
    nion_context_menu_relabel_stock(context_menu,
        WEBKIT_CONTEXT_MENU_ACTION_OPEN_IMAGE_IN_NEW_WINDOW,
        "Open Image in New Tab");
    nion_context_menu_relabel_stock(context_menu,
        WEBKIT_CONTEXT_MENU_ACTION_DOWNLOAD_IMAGE_TO_DISK,
        "Save Image");

    WebKitContextMenuItem *separator = webkit_context_menu_item_new_separator();
    webkit_context_menu_append(context_menu, separator);
    g_object_unref(separator);

    GAction *print_action = g_action_map_lookup_action(
        G_ACTION_MAP(tab->app->application), "print");
    if (print_action) {
        WebKitContextMenuItem *print_item = webkit_context_menu_item_new_from_gaction(
            print_action, "Print / Save as PDF…", NULL);
        webkit_context_menu_append(context_menu, print_item);
        g_object_unref(print_item);
    }

    return FALSE;
}

static gboolean on_permission_request(WebKitWebView *web_view,
                                      WebKitPermissionRequest *request,
                                      gpointer user_data)
{
    (void)web_view;
    (void)user_data;
    webkit_permission_request_deny(request);
    return TRUE;
}

static WebKitWebView *on_webview_create(WebKitWebView *web_view,
                                        WebKitNavigationAction *navigation_action,
                                        gpointer user_data)
{
    (void)web_view;
    (void)navigation_action;
    NionApp *app = user_data;

    /* Empty string creates an unloaded WebView, so target=_blank does not
     * first load NiOn's home page and cancel it immediately. */
    NionTab *tab = nion_new_tab(app, "", TRUE);
    if (tab) {
        tab->home_page = FALSE;
        tab->error_page = FALSE;
    }
    return tab ? tab->web_view : NULL;
}

static void on_notebook_switch_page(GtkNotebook *notebook,
                                    GtkWidget *page,
                                    guint page_num,
                                    gpointer user_data)
{
    (void)notebook;
    (void)page;
    (void)page_num;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);

    if (tab && app->tor_ready && webkit_web_view_is_loading(tab->web_view) &&
        !tab->home_page && !tab->error_page) {
        gint percent = (gint)(webkit_web_view_get_estimated_load_progress(tab->web_view) * 100.0 + 0.5);
        gchar *status = g_strdup_printf("● TOR CONNECTED — loading %d%%", percent);
        nion_set_status(app, status);
        g_free(status);
    } else if (tab && tab->error_page) {
        /* Keep the error category already shown in the page/status when possible. */
    } else if (app->tor_ready) {
        nion_set_status(app, nion_tab_has_mixed_content(tab)
            ? "● TOR CONNECTED — MIXED CONTENT DETECTED"
            : "● TOR CONNECTED");
    }

    nion_update_controls(app);
    if (app->find_bar && gtk_widget_get_visible(app->find_bar))
        nion_find_run(app);
    nion_schedule_session_save(app);
}

static void on_notebook_page_reordered(GtkNotebook *notebook,
                                       GtkWidget *child,
                                       guint page_num,
                                       gpointer user_data)
{
    (void)notebook;
    (void)child;
    (void)page_num;
    nion_schedule_session_save(user_data);
}

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    NionTab *tab = g_new0(NionTab, 1);
    tab->app = app;

    WebKitSettings *settings = webkit_settings_new();
    nion_apply_privacy_settings(settings);

    tab->web_view = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "network-session", app->network_session,
        "settings", settings,
        NULL));
    g_object_unref(settings);

    tab->page = GTK_WIDGET(tab->web_view);
    g_object_set_data_full(G_OBJECT(tab->page), "nion-tab", tab, nion_tab_free);
    g_object_set_data(G_OBJECT(tab->web_view), "nion-tab-pointer", tab);

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

    nion_update_controls(app);
    nion_schedule_session_save(app);
    return tab;
}

static void on_new_tab_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_new_tab(user_data, NULL, TRUE);
}

static void action_new_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_new_tab(user_data, NULL, TRUE);
}

static void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionTab *tab = nion_current_tab(user_data);
    if (tab)
        nion_close_tab(tab);
}

static void action_reopen_closed_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_reopen_closed_tab(user_data);
}

static void action_focus_location(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    if (app->tor_ready) {
        gtk_widget_grab_focus(app->address);
        gtk_editable_select_region(GTK_EDITABLE(app->address), 0, -1);
    }
}

static void action_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_clear_retry(tab);
        tab->load_failed = FALSE;
        tab->error_page = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
        webkit_web_view_reload(tab->web_view);
    }
}

static void action_hard_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_clear_retry(tab);
        tab->load_failed = FALSE;
        tab->error_page = FALSE;
        g_clear_pointer(&tab->display_uri_override, g_free);
        webkit_web_view_reload_bypass_cache(tab->web_view);
        nion_set_status(app, "● TOR CONNECTED — HARD RELOAD");
    }
}

static void nion_set_zoom(NionApp *app, gdouble zoom)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab)
        return;

    gint percent = nion_zoom_percent(zoom);
    zoom = (gdouble)percent / 100.0;
    webkit_web_view_set_zoom_level(tab->web_view, zoom);

    if (!tab->home_page && !tab->error_page) {
        const gchar *uri = webkit_web_view_get_uri(tab->web_view);
        gchar *key = nion_site_zoom_key_for_uri(uri);
        if (key) {
            if (nion_remember_site_zoom(app, key, percent))
                nion_save_site_zoom(app);
            g_free(key);
        }
    }

    gchar *status = g_strdup_printf(app->tor_ready
        ? "● TOR CONNECTED — ZOOM %d%%"
        : "○ TOR NOT READY — ZOOM %d%%",
        percent);
    nion_set_status(app, status);
    g_free(status);
}

static void action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab)
        nion_set_zoom(app, webkit_web_view_get_zoom_level(tab->web_view) + 0.1);
}

static void action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab)
        nion_set_zoom(app, webkit_web_view_get_zoom_level(tab->web_view) - 0.1);
}

static void action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    nion_set_zoom(user_data, 1.0);
}

static void nion_find_set_result(NionApp *app, const gchar *text)
{
    if (app && app->find_match_label)
        gtk_label_set_text(GTK_LABEL(app->find_match_label), text ? text : "");
}

static void on_find_found(WebKitFindController *controller, guint match_count, gpointer user_data)
{
    (void)controller;
    NionTab *tab = user_data;
    if (!tab || nion_current_tab(tab->app) != tab || !gtk_widget_get_visible(tab->app->find_bar))
        return;
    gchar *label = NULL;
    if (match_count == G_MAXUINT)
        label = g_strdup("1000+ matches");
    else if (match_count == 1)
        label = g_strdup("1 match");
    else
        label = g_strdup_printf("%u matches", match_count);
    nion_find_set_result(tab->app, label);
    g_free(label);
}

static void on_find_failed(WebKitFindController *controller, gpointer user_data)
{
    (void)controller;
    NionTab *tab = user_data;
    if (tab && nion_current_tab(tab->app) == tab && gtk_widget_get_visible(tab->app->find_bar))
        nion_find_set_result(tab->app, "No matches");
}

static void nion_find_run(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || !app->find_entry)
        return;
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(app->find_entry));
    WebKitFindController *controller = webkit_web_view_get_find_controller(tab->web_view);
    if (!text || !*text) {
        webkit_find_controller_search_finish(controller);
        nion_find_set_result(app, "");
        return;
    }
    webkit_find_controller_search(controller, text,
        WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND, 1000);
}

static void nion_find_open(NionApp *app)
{
    if (!app || !app->find_bar)
        return;
    gtk_widget_set_visible(app->find_bar, TRUE);
    gtk_widget_grab_focus(app->find_entry);
    gtk_editable_select_region(GTK_EDITABLE(app->find_entry), 0, -1);
    nion_find_run(app);
}

static void nion_find_close(NionApp *app)
{
    if (!app || !app->find_bar)
        return;
    NionTab *tab = nion_current_tab(app);
    if (tab)
        webkit_find_controller_search_finish(webkit_web_view_get_find_controller(tab->web_view));
    gtk_widget_set_visible(app->find_bar, FALSE);
    nion_find_set_result(app, "");
    if (tab)
        gtk_widget_grab_focus(GTK_WIDGET(tab->web_view));
}

static void action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    nion_find_open(user_data);
}

static void on_find_entry_changed(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    nion_find_run(user_data);
}

static void on_find_prev_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab)
        webkit_find_controller_search_previous(webkit_web_view_get_find_controller(tab->web_view));
}

static void on_find_next_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab)
        webkit_find_controller_search_next(webkit_web_view_get_find_controller(tab->web_view));
}

static void on_find_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_find_close(user_data);
}

static gboolean on_find_key_pressed(GtkEventControllerKey *controller,
                                    guint keyval, guint keycode,
                                    GdkModifierType state, gpointer user_data)
{
    (void)controller; (void)keycode;
    NionApp *app = user_data;
    if (keyval == GDK_KEY_Escape) {
        nion_find_close(app);
        return TRUE;
    }
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        NionTab *tab = nion_current_tab(app);
        if (!tab)
            return TRUE;
        WebKitFindController *find = webkit_web_view_get_find_controller(tab->web_view);
        if (state & GDK_SHIFT_MASK)
            webkit_find_controller_search_previous(find);
        else
            webkit_find_controller_search_next(find);
        return TRUE;
    }
    return FALSE;
}

static void nion_sync_fullscreen_chrome(NionApp *app)
{
    gboolean fullscreen = gtk_window_is_fullscreen(GTK_WINDOW(app->window));
    if (app->toolbar)
        gtk_widget_set_visible(app->toolbar, !fullscreen);
    if (app->status_label)
        gtk_widget_set_visible(app->status_label, !fullscreen);
    if (app->notebook)
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(app->notebook), !fullscreen);
    if (fullscreen) {
        if (app->progress_bar)
            gtk_widget_set_visible(app->progress_bar, FALSE);
        nion_find_close(app);
    } else {
        nion_update_controls(app);
    }
}

static void on_window_fullscreen_notify(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    (void)object; (void)pspec;
    nion_sync_fullscreen_chrome(user_data);
}

static void action_print(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    nion_print_tab(nion_current_tab(app));
}

static void action_fullscreen(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    NionApp *app = user_data;
    if (gtk_window_is_fullscreen(GTK_WINDOW(app->window)))
        gtk_window_unfullscreen(GTK_WINDOW(app->window));
    else
        gtk_window_fullscreen(GTK_WINDOW(app->window));
}

static void action_back(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready && webkit_web_view_can_go_back(tab->web_view))
        webkit_web_view_go_back(tab->web_view);
}

static void action_forward(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready && webkit_web_view_can_go_forward(tab->web_view))
        webkit_web_view_go_forward(tab->web_view);
}

static void nion_cycle_tab(NionApp *app, gint direction)
{
    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    if (pages <= 1)
        return;

    gint current = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    gint next = (current + direction + pages) % pages;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), next);
}

static void action_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_cycle_tab(user_data, 1);
}

static void action_previous_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_cycle_tab(user_data, -1);
}

static void action_downloads(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_downloads(user_data);
}

static void action_bookmark_page(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_add_current_bookmark(user_data);
}

static void action_bookmarks(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_bookmarks(user_data);
}

static void action_exit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_request_close(user_data);
}

static gboolean on_about_activate_link(GtkAboutDialog *dialog,
                                              const gchar *uri,
                                              gpointer user_data)
{
    NionApp *app = user_data;
    if (!app || !uri || !*uri)
        return TRUE;

    /* Never hand About links to the desktop's default browser. Repository and
     * future HTTP(S) links stay inside NiOn and therefore keep NiOn's Tor-only
     * network policy. */
    gchar *validation = NULL;
    if (nion_validate_uri(uri, &validation)) {
        nion_new_tab(app, uri, TRUE);
        gtk_window_destroy(GTK_WINDOW(dialog));
    } else if (app->tor_ready) {
        gchar *status = g_strdup_printf("● TOR CONNECTED — ABOUT LINK BLOCKED: %s",
                                        validation ? validation : "unsupported address");
        nion_set_status(app, status);
        g_free(status);
    }
    g_free(validation);
    return TRUE;
}

static void action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    GtkWidget *dialog = gtk_about_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_icon_name(GTK_WINDOW(dialog), NION_APP_ID);

    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "NiOn");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), NION_VERSION);
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog), NION_APP_ID);
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
        "NiOn (Minimal Onion) is a minimal Linux browser for clearnet and .onion sites, with all browsing routed through its bundled Tor runtime.");

    const gchar *authors[] = { "Jeannes Bryan", NULL };
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog), NION_REPOSITORY_URL);
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog), "NiOn source repository");
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog), "Copyright © 2026 Jeannes Bryan");
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dialog),
        "GNU General Public License v3.0 or later (GPL-3.0-or-later)");
    gtk_about_dialog_set_wrap_license(GTK_ABOUT_DIALOG(dialog), TRUE);
    g_signal_connect(dialog, "activate-link", G_CALLBACK(on_about_activate_link), app);

    const gchar *package_mode = g_getenv("APPIMAGE") ? "AppImage" : "development/native";
    gchar *system_info = g_strdup_printf(
        "NiOn version: %s\n"
        "Author: Jeannes Bryan\n"
        "Repository: %s\n"
        "Package: %s\n"
        "License: GPL-3.0-or-later\n"
        "Status: %s\n\n"
        "Runtime\n"
        "Tor status: %s\n"
        "SOCKS: 127.0.0.1:%u\n\n"
        "Dependencies\n"
        "GTK: %u.%u.%u\n"
        "WebKitGTK: %u.%u.%u\n"
        "libsoup: %u.%u.%u\n"
        "GLib: %u.%u.%u\n"
        "Tor: %s (Expert Bundle %s)",
        NION_VERSION,
        NION_REPOSITORY_URL,
        package_mode,
        NION_RELEASE_STATUS,
        app->tor_ready ? "connected" : (app->tor_failed ? "error" : "connecting"),
        app->tor_socks_port,
        gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
        webkit_get_major_version(), webkit_get_minor_version(), webkit_get_micro_version(),
        soup_get_major_version(), soup_get_minor_version(), soup_get_micro_version(),
        glib_major_version, glib_minor_version, glib_micro_version,
        NION_TOR_DAEMON_VERSION, NION_TOR_BROWSER_BUNDLE_VERSION);
    gtk_about_dialog_set_system_information(GTK_ABOUT_DIALOG(dialog), system_info);
    g_free(system_info);

    gtk_window_present(GTK_WINDOW(dialog));
}

static void on_preferences_cancel_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void on_preferences_save_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!root || !GTK_IS_WINDOW(root))
        return;

    GtkWindow *window = GTK_WINDOW(root);
    GtkCheckButton *restore_check = g_object_get_data(G_OBJECT(window), "nion-restore-check");
    GtkCheckButton *third_party_check = g_object_get_data(G_OBJECT(window), "nion-third-party-check");
    GtkDropDown *search_dropdown = g_object_get_data(G_OBJECT(window), "nion-search-dropdown");
    if (!restore_check || !third_party_check || !search_dropdown)
        return;

    gboolean old_restore = app->restore_session;
    app->restore_session = gtk_check_button_get_active(restore_check);
    app->block_third_party_cookies = gtk_check_button_get_active(third_party_check);

    const gchar *search_ids[] = { "duckduckgo", "brave", "startpage" };
    guint selected = gtk_drop_down_get_selected(search_dropdown);
    if (selected >= G_N_ELEMENTS(search_ids))
        selected = 0;
    g_free(app->search_engine);
    app->search_engine = g_strdup(search_ids[selected]);

    nion_save_preferences(app);
    nion_apply_cookie_policy(app);

    if (!app->restore_session) {
        if (app->session_save_source_id) {
            g_source_remove(app->session_save_source_id);
            app->session_save_source_id = 0;
        }
        g_unlink(app->session_file);
    } else {
        nion_save_session(app, FALSE);
    }

    if (app->tor_ready) {
        if (old_restore != app->restore_session) {
            nion_set_status(app, app->restore_session
                ? "● TOR CONNECTED — TAB RESTORE ENABLED"
                : "● TOR CONNECTED — TAB RESTORE DISABLED");
        } else {
            nion_set_status(app, "● TOR CONNECTED — PREFERENCES SAVED");
        }
    } else {
        nion_set_status(app, "○ TOR NOT READY — PREFERENCES SAVED");
    }

    gtk_window_destroy(window);
}

static void action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    if (app->preferences_window) {
        gtk_window_present(GTK_WINDOW(app->preferences_window));
        return;
    }

    GtkWidget *window = gtk_window_new();
    app->preferences_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->preferences_window);

    gtk_window_set_title(GTK_WINDOW(window), "NiOn Preferences");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 480, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Browsing, Session &amp; Privacy</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *restore = gtk_check_button_new_with_label("Restore tabs when NiOn starts");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(restore), app->restore_session);
    gtk_box_append(GTK_BOX(box), restore);

    GtkWidget *third_party = gtk_check_button_new_with_label("Block third-party cookies");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(third_party), app->block_third_party_cookies);
    gtk_box_append(GTK_BOX(box), third_party);


    GtkWidget *search_heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(search_heading), "<b>Default search engine</b>");
    gtk_label_set_xalign(GTK_LABEL(search_heading), 0.0f);
    gtk_widget_set_margin_top(search_heading, 4);
    gtk_box_append(GTK_BOX(box), search_heading);

    const gchar *search_names[] = { "DuckDuckGo", "Brave Search", "Startpage", NULL };
    GtkWidget *search_dropdown = gtk_drop_down_new_from_strings(search_names);
    guint search_selected = 0;
    if (app->search_engine && g_str_equal(app->search_engine, "brave"))
        search_selected = 1;
    else if (app->search_engine && g_str_equal(app->search_engine, "startpage"))
        search_selected = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(search_dropdown), search_selected);
    gtk_box_append(GTK_BOX(box), search_dropdown);

    GtkWidget *note = gtk_label_new(
        "Cookies and website data remain persistent between NiOn restarts so website logins can survive. "
        "Blocking third-party cookies may affect some sign-in flows.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data(G_OBJECT(window), "nion-restore-check", restore);
    g_object_set_data(G_OBJECT(window), "nion-third-party-check", third_party);
    g_object_set_data(G_OBJECT(window), "nion-search-dropdown", search_dropdown);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(save, "clicked", G_CALLBACK(on_preferences_save_clicked), app);

    gtk_window_present(GTK_WINDOW(window));
}

static void nion_clear_site_request_free(NionClearSiteRequest *request)
{
    if (!request)
        return;

    g_free(request->host);
    g_free(request->uri);
    g_clear_object(&request->web_view);
    g_list_free_full(request->website_data, (GDestroyNotify)webkit_website_data_unref);
    g_free(request);
}

static gchar *nion_current_site_host(NionApp *app, gchar **uri_out, WebKitWebView **web_view_out)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page)
        return NULL;

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    if (!uri || !*uri || g_str_equal(uri, "about:blank"))
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean supported = scheme && host &&
        (g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0);

    gchar *result = supported ? g_ascii_strdown(host, -1) : NULL;
    if (result && uri_out)
        *uri_out = g_strdup(uri);
    if (result && web_view_out)
        *web_view_out = g_object_ref(tab->web_view);

    g_uri_unref(parsed);
    g_clear_error(&error);
    return result;
}

static gboolean nion_website_data_matches_host(WebKitWebsiteData *data, const gchar *host)
{
    if (!data || !host || !*host)
        return FALSE;

    const gchar *name_raw = webkit_website_data_get_name(data);
    if (!name_raw || !*name_raw || g_str_equal(name_raw, "Local files"))
        return FALSE;

    while (*name_raw == '.')
        name_raw++;
    gchar *name = g_ascii_strdown(name_raw, -1);
    gboolean matches = g_strcmp0(name, host) == 0;

    /* WebKit normally groups website data by domain. If the current page is
     * a subdomain and WebKit reports the parent domain, treat that record as
     * belonging to the current site. Do not sweep sibling/third-party hosts. */
    if (!matches) {
        gsize host_len = strlen(host);
        gsize name_len = strlen(name);
        if (host_len > name_len &&
            g_str_has_suffix(host, name) &&
            host[host_len - name_len - 1] == '.')
            matches = TRUE;
    }

    g_free(name);
    return matches;
}

static void on_clear_site_data_removed(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionClearSiteRequest *request = user_data;
    NionApp *app = request->app;
    GError *error = NULL;
    gboolean ok = webkit_website_data_manager_remove_finish(
        WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);

    if (!ok) {
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? "● TOR CONNECTED — SITE DATA CLEAR FAILED: %s"
                : "○ TOR OFFLINE — SITE DATA CLEAR FAILED: %s",
            error ? error->message : "unknown error");
        nion_set_status(app, status);
        g_free(status);
        g_clear_error(&error);
        nion_clear_site_request_free(request);
        return;
    }

    gchar *status = g_strdup_printf(
        app->tor_ready
            ? "● TOR CONNECTED — DATA CLEARED FOR %s"
            : "○ TOR OFFLINE — DATA CLEARED FOR %s",
        request->host);
    nion_set_status(app, status);
    g_free(status);

    /* Reload only if the same WebView is still the active tab. This makes
     * cookie/storage removal immediately visible without disturbing a tab
     * the user switched away from while the async operation ran. */
    NionTab *current = nion_current_tab(app);
    if (app->tor_ready && current && current->web_view == request->web_view &&
        !current->home_page && !current->error_page)
        webkit_web_view_reload_bypass_cache(request->web_view);

    nion_clear_site_request_free(request);
}

static void on_clear_site_data_fetched(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionClearSiteRequest *request = user_data;
    NionApp *app = request->app;
    WebKitWebsiteDataManager *manager = WEBKIT_WEBSITE_DATA_MANAGER(source);
    GError *error = NULL;
    GList *all_data = webkit_website_data_manager_fetch_finish(manager, result, &error);

    if (error) {
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? "● TOR CONNECTED — SITE DATA LOOKUP FAILED: %s"
                : "○ TOR OFFLINE — SITE DATA LOOKUP FAILED: %s",
            error->message);
        nion_set_status(app, status);
        g_free(status);
        g_clear_error(&error);
        nion_clear_site_request_free(request);
        return;
    }

    for (GList *item = all_data; item; item = item->next) {
        WebKitWebsiteData *data = item->data;
        if (nion_website_data_matches_host(data, request->host))
            request->website_data = g_list_prepend(
                request->website_data, webkit_website_data_ref(data));
    }
    g_list_free_full(all_data, (GDestroyNotify)webkit_website_data_unref);

    if (!request->website_data) {
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? "● TOR CONNECTED — NO STORED DATA FOR %s"
                : "○ TOR OFFLINE — NO STORED DATA FOR %s",
            request->host);
        nion_set_status(app, status);
        g_free(status);
        nion_clear_site_request_free(request);
        return;
    }

    request->website_data = g_list_reverse(request->website_data);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — CLEARING SITE DATA…"
        : "○ TOR OFFLINE — CLEARING SITE DATA…");

    webkit_website_data_manager_remove(manager,
                                       WEBKIT_WEBSITE_DATA_ALL,
                                       request->website_data,
                                       NULL,
                                       on_clear_site_data_removed,
                                       request);
}

static void on_clear_site_data_confirm_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    const gchar *host = g_object_get_data(G_OBJECT(button), "nion-site-host");
    const gchar *uri = g_object_get_data(G_OBJECT(button), "nion-site-uri");
    WebKitWebView *web_view = g_object_get_data(G_OBJECT(button), "nion-site-webview");

    if (!host || !uri || !web_view)
        return;

    NionClearSiteRequest *request = g_new0(NionClearSiteRequest, 1);
    request->app = app;
    request->host = g_strdup(host);
    request->uri = g_strdup(uri);
    request->web_view = g_object_ref(web_view);

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));

    WebKitWebsiteDataManager *manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — LOOKING UP SITE DATA…"
        : "○ TOR OFFLINE — LOOKING UP SITE DATA…");

    webkit_website_data_manager_fetch(manager,
                                      WEBKIT_WEBSITE_DATA_ALL,
                                      NULL,
                                      on_clear_site_data_fetched,
                                      request);
}

static void action_clear_site_data(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    gchar *uri = NULL;
    WebKitWebView *web_view = NULL;
    gchar *host = nion_current_site_host(app, &uri, &web_view);
    if (!host) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NO WEBSITE DATA ON THIS TAB"
            : "○ TOR OFFLINE — NO WEBSITE DATA ON THIS TAB");
        g_free(uri);
        g_clear_object(&web_view);
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Clear data for this site");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gchar *heading_markup = g_markup_printf_escaped(
        "<b>Clear data for %s?</b>", host);
    gtk_label_set_markup(GTK_LABEL(heading), heading_markup);
    g_free(heading_markup);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *message = gtk_label_new(
        "NiOn will remove WebKit data attributed to this site, including cookies, cache, "
        "local/session storage, IndexedDB and other stored website data. Other websites "
        "are left alone. You may be signed out of this site.");
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *url_label = gtk_label_new(uri);
    gtk_label_set_wrap(GTK_LABEL(url_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(url_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(url_label), 0.0f);
    gtk_widget_add_css_class(url_label, "nion-muted");
    gtk_box_append(GTK_BOX(box), url_label);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *clear = gtk_button_new_with_label("Clear Site Data");
    gtk_widget_add_css_class(clear, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), clear);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data_full(G_OBJECT(clear), "nion-site-host", g_strdup(host), g_free);
    g_object_set_data_full(G_OBJECT(clear), "nion-site-uri", g_strdup(uri), g_free);
    g_object_set_data_full(G_OBJECT(clear), "nion-site-webview", g_object_ref(web_view), g_object_unref);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_site_data_confirm_clicked), app);
    gtk_window_present(GTK_WINDOW(window));

    g_free(host);
    g_free(uri);
    g_object_unref(web_view);
}

static void on_clear_data_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    GError *error = NULL;
    gboolean ok = webkit_website_data_manager_clear_finish(
        WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);

    if (!ok) {
        gchar *status = g_strdup_printf("● TOR CONNECTED — CLEAR DATA FAILED: %s",
                                        error ? error->message : "unknown error");
        nion_set_status(app, status);
        g_free(status);
        g_clear_error(&error);
        return;
    }

    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — BROWSING DATA CLEARED"
        : "○ TOR OFFLINE — BROWSING DATA CLEARED");
}

static void on_clear_data_confirm_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));

    WebKitWebsiteDataManager *manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — CLEARING BROWSING DATA…"
        : "○ TOR OFFLINE — CLEARING BROWSING DATA…");

    webkit_website_data_manager_clear(manager,
                                      WEBKIT_WEBSITE_DATA_ALL,
                                      0,
                                      NULL,
                                      on_clear_data_finished,
                                      app);
}

static void action_clear_data(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Clear browsing data");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 470, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Clear browsing data?</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *message = gtk_label_new(
        "NiOn will clear WebKit website data including cookies, cache, local/site storage, IndexedDB, "
        "service-worker data, and other stored website data. This will sign you out of websites. "
        "Open tabs and NiOn's saved tab session are kept.");
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *clear = gtk_button_new_with_label("Clear Data");
    gtk_widget_add_css_class(clear, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), clear);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_data_confirm_clicked), app);
    gtk_window_present(GTK_WINDOW(window));
}


static GtkWidget *nion_audit_row(const gchar *title, const gchar *status, const gchar *detail)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *name = gtk_label_new(title);
    GtkWidget *state = gtk_label_new(status);
    GtkWidget *description = gtk_label_new(detail);

    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(state), 1.0f);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_widget_add_css_class(state, g_str_equal(status, "LIMITATION") ? "warning" : "success");
    gtk_widget_add_css_class(description, "nion-muted");
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);

    gtk_box_append(GTK_BOX(top), name);
    gtk_box_append(GTK_BOX(top), state);
    gtk_box_append(GTK_BOX(row), top);
    gtk_box_append(GTK_BOX(row), description);
    return row;
}

static void action_privacy_audit(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "NiOn Privacy & Leak Audit");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 620);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_window_set_child(GTK_WINDOW(window), scroller);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gchar *heading_markup = g_strdup_printf("<b>Privacy &amp; Leak Audit — NiOn %s</b>", NION_VERSION);
    gtk_label_set_markup(GTK_LABEL(heading), heading_markup);
    g_free(heading_markup);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *intro = gtk_label_new(
        "These are NiOn's enforced browser/runtime controls. "
        "They reduce leak surfaces but do not make NiOn equivalent to Tor Browser.");
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0.0f);
    gtk_widget_add_css_class(intro, "nion-muted");
    gtk_box_append(GTK_BOX(box), intro);

    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Tor-only proxy", "ENFORCED",
        "WebKit uses a custom SOCKS proxy on a runtime-selected 127.0.0.1:19050-19069 port with no bypass list; HTTP, HTTPS, WS and WSS are explicitly mapped to it."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Bundled Tor runtime", app->tor_binary_path ? "ACTIVE" : "MISSING",
        "NiOn prefers its verified Tor Expert Bundle runtime and does not silently fall back to a system Tor executable."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "DNS prefetching", "BLOCKED",
        "WebKit DNS prefetching is disabled and Tor SafeSocks rejects unsafe SOCKS usage."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "WebRTC / media capture", "BLOCKED",
        "WebRTC and media-stream capture are disabled."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Camera / microphone / geolocation / notifications", "BLOCKED",
        "All WebKit permission requests are denied by NiOn."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Clipboard read", "BLOCKED",
        "JavaScript clipboard access is disabled and permission requests are denied."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "WebGL / WebAudio", "BLOCKED",
        "WebGL and WebAudio are disabled to reduce device/fingerprinting surface."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "External URI handlers", "BLOCKED",
        "Top-level navigation is restricted to HTTP and HTTPS; file:, mailto:, ftp: and custom schemes are not handed to external applications."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Local/private network navigation", "BLOCKED",
        "localhost, local/private IP literals, link-local, multicast and common local-name suffixes are rejected."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Tor failure", app->tor_ready ? "ARMED" : "BLOCKED",
        "If Tor is unavailable or exits, NiOn stops active page loads, cancels downloads and disables browsing controls."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Canvas 2D fingerprinting", "LIMITATION",
        "GPU-accelerated canvas is disabled, but standard Canvas 2D remains available for website compatibility. NiOn does not claim Tor Browser-grade anti-fingerprinting."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "WebSocket", "TOR-PROXIED",
        "WebSocket remains enabled for modern sites. WS/WSS are explicitly assigned to the same Tor SOCKS proxy; use scripts/audit-network.sh for runtime socket verification."));

    GtkWidget *close = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(close, GTK_ALIGN_END);
    g_signal_connect(close, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    gtk_box_append(GTK_BOX(box), close);

    gtk_window_present(GTK_WINDOW(window));
}

static void nion_install_actions(NionApp *app)
{
    const GActionEntry actions[] = {
        { "new-tab", action_new_tab, NULL, NULL, NULL, {0} },
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
        { "clear-data", action_clear_data, NULL, NULL, NULL, {0} },
        { "about", action_about, NULL, NULL, NULL, {0} },
        { "exit", action_exit, NULL, NULL, NULL, {0} },
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app->application),
                                    actions, G_N_ELEMENTS(actions), app);

    const gchar *new_tab_accels[] = { "<Primary>t", NULL };
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

    gtk_application_set_accels_for_action(app->application, "app.new-tab", new_tab_accels);
    gtk_application_set_accels_for_action(app->application, "app.close-tab", close_tab_accels);
    gtk_application_set_accels_for_action(app->application, "app.reopen-closed-tab", reopen_closed_tab_accels);
    gtk_application_set_accels_for_action(app->application, "app.focus-location", focus_accels);
    gtk_application_set_accels_for_action(app->application, "app.reload", reload_accels);
    gtk_application_set_accels_for_action(app->application, "app.hard-reload", hard_reload_accels);
    gtk_application_set_accels_for_action(app->application, "app.find", find_accels);
    gtk_application_set_accels_for_action(app->application, "app.zoom-in", zoom_in_accels);
    gtk_application_set_accels_for_action(app->application, "app.zoom-out", zoom_out_accels);
    gtk_application_set_accels_for_action(app->application, "app.zoom-reset", zoom_reset_accels);
    gtk_application_set_accels_for_action(app->application, "app.print", print_accels);
    gtk_application_set_accels_for_action(app->application, "app.fullscreen", fullscreen_accels);
    gtk_application_set_accels_for_action(app->application, "app.back", back_accels);
    gtk_application_set_accels_for_action(app->application, "app.forward", forward_accels);
    gtk_application_set_accels_for_action(app->application, "app.next-tab", next_tab_accels);
    gtk_application_set_accels_for_action(app->application, "app.previous-tab", previous_tab_accels);
    gtk_application_set_accels_for_action(app->application, "app.downloads", downloads_accels);
    gtk_application_set_accels_for_action(app->application, "app.bookmark-page", bookmark_accels);
}

static gchar *nion_find_tor_binary(void)
{
    const gchar *override = g_getenv("NION_TOR_BINARY");
    if (override && g_file_test(override, G_FILE_TEST_IS_EXECUTABLE))
        return g_canonicalize_filename(override, NULL);

    const gchar *appdir = g_getenv("APPDIR");
    if (appdir) {
        const gchar *candidates[] = {
            "usr/lib/nion/tor/tor",
            "usr/lib/nion/tor/bin/tor",
            "usr/bin/tor",
            NULL,
        };
        for (guint i = 0; candidates[i]; i++) {
            gchar *bundled = g_build_filename(appdir, candidates[i], NULL);
            if (g_file_test(bundled, G_FILE_TEST_IS_EXECUTABLE))
                return bundled;
            g_free(bundled);
        }
    }

    /* Development tree: build/nion -> ../runtime/tor/tor. */
    GError *error = NULL;
    gchar *exe = g_file_read_link("/proc/self/exe", &error);
    g_clear_error(&error);
    if (exe) {
        gchar *exe_dir = g_path_get_dirname(exe);
        gchar *project_dir = g_path_get_dirname(exe_dir);
        gchar *bundled = g_build_filename(project_dir, "runtime", "tor", "tor", NULL);
        g_free(project_dir);
        g_free(exe_dir);
        g_free(exe);
        if (g_file_test(bundled, G_FILE_TEST_IS_EXECUTABLE))
            return bundled;
        g_free(bundled);
    }

    /* System Tor is intentionally opt-in from 0.6.0 onward. The normal
     * runtime path is the Tor Expert Bundle prepared by our scripts. */
    if (g_strcmp0(g_getenv("NION_ALLOW_SYSTEM_TOR"), "1") == 0)
        return g_find_program_in_path("tor");

    return NULL;
}

static gboolean nion_pid_alive(gint64 pid)
{
    if (pid <= 1)
        return FALSE;
    if (kill((pid_t)pid, 0) == 0)
        return TRUE;
    return errno == EPERM;
}

static gboolean nion_pid_looks_like_our_tor(NionApp *app, gint64 pid)
{
    gchar *proc_path = g_strdup_printf("/proc/%" G_GINT64_FORMAT "/cmdline", pid);
    gchar *contents = NULL;
    gsize length = 0;
    gboolean ok = FALSE;

    if (g_file_get_contents(proc_path, &contents, &length, NULL) && length > 0) {
        for (gsize i = 0; i < length; i++) {
            if (contents[i] == '\0')
                contents[i] = ' ';
        }
        ok = strstr(contents, "tor") != NULL &&
             app->tor_dir && strstr(contents, app->tor_dir) != NULL;
    }

    g_free(contents);
    g_free(proc_path);
    return ok;
}

static void nion_wait_for_pid_exit(gint64 pid, guint timeout_ms)
{
    const guint step_ms = 50;
    guint waited = 0;
    while (nion_pid_alive(pid) && waited < timeout_ms) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(step_ms * 1000);
        waited += step_ms;
    }
}

static void nion_cleanup_stale_tor(NionApp *app)
{
    if (!app->tor_runtime_file || !g_file_test(app->tor_runtime_file, G_FILE_TEST_EXISTS))
        return;

    GKeyFile *state = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(state, app->tor_runtime_file, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_free(state);
        g_unlink(app->tor_runtime_file);
        return;
    }

    gint64 pid = g_key_file_get_int64(state, "Tor", "pid", NULL);
    if (nion_pid_alive(pid)) {
        if (nion_pid_looks_like_our_tor(app, pid)) {
            g_printerr("[NiOn] Found stale NiOn Tor PID %" G_GINT64_FORMAT "; terminating it.\n", pid);
            kill((pid_t)pid, SIGTERM);
            nion_wait_for_pid_exit(pid, 1500);
            if (nion_pid_alive(pid)) {
                g_printerr("[NiOn] Stale Tor did not stop gracefully; sending SIGKILL.\n");
                kill((pid_t)pid, SIGKILL);
                nion_wait_for_pid_exit(pid, 500);
            }
        } else {
            g_printerr("[NiOn] Runtime state references PID %" G_GINT64_FORMAT
                       " but it is not recognizably NiOn's Tor; leaving it untouched.\n", pid);
        }
    }

    g_key_file_free(state);
    g_unlink(app->tor_runtime_file);

    gchar *lock_file = g_build_filename(app->tor_dir, "lock", NULL);
    g_unlink(lock_file);
    g_free(lock_file);
}

static gboolean nion_tcp_port_available(guint16 port)
{
    GError *error = NULL;
    GSocket *socket = g_socket_new(G_SOCKET_FAMILY_IPV4,
                                   G_SOCKET_TYPE_STREAM,
                                   G_SOCKET_PROTOCOL_TCP,
                                   &error);
    if (!socket) {
        g_clear_error(&error);
        return FALSE;
    }

    GInetAddress *address = g_inet_address_new_from_string(NION_TOR_HOST);
    GSocketAddress *socket_address = g_inet_socket_address_new(address, port);
    gboolean ok = g_socket_bind(socket, socket_address, FALSE, &error);

    g_clear_error(&error);
    g_object_unref(socket_address);
    g_object_unref(address);
    g_object_unref(socket);
    return ok;
}

static gboolean nion_choose_tor_port(NionApp *app)
{
    for (guint i = 0; i <= (NION_TOR_PORT_LAST - NION_TOR_PORT_FIRST); i++) {
        guint16 socks_port = (guint16)(NION_TOR_PORT_FIRST + i);
        if (nion_tcp_port_available(socks_port)) {
            app->tor_socks_port = socks_port;
            g_free(app->tor_proxy_uri);
            app->tor_proxy_uri = g_strdup_printf("socks://%s:%u", NION_TOR_HOST, socks_port);
            return TRUE;
        }
    }

    nion_set_tor_error(app,
        "No free local SOCKS port is available in NiOn's runtime range");
    return FALSE;
}

static gboolean nion_write_tor_runtime_state(NionApp *app)
{
    if (!app->tor_process || !app->tor_runtime_file)
        return FALSE;

    const gchar *identifier = g_subprocess_get_identifier(app->tor_process);
    if (!identifier || !*identifier)
        return FALSE;

    gchar *end = NULL;
    gint64 pid = g_ascii_strtoll(identifier, &end, 10);
    if (!end || *end != '\0' || pid <= 1)
        return FALSE;

    GKeyFile *state = g_key_file_new();
    g_key_file_set_int64(state, "Tor", "pid", pid);
    g_key_file_set_integer(state, "Tor", "socks-port", app->tor_socks_port);
    g_key_file_set_string(state, "Tor", "binary", app->tor_binary_path ? app->tor_binary_path : "");
    g_key_file_set_string(state, "Tor", "nion-version", NION_VERSION);
    gboolean ok = nion_write_key_file_atomic(state, app->tor_runtime_file);
    g_key_file_free(state);
    return ok;
}

static gboolean nion_tor_log_suggests_port_conflict(const gchar *line)
{
    if (!line || !*line)
        return FALSE;

    gchar *lower = g_ascii_strdown(line, -1);
    gboolean conflict = strstr(lower, "address already in use") ||
                        strstr(lower, "failed to bind") ||
                        strstr(lower, "could not bind");
    g_free(lower);
    return conflict;
}

static gboolean nion_tor_log_suggests_corruption(const gchar *line)
{
    if (!line || !*line)
        return FALSE;

    gchar *lower = g_ascii_strdown(line, -1);
    gboolean has_damage_word = strstr(lower, "corrupt") ||
                               strstr(lower, "unparseable") ||
                               strstr(lower, "invalid") ||
                               strstr(lower, "parse error") ||
                               strstr(lower, "failed to parse") ||
                               strstr(lower, "failed to read");
    gboolean mentions_state = strstr(lower, "state") ||
                              strstr(lower, "cached-microdesc") ||
                              strstr(lower, "cached-consensus") ||
                              strstr(lower, "cached-descriptor") ||
                              strstr(lower, "unverified-consensus");
    gboolean corruption = has_damage_word && mentions_state;
    g_free(lower);
    return corruption;
}

static gboolean nion_quarantine_file(const gchar *source, const gchar *backup_dir)
{
    if (!g_file_test(source, G_FILE_TEST_EXISTS))
        return TRUE;

    gchar *base = g_path_get_basename(source);
    gchar *target = g_build_filename(backup_dir, base, NULL);
    gboolean ok = g_rename(source, target) == 0;
    if (!ok)
        g_warning("Could not quarantine Tor state file %s", source);
    g_free(target);
    g_free(base);
    return ok;
}

static gboolean nion_recover_tor_state(NionApp *app)
{
    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    gchar *backup_name = g_strdup_printf("recovery-%s", stamp);
    gchar *backup_dir = g_build_filename(app->tor_dir, backup_name, NULL);
    if (g_mkdir_with_parents(backup_dir, 0700) != 0) {
        g_warning("Could not create Tor recovery directory %s: %s", backup_dir, g_strerror(errno));
        g_free(backup_dir);
        g_free(backup_name);
        g_free(stamp);
        g_date_time_unref(now);
        return FALSE;
    }
    g_chmod(backup_dir, 0700);

    const gchar *cache_files[] = {
        "lock",
        "cached-certs",
        "cached-consensus",
        "cached-consensus.new",
        "cached-microdesc-consensus",
        "cached-microdescs",
        "cached-microdescs.new",
        "cached-descriptors",
        "cached-descriptors.new",
        "unverified-consensus",
        NULL,
    };

    for (guint i = 0; cache_files[i]; i++) {
        gchar *path = g_build_filename(app->tor_dir, cache_files[i], NULL);
        nion_quarantine_file(path, backup_dir);
        g_free(path);
    }

    /* Only quarantine Tor's persistent state when the actual error points to
     * that file. This can reset entry-guard state, so do not do it for a
     * generic startup failure. */
    if (app->tor_last_log) {
        gchar *lower = g_ascii_strdown(app->tor_last_log, -1);
        if (strstr(lower, "state") &&
            (strstr(lower, "parse") || strstr(lower, "invalid") || strstr(lower, "corrupt"))) {
            gchar *state_file = g_build_filename(app->tor_dir, "state", NULL);
            nion_quarantine_file(state_file, backup_dir);
            g_free(state_file);
        }
        g_free(lower);
    }

    g_printerr("[NiOn] Tor cache/state recovery backup: %s\n", backup_dir);
    g_free(backup_dir);
    g_free(backup_name);
    g_free(stamp);
    g_date_time_unref(now);
    return TRUE;
}

static gboolean nion_restart_tor_delayed(gpointer user_data)
{
    NionApp *app = user_data;
    if (app->shutting_down)
        return G_SOURCE_REMOVE;

    g_clear_object(&app->tor_output);
    g_clear_object(&app->tor_process);
    g_unlink(app->tor_runtime_file);

    if (!nion_choose_tor_port(app))
        return G_SOURCE_REMOVE;

    nion_apply_network_proxy(app);
    nion_start_tor(app);
    return G_SOURCE_REMOVE;
}

static gboolean nion_tor_startup_timeout(gpointer user_data)
{
    NionApp *app = user_data;
    app->tor_startup_timeout_id = 0;
    if (app->shutting_down || app->tor_ready)
        return G_SOURCE_REMOVE;

    nion_store_tor_log(app, "Tor bootstrap timed out after 120 seconds");
    nion_set_tor_error(app, "Tor bootstrap timed out after 120 seconds");
    if (app->tor_process)
        g_subprocess_force_exit(app->tor_process);
    return G_SOURCE_REMOVE;
}

static void nion_read_tor_line(NionApp *app);

static void nion_store_tor_log(NionApp *app, const gchar *line)
{
    if (!line || !*line)
        return;

    g_free(app->tor_last_log);
    app->tor_last_log = g_strdup(line);

    g_printerr("[NiOn:Tor] %s\n", line);
}

static void on_tor_line_read(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    if (app->shutting_down)
        return;

    GError *error = NULL;
    gsize length = 0;
    GDataInputStream *stream = G_DATA_INPUT_STREAM(source);
    gchar *line = g_data_input_stream_read_line_finish(stream,
                                                       result, &length, &error);
    (void)length;

    /* A previous Tor instance can finish an outstanding async read after a
     * recovery restart. Never let that stale stream alter the new runtime. */
    if (stream != app->tor_output) {
        g_free(line);
        g_clear_error(&error);
        return;
    }

    if (error) {
        gchar *message = g_strdup_printf("Tor log read failed: %s", error->message);
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    if (!line)
        return;

    nion_store_tor_log(app, line);
    if (nion_tor_log_suggests_port_conflict(line))
        app->tor_saw_port_conflict = TRUE;
    if (nion_tor_log_suggests_corruption(line))
        app->tor_saw_corruption = TRUE;

    const gchar *bootstrap = strstr(line, "Bootstrapped ");
    if (bootstrap) {
        gint percent = -1;
        if (sscanf(bootstrap, "Bootstrapped %d%%", &percent) == 1)
            nion_set_tor_progress(app, percent);
    }

    g_free(line);
    nion_read_tor_line(app);
}

static void nion_read_tor_line(NionApp *app)
{
    if (!app->tor_output || app->shutting_down)
        return;

    g_data_input_stream_read_line_async(app->tor_output,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        on_tor_line_read,
                                        app);
}

static void on_tor_process_waited(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    GSubprocess *process = G_SUBPROCESS(source);
    GError *error = NULL;

    if (app->tor_startup_timeout_id) {
        g_source_remove(app->tor_startup_timeout_id);
        app->tor_startup_timeout_id = 0;
    }

    if (!g_subprocess_wait_finish(process, result, &error)) {
        if (!app->shutting_down) {
            gchar *message = g_strdup_printf("Could not monitor Tor: %s",
                                             error ? error->message : "unknown error");
            nion_store_tor_log(app, message);
            nion_set_tor_error(app, message);
            g_free(message);
        }
        g_clear_error(&error);
        return;
    }

    if (app->tor_runtime_file)
        g_unlink(app->tor_runtime_file);

    if (app->shutting_down)
        return;

    /* A port can be taken in the tiny race between our preflight bind test
     * and Tor actually binding. Retry with another port rather than failing
     * the whole browser. */
    if (!app->tor_ready &&
        (app->tor_saw_port_conflict || nion_tor_log_suggests_port_conflict(app->tor_last_log)) &&
        app->tor_port_retry_count < 3) {
        app->tor_port_retry_count++;
        app->tor_failed = FALSE;
        nion_set_status(app, "○ TOR PORT CONFLICT — selecting another SOCKS port…");
        g_timeout_add(200, nion_restart_tor_delayed, app);
        return;
    }

    /* Recover only from logs that actually look like damaged Tor cache/state.
     * The affected files are quarantined, never silently discarded. */
    if (!app->tor_ready && !app->tor_recovery_attempted &&
        (app->tor_saw_corruption || nion_tor_log_suggests_corruption(app->tor_last_log))) {
        app->tor_recovery_attempted = TRUE;
        app->tor_failed = FALSE;
        nion_set_status(app, "○ TOR STATE RECOVERY — quarantining damaged cache and retrying…");
        if (!nion_recover_tor_state(app)) {
            nion_set_tor_error(app, "Tor state recovery could not create its quarantine directory");
            return;
        }
        g_timeout_add(250, nion_restart_tor_delayed, app);
        return;
    }

    gchar *message = NULL;
    if (g_subprocess_get_if_signaled(process)) {
        message = g_strdup_printf("Tor terminated by signal %d",
                                  g_subprocess_get_term_sig(process));
    } else if (g_subprocess_get_if_exited(process)) {
        message = g_strdup_printf("Tor exited with status %d",
                                  g_subprocess_get_exit_status(process));
    } else {
        message = g_strdup("Tor stopped unexpectedly");
    }

    if (app->tor_last_log && *app->tor_last_log) {
        gchar *combined = g_strdup_printf("%s — %s", message, app->tor_last_log);
        g_free(message);
        message = combined;
    }

    nion_set_tor_error(app, message);
    g_free(message);
}

static gboolean nion_start_tor(NionApp *app)
{
    if (!app->tor_binary_path)
        app->tor_binary_path = nion_find_tor_binary();

    if (!app->tor_binary_path) {
        nion_store_tor_log(app, "Bundled Tor runtime was not found");
        nion_set_tor_error(app,
            "Bundled Tor runtime not found — run scripts/fetch-tor-runtime.sh");
        return FALSE;
    }

    if (!app->tor_socks_port || !app->tor_proxy_uri) {
        nion_store_tor_log(app, "NiOn could not reserve a local Tor SOCKS port");
        nion_set_tor_error(app,
            "No free Tor SOCKS port was found in the 19050-19069 range");
        return FALSE;
    }

    app->tor_ready = FALSE;
    app->tor_failed = FALSE;
    app->tor_bootstrap_percent = 0;
    app->tor_saw_port_conflict = FALSE;
    app->tor_saw_corruption = FALSE;
    g_clear_pointer(&app->tor_last_log, g_free);

    GError *error = NULL;
    gchar *torrc_path = g_build_filename(app->tor_dir, "torrc", NULL);
    const gchar *torrc =
        "# NiOn private Tor configuration\n"
        "ClientOnly 1\n"
        "SafeSocks 1\n"
        "WarnUnsafeSocks 1\n"
        "ClientRejectInternalAddresses 1\n"
        "ClientDNSRejectInternalAddresses 1\n";

    if (!g_file_set_contents(torrc_path, torrc, -1, &error)) {
        gchar *message = g_strdup_printf("Could not create NiOn torrc: %s",
                                         error ? error->message : "unknown error");
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        g_free(torrc_path);
        return FALSE;
    }
    g_chmod(torrc_path, 0600);

    gchar *socks_endpoint = g_strdup_printf("%s:%u", NION_TOR_HOST, app->tor_socks_port);
    gchar *owner_pid = g_strdup_printf("%ld", (long)getpid());

    app->tor_process = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error,
        app->tor_binary_path,
        "-f", torrc_path,
        "--DataDirectory", app->tor_dir,
        "--SocksPort", socks_endpoint,
        "--__OwningControllerProcess", owner_pid,
        "--Log", "notice stdout",
        NULL);

    g_free(owner_pid);
    g_free(socks_endpoint);

    if (!app->tor_process) {
        gchar *message = g_strdup_printf("Tor start failed: %s",
                                         error ? error->message : "unknown error");
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        g_free(torrc_path);
        return FALSE;
    }

    g_printerr("[NiOn] Starting bundled Tor: %s\n", app->tor_binary_path);
    g_printerr("[NiOn] Tor data: %s\n", app->tor_dir);
    g_printerr("[NiOn] Tor config: %s\n", torrc_path);
    g_printerr("[NiOn] SOCKS: %s:%u\n", NION_TOR_HOST, app->tor_socks_port);
    g_printerr("[NiOn] Tor owner PID: %ld\n", (long)getpid());
    g_free(torrc_path);

    nion_write_tor_runtime_state(app);

    GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(app->tor_process);
    app->tor_output = g_data_input_stream_new(stdout_stream);

    nion_set_tor_progress(app, 0);
    nion_read_tor_line(app);
    g_subprocess_wait_async(app->tor_process, NULL, on_tor_process_waited, app);
    app->tor_startup_timeout_id = g_timeout_add_seconds(
        NION_TOR_STARTUP_TIMEOUT_SECONDS,
        nion_tor_startup_timeout,
        app);
    return TRUE;
}

static void nion_prepare_dirs(NionApp *app)
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

static void nion_validate_cookie_store(NionApp *app)
{
    if (!app || !app->cookie_file ||
        !g_file_test(app->cookie_file, G_FILE_TEST_IS_REGULAR))
        return;

    FILE *file = g_fopen(app->cookie_file, "rb");
    if (!file)
        return;

    unsigned char header[16] = {0};
    size_t read_bytes = fread(header, 1, sizeof(header), file);
    fclose(file);

    static const unsigned char sqlite_header[16] = {
        'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'
    };

    if (read_bytes == 0)
        return;
    if (read_bytes == sizeof(header) && memcmp(header, sqlite_header, sizeof(header)) == 0) {
        g_chmod(app->cookie_file, 0600);
        return;
    }

    g_warning("NiOn cookie database does not have a valid SQLite header; quarantining it");
    nion_quarantine_profile_file(app->cookie_file, "cookie database");

    gchar *wal = g_strdup_printf("%s-wal", app->cookie_file);
    gchar *shm = g_strdup_printf("%s-shm", app->cookie_file);
    nion_quarantine_profile_file(wal, "cookie database WAL");
    nion_quarantine_profile_file(shm, "cookie database SHM");
    g_free(wal);
    g_free(shm);
}

static void nion_apply_network_proxy(NionApp *app)
{
    if (!app->network_session)
        return;

    const gchar *proxy_uri = app->tor_proxy_uri ? app->tor_proxy_uri : "socks://127.0.0.1:9";
    WebKitNetworkProxySettings *proxy = webkit_network_proxy_settings_new(proxy_uri, NULL);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "http", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "https", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "ws", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "wss", proxy_uri);
    webkit_network_session_set_proxy_settings(app->network_session,
                                              WEBKIT_NETWORK_PROXY_MODE_CUSTOM,
                                              proxy);
    webkit_network_proxy_settings_free(proxy);
}

static void nion_prepare_network(NionApp *app)
{
    if (app->network_session) {
        nion_apply_network_proxy(app);
        return;
    }

    app->network_session = webkit_network_session_new(app->data_dir, app->cache_dir);
    /* Site information relies on strict certificate verification. Make the
     * existing fail-on-TLS-errors behavior explicit instead of relying on a
     * library default. */
    webkit_network_session_set_tls_errors_policy(app->network_session,
                                                  WEBKIT_TLS_ERRORS_POLICY_FAIL);

    WebKitWebsiteDataManager *data_manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    webkit_website_data_manager_set_favicons_enabled(data_manager, TRUE);

    WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(app->network_session);
    webkit_cookie_manager_set_persistent_storage(cookies,
                                                  app->cookie_file,
                                                  WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    nion_apply_cookie_policy(app);

    webkit_network_session_set_persistent_credential_storage_enabled(app->network_session, TRUE);
    nion_apply_network_proxy(app);

    g_signal_connect(app->network_session, "download-started",
                     G_CALLBACK(on_download_started), app);
}

static void nion_apply_css(void)
{
    const gchar *css =
        ".nion-toolbar { padding: 7px 8px; }"
        ".nion-toolbar button { min-width: 32px; min-height: 32px; padding: 2px; }"
        ".nion-toolbar entry { min-height: 32px; }"
        ".nion-find-bar { border-top: 1px solid alpha(currentColor,0.10); border-bottom: 1px solid alpha(currentColor,0.10); }"
        ".nion-find-bar button { min-width: 30px; min-height: 30px; padding: 2px; }"
        ".nion-onion-badge { min-width: 54px; padding: 3px 10px; border-radius: 999px; font-weight: 700; }"
        ".nion-site-info-warning { font-weight: 800; }"
        ".nion-site-info-title { font-weight: 800; font-size: 1.08em; }"
        ".nion-site-info-key { opacity: 0.68; font-size: 0.82em; font-weight: 700; }"
        ".nion-site-info-value { font-size: 0.94em; }"
        ".nion-bookmark-active { color: #e5a50a; }"
        ".nion-status { padding: 6px 10px; font-size: 0.84em; font-weight: 600; border-top: 1px solid alpha(currentColor,0.12); }"
        ".nion-status-connected { opacity: 0.92; }"
        ".nion-status-connecting { opacity: 0.78; }"
        ".nion-status-warning { font-weight: 700; }"
        ".nion-status-error { font-weight: 700; }"
        ".nion-progress { margin: 0; padding: 0; }"
        ".nion-progress trough, .nion-progress progress { min-height: 3px; }"
        ".nion-downloads { padding: 8px 10px; border-top: 1px solid alpha(currentColor,0.16); }"
        ".nion-downloads-header { padding: 2px; }"
        ".nion-download-header { font-weight: 700; }"
        ".nion-download-row { padding: 5px 0; }"
        ".nion-download-detail { opacity: 0.72; font-size: 0.88em; }"
        ".nion-muted { opacity: 0.72; }"
        ".nion-tab-label { min-height: 28px; }"
        ".nion-tab-audio { min-width: 24px; min-height: 24px; padding: 0; }"
        ".nion-tab-close { min-width: 24px; min-height: 24px; padding: 0; }"
        ".nion-new-tab { min-width: 30px; min-height: 30px; padding: 0 5px; margin: 3px 6px; }"
        "notebook > header { padding: 2px 4px 0; }"
        "notebook > header tab { padding: 3px 5px; }";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1);
    GdkDisplay *display = gdk_display_get_default();
    if (display)
        gtk_style_context_add_provider_for_display(display,
                                                   GTK_STYLE_PROVIDER(provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
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

static gboolean on_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    NionApp *app = user_data;
    if (app->shutting_down || app->close_confirmed)
        return FALSE;

    nion_request_close(app);
    return TRUE;
}

static void nion_build_downloads_window(NionApp *app)
{
    app->downloads_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(app->downloads_window), "Downloads — NiOn");
    gtk_window_set_transient_for(GTK_WINDOW(app->downloads_window), GTK_WINDOW(app->window));
    gtk_window_set_default_size(GTK_WINDOW(app->downloads_window), 760, 520);
    gtk_window_set_icon_name(GTK_WINDOW(app->downloads_window), NION_APP_ID);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->downloads_window), root);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(header, "nion-downloads-header");
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 12);
    gtk_widget_set_margin_start(header, 14);
    gtk_widget_set_margin_end(header, 14);

    GtkWidget *titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Downloads");
    GtkWidget *path = gtk_label_new(app->download_dir);
    gtk_widget_add_css_class(title, "title-3");
    gtk_widget_add_css_class(path, "nion-muted");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(path), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(path), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text(path, app->download_dir);
    gtk_widget_set_hexpand(titles, TRUE);
    gtk_box_append(GTK_BOX(titles), title);
    gtk_box_append(GTK_BOX(titles), path);

    GtkWidget *clear = gtk_button_new_with_label("Clear Downloads");
    gtk_widget_set_valign(clear, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(clear, "Remove completed, failed and cancelled entries from history");
    gtk_box_append(GTK_BOX(header), titles);
    gtk_box_append(GTK_BOX(header), clear);
    gtk_box_append(GTK_BOX(root), header);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(root), separator);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_box_append(GTK_BOX(root), overlay);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scroller);

    app->downloads_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(app->downloads_list, 8);
    gtk_widget_set_margin_bottom(app->downloads_list, 8);
    gtk_widget_set_margin_start(app->downloads_list, 14);
    gtk_widget_set_margin_end(app->downloads_list, 14);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), app->downloads_list);

    app->downloads_empty_label = gtk_label_new("No downloads yet.\nDownloads made by NiOn will appear here.");
    gtk_label_set_justify(GTK_LABEL(app->downloads_empty_label), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(app->downloads_empty_label, "nion-muted");
    gtk_widget_set_halign(app->downloads_empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app->downloads_empty_label, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app->downloads_empty_label);

    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_downloads_clicked), app);
    g_signal_connect(app->downloads_window, "close-request",
                     G_CALLBACK(on_downloads_window_close_request), app);

    nion_load_download_history(app);
}

static void nion_build_ui(NionApp *app)
{
    app->window = gtk_application_window_new(app->application);
    gtk_window_set_title(GTK_WINDOW(app->window), "NiOn");
    gtk_window_set_icon_name(GTK_WINDOW(app->window), NION_APP_ID);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1100, 760);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), root);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app->toolbar = toolbar;
    gtk_widget_add_css_class(toolbar, "nion-toolbar");
    gtk_box_append(GTK_BOX(root), toolbar);

    app->back_button = gtk_button_new_from_icon_name("go-previous-symbolic");
    app->forward_button = gtk_button_new_from_icon_name("go-next-symbolic");
    app->reload_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
    app->home_button = gtk_button_new_from_icon_name("go-home-symbolic");
    app->onion_button = gtk_button_new_with_label("Onion");
    gtk_widget_add_css_class(app->onion_button, "nion-onion-badge");
    gtk_widget_set_visible(app->onion_button, FALSE);

    app->site_info_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->site_info_button),
                                  "dialog-information-symbolic");
    gtk_widget_add_css_class(app->site_info_button, "flat");
    gtk_widget_set_sensitive(app->site_info_button, FALSE);

    app->site_info_popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(app->site_info_popover), TRUE);
    GtkWidget *site_info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(site_info_box, 14);
    gtk_widget_set_margin_bottom(site_info_box, 14);
    gtk_widget_set_margin_start(site_info_box, 14);
    gtk_widget_set_margin_end(site_info_box, 14);
    gtk_widget_set_size_request(site_info_box, 340, -1);

    app->site_info_title_label = gtk_label_new("Site information");
    gtk_label_set_xalign(GTK_LABEL(app->site_info_title_label), 0.0f);
    gtk_widget_add_css_class(app->site_info_title_label, "nion-site-info-title");
    gtk_box_append(GTK_BOX(site_info_box), app->site_info_title_label);
    gtk_box_append(GTK_BOX(site_info_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Host", &app->site_info_host_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Connection", &app->site_info_connection_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Route", &app->site_info_route_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Mixed content", &app->site_info_mixed_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Address", &app->site_info_uri_label));
    gtk_label_set_selectable(GTK_LABEL(app->site_info_uri_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(app->site_info_uri_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app->site_info_uri_label), 52);
    gtk_popover_set_child(GTK_POPOVER(app->site_info_popover), site_info_box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(app->site_info_button), app->site_info_popover);

    app->address = gtk_entry_new();
    app->bookmark_button = gtk_button_new_from_icon_name("non-starred-symbolic");
    gtk_widget_add_css_class(app->bookmark_button, "flat");
    gtk_widget_set_sensitive(app->bookmark_button, FALSE);
    app->new_tab_button = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(app->new_tab_button, "flat");
    gtk_widget_add_css_class(app->new_tab_button, "nion-new-tab");
    app->menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(app->menu_button), "open-menu-symbolic");

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Find in Page", "app.find");
    g_menu_append(menu, "Reload Without Cache", "app.hard-reload");
    GMenu *zoom_menu = g_menu_new();
    g_menu_append(zoom_menu, "Zoom In", "app.zoom-in");
    g_menu_append(zoom_menu, "Zoom Out", "app.zoom-out");
    g_menu_append(zoom_menu, "Reset Zoom", "app.zoom-reset");
    g_menu_append_submenu(menu, "Zoom", G_MENU_MODEL(zoom_menu));
    g_object_unref(zoom_menu);
    g_menu_append(menu, "Fullscreen", "app.fullscreen");
    g_menu_append(menu, "Print / Save as PDF…", "app.print");
    g_menu_append(menu, "Bookmarks", "app.bookmarks");
    g_menu_append(menu, "Downloads", "app.downloads");
    g_menu_append(menu, "Preferences", "app.preferences");
    g_menu_append(menu, "Privacy & Leak Audit", "app.privacy-audit");
    g_menu_append(menu, "Clear Data for This Site…", "app.clear-site-data");
    g_menu_append(menu, "Clear Browsing Data…", "app.clear-data");
    g_menu_append(menu, "About NiOn", "app.about");
    g_menu_append(menu, "Exit", "app.exit");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(app->menu_button), G_MENU_MODEL(menu));
    g_object_unref(menu);

    gtk_widget_set_tooltip_text(app->back_button, "Back");
    gtk_widget_set_tooltip_text(app->forward_button, "Forward");
    gtk_widget_set_tooltip_text(app->reload_button, "Reload");
    gtk_widget_set_tooltip_text(app->home_button, "Home / blank tab");
    gtk_widget_set_tooltip_text(app->onion_button, "Open advertised Onion-Location in a new tab");
    gtk_widget_set_tooltip_text(app->site_info_button, "No website connection information");
    gtk_widget_set_tooltip_text(app->bookmark_button, "This page cannot be bookmarked");
    gtk_widget_set_tooltip_text(app->new_tab_button, "New tab");
    gtk_widget_set_tooltip_text(app->menu_button, "NiOn menu");
    gtk_widget_add_css_class(app->back_button, "flat");
    gtk_widget_add_css_class(app->forward_button, "flat");
    gtk_widget_add_css_class(app->reload_button, "flat");
    gtk_widget_add_css_class(app->home_button, "flat");
    gtk_widget_add_css_class(app->menu_button, "flat");
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->address), "Enter URL, .onion address, or search");
    gtk_widget_set_hexpand(app->address, TRUE);

    gtk_box_append(GTK_BOX(toolbar), app->back_button);
    gtk_box_append(GTK_BOX(toolbar), app->forward_button);
    gtk_box_append(GTK_BOX(toolbar), app->reload_button);
    gtk_box_append(GTK_BOX(toolbar), app->home_button);
    gtk_box_append(GTK_BOX(toolbar), app->onion_button);
    gtk_box_append(GTK_BOX(toolbar), app->site_info_button);
    gtk_box_append(GTK_BOX(toolbar), app->address);
    gtk_box_append(GTK_BOX(toolbar), app->bookmark_button);
    gtk_box_append(GTK_BOX(toolbar), app->menu_button);

    app->find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(app->find_bar, "nion-find-bar");
    gtk_widget_set_margin_top(app->find_bar, 4);
    gtk_widget_set_margin_bottom(app->find_bar, 4);
    gtk_widget_set_margin_start(app->find_bar, 8);
    gtk_widget_set_margin_end(app->find_bar, 8);
    app->find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->find_entry), "Find in page");
    gtk_widget_set_hexpand(app->find_entry, TRUE);
    app->find_match_label = gtk_label_new("");
    gtk_widget_add_css_class(app->find_match_label, "nion-muted");
    app->find_prev_button = gtk_button_new_from_icon_name("go-up-symbolic");
    app->find_next_button = gtk_button_new_from_icon_name("go-down-symbolic");
    app->find_close_button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(app->find_prev_button, "Previous match (Shift+Enter)");
    gtk_widget_set_tooltip_text(app->find_next_button, "Next match (Enter)");
    gtk_widget_set_tooltip_text(app->find_close_button, "Close find bar (Esc)");
    gtk_box_append(GTK_BOX(app->find_bar), app->find_entry);
    gtk_box_append(GTK_BOX(app->find_bar), app->find_match_label);
    gtk_box_append(GTK_BOX(app->find_bar), app->find_prev_button);
    gtk_box_append(GTK_BOX(app->find_bar), app->find_next_button);
    gtk_box_append(GTK_BOX(app->find_bar), app->find_close_button);
    gtk_widget_set_visible(app->find_bar, FALSE);
    gtk_box_append(GTK_BOX(root), app->find_bar);

    GtkEventController *find_keys = gtk_event_controller_key_new();
    g_signal_connect(find_keys, "key-pressed", G_CALLBACK(on_find_key_pressed), app);
    gtk_widget_add_controller(app->find_entry, find_keys);
    g_signal_connect(app->find_entry, "changed", G_CALLBACK(on_find_entry_changed), app);
    g_signal_connect(app->find_prev_button, "clicked", G_CALLBACK(on_find_prev_clicked), app);
    g_signal_connect(app->find_next_button, "clicked", G_CALLBACK(on_find_next_clicked), app);
    g_signal_connect(app->find_close_button, "clicked", G_CALLBACK(on_find_close_clicked), app);

    app->progress_bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(app->progress_bar, "nion-progress");
    gtk_widget_set_visible(app->progress_bar, FALSE);
    gtk_box_append(GTK_BOX(root), app->progress_bar);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(app->notebook), app->new_tab_button, GTK_PACK_END);
    gtk_widget_set_vexpand(app->notebook, TRUE);
    gtk_box_append(GTK_BOX(root), app->notebook);

    nion_build_downloads_window(app);

    app->status_label = gtk_label_new("○ CONNECTING TO TOR… 0%");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(app->status_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(app->status_label, "nion-status");
    gtk_widget_add_css_class(app->status_label, "nion-status-connecting");
    gtk_widget_set_tooltip_text(app->status_label, "NiOn blocks browsing unless its Tor runtime is connected.");
    gtk_box_append(GTK_BOX(root), app->status_label);

    g_signal_connect(app->address, "activate", G_CALLBACK(on_address_activate), app);
    g_signal_connect(app->back_button, "clicked", G_CALLBACK(on_back_clicked), app);
    g_signal_connect(app->forward_button, "clicked", G_CALLBACK(on_forward_clicked), app);
    g_signal_connect(app->reload_button, "clicked", G_CALLBACK(on_reload_clicked), app);
    g_signal_connect(app->home_button, "clicked", G_CALLBACK(on_home_clicked), app);
    g_signal_connect(app->onion_button, "clicked", G_CALLBACK(on_onion_button_clicked), app);
    g_signal_connect(app->bookmark_button, "clicked", G_CALLBACK(on_bookmark_toolbar_clicked), app);
    g_signal_connect(app->new_tab_button, "clicked", G_CALLBACK(on_new_tab_clicked), app);
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(on_notebook_switch_page), app);
    g_signal_connect(app->notebook, "page-reordered", G_CALLBACK(on_notebook_page_reordered), app);
    g_signal_connect(app->window, "close-request", G_CALLBACK(on_window_close_request), app);
    g_signal_connect(app->window, "notify::fullscreened", G_CALLBACK(on_window_fullscreen_notify), app);

    if (!nion_restore_saved_session(app))
        nion_new_tab(app, NULL, TRUE);

    /* Mark the live session dirty immediately. A graceful close writes it
     * back with clean-shutdown=true; a crash leaves the most recent dirty
     * snapshot available for recovery. */
    nion_save_session(app, FALSE);
    nion_update_controls(app);
    gtk_window_present(GTK_WINDOW(app->window));
}

static void nion_stop_tor_gracefully(NionApp *app)
{
    if (app->tor_startup_timeout_id) {
        g_source_remove(app->tor_startup_timeout_id);
        app->tor_startup_timeout_id = 0;
    }

    if (!app->tor_process) {
        if (app->tor_runtime_file)
            g_unlink(app->tor_runtime_file);
        return;
    }

    const gchar *identifier = g_subprocess_get_identifier(app->tor_process);
    gint64 pid = 0;
    if (identifier && *identifier) {
        gchar *end = NULL;
        pid = g_ascii_strtoll(identifier, &end, 10);
        if (!end || *end != '\0')
            pid = 0;
    }

    g_printerr("[NiOn] Stopping Tor gracefully…\n");
    g_subprocess_send_signal(app->tor_process, SIGTERM);

    if (pid > 1)
        nion_wait_for_pid_exit(pid, NION_TOR_GRACEFUL_SHUTDOWN_MS);

    if (pid > 1 && nion_pid_alive(pid)) {
        g_printerr("[NiOn] Tor did not stop within %d ms; forcing exit.\n",
                   NION_TOR_GRACEFUL_SHUTDOWN_MS);
        g_subprocess_force_exit(app->tor_process);
        nion_wait_for_pid_exit(pid, 500);
    }

    /* Reap the child if the async waiter has not already done so. */
    g_subprocess_wait(app->tor_process, NULL, NULL);

    if (app->tor_runtime_file)
        g_unlink(app->tor_runtime_file);
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
    if (app->closed_tabs) {
        g_queue_free_full(app->closed_tabs, nion_closed_tab_free);
        app->closed_tabs = NULL;
    }
}

static void nion_prepare_appimage_webkit_sandbox(void)
{
    const gchar *appdir = g_getenv("APPDIR");
    if (!appdir || !g_path_is_absolute(appdir) ||
        !g_file_test(appdir, G_FILE_TEST_IS_DIR))
        return;

    /* WebKitGTK 6 keeps its WebProcess sandbox mandatory. The AppImage mount
     * is outside the normal system prefixes, so explicitly make the read-only
     * AppDir visible before any WebKit subprocess can be created. */
    WebKitWebContext *context = webkit_web_context_get_default();
    webkit_web_context_add_path_to_sandbox(context, appdir, TRUE);
}

static void on_activate(GtkApplication *application, gpointer user_data)
{
    NionApp *app = user_data;
    app->application = application;

    if (app->window) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }

    nion_apply_css();
    nion_prepare_appimage_webkit_sandbox();
    nion_prepare_dirs(app);
    nion_validate_cookie_store(app);
    nion_cleanup_stale_tor(app);
    nion_load_preferences(app);
    nion_load_bookmarks(app);
    nion_load_site_zoom(app);
    gboolean tor_port_ok = nion_choose_tor_port(app);
    nion_prepare_network(app);
    nion_install_actions(app);
    nion_build_ui(app);
    if (tor_port_ok)
        nion_start_tor(app);
    else
        nion_set_tor_error(app, "No free Tor SOCKS port was found in NiOn runtime range");
}

static void on_shutdown(GApplication *application, gpointer user_data)
{
    (void)application;
    nion_cleanup(user_data);
}

int main(int argc, char **argv)
{
    NionApp app = {0};
    GtkApplication *application = gtk_application_new(NION_APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_application_set_resource_base_path(G_APPLICATION(application), "/io/github/jeannesbryan/Nion");

    g_signal_connect(application, "activate", G_CALLBACK(on_activate), &app);
    g_signal_connect(application, "shutdown", G_CALLBACK(on_shutdown), &app);

    int status = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return status;
}
