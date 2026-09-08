/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_SITE_DATA_H
#define NION_SITE_DATA_H

#include "types.h"

/* Site-information window, per-site data inspection & the clear-site-data /
 * clear-data flows (extracted from src/main.c, Phase 6, NiOn 2.0.0). */

void nion_update_site_info(NionApp *app);
void nion_update_site_info_button(NionApp *app);
gboolean nion_tab_has_mixed_content(const NionTab *tab);
GtkWidget *nion_site_info_row(const gchar *heading, GtkWidget **value_out);
gchar *nion_web_origin_key_for_uri(const gchar *uri);

/* Public GAction handlers (registered by main.c's action table). */
void action_clear_site_data(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_forget_site(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_clear_data(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Public signal handlers (connected by main.c's UI builder). */
void on_site_content_blocking_switch_notify(GObject *object, GParamSpec *pspec,
                                            gpointer user_data);
void on_site_javascript_switch_notify(GObject *object, GParamSpec *pspec,
                                      gpointer user_data);
void on_site_autoplay_switch_notify(GObject *object, GParamSpec *pspec,
                                    gpointer user_data);
void on_site_permissions_reset_clicked(GtkButton *button, gpointer user_data);
void on_site_forget_clicked(GtkButton *button, gpointer user_data);
gboolean on_site_info_window_close_request(GtkWindow *window, gpointer user_data);
void on_site_info_button_clicked(GtkButton *button, gpointer user_data);

/* ---- UI callback registration ---- */

/* UI/tab operations that still live in main.c. main.c registers
 * implementations so site-data.c never calls into main.c directly. */
typedef struct {
    NionTab *(*current_tab)(NionApp *app);
    void (*set_status)(NionApp *app, const gchar *text);
    void (*apply_content_filter_to_window)(NionApp *app);
} NionSiteDataCallbacks;

void nion_site_data_set_callbacks(const NionSiteDataCallbacks *callbacks);

#endif /* NION_SITE_DATA_H */
