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
void nion_update_tab_audio_button(NionTab *tab);
void on_webview_muted_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
GtkWidget *nion_make_tab_label(NionTab *tab);

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
