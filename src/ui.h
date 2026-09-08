/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_UI_H
#define NION_UI_H

#include "types.h"
#include <gio/gio.h>

/* Core UI chrome: toolbar/window construction, address & navigation bar,
 * status/title/progress/control updates, find bar, zoom, dialogs (HTTP
 * warning, external protocol, permission, about, privacy audit) and the
 * remaining GAction handlers. Extracted from src/main.c, Phase 7, NiOn 2.0.0.
 *
 * This module never calls into main.c; the handful of tab/status/action-table
 * operations main.c still owns are injected through NionUiCallbacks. */

void nion_apply_css(void);
void nion_build_ui(NionApp *app);

/* UI state synchronizers (main.c registers them as the session / tab /
 * webview / app-lifecycle callback implementations). */
void nion_update_controls(NionApp *app);
void nion_update_window_title(NionApp *app, NionTab *tab);
void nion_update_progress(NionApp *app, NionTab *tab);
void nion_update_onion_button(NionApp *app);
void nion_load_uri(NionTab *tab, const gchar *uri);
void nion_load_home(NionTab *tab);
void nion_refresh_home_pages(NionApp *app);
void nion_prepare_normal_navigation(NionTab *tab);
void nion_reload_crashed_tab(NionTab *tab);
void nion_stop_all_web_activity(NionApp *app);
void nion_clear_retry(NionTab *tab);
gchar *nion_tab_fallback_title(NionTab *tab);
gchar *nion_http_origin_key(const gchar *uri);
void nion_show_error_page(NionTab *tab, const gchar *category, const gchar *heading,
                          const gchar *detail, const gchar *failing_uri,
                          gboolean preserve_failing_uri);
void nion_close_http_warning(NionTab *tab);
void nion_show_http_warning(NionTab *tab, const gchar *uri);
void nion_show_external_protocol_prompt(NionTab *tab, const gchar *uri,
                                        const gchar *scheme);

/* Public WebKit signal handlers (main.c's webview-creation hub connects
 * them on every new tab's find controller / web view). */
void on_find_found(WebKitFindController *controller, guint match_count,
                   gpointer user_data);
void on_find_failed(WebKitFindController *controller, gpointer user_data);
gboolean on_permission_request(WebKitWebView *web_view, WebKitPermissionRequest *request,
                               gpointer user_data);

/* Public GAction handlers (referenced by main.c's action table). */
void action_new_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_close_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_reopen_closed_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_focus_location(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_hard_reload(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_print(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_fullscreen(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_back(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_forward(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_next_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_previous_tab(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_privacy_audit(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* ---- Coordinator callback registration ---- */

/* Tab creation / current-tab / status-bar / action-table wiring that main.c
 * still owns (the webview-creation hub and the GActionEntry table stay in
 * main.c). main.c registers implementations so ui.c never calls into
 * main.c directly. */
typedef struct {
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    NionTab *(*current_tab)(NionApp *app);
    void (*set_status)(NionApp *app, const gchar *text);
    void (*install_actions)(NionApp *app);
} NionUiCallbacks;

void nion_ui_set_callbacks(const NionUiCallbacks *callbacks);

#endif /* NION_UI_H */
