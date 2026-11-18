/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_READING_LIST_H
#define NION_READING_LIST_H

#include "types.h"

/* Reading list: pages saved for offline reading (v2.3.3, roadmap T1-5).
 *
 * The rules this module exists to enforce:
 *   - A saved page is TEXT. Keeping the HTML would keep its external images and
 *     frames, and opening it would try to fetch them: offline in name only.
 *   - Opening a saved page never touches the network, because the text is
 *     rendered by GTK, not by WebKit.
 *   - Local file only. No sync, no network call, no third party.
 *   - Private Windows never save and never receive a file path, so no future
 *     call site can keep a trail by accident.
 *   - Only http and https are saved, through the same gate the history store
 *     uses. Everything else is refused rather than guessed at.
 *   - The list is capped and each entry is limited, so the file cannot grow
 *     without bound.
 */

/* ---- Model (no NionApp, no widgets: exercised directly by the test harness) */

void nion_reading_entry_free(gpointer data);
NionReadingEntry *nion_reading_entry_new(const gchar *uri, const gchar *title,
                                         const gchar *text, gint64 saved_at);

/* Collapse the whitespace that extraction leaves behind: trailing spaces on
 * every line, runs of blank lines, tabs. Returns newly allocated text, or NULL
 * when nothing readable survives. Capped at NION_MAX_READING_TEXT_BYTES. */
gchar *nion_reading_normalize_text(const gchar *raw);

/* Words in normalized text: runs of non-space characters. */
guint nion_reading_count_words(const gchar *text);

/* TRUE when the text is long enough to be worth saving. A login wall or an
 * error page must not become a reading list entry. */
gboolean nion_reading_text_is_worth_saving(const gchar *text);

/* Parse the length-prefixed triple the page returns when a page is saved:
 *   <len>:<uri><len>:<title><len>:<text>
 * Byte-exact, so a truncated payload is refused rather than guessed at. */
gboolean nion_reading_parse_extraction(const gchar *payload, gchar **uri,
                                       gchar **title, gchar **text);

/* Index of the entry with this URI, or -1. Used to update instead of
 * duplicating when the same page is saved twice. */
gint nion_reading_find_uri(GPtrArray *entries, const gchar *uri);

/* Remove everything past `max_entries`, assuming the array is newest-first. */
guint nion_reading_prune(GPtrArray *entries, guint max_entries);

/* ---- App level ---- */

void nion_reading_list_load(NionApp *app);
void nion_reading_list_save(NionApp *app);

/* Save one page. No-op in a Private Window, for a non-recordable URI, for text
 * that is too short, or when the app has no store path.
 * Returns TRUE when an entry was added or refreshed. */
gboolean nion_reading_list_add(NionApp *app, const gchar *uri, const gchar *title,
                               const gchar *text);

/* Ask the current page for its title and text, then save it. The extraction is
 * asynchronous (it runs in the page), so the status line reports the outcome
 * when it arrives rather than before. */
void nion_reading_list_save_current(NionApp *app);

void nion_reading_list_remove(NionApp *app, const gchar *uri);
void nion_reading_list_clear(NionApp *app);

/* ---- UI ---- */

typedef struct {
    NionTab *(*new_tab)(NionApp *app, const gchar *uri, gboolean select);
    void (*set_status)(NionApp *app, const gchar *text);
} NionReadingCallbacks;

void nion_reading_list_set_callbacks(const NionReadingCallbacks *callbacks);

/* Public GAction handlers (registered by main.c's action table). */
void action_reading_list(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void action_save_for_later(GSimpleAction *action, GVariant *parameter, gpointer user_data);

#endif /* NION_READING_LIST_H */
