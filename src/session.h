/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_SESSION_H
#define NION_SESSION_H

#include "types.h"

/* Session save/restore & crash recovery (extracted from src/main.c,
 * Phase 4, NiOn 2.0.0). */

void nion_save_session(NionApp *app, gboolean clean_shutdown);
void nion_schedule_session_save(NionApp *app);
gboolean nion_restore_saved_session(NionApp *app);
void nion_start_pending_restores(NionApp *app);
void nion_show_crash_recovery_prompt(NionApp *app);

/* ---- UI callback registration ---- */

/* UI/tab operations that still live in main.c (Phase 4: tabs/webview/UI not
 * yet extracted). main.c registers implementations so session.c never calls
 * into main.c directly. */
typedef struct {
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    void (*set_tab_pinned)(NionTab *tab, gboolean pinned, gboolean schedule_save);
    void (*load_home)(NionTab *tab);
    void (*load_uri)(NionTab *tab, const gchar *uri);
    void (*prepare_normal_navigation)(NionTab *tab);
    void (*update_controls)(NionApp *app);
    void (*set_status)(NionApp *app, const gchar *text);
    void (*closed_tab_free)(gpointer data);
} NionSessionCallbacks;

void nion_session_set_callbacks(const NionSessionCallbacks *callbacks);

#endif /* NION_SESSION_H */
