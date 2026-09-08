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

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select);
static NionTab *nion_new_tab_internal(NionApp *app, const gchar *uri, gboolean select,
                                      WebKitWebView *related_view);
static void nion_update_controls(NionApp *app);
static void nion_load_home(NionTab *tab);
static void nion_refresh_home_pages(NionApp *app);
static void nion_prepare_normal_navigation(NionTab *tab);
static void nion_reload_crashed_tab(NionTab *tab);
static void nion_close_http_warning(NionTab *tab);
static void nion_load_uri(NionTab *tab, const gchar *uri);
static void nion_stop_all_web_activity(NionApp *app);
static void nion_update_onion_button(NionApp *app);
static void nion_detect_onion_location(NionTab *tab);
static void nion_print_tab(NionTab *tab);
static void nion_build_ui(NionApp *app);
static void nion_find_open(NionApp *app);
static void nion_find_close(NionApp *app);
static void nion_find_run(NionApp *app);
static void on_find_found(WebKitFindController *controller, guint match_count, gpointer user_data);
static void on_find_failed(WebKitFindController *controller, gpointer user_data);
static void nion_set_zoom(NionApp *app, gdouble zoom);
static void nion_prepare_content_filter(NionApp *app);
static const gchar *nion_search_template(const NionApp *app);
static gchar *nion_http_origin_key(const gchar *uri);
static void nion_show_http_warning(NionTab *tab, const gchar *uri);
static void nion_show_external_protocol_prompt(NionTab *tab, const gchar *uri, const gchar *scheme);

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

static gchar *nion_external_protocol_preview(const gchar *uri)
{
    if (!uri || !*uri)
        return g_strdup("(empty target)");

    if (!g_utf8_validate(uri, -1, NULL))
        return g_strdup("(target omitted: invalid UTF-8)");

    glong chars = g_utf8_strlen(uri, -1);
    if (chars <= 160)
        return g_strdup(uri);

    const gchar *end = g_utf8_offset_to_pointer(uri, 157);
    gchar *prefix = g_strndup(uri, (gsize)(end - uri));
    gchar *preview = g_strconcat(prefix, "…", NULL);
    g_free(prefix);
    return preview;
}

static void nion_external_protocol_prompt_free(NionExternalProtocolPrompt *prompt)
{
    if (!prompt)
        return;
    NionTab *tab = prompt->page
        ? g_object_get_data(G_OBJECT(prompt->page), "nion-tab") : NULL;
    if (tab && tab->app)
        tab->app->external_protocol_prompt_open = FALSE;
    g_clear_object(&prompt->page);
    g_clear_pointer(&prompt->uri, g_free);
    g_clear_pointer(&prompt->scheme, g_free);
    g_free(prompt);
}

static void on_external_protocol_launch_finished(GObject *source,
                                                 GAsyncResult *result,
                                                 gpointer user_data)
{
    (void)source;
    NionExternalProtocolPrompt *prompt = user_data;
    GError *error = NULL;
    gboolean launched = g_app_info_launch_default_for_uri_finish(result, &error);
    NionTab *tab = prompt && prompt->page
        ? g_object_get_data(G_OBJECT(prompt->page), "nion-tab") : NULL;

    if (tab && tab->app) {
        if (launched) {
            nion_set_status(tab->app, tab->app->tor_ready
                ? "● TOR CONNECTED — EXTERNAL APPLICATION OPENED OUTSIDE NION"
                : "○ TOR OFFLINE — EXTERNAL APPLICATION OPENED OUTSIDE NION");
        } else {
            gchar *status = g_strdup_printf("%s — EXTERNAL APPLICATION FAILED: %s",
                tab->app->tor_ready ? "● TOR CONNECTED" : "○ TOR OFFLINE",
                (error && error->message) ? error->message : "no handler available");
            nion_set_status(tab->app, status);
            g_free(status);
        }
    }

    g_clear_error(&error);
    nion_external_protocol_prompt_free(prompt);
}

static void on_external_protocol_prompt_chosen(GObject *source,
                                               GAsyncResult *result,
                                               gpointer user_data)
{
    NionExternalProtocolPrompt *prompt = user_data;
    GError *error = NULL;
    gint choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
    NionTab *tab = prompt && prompt->page
        ? g_object_get_data(G_OBJECT(prompt->page), "nion-tab") : NULL;

    if (!error && choice == 1 && tab && tab->app && !tab->app->shutting_down) {
        nion_set_status(tab->app,
            tab->app->tor_ready
                ? "● TOR CONNECTED — OPENING EXTERNAL APPLICATION (OUTSIDE TOR GUARANTEE)"
                : "○ TOR OFFLINE — OPENING EXTERNAL APPLICATION (OUTSIDE TOR GUARANTEE)");
        g_app_info_launch_default_for_uri_async(prompt->uri, NULL, NULL,
                                                on_external_protocol_launch_finished,
                                                prompt);
        g_clear_error(&error);
        return;
    }

    if (tab && tab->app) {
        nion_set_status(tab->app,
            tab->app->tor_ready
                ? "● TOR CONNECTED — EXTERNAL APPLICATION BLOCKED"
                : "○ TOR OFFLINE — EXTERNAL APPLICATION BLOCKED");
    }
    g_clear_error(&error);
    nion_external_protocol_prompt_free(prompt);
}

