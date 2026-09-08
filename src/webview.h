/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_WEBVIEW_H
#define NION_WEBVIEW_H

#include "types.h"

/* WebKitWebView signal handlers (extracted from src/main.c, Phase 5b,
 * NiOn 2.0.0).  The WebKitWebView creation hub (nion_new_tab_internal)
 * remains in main.c as the coordinator; handlers here are public so the
 * hub can connect them by name. */

/* UI/navigation operations that still live in main.c.  main.c registers
 * implementations so webview.c never calls into main.c directly. */
typedef struct {
    NionTab *(*new_tab_internal)(NionApp *app, const gchar *uri, gboolean select,
                                 WebKitWebView *related_view);
    NionTab *(*current_tab)(NionApp *app);
    void (*set_status)(NionApp *app, const gchar *text);
    void (*update_controls)(NionApp *app);
    void (*update_window_title)(NionApp *app, NionTab *tab);
    void (*update_progress)(NionApp *app, NionTab *tab);
    void (*update_site_info)(NionApp *app);
    void (*update_bookmark_button)(NionApp *app);
    void (*clear_retry)(NionTab *tab);
    void (*reload_crashed_tab)(NionTab *tab);
    gchar *(*tab_fallback_title)(NionTab *tab);
    gboolean (*tab_has_mixed_content)(const NionTab *tab);
    void (*show_error_page)(NionTab *tab, const gchar *category,
                            const gchar *heading, const gchar *detail,
                            const gchar *failing_uri, gboolean preserve_failing_uri);
    void (*detect_onion_location)(NionTab *tab);
    void (*set_onion_location)(NionTab *tab, const gchar *candidate);
    gchar *(*http_origin_key)(const gchar *uri);
    void (*close_http_warning)(NionTab *tab);
    void (*show_http_warning)(NionTab *tab, const gchar *uri);
    void (*show_external_protocol_prompt)(NionTab *tab, const gchar *uri,
                                           const gchar *scheme);
    /* v2.1: the internal Start Page's "New Identity" button routes here
     * (nion://new-identity), activating the window action. */
    void (*request_new_identity)(NionApp *app);
} NionWebviewCallbacks;

void nion_webview_set_callbacks(const NionWebviewCallbacks *callbacks);

/* Public signal handlers (connected by main.c's tab-creation hub). */
void on_webview_title_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_webview_favicon_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_webview_uri_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_back_forward_list_changed(WebKitBackForwardList *list, WebKitBackForwardListItem *item_added, GList *items_removed, gpointer user_data);
void on_webview_progress_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void on_webview_insecure_content_detected(WebKitWebView *web_view, WebKitInsecureContentEvent event, gpointer user_data);
void on_webview_web_process_terminated(WebKitWebView *web_view, WebKitWebProcessTerminationReason reason, gpointer user_data);
void on_webview_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data);
gboolean on_webview_load_failed(WebKitWebView *web_view, WebKitLoadEvent load_event, const gchar *failing_uri, GError *error, gpointer user_data);
gboolean on_webview_tls_failed(WebKitWebView *web_view, const gchar *failing_uri, GTlsCertificate *certificate, GTlsCertificateFlags errors, gpointer user_data);
gboolean on_webview_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType decision_type, gpointer user_data);
gboolean on_webview_context_menu(WebKitWebView *web_view, WebKitContextMenu *context_menu, WebKitHitTestResult *hit_test_result, gpointer user_data);
WebKitWebView *on_webview_create(WebKitWebView *web_view, WebKitNavigationAction *navigation_action, gpointer user_data);

#endif /* NION_WEBVIEW_H */
