/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_BOOKMARKS_H
#define NION_BOOKMARKS_H

#include "types.h"

/* Bookmarks model, bookmarks window UI & toolbar state (extracted from
 * src/main.c, Phase 6, NiOn 2.0.0). */

void nion_bookmark_free(gpointer data);
void nion_load_bookmarks(NionApp *app);
void nion_save_bookmarks(NionApp *app);
void nion_update_bookmark_button(NionApp *app);

/* Public GAction handlers (registered by main.c's action table). */
void action_bookmark_page(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_bookmarks(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Public signal handler (connected by main.c's toolbar builder). */
void on_bookmark_toolbar_clicked(GtkButton *button, gpointer user_data);

/* ---- UI callback registration ---- */

/* UI/tab operations that still live in main.c. main.c registers
 * implementations so bookmarks.c never calls into main.c directly. */
typedef struct {
    NionTab *(*current_tab)(NionApp *app);
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    void (*set_status)(NionApp *app, const gchar *text);
} NionBookmarkCallbacks;

void nion_bookmarks_set_callbacks(const NionBookmarkCallbacks *callbacks);

#endif /* NION_BOOKMARKS_H */