static void nion_show_external_protocol_prompt(NionTab *tab,
                                               const gchar *uri,
                                               const gchar *scheme)
{
    if (!tab || !tab->app || !tab->page || !uri || !*uri || !scheme || !*scheme)
        return;
    if (tab->app->external_protocol_prompt_open) {
        nion_set_status(tab->app, tab->app->tor_ready
            ? "● TOR CONNECTED — EXTERNAL APPLICATION REQUEST ALREADY PENDING"
            : "○ TOR OFFLINE — EXTERNAL APPLICATION REQUEST ALREADY PENDING");
        return;
    }
    tab->app->external_protocol_prompt_open = TRUE;

    gchar *preview = nion_external_protocol_preview(uri);
    gchar *message = g_strdup_printf("Open an external application for %s:?", scheme);
    gchar *detail = g_strdup_printf(
        "This action leaves NiOn. The external application is not controlled by NiOn and may access the network without Tor.\n\nTarget: %s",
        preview);

    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_detail(dialog, detail);
    const char *buttons[] = { "Cancel", "Open Anyway", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    NionExternalProtocolPrompt *prompt = g_new0(NionExternalProtocolPrompt, 1);
    prompt->page = g_object_ref(tab->page);
    prompt->uri = g_strdup(uri);
    prompt->scheme = g_strdup(scheme);

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(tab->app->window), NULL,
                            on_external_protocol_prompt_chosen, prompt);
    g_object_unref(dialog);
    g_free(detail);
    g_free(message);
    g_free(preview);
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
        detail = app->is_private
            ? g_strdup("Private browsing is active. Website data is ephemeral and is discarded when this private window closes. NiOn still routes browsing through Tor.")
            : g_strdup("Open a clearnet website, a Tor v3 .onion address, or type a search query. NiOn routes browsing through Tor.");
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
    g_clear_object(&tab->home_return_item);
    tab->web_process_terminated = FALSE;
    g_clear_pointer(&tab->web_process_uri, g_free);
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
    WebKitSettings *settings = webkit_web_view_get_settings(tab->web_view);
    if (settings)
        webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_web_view_set_zoom_level(tab->web_view, 1.0);
    webkit_web_view_load_html(tab->web_view, html, "about:blank");
    g_free(html);
}

static void nion_reload_crashed_tab(NionTab *tab)
{
    if (!tab || !tab->web_process_terminated)
        return;

    gchar *uri = g_strdup(tab->web_process_uri);
    NionApp *app = tab->app;

    if (uri && *uri && (!app || !app->tor_ready)) {
        if (app)
            nion_set_status(app, "○ TOR OFFLINE — CRASHED TAB RELOAD BLOCKED");
        g_free(uri);
        return;
    }

    nion_prepare_normal_navigation(tab);
    if (uri && *uri) {
        nion_set_status(app, "● TOR CONNECTED — RELOADING CRASHED TAB");
        webkit_web_view_load_uri(tab->web_view, uri);
    } else {
        nion_load_home(tab);
    }
    g_free(uri);
    if (app)
        nion_update_controls(app);
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
        webkit_web_view_set_camera_capture_state(
            tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
        webkit_web_view_set_microphone_capture_state(
            tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
    }

    /* Temporary permission grants are meaningful only while this window is
     * online through its Tor-backed session. Drop them on fail-closed so a
     * stale permission cannot silently survive a Tor failure/recovery. */
    if (app->temporary_permissions)
        g_hash_table_remove_all(app->temporary_permissions);
}

static void nion_load_uri(NionTab *tab, const gchar *uri)
{
    if (!tab || !uri || !*uri)
        return;

    nion_prepare_normal_navigation(tab);
    nion_apply_site_javascript(tab, uri);
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

static gboolean nion_tab_can_go_back(NionTab *tab)
{
    if (!tab)
        return FALSE;
    if (tab->home_page && tab->home_return_item)
        return TRUE;
    return webkit_web_view_can_go_back(tab->web_view);
}

static void nion_go_back(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || !app->tor_ready || !nion_tab_can_go_back(tab))
        return;

    if (tab->home_page && tab->home_return_item) {
        WebKitBackForwardListItem *target = g_object_ref(tab->home_return_item);
        nion_prepare_normal_navigation(tab);
        webkit_web_view_go_to_back_forward_list_item(tab->web_view, target);
        g_object_unref(target);
        return;
    }

    nion_prepare_normal_navigation(tab);
    webkit_web_view_go_back(tab->web_view);
}

static void on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_go_back(user_data);
}

static void on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        nion_prepare_normal_navigation(tab);
        webkit_web_view_go_forward(tab->web_view);
    }
}

