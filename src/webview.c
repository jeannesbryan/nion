/* Copyright (C) 2026 Jeannes Bryan */

/* WebKitWebView signal handlers (extracted from src/main.c, Phase 5b,
 * NiOn 2.0.0).

 * UI/navigation operations go through registered callbacks (see webview.h)
 * so this module never calls into main.c code directly.  The webview
 * creation hub (nion_new_tab_internal) still lives in main.c. */

#include "config.h"
#include "webview.h"
#include "types.h"
#include "navigation.h"
#include "per-site.h"
#include "content-filter.h"
#include "session.h"
#include "util.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib.h>
#include <string.h>

static NionWebviewCallbacks s_webview_callbacks;

void nion_webview_set_callbacks(const NionWebviewCallbacks *callbacks)
{
    if (callbacks)
        s_webview_callbacks = *callbacks;
}

static NionTab * nion_new_tab_internal(NionApp *app,
                              const gchar *uri,
                              gboolean select,
                              WebKitWebView *related_view)
{
    return s_webview_callbacks.new_tab_internal ? s_webview_callbacks.new_tab_internal(app, uri, select, related_view) : NULL;
}

static NionTab * nion_current_tab(NionApp *app)
{
    return s_webview_callbacks.current_tab ? s_webview_callbacks.current_tab(app) : NULL;
}

static void nion_set_status(NionApp *app, const gchar *text)
{
    if (s_webview_callbacks.set_status) s_webview_callbacks.set_status(app, text);
}

static void nion_update_controls(NionApp *app)
{
    if (s_webview_callbacks.update_controls) s_webview_callbacks.update_controls(app);
}

static void nion_update_window_title(NionApp *app, NionTab *tab)
{
    if (s_webview_callbacks.update_window_title) s_webview_callbacks.update_window_title(app, tab);
}

static void nion_update_progress(NionApp *app, NionTab *tab)
{
    if (s_webview_callbacks.update_progress) s_webview_callbacks.update_progress(app, tab);
}

static void nion_update_site_info(NionApp *app)
{
    if (s_webview_callbacks.update_site_info) s_webview_callbacks.update_site_info(app);
}

static void nion_update_bookmark_button(NionApp *app)
{
    if (s_webview_callbacks.update_bookmark_button) s_webview_callbacks.update_bookmark_button(app);
}

static void nion_clear_retry(NionTab *tab)
{
    if (s_webview_callbacks.clear_retry) s_webview_callbacks.clear_retry(tab);
}

static void nion_reload_crashed_tab(NionTab *tab)
{
    if (s_webview_callbacks.reload_crashed_tab) s_webview_callbacks.reload_crashed_tab(tab);
}

static void nion_request_new_identity(NionApp *app)
{
    if (s_webview_callbacks.request_new_identity) s_webview_callbacks.request_new_identity(app);
}

static gchar * nion_tab_fallback_title(NionTab *tab)
{
    return s_webview_callbacks.tab_fallback_title ? s_webview_callbacks.tab_fallback_title(tab) : NULL;
}

static gboolean nion_tab_has_mixed_content(const NionTab *tab)
{
    return s_webview_callbacks.tab_has_mixed_content ? s_webview_callbacks.tab_has_mixed_content(tab) : FALSE;
}

static void nion_show_error_page(NionTab *tab,
                              const gchar *category,
                              const gchar *heading,
                              const gchar *detail,
                              const gchar *failing_uri,
                              gboolean preserve_failing_uri)
{
    if (s_webview_callbacks.show_error_page) s_webview_callbacks.show_error_page(tab, category, heading, detail, failing_uri, preserve_failing_uri);
}

static void nion_detect_onion_location(NionTab *tab)
{
    if (s_webview_callbacks.detect_onion_location) s_webview_callbacks.detect_onion_location(tab);
}

static void nion_set_onion_location(NionTab *tab, const gchar *candidate)
{
    if (s_webview_callbacks.set_onion_location) s_webview_callbacks.set_onion_location(tab, candidate);
}

static gchar * nion_http_origin_key(const gchar *uri)
{
    return s_webview_callbacks.http_origin_key ? s_webview_callbacks.http_origin_key(uri) : NULL;
}

