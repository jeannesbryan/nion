/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_HISTORY_H
#define NION_HISTORY_H

#include "types.h"

/* Local browsing history (v2.3.0, roadmap T1-1).
 *
 * The rules this module exists to enforce:
 *   - Local file only. No sync, no network call, no third party.
 *   - Private Windows never record, and never even receive a file path, so no
 *     future call site can write a trail by accident.
 *   - New Identity wipes the store: keeping the old URL trail would make a
 *     "new identity" a rename rather than a reset.
 *   - Only http and https are stored. Everything else (file, data, about,
 *     javascript) is dropped instead of being guessed at.
 *   - Entries expire after NION_HISTORY_RETENTION_DAYS and the store is capped,
 *     so the file cannot grow without bound.
 */

/* ---- Model (no NionApp, no widgets: exercised directly by the test harness) */

void nion_history_free_entry(gpointer data);
NionHistoryEntry *nion_history_entry_new(const gchar *uri, const gchar *title,
                                         gint64 last_visit);

/* TRUE only for a well formed http/https URI with a non-empty host and no
 * control bytes. This is the single gate every stored URI passes through. */
gboolean nion_history_uri_is_recordable(const gchar *uri);

/* Fallback title (the URI itself) and length limit applied, newly allocated. */
gchar *nion_history_normalize_title(const gchar *title, const gchar *uri);

/* Drop expired entries and everything past `max_entries`, assuming the array is
 * newest-first. Returns how many entries were removed. */
guint nion_history_prune(GPtrArray *entries, gint64 now, guint max_entries);

/* Remove every entry whose host matches. Returns how many were removed. */
guint nion_history_forget_host(GPtrArray *entries, const gchar *host);

/* ---- App level ---- */

void nion_history_load(NionApp *app);
void nion_history_save(NionApp *app);

/* Record one visit. No-op in a Private Window, for a non-recordable URI, or
 * when the app has no history path. Revisiting an existing URI moves it to the
 * front and bumps its counter instead of appending a duplicate row. */
void nion_history_record(NionApp *app, const gchar *uri, const gchar *title);

/* Replace a placeholder title (one that is still the raw URI) once the page
 * reports a real one. Real titles are never overwritten with a later, worse
 * one. */
void nion_history_upgrade_title(NionApp *app, const gchar *uri, const gchar *title);

/* Delete the store: entries and file. Used by New Identity and by the window's
 * "Clear All History" action. */
void nion_history_clear(NionApp *app);

/* Drop this site's entries. Called by "Forget This Site", because a forget that
 * leaves the URL in history is not a forget. */
void nion_history_forget_site(NionApp *app, const gchar *uri);

/* ---- UI ---- */

typedef struct {
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    void (*set_status)(NionApp *app, const gchar *text);
} NionHistoryCallbacks;

void nion_history_set_callbacks(const NionHistoryCallbacks *callbacks);

/* Public GAction handler (registered by main.c's action table). */
void action_history(GSimpleAction *action, GVariant *parameter, gpointer user_data);

#endif /* NION_HISTORY_H */