static void on_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready) {
        if (tab->web_process_terminated) {
            nion_reload_crashed_tab(tab);
            return;
        }
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

    if (!tab->home_page) {
        WebKitBackForwardList *history = webkit_web_view_get_back_forward_list(tab->web_view);
        WebKitBackForwardListItem *current = history
            ? webkit_back_forward_list_get_current_item(history)
            : NULL;
        g_set_object(&tab->home_return_item, current);
    }

    nion_clear_retry(tab);
    nion_load_home(tab);
    nion_update_controls(app);
    nion_schedule_session_save(app);
}

static void nion_update_window_title(NionApp *app, NionTab *tab)
{
    if (!tab) {
        gtk_window_set_title(GTK_WINDOW(app->window), app->is_private ? "NiOn — Private" : "NiOn");
        return;
    }

    const gchar *title = webkit_web_view_get_title(tab->web_view);
    gchar *fallback = NULL;
    if (!title || !*title) {
        fallback = nion_tab_fallback_title(tab);
        title = fallback;
    }

    if (g_str_equal(title, "NiOn")) {
        gtk_window_set_title(GTK_WINDOW(app->window), app->is_private ? "NiOn — Private" : "NiOn");
    } else {
        gchar *window_title = g_strdup_printf(app->is_private ? "%s — NiOn Private" : "%s — NiOn", title);
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

static void nion_update_controls(NionApp *app)
{
    if (!app->notebook)
        return;

    NionTab *tab = nion_current_tab(app);
    gboolean usable = tab != NULL && app->tor_ready;

    gtk_widget_set_sensitive(app->address, usable);
    gtk_widget_set_sensitive(app->reload_button, usable);
    gtk_widget_set_sensitive(app->back_button,
        usable && nion_tab_can_go_back(tab));
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
    nion_update_site_info_button(app);
    if (app->site_info_window && gtk_widget_get_visible(app->site_info_window))
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

    nion_policy_decision_use_for_uri(tab, tab->http_warning_decision, tab->http_warning_uri);
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
    gtk_label_set_wrap_mode(GTK_LABEL(url_label), PANGO_WRAP_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(url_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(url_label), 60);
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

/* WebKitGTK 6 / GTK4 removed the GdkEvent parameter from
 * WebKitWebView::context-menu. Keep this callback signature exact: an
 * extra legacy argument shifts hit_test_result/user_data and can turn
 * user_data into an invalid pointer as soon as the menu is opened. */

static void nion_permission_prompt_free(NionPermissionPrompt *prompt)
{
    if (!prompt)
        return;
    g_clear_object(&prompt->request);
    g_clear_object(&prompt->page);
    g_clear_pointer(&prompt->origin, g_free);
    g_free(prompt);
}

static gchar *nion_permission_prompt_label(guint mask)
{
    if ((mask & NION_PERMISSION_CAMERA) && (mask & NION_PERMISSION_MICROPHONE))
        return g_strdup("camera and microphone");
    if (mask & NION_PERMISSION_CAMERA)
        return g_strdup("camera");
    if (mask & NION_PERMISSION_MICROPHONE)
        return g_strdup("microphone");
    if (mask & NION_PERMISSION_GEOLOCATION)
        return g_strdup("location");
    if (mask & NION_PERMISSION_NOTIFICATIONS)
        return g_strdup("notifications");
    return g_strdup("this permission");
}

static void on_permission_prompt_chosen(GObject *source,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
    NionPermissionPrompt *prompt = user_data;
    GError *error = NULL;
    gint choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    NionTab *tab = prompt && prompt->page
        ? g_object_get_data(G_OBJECT(prompt->page), "nion-tab") : NULL;
    gboolean same_origin = FALSE;
    if (tab && tab->web_view) {
        gchar *current_origin = nion_web_origin_key_for_uri(
            webkit_web_view_get_uri(tab->web_view));
        same_origin = current_origin &&
            g_strcmp0(current_origin, prompt->origin) == 0;
        g_free(current_origin);
    }

    if (!error && choice == 1 && same_origin && tab && tab->app && tab->app->tor_ready) {
        nion_allow_permission_mask_temporarily(tab->app, prompt->origin,
                                               prompt->permission_mask);
        webkit_permission_request_allow(prompt->request);
        if (nion_current_tab(tab->app) == tab) {
            nion_set_status(tab->app,
                tab->app->is_private
                    ? "● TOR CONNECTED — PRIVATE PERMISSION ALLOWED TEMPORARILY"
                    : "● TOR CONNECTED — SITE PERMISSION ALLOWED TEMPORARILY");
            nion_update_site_info(tab->app);
        }
    } else {
        webkit_permission_request_deny(prompt->request);
        if (tab && tab->app && nion_current_tab(tab->app) == tab) {
            nion_set_status(tab->app, tab->app->tor_ready
                ? "● TOR CONNECTED — SITE PERMISSION BLOCKED"
                : "○ TOR NOT READY — SITE PERMISSION BLOCKED");
            nion_update_site_info(tab->app);
        }
    }

    g_clear_error(&error);
    nion_permission_prompt_free(prompt);
}

static void nion_show_permission_prompt(NionTab *tab,
                                        WebKitPermissionRequest *request,
                                        const gchar *origin,
                                        guint permission_mask)
{
    if (!tab || !tab->app || !request || !origin || permission_mask == 0) {
        if (request)
            webkit_permission_request_deny(request);
        return;
    }

    gchar *permission = nion_permission_prompt_label(permission_mask);
    gchar *message = g_strdup_printf("Allow %s to access your %s?", origin, permission);
    gchar *detail = NULL;
    if (permission_mask & NION_PERMISSION_GEOLOCATION) {
        detail = g_strdup(
            "Location access can reveal your real physical location to the site. "
            "Allow temporarily lasts only until this NiOn window closes.");
    } else if (permission_mask & (NION_PERMISSION_CAMERA | NION_PERMISSION_MICROPHONE)) {
        detail = g_strdup(
            "Device capture can expose sensitive audio/video to the site. "
            "WebRTC peer connections remain disabled. Allow temporarily lasts only until this NiOn window closes.");
    } else {
        detail = g_strdup(
            "Notifications can expose site activity through your desktop notification system. "
            "Allow temporarily lasts only until this NiOn window closes.");
    }

    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_set_detail(dialog, detail);
    const char *buttons[] = { "Block", "Allow temporarily", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    NionPermissionPrompt *prompt = g_new0(NionPermissionPrompt, 1);
    prompt->request = g_object_ref(request);
    prompt->page = g_object_ref(tab->page);
    prompt->origin = g_strdup(origin);
    prompt->permission_mask = permission_mask;

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(tab->app->window), NULL,
                            on_permission_prompt_chosen, prompt);
    g_object_unref(dialog);
    g_free(detail);
    g_free(message);
    g_free(permission);
}

static gboolean on_permission_request(WebKitWebView *web_view,
                                      WebKitPermissionRequest *request,
                                      gpointer user_data)
{
    NionTab *tab = user_data;
    if (!tab || !tab->app || !request) {
        if (request)
            webkit_permission_request_deny(request);
        return TRUE;
    }

    NionApp *app = tab->app;
    const gchar *uri = webkit_web_view_get_uri(web_view);
    gchar *origin = nion_web_origin_key_for_uri(uri);
    if (!origin) {
        webkit_permission_request_deny(request);
        return TRUE;
    }

    guint permission_mask = 0;
    if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request)) {
        permission_mask = NION_PERMISSION_GEOLOCATION;
    } else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request)) {
        permission_mask = NION_PERMISSION_NOTIFICATIONS;
    } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
        WebKitUserMediaPermissionRequest *media = WEBKIT_USER_MEDIA_PERMISSION_REQUEST(request);
        if (webkit_user_media_permission_is_for_display_device(media)) {
            /* Screen/display capture is outside the Stage-1 permission scope
             * and stays hard-blocked. */
            webkit_permission_request_deny(request);
            g_free(origin);
            return TRUE;
        }
        if (webkit_user_media_permission_is_for_video_device(media))
            permission_mask |= NION_PERMISSION_CAMERA;
        if (webkit_user_media_permission_is_for_audio_device(media))
            permission_mask |= NION_PERMISSION_MICROPHONE;
    } else if (WEBKIT_IS_DEVICE_INFO_PERMISSION_REQUEST(request)) {
        /* WebKit may request device-info access after camera/microphone access.
         * Allow it only if this origin already has a temporary media grant. */
        if (nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_CAMERA) ||
            nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_MICROPHONE))
            webkit_permission_request_allow(request);
        else
            webkit_permission_request_deny(request);
        g_free(origin);
        return TRUE;
    } else {
        /* Clipboard read, pointer lock, DRM, storage-access, XR and future
         * permission classes remain deny-by-default until explicitly scoped. */
        webkit_permission_request_deny(request);
        g_free(origin);
        return TRUE;
    }

    if (permission_mask == 0) {
        webkit_permission_request_deny(request);
        g_free(origin);
        return TRUE;
    }

    if (nion_permission_mask_is_temporarily_allowed(app, origin, permission_mask)) {
        webkit_permission_request_allow(request);
        g_free(origin);
        return TRUE;
    }

    nion_show_permission_prompt(tab, request, origin, permission_mask);
    g_free(origin);
    return TRUE;
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
    NionApp *app = user_data;
    if (!app || app->normalizing_tab_order)
        return;

    NionTab *tab = child ? g_object_get_data(G_OBJECT(child), "nion-tab") : NULL;
    if (tab) {
        gint pinned_count = nion_count_pinned_tabs(app);
        gint target = -1;
        if (tab->pinned && (gint)page_num >= pinned_count)
            target = MAX(pinned_count - 1, 0);
        else if (!tab->pinned && (gint)page_num < pinned_count)
            target = pinned_count;

        if (target >= 0 && target != (gint)page_num) {
            app->normalizing_tab_order = TRUE;
            gtk_notebook_reorder_child(notebook, child, target);
            app->normalizing_tab_order = FALSE;
        }
    }

    nion_schedule_session_save(app);
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

    nion_update_controls(app);
    nion_schedule_session_save(app);
    return tab;
}

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return nion_new_tab_internal(app, uri, select, NULL);
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
        if (tab->web_process_terminated) {
            nion_reload_crashed_tab(tab);
            return;
        }
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
        if (tab->web_process_terminated) {
            nion_reload_crashed_tab(tab);
            return;
        }
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
    nion_go_back(user_data);
}

