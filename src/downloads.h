/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_DOWNLOADS_H
#define NION_DOWNLOADS_H

#include "types.h"

/* Downloads model, download window UI & lifecycle (extracted from
 * src/main.c, Phase 6, NiOn 2.0.0). */

void nion_save_download_history(NionApp *app);
void nion_cancel_active_downloads(NionApp *app);
void nion_private_cleanup_partial_downloads(NionApp *app);
void nion_build_downloads_window(NionApp *app);

/* Public GAction handler (registered by main.c's action table). */
void action_downloads(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Public WebKit signal handler (connected by main.c after a network session
 * is prepared, for the main window and each private window). */
void on_download_started(WebKitNetworkSession *session, WebKitDownload *download,
                         gpointer user_data);

/* ---- UI callback registration ---- */

/* UI/tab operations that still live in main.c. main.c registers
 * implementations so downloads.c never calls into main.c directly. */
typedef struct {
    NionTab *(*current_tab)(NionApp *app);
    void (*set_status)(NionApp *app, const gchar *text);
} NionDownloadCallbacks;

void nion_downloads_set_callbacks(const NionDownloadCallbacks *callbacks);

#endif /* NION_DOWNLOADS_H */