static void nion_close_http_warning(NionTab *tab)
{
    if (s_webview_callbacks.close_http_warning) s_webview_callbacks.close_http_warning(tab);
}

static void nion_show_http_warning(NionTab *tab, const gchar *uri)
{
    if (s_webview_callbacks.show_http_warning) s_webview_callbacks.show_http_warning(tab, uri);
}

static void nion_show_external_protocol_prompt(NionTab *tab,
                                   const gchar *uri,
                                   const gchar *scheme)
{
    if (s_webview_callbacks.show_external_protocol_prompt) s_webview_callbacks.show_external_protocol_prompt(tab, uri, scheme);
}

/* ---- Signal handlers (source order preserved) ---- */

static const gchar *nion_web_process_reason_text(WebKitWebProcessTerminationReason reason)
{
    switch (reason) {
    case WEBKIT_WEB_PROCESS_CRASHED:
        return "The WebKit web process for this tab crashed.";
    case WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT:
        return "The WebKit web process was stopped after exceeding its memory limit.";
    case WEBKIT_WEB_PROCESS_TERMINATED_BY_API:
        return "The WebKit web process was terminated.";
    default:
        return "The WebKit web process ended unexpectedly.";
    }
}

static gchar *nion_web_process_recovery_html(WebKitWebProcessTerminationReason reason,
                                              const gchar *uri)
{
    gchar *safe_reason = g_markup_escape_text(nion_web_process_reason_text(reason), -1);
    gchar *safe_uri = g_markup_escape_text(uri ? uri : "", -1);
    gchar *html = g_strdup_printf(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta name='color-scheme' content='light dark'><title>Tab crashed — NiOn</title>"
        "<style>:root{color-scheme:light dark;--bg:#f5f5f5;--card:#fff;--fg:#171717;--muted:#666;--border:#d9d9d9;--soft:#ededed}"
        "@media(prefers-color-scheme:dark){:root{--bg:#101010;--card:#181818;--fg:#eee;--muted:#aaa;--border:#333;--soft:#222}}"
        "*{box-sizing:border-box}html,body{height:100%%;margin:0;font-family:system-ui,sans-serif}"
        "body{display:grid;place-items:center;background:var(--bg);color:var(--fg);padding:24px}"
        "main{width:min(680px,100%%);border:1px solid var(--border);background:var(--card);border-radius:14px;padding:24px}"
        ".tag{text-transform:uppercase;letter-spacing:.08em;font-size:.72rem;color:var(--muted);font-weight:700}"
        "h1{font-size:1.8rem;margin:.55rem 0 1rem}p{line-height:1.6;color:var(--muted)}"
        "code{display:block;overflow-wrap:anywhere;padding:.65rem .75rem;border-radius:8px;background:var(--soft);color:var(--fg)}"
        "a{display:inline-block;margin-top:1rem;padding:.65rem .9rem;border:1px solid var(--border);border-radius:8px;color:var(--fg);text-decoration:none;font-weight:700}"
        "</style></head><body><main><div class='tag'>Web process recovery</div>"
        "<h1>This tab stopped unexpectedly</h1><p>%s</p><p><code>%s</code></p>"
        "<p>NiOn itself is still running. Reloading creates a fresh WebKit process and keeps the normal Tor-only navigation checks.</p>"
        "<a href='nion://reload-crashed/'>Reload Tab</a></main></body></html>",
        safe_reason, safe_uri);
    g_free(safe_reason);
    g_free(safe_uri);
    return html;
}

void on_webview_title_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
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

void on_webview_favicon_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
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

void on_webview_uri_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
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