static void action_forward(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (tab && app->tor_ready && webkit_web_view_can_go_forward(tab->web_view)) {
        nion_prepare_normal_navigation(tab);
        webkit_web_view_go_forward(tab->web_view);
    }
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
        "Tor: %s (Expert Bundle %s)\n\n"
        "Stable dependency baseline\n"
        "GTK: %s\n"
        "WebKitGTK: %s\n"
        "GLib: %s",
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
        NION_TOR_DAEMON_VERSION, NION_TOR_BROWSER_BUNDLE_VERSION,
        NION_GTK_TESTED_VERSION, NION_WEBKITGTK_TESTED_VERSION, NION_GLIB_TESTED_VERSION);
    gtk_about_dialog_set_system_information(GTK_ABOUT_DIALOG(dialog), system_info);
    g_free(system_info);

    gtk_window_present(GTK_WINDOW(dialog));
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
    gtk_widget_add_css_class(state,
                             (g_str_equal(status, "LIMITATION") ||
                              g_str_equal(status, "UNSAFE") ||
                              g_str_equal(status, "MISSING"))
                                 ? "warning" : "success");
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

    if (app->is_private) {
        gboolean ephemeral = app->network_session &&
            webkit_network_session_is_ephemeral(app->network_session);
        gboolean persistent_credentials = app->network_session &&
            webkit_network_session_get_persistent_credential_storage_enabled(app->network_session);

        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Private WebKit session", ephemeral ? "EPHEMERAL" : "UNSAFE",
            "Private Window creation is allowed only with an ephemeral WebKitNetworkSession. Cookies, cache, local storage, IndexedDB and other website data are not backed by NiOn's persistent profile."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Private credentials", persistent_credentials ? "UNSAFE" : "BLOCKED",
            "Persistent WebKit credential storage is disabled for Private Window and verified when the private network session is created."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Session / pinned / zoom persistence", "BLOCKED",
            "Private tabs and pinned state never enter session.ini, and Private Window does not read or write site-zoom.ini."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Private downloads", "MEMORY-ONLY",
            "Download rows, status and source URLs live only for this Private Window. Active partial downloads are cancelled and cleaned on close; completed files explicitly saved by the user remain on disk."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Closed-tab recovery", "MEMORY-ONLY",
            "Ctrl+Shift+T can recover a private tab only while this Private Window is alive. The closed-tab queue is erased when the window closes."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "Bookmarks / search preference", "GLOBAL BY DESIGN",
            "Bookmarks and the selected search engine are intentionally shared with normal NiOn. Adding a bookmark from Private Window therefore creates persistent global data."));
        gtk_box_append(GTK_BOX(box), nion_audit_row(
            "User-exported artifacts", "LIMITATION",
            "Completed downloads, saved/printed files, bookmarks and text explicitly copied to the system clipboard can outlive Private Window because the user exported them outside the ephemeral browser session."));
    }

    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Security level", nion_security_level_label(app->security_level),
        app->security_level == NION_SECURITY_STANDARD
            ? "Standard preserves NiOn's existing hardening and compatibility baseline."
            : (app->security_level == NION_SECURITY_SAFER
                ? "Safer keeps JavaScript and permission-gated MediaStream available, but blocks page fullscreen and autoplay by default."
                : "Safest disables JavaScript and MediaStream by default, blocks page fullscreen/autoplay, and permits JavaScript only through explicit per-site exceptions.")));

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
        "WebRTC peer connections", "BLOCKED",
        app->security_level == NION_SECURITY_SAFEST
            ? "Peer-to-peer WebRTC remains disabled, and Safest also disables MediaStream at the WebKit settings layer."
            : "Peer-to-peer WebRTC remains disabled. MediaStream exists only so camera/microphone requests can reach NiOn's permission gate."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Camera / microphone / geolocation / notifications", "PERMISSION-GATED",
        "The default is Block. A site can receive access only after an explicit Allow temporarily decision for the current NiOn window."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Per-site JavaScript", app->is_private ? "MEMORY-ONLY" : "LOCAL RULES",
        app->is_private
            ? "Private Window JavaScript overrides exist only in memory and disappear with the window. Safest defaults JavaScript off unless the site has an explicit memory-only enable rule."
            : "Per-site JavaScript overrides are stored locally in ~/.config/nion/site-javascript.ini. Standard/Safer default to enabled; Safest defaults to disabled and can persist explicit enable exceptions."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Tracking prevention", app->block_third_party_cookies ? "STRICT COOKIES" : "ITP ENABLED",
        app->block_third_party_cookies
            ? "Strict third-party-cookie blocking is enabled; WebKit ITP is disabled because upstream WebKit supersedes that cookie policy while ITP is active."
            : "WebKit Intelligent Tracking Prevention is enabled on this NetworkSession and handles tracking state/cookie restrictions at engine level."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Autoplay protection", app->is_private ? "MEMORY-ONLY EXCEPTIONS" : "LOCAL EXCEPTIONS",
        app->security_level == NION_SECURITY_STANDARD
            ? (app->is_private
                ? "Audible autoplay is blocked by default while muted autoplay is allowed; per-site Allow exceptions exist only for this Private Window."
                : "Audible autoplay is blocked by default while muted autoplay is allowed. Per-site Allow exceptions are stored in ~/.config/nion/autoplay.ini.")
            : (app->is_private
                ? "Safer/Safest block all autoplay by default; explicit per-site Allow exceptions remain memory-only in this Private Window."
                : "Safer/Safest block all autoplay by default; explicit per-site Allow exceptions are stored in ~/.config/nion/autoplay.ini.")));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Clipboard read", "BLOCKED",
        "JavaScript clipboard access is disabled and permission requests are denied."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "WebGL / WebAudio", "BLOCKED",
        "WebGL and WebAudio are disabled to reduce device/fingerprinting surface."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "External URI handlers", "USER-GESTURE + CONFIRM",
        "HTTP/HTTPS stay inside Tor-routed NiOn. file:, javascript:, data:, blob:, about: and nion: are never handed to the OS. Other schemes require a direct user gesture plus an explicit warning before NiOn asks the desktop to open an external application; that application is outside NiOn's Tor guarantee."));
    gtk_box_append(GTK_BOX(box), nion_audit_row(
        "Popup / new-window escape", "USER-GESTURE ONLY",
        "Automatic JavaScript popups remain disabled in WebKit settings, and NiOn additionally rejects NEW_WINDOW_ACTION decisions and ::create callbacks that are not attributed to a direct user gesture. User-clicked target=_blank links continue to open as NiOn tabs."));
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
        { "private-window", action_private_window, NULL, NULL, NULL, {0} },
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

