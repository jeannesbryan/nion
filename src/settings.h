/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_SETTINGS_H
#define NION_SETTINGS_H

#include "types.h"

/* Preferences persistence, security-level helpers & the Preferences dialog
 * (extracted from src/main.c, Phase 6, NiOn 2.0.0). */

void nion_load_preferences(NionApp *app);
const gchar *nion_security_level_label(NionSecurityLevel level);

/* Public GAction handler (registered by main.c's action table). */
void action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Public generic dialog-close handler (destroy the toplevel the button
 * belongs to). Reused by the preferences, site-data, clear-data and
 * privacy-audit dialogs. */
void on_preferences_cancel_clicked(GtkButton *button, gpointer user_data);

/* ---- UI callback registration ---- */

/* UI/status operations that still live in main.c (nion_set_status) or are
 * shared UI the module must not reach into directly (nion_update_controls,
 * nion_update_site_info). main.c registers implementations so settings.c
 * never calls into main.c directly. */
typedef struct {
    void (*set_status)(NionApp *app, const gchar *text);
    void (*update_controls)(NionApp *app);
    void (*update_site_info)(NionApp *app);
} NionSettingsCallbacks;

void nion_settings_set_callbacks(const NionSettingsCallbacks *callbacks);

#endif /* NION_SETTINGS_H */
