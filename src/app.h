/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_APP_H
#define NION_APP_H

#include "types.h"

/* Application lifecycle: startup directory/sandbox preparation, Tor-state
 * coordination, window close / shutdown flow and private-window lifecycle.
 * Extracted from src/main.c, Phase 7, NiOn 2.0.0.
 *
 * This module owns the app-wide Tor state (registered as the tor-core
 * callbacks by main.c) because fail-closed propagation to every window is a
 * lifecycle concern, not a UI concern. */

void nion_prepare_dirs(NionApp *app);
void nion_prepare_appimage_webkit_sandbox(void);

/* Tor-state reporters (main.c registers them as NionTorCallbacks). */
void nion_set_tor_ready(NionApp *app, gboolean ready);
void nion_set_tor_progress(NionApp *app, gint percent);
void nion_set_tor_error(NionApp *app, const gchar *message);

/* GApplication / GAction handlers (registered by main.c). */
void on_shutdown(GApplication *application, gpointer user_data);
void action_private_window(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_exit(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Window close-request handler (connected by the UI chrome builder in
 * main.c / ui.c for both normal and private windows). */
gboolean on_window_close_request(GtkWindow *window, gpointer user_data);

/* ---- UI callback registration ---- */

/* UI chrome / status operations that still live in the UI layer (main.c,
 * later ui.c). main.c registers implementations so this module never calls
 * into the UI layer directly. */
typedef struct {
    void (*set_status)(NionApp *app, const gchar *text);
    void (*update_controls)(NionApp *app);
    void (*refresh_home_pages)(NionApp *app);
    void (*stop_all_web_activity)(NionApp *app);
    void (*build_ui)(NionApp *app);
} NionAppLifecycleCallbacks;

void nion_app_set_callbacks(const NionAppLifecycleCallbacks *callbacks);

#endif /* NION_APP_H */