static void nion_apply_css(void)
{
    const gchar *css =
        ".nion-toolbar { padding: 7px 8px; }"
        ".nion-private-toolbar { border-bottom: 2px solid alpha(currentColor,0.28); }"
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
        ".nion-tab-pinned { font-weight: 700; }"
        ".nion-tab-pin { font-size: 0.90em; }"
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

static void nion_build_ui(NionApp *app)
{
    app->window = gtk_application_window_new(app->application);
    gtk_window_set_title(GTK_WINDOW(app->window), app->is_private ? "NiOn — Private" : "NiOn");
    gtk_window_set_icon_name(GTK_WINDOW(app->window), NION_APP_ID);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1100, 760);
    nion_install_actions(app);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->window), root);

    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    app->toolbar = toolbar;
    gtk_widget_add_css_class(toolbar, "nion-toolbar");
    if (app->is_private)
        gtk_widget_add_css_class(toolbar, "nion-private-toolbar");
    gtk_box_append(GTK_BOX(root), toolbar);

    app->back_button = gtk_button_new_from_icon_name("go-previous-symbolic");
    app->forward_button = gtk_button_new_from_icon_name("go-next-symbolic");
    app->reload_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
    app->home_button = gtk_button_new_from_icon_name("go-home-symbolic");
    app->onion_button = gtk_button_new_with_label("Onion");
    gtk_widget_add_css_class(app->onion_button, "nion-onion-badge");
    gtk_widget_set_visible(app->onion_button, FALSE);

    /* Use a transient window instead of a GtkPopover. Overlay popovers above
     * an accelerated WebKitGTK view can flicker badly on lightweight X11 WMs.
     * A separate transient window avoids that compositing path completely. */
    app->site_info_button = gtk_button_new();
    app->site_info_icon = gtk_image_new_from_icon_name("dialog-information-symbolic");
    gtk_button_set_child(GTK_BUTTON(app->site_info_button), app->site_info_icon);
    gtk_widget_add_css_class(app->site_info_button, "flat");
    gtk_widget_set_sensitive(app->site_info_button, FALSE);

    app->site_info_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(app->site_info_window), "Site Information — NiOn");
    gtk_window_set_transient_for(GTK_WINDOW(app->site_info_window), GTK_WINDOW(app->window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(app->site_info_window), TRUE);
    /* Keep Site Information usable on small laptop displays.  The content
     * grows as protections are added, so never let its natural height force
     * the transient window beyond the screen.  GtkScrolledWindow owns the
     * viewport and begins scrolling once the compact content height is hit. */
    gtk_window_set_resizable(GTK_WINDOW(app->site_info_window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(app->site_info_window), 420, 500);
    GtkWidget *site_info_scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(site_info_scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(site_info_scroller), 360);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(site_info_scroller), 420);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(site_info_scroller), FALSE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(site_info_scroller), 280);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(site_info_scroller), 480);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(site_info_scroller), FALSE);
    gtk_widget_set_hexpand(site_info_scroller, TRUE);
    gtk_widget_set_vexpand(site_info_scroller, TRUE);

    GtkWidget *site_info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(site_info_box, 14);
    gtk_widget_set_margin_bottom(site_info_box, 14);
    gtk_widget_set_margin_start(site_info_box, 14);
    gtk_widget_set_margin_end(site_info_box, 14);
    /* Let the scrolled viewport determine width; do not impose an additional
     * child request that can combine with margins into a wider toplevel. */
    gtk_widget_set_size_request(site_info_box, -1, -1);

    app->site_info_title_label = gtk_label_new("Site information");
    gtk_label_set_xalign(GTK_LABEL(app->site_info_title_label), 0.0f);
    gtk_widget_add_css_class(app->site_info_title_label, "nion-site-info-title");
    gtk_box_append(GTK_BOX(site_info_box), app->site_info_title_label);
    gtk_box_append(GTK_BOX(site_info_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Host", &app->site_info_host_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Connection", &app->site_info_connection_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Route", &app->site_info_route_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Mixed content", &app->site_info_mixed_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Tracking protection", &app->site_info_tracking_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Security level", &app->site_info_security_label));

    gtk_box_append(GTK_BOX(site_info_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *content_blocking_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *content_blocking_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *content_blocking_title = gtk_label_new("Content blocking");
    app->site_info_content_blocking_status_label = gtk_label_new("Initializing bundled filter…");
    gtk_label_set_xalign(GTK_LABEL(content_blocking_title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(app->site_info_content_blocking_status_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(app->site_info_content_blocking_status_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(app->site_info_content_blocking_status_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(app->site_info_content_blocking_status_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(app->site_info_content_blocking_status_label), 42);
    gtk_widget_add_css_class(content_blocking_title, "nion-site-info-key");
    gtk_widget_add_css_class(app->site_info_content_blocking_status_label, "nion-site-info-value");
    gtk_widget_set_hexpand(content_blocking_text, TRUE);
    gtk_box_append(GTK_BOX(content_blocking_text), content_blocking_title);
    gtk_box_append(GTK_BOX(content_blocking_text), app->site_info_content_blocking_status_label);
    app->site_info_content_blocking_switch = gtk_switch_new();
    gtk_widget_set_valign(app->site_info_content_blocking_switch, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content_blocking_row), content_blocking_text);
    gtk_box_append(GTK_BOX(content_blocking_row), app->site_info_content_blocking_switch);
    gtk_box_append(GTK_BOX(site_info_box), content_blocking_row);
    g_signal_connect(app->site_info_content_blocking_switch, "notify::active",
                     G_CALLBACK(on_site_content_blocking_switch_notify), app);

    GtkWidget *javascript_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *javascript_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *javascript_title = gtk_label_new("JavaScript");
    app->site_info_javascript_status_label = gtk_label_new("Enabled");
    gtk_label_set_xalign(GTK_LABEL(javascript_title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(app->site_info_javascript_status_label), 0.0f);
    gtk_widget_add_css_class(javascript_title, "nion-site-info-key");
    gtk_label_set_wrap(GTK_LABEL(app->site_info_javascript_status_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(app->site_info_javascript_status_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(app->site_info_javascript_status_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(app->site_info_javascript_status_label), 42);
    gtk_widget_add_css_class(app->site_info_javascript_status_label, "nion-site-info-value");
    gtk_widget_set_hexpand(javascript_text, TRUE);
    gtk_box_append(GTK_BOX(javascript_text), javascript_title);
    gtk_box_append(GTK_BOX(javascript_text), app->site_info_javascript_status_label);
    app->site_info_javascript_switch = gtk_switch_new();
    gtk_widget_set_valign(app->site_info_javascript_switch, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(javascript_row), javascript_text);
    gtk_box_append(GTK_BOX(javascript_row), app->site_info_javascript_switch);
    gtk_box_append(GTK_BOX(site_info_box), javascript_row);
    g_signal_connect(app->site_info_javascript_switch, "notify::active",
                     G_CALLBACK(on_site_javascript_switch_notify), app);

    GtkWidget *autoplay_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *autoplay_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *autoplay_title = gtk_label_new("Allow autoplay for this site");
    app->site_info_autoplay_status_label = gtk_label_new("Audible autoplay blocked; muted autoplay allowed");
    gtk_label_set_xalign(GTK_LABEL(autoplay_title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(app->site_info_autoplay_status_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(app->site_info_autoplay_status_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(app->site_info_autoplay_status_label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(app->site_info_autoplay_status_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(app->site_info_autoplay_status_label), 42);
    gtk_widget_add_css_class(autoplay_title, "nion-site-info-key");
    gtk_widget_add_css_class(app->site_info_autoplay_status_label, "nion-site-info-value");
    gtk_widget_set_hexpand(autoplay_text, TRUE);
    gtk_box_append(GTK_BOX(autoplay_text), autoplay_title);
    gtk_box_append(GTK_BOX(autoplay_text), app->site_info_autoplay_status_label);
    app->site_info_autoplay_switch = gtk_switch_new();
    gtk_widget_set_valign(app->site_info_autoplay_switch, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(autoplay_row), autoplay_text);
    gtk_box_append(GTK_BOX(autoplay_row), app->site_info_autoplay_switch);
    gtk_box_append(GTK_BOX(site_info_box), autoplay_row);
    g_signal_connect(app->site_info_autoplay_switch, "notify::active",
                     G_CALLBACK(on_site_autoplay_switch_notify), app);

    GtkWidget *permissions_title = gtk_label_new("Temporary site permissions");
    gtk_label_set_xalign(GTK_LABEL(permissions_title), 0.0f);
    gtk_widget_add_css_class(permissions_title, "nion-site-info-title");
    gtk_box_append(GTK_BOX(site_info_box), permissions_title);
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Camera", &app->site_info_camera_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Microphone", &app->site_info_microphone_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Location", &app->site_info_geolocation_label));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Notifications", &app->site_info_notifications_label));
    app->site_info_permissions_reset_button = gtk_button_new_with_label("Reset temporary permissions");
    gtk_widget_set_halign(app->site_info_permissions_reset_button, GTK_ALIGN_START);
    gtk_widget_set_sensitive(app->site_info_permissions_reset_button, FALSE);
    g_signal_connect(app->site_info_permissions_reset_button, "clicked",
                     G_CALLBACK(on_site_permissions_reset_clicked), app);
    gtk_box_append(GTK_BOX(site_info_box), app->site_info_permissions_reset_button);

    app->site_info_forget_button = gtk_button_new_with_label("Forget This Site…");
    gtk_widget_set_halign(app->site_info_forget_button, GTK_ALIGN_START);
    gtk_widget_add_css_class(app->site_info_forget_button, "destructive-action");
    gtk_widget_set_sensitive(app->site_info_forget_button, FALSE);
    g_signal_connect(app->site_info_forget_button, "clicked",
                     G_CALLBACK(on_site_forget_clicked), app);
    gtk_box_append(GTK_BOX(site_info_box), app->site_info_forget_button);

    gtk_box_append(GTK_BOX(site_info_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(site_info_box), nion_site_info_row("Address", &app->site_info_uri_label));
    gtk_label_set_selectable(GTK_LABEL(app->site_info_uri_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(app->site_info_uri_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(app->site_info_uri_label), PANGO_WRAP_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(app->site_info_uri_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(app->site_info_uri_label), 48);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(site_info_scroller), site_info_box);
    gtk_window_set_child(GTK_WINDOW(app->site_info_window), site_info_scroller);
    g_signal_connect(app->site_info_window, "close-request",
                     G_CALLBACK(on_site_info_window_close_request), app);
    g_signal_connect(app->site_info_button, "clicked",
                     G_CALLBACK(on_site_info_button_clicked), app);

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
    g_menu_append(menu, "New Private Window", "win.private-window");
    g_menu_append(menu, "Find in Page", "win.find");
    g_menu_append(menu, "Reload Without Cache", "win.hard-reload");
    GMenu *zoom_menu = g_menu_new();
    g_menu_append(zoom_menu, "Zoom In", "win.zoom-in");
    g_menu_append(zoom_menu, "Zoom Out", "win.zoom-out");
    g_menu_append(zoom_menu, "Reset Zoom", "win.zoom-reset");
    g_menu_append_submenu(menu, "Zoom", G_MENU_MODEL(zoom_menu));
    g_object_unref(zoom_menu);
    g_menu_append(menu, "Fullscreen", "win.fullscreen");
    g_menu_append(menu, "Print / Save as PDF…", "win.print");
    g_menu_append(menu, "Bookmarks", "win.bookmarks");
    g_menu_append(menu, app->is_private ? "Private Downloads" : "Downloads", "win.downloads");
    g_menu_append(menu, "Preferences", "win.preferences");
    g_menu_append(menu, "Privacy & Leak Audit", "win.privacy-audit");
    g_menu_append(menu, "Clear Data for This Site…", "win.clear-site-data");
    g_menu_append(menu, "Forget This Site…", "win.forget-site");
    g_menu_append(menu, "Browsing Data…", "win.clear-data");
    g_menu_append(menu, "About NiOn", "win.about");
    g_menu_append(menu, app->is_private ? "Close Private Window" : "Exit", "win.exit");
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

    if (app->is_private || !nion_restore_saved_session(app))
        nion_new_tab(app, NULL, TRUE);

    /* Mark the live session dirty immediately. A graceful close writes it
     * back with clean-shutdown=true; a crash leaves the most recent dirty
     * snapshot available for recovery. */
    nion_save_session(app, FALSE);
    nion_update_controls(app);
    gtk_window_present(GTK_WINDOW(app->window));
    if (app->crash_recovery_decision_pending)
        nion_show_crash_recovery_prompt(app);
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
