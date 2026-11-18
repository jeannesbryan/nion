/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TABS_H
#define NION_TABS_H

#include "types.h"

/* Tab model, notebook & context-menu logic (extracted from src/main.c,
 * Phase 5a, NiOn 2.0.0).  The WebKitWebView creation hub and webview signal
 * handlers remain in main.c (Phase 5b). */

void nion_closed_tab_free(gpointer data);
void nion_reopen_closed_tab(NionApp *app);
void nion_close_tab(NionTab *tab);
void nion_tab_free(gpointer data);
gint nion_count_pinned_tabs(NionApp *app);
void nion_set_tab_pinned(NionTab *tab, gboolean pinned, gboolean schedule_save);

/* The address that identifies a tab when it is recorded outside the live
 * WebView: session save, closed-tab memory. Resolves the display override, the
 * live WebView URI, the pending-restore URI and the suspended-tab URI, so no
 * caller has to remember all four cases. Returns NULL for a tab with no real
 * address (blank/home-only). The returned pointer is borrowed, never copied. */
const gchar *nion_tab_recorded_uri(NionTab *tab);
void nion_update_tab_audio_button(NionTab *tab);
void on_webview_muted_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
GtkWidget *nion_make_tab_label(NionTab *tab);

/* v2.3.2 (roadmap T1-4): show or hide the "waiting for Tor" spinner on every
 * tab in this window. Called whenever Tor changes state and after a tab is
 * added, so a tab opened mid-bootstrap does not sit there looking broken. */
void nion_tabs_update_tor_waiting(NionApp *app);

/* ---- Tab strip overflow (Priority+) ---- */

/* How many tabs are currently clipped out of the strip: no allocated area, or
 * lying outside the region the action widgets leave free. Measured from widget
 * geometry, because GtkNotebook hides its own scroll arrows and offers no
 * "visible pages" API. Returns 0 while the notebook is not yet allocated. */
guint nion_count_hidden_tabs(NionApp *app);
/* Build the strip's overflow button (a "all tabs" popover). The widget is owned
 * by the caller, which stores it as a notebook action widget. */
GtkWidget *nion_build_tab_overflow_button(NionApp *app);
/* Repopulate the overflow menu from the live tab list. */
void nion_rebuild_tab_overflow(NionApp *app);
/* Open the overflow menu (toolbar button and Ctrl+Shift+A both land here). */
void nion_show_tab_overflow(NionApp *app);
/* Env-gated (NION_TAB_DEBUG) one-line trace of the same measurement. */
void nion_log_tab_overflow(NionApp *app, const gchar *when);
/* ---- Background Tab Discard (v2.1 #1) ---- */

/* Refresh a tab's "last active" timestamp (call when it becomes current). */
void nion_tab_touch(NionTab *tab);
/* Suspend a background tab: unloads its heavy page to a tiny internal
 * suspended document (releasing the page's heap from the web process) while
 * the lightweight shell + tab-strip entry survive. */
void nion_tab_discard(NionTab *tab);
/* Revive a suspended tab by reloading its saved URI through the normal
 * Tor-gated path. No-op for live tabs. */
void nion_resume_discarded_tab(NionTab *tab);
/* Count tabs currently in the suspended/discarded state. */
gint nion_count_discarded_tabs(NionApp *app);
/* Sweep every background tab idle past NION_TAB_DISCARD_AFTER_MS and discard
 * it. Called periodically from the UI layer. */
void nion_discard_idle_tabs(NionApp *app);

/* ---- UI callback registration ---- */

/* UI/navigation operations that still live in main.c (tab creation hub,
 * navigation pages, status/controls).  main.c registers implementations so
 * tabs.c never calls into main.c directly. */
typedef struct {
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    void (*set_status)(NionApp *app, const gchar *text);
    void (*update_controls)(NionApp *app);
    void (*clear_retry)(NionTab *tab);
    void (*load_home)(NionTab *tab);
    void (*load_uri)(NionTab *tab, const gchar *uri);
    void (*reload_crashed_tab)(NionTab *tab);
} NionTabCallbacks;

void nion_tabs_set_callbacks(const NionTabCallbacks *callbacks);

#endif /* NION_TABS_H */