void on_back_forward_list_changed(WebKitBackForwardList *list,
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

void on_webview_progress_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
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

void on_webview_insecure_content_detected(WebKitWebView *web_view,
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

void on_webview_web_process_terminated(WebKitWebView *web_view,
                                               WebKitWebProcessTerminationReason reason,
                                               gpointer user_data)
{
    NionTab *tab = user_data;
    if (!tab || !tab->app || tab->app->shutting_down)
        return;

    /* Avoid a recovery-page respawn loop if even the fresh process fails. */
    if (tab->web_process_terminated) {
        if (nion_current_tab(tab->app) == tab)
            nion_set_status(tab->app, "● TOR CONNECTED — WEB PROCESS RECOVERY FAILED");
        return;
    }

    const gchar *current_uri = tab->display_uri_override && *tab->display_uri_override
        ? tab->display_uri_override
        : webkit_web_view_get_uri(web_view);
    gchar *validation = NULL;
    gboolean keep_uri = current_uri && *current_uri &&
        !g_str_equal(current_uri, "about:blank") &&
        nion_validate_uri(current_uri, &validation);
    g_free(validation);

    g_clear_pointer(&tab->web_process_uri, g_free);
    if (keep_uri)
        tab->web_process_uri = g_strdup(current_uri);

    nion_clear_retry(tab);
    if (tab->http_warning_decision) {
        webkit_policy_decision_ignore(tab->http_warning_decision);
        g_clear_object(&tab->http_warning_decision);
    }
    nion_close_http_warning(tab);
    g_clear_pointer(&tab->http_warning_uri, g_free);

    tab->web_process_terminated = TRUE;
    tab->home_page = FALSE;
    tab->error_page = TRUE;
    tab->load_failed = TRUE;
    tab->connection_committed = FALSE;
    tab->mixed_content_displayed = FALSE;
    tab->mixed_content_run = FALSE;
    tab->mixed_content_other = FALSE;
    g_clear_pointer(&tab->display_uri_override, g_free);
    if (tab->web_process_uri)
        tab->display_uri_override = g_strdup(tab->web_process_uri);

    gchar *html = nion_web_process_recovery_html(reason, tab->web_process_uri);
    webkit_web_view_set_zoom_level(web_view, 1.0);
    webkit_web_view_load_html(web_view, html, "about:blank");
    g_free(html);

    if (nion_current_tab(tab->app) == tab) {
        const gchar *status = reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT
            ? "● TOR CONNECTED — TAB WEB PROCESS HIT MEMORY LIMIT — RELOAD AVAILABLE"
            : "● TOR CONNECTED — TAB WEB PROCESS TERMINATED — RELOAD AVAILABLE";
        nion_set_status(tab->app, status);
    }
    nion_schedule_session_save(tab->app);
    nion_update_controls(tab->app);
}

void on_webview_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data)
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
        nion_apply_site_javascript(tab, committed_uri);
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

gboolean on_webview_load_failed(WebKitWebView *web_view,
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

gboolean on_webview_tls_failed(WebKitWebView *web_view,
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

gboolean on_webview_decide_policy(WebKitWebView *web_view,
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
    gboolean user_gesture = action && webkit_navigation_action_is_user_gesture(action);

    /* Defense in depth on top of javascript-can-open-windows-automatically=FALSE:
     * a new browsing context is accepted only when WebKit attributes it to a
     * direct user gesture. target=_blank links clicked by the user still open
     * as NiOn tabs; script-driven popup/window attempts are discarded. */
    if (decision_type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION && !user_gesture) {
        webkit_policy_decision_ignore(decision);
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — POPUP BLOCKED (NO USER GESTURE)"
            : "○ TOR OFFLINE — POPUP BLOCKED");
        return TRUE;
    }

    /* External protocol escape guard. HTTP/HTTPS remain inside NiOn. file:,
     * javascript:, data:, blob:, about: and nion: are never handed to the OS.
     * Other schemes may leave NiOn only after a direct user gesture and an
     * explicit warning that Tor-only guarantees end at the application edge. */
    gchar *external_scheme = nion_external_protocol_scheme(uri);
    if (external_scheme) {
        webkit_policy_decision_ignore(decision);
        if (user_gesture) {
            nion_show_external_protocol_prompt(tab, uri, external_scheme);
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — EXTERNAL APPLICATION CONFIRMATION REQUIRED"
                : "○ TOR OFFLINE — EXTERNAL APPLICATION CONFIRMATION REQUIRED");
        } else {
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — EXTERNAL PROTOCOL BLOCKED (NO USER GESTURE)"
                : "○ TOR OFFLINE — EXTERNAL PROTOCOL BLOCKED");
        }
        g_free(external_scheme);
        return TRUE;
    }

    /* Internal recovery action from the local WebProcess crash page. Never
     * send the nion:// URI to the network or URI validator. */
    if (uri && g_str_has_prefix(uri, "nion://reload-crashed")) {
        webkit_policy_decision_ignore(decision);
        nion_reload_crashed_tab(tab);
        return TRUE;
    }

    /* Internal action from the Start Page ("New Identity" button). Only a
     * direct user click may rotate the circuit; an auto-navigation from any
     * page (remote or local) without a gesture is ignored so a site cannot
     * force repeated rotations. */
    if (uri && g_str_has_prefix(uri, "nion://new-identity")) {
        webkit_policy_decision_ignore(decision);
        if (user_gesture)
            nion_request_new_identity(tab->app);
        return TRUE;
    }

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

    if (decision_type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
        nion_apply_content_blocking(tab, uri);
        nion_apply_site_javascript(tab, uri);
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

    nion_policy_decision_use_for_uri(tab, decision, uri);
    return TRUE;
}

static void nion_context_menu_relabel_stock(WebKitContextMenu *context_menu,
                                             WebKitContextMenuAction stock_action,
                                             const gchar *label)
{
    /* webkit_context_menu_get_items() returns WebKit-owned storage. Never free
     * or mutate that GList directly. Iterate through the indexed API instead,
     * then replace the matching stock item at the same position. */
    guint n_items = webkit_context_menu_get_n_items(context_menu);

    for (guint position = 0; position < n_items; position++) {
        WebKitContextMenuItem *item =
            webkit_context_menu_get_item_at_position(context_menu, position);
        if (!item || webkit_context_menu_item_get_stock_action(item) != stock_action)
            continue;

        WebKitContextMenuItem *replacement =
            webkit_context_menu_item_new_from_stock_action_with_label(stock_action, label);

        /* ContextMenuItem derives from GInitiallyUnowned. Sink our reference
         * before handing it to WebKit so the ownership is unambiguous across
         * WebKitGTK versions, then release only our own reference. */
        g_object_ref_sink(replacement);
        webkit_context_menu_remove(context_menu, item);
        webkit_context_menu_insert(context_menu, replacement, (gint)position);
        g_object_unref(replacement);
        break;
    }
}

gboolean on_webview_context_menu(WebKitWebView *web_view,
                                        WebKitContextMenu *context_menu,
                                        WebKitHitTestResult *hit_test_result,
                                        gpointer user_data)
{
    (void)web_view;
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
    g_object_ref_sink(separator);
    webkit_context_menu_append(context_menu, separator);
    g_object_unref(separator);

    GAction *print_action = g_action_map_lookup_action(
        G_ACTION_MAP(tab->app->window), "print");
    if (print_action) {
        WebKitContextMenuItem *print_item = webkit_context_menu_item_new_from_gaction(
            print_action, "Print / Save as PDF…", NULL);
        g_object_ref_sink(print_item);
        webkit_context_menu_append(context_menu, print_item);
        g_object_unref(print_item);
    }

    return FALSE;
}

WebKitWebView *on_webview_create(WebKitWebView *web_view,
                                        WebKitNavigationAction *navigation_action,
                                        gpointer user_data)
{
    NionApp *app = user_data;

    /* ::decide-policy is the primary popup gate. Repeat the direct-user-gesture
     * requirement here so a future policy-path regression cannot silently
     * recreate script-driven windows. */
    if (!navigation_action || !webkit_navigation_action_is_user_gesture(navigation_action)) {
        if (app)
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — POPUP BLOCKED (NO USER GESTURE)"
                : "○ TOR OFFLINE — POPUP BLOCKED");
        return NULL;
    }

    /* WebKit requires the WebView returned from ::create to be related to
     * the opener. This is the path used by target=_blank/window.open() and
     * by WebKit's stock "Open ... in New Window" actions that NiOn maps to
     * tabs. Creating an unrelated WebView here can violate WebKit's popup
     * lifecycle/process assumptions and crash the application.
     *
     * Empty string keeps the related view unloaded until WebKit supplies the
     * requested target navigation; do not load NiOn's home page first. */
    NionTab *tab = nion_new_tab_internal(app, "", TRUE, web_view);
    if (tab) {
        tab->home_page = FALSE;
        tab->error_page = FALSE;
    }
    return tab ? tab->web_view : NULL;
}
