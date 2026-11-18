/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <glib.h>
#include <string.h>

#include "types.h"
#include "history.h"
#include "autocomplete.h"

/* Local-only address bar autocomplete (v2.3.1, roadmap T1-2).
 *
 * Pure model: no GTK widgets and no I/O, so the ranking rules can be tested
 * directly instead of inferred from what a popup happened to show.
 */

NionAutocompleteItem *
nion_autocomplete_item_new(const gchar *uri, const gchar *title,
                           NionAutocompleteSource source, gint64 recency)
{
    NionAutocompleteItem *item = g_new0(NionAutocompleteItem, 1);
    item->uri = g_strdup(uri);
    item->title = g_strdup(title && *title ? title : uri);
    item->source = source;
    item->recency = recency;
    return item;
}

void
nion_autocomplete_item_free(gpointer data)
{
    NionAutocompleteItem *item = data;
    if (!item)
        return;
    g_free(item->title);
    g_free(item->uri);
    g_free(item);
}

static gchar *
nion_autocomplete_fold(const gchar *text)
{
    if (!text || !*text)
        return g_strdup("");
    if (g_utf8_validate(text, -1, NULL))
        return g_utf8_casefold(text, -1);
    return g_ascii_strdown(text, -1);
}

/* Host plus path, with the scheme removed, so "exa" matches example.com whether
 * the stored address is http or https. */
static gchar *
nion_autocomplete_address_body(const gchar *uri)
{
    if (!uri)
        return g_strdup("");
    const gchar *body = uri;
    if (g_str_has_prefix(body, "https://"))
        body += strlen("https://");
    else if (g_str_has_prefix(body, "http://"))
        body += strlen("http://");
    return nion_autocomplete_fold(body);
}

/* Lower is better. NION_AUTOCOMPLETE_NO_MATCH means "do not show". */
#define NION_AUTOCOMPLETE_NO_MATCH 99

static gint
nion_autocomplete_score(const NionAutocompleteItem *item, const gchar *query_fold)
{
    if (!item || !item->uri || !*item->uri || !query_fold || !*query_fold)
        return NION_AUTOCOMPLETE_NO_MATCH;

    gchar *body = nion_autocomplete_address_body(item->uri);
    gchar *title = nion_autocomplete_fold(item->title);

    /* An address the user typed the beginning of is the strongest signal. */
    gint score = NION_AUTOCOMPLETE_NO_MATCH;
    if (g_str_has_prefix(body, query_fold))
        score = 0;
    else if (strstr(body, query_fold) != NULL)
        score = 1;
    else {
        /* Fall back to matching every whitespace separated term in the title,
         * which is how a remembered page name is usually typed. */
        gchar *query_copy = g_strdup(query_fold);
        gchar **terms = g_strsplit_set(query_copy, " \t\r\n", -1);
        gboolean all = TRUE;
        gboolean any = FALSE;
        for (guint i = 0; terms && terms[i]; i++) {
            if (!terms[i] || !*terms[i])
                continue;
            any = TRUE;
            if (!strstr(title, terms[i]) && !strstr(body, terms[i])) {
                all = FALSE;
                break;
            }
        }
        if (any && all)
            score = 2;
        g_strfreev(terms);
        g_free(query_copy);
    }

    g_free(title);
    g_free(body);
    return score;
}

typedef struct {
    NionAutocompleteItem *item;
    gint score;
    guint original_index;
} NionAutocompleteRanked;

static gint
nion_autocomplete_compare(gconstpointer a, gconstpointer b)
{
    const NionAutocompleteRanked *left = a;
    const NionAutocompleteRanked *right = b;

    if (left->score != right->score)
        return left->score < right->score ? -1 : 1;

    /* Within a tier: open tab before history before bookmark. */
    if (left->item->source != right->item->source)
        return left->item->source < right->item->source ? -1 : 1;

    if (left->item->recency != right->item->recency)
        return left->item->recency > right->item->recency ? -1 : 1;

    /* Stable tie-break so the list does not reshuffle between keystrokes. */
    if (left->original_index != right->original_index)
        return left->original_index < right->original_index ? -1 : 1;
    return 0;
}

GPtrArray *
nion_autocomplete_rank(GPtrArray *candidates, const gchar *query, guint max_results)
{
    GPtrArray *result = g_ptr_array_new();
    if (!candidates || candidates->len == 0 || !query)
        return result;

    gchar *trimmed = g_strdup(query);
    g_strstrip(trimmed);
    if (!*trimmed) {
        g_free(trimmed);
        return result;
    }

    gchar *query_fold = nion_autocomplete_fold(trimmed);
    GArray *ranked = g_array_new(FALSE, FALSE, sizeof(NionAutocompleteRanked));

    for (guint i = 0; i < candidates->len; i++) {
        NionAutocompleteItem *item = g_ptr_array_index(candidates, i);
        gint score = nion_autocomplete_score(item, query_fold);
        if (score == NION_AUTOCOMPLETE_NO_MATCH)
            continue;
        NionAutocompleteRanked entry = { item, score, i };
        g_array_append_val(ranked, entry);
    }

    g_array_sort(ranked, nion_autocomplete_compare);

    for (guint i = 0; i < ranked->len; i++) {
        NionAutocompleteRanked *entry = &g_array_index(ranked, NionAutocompleteRanked, i);
        if (max_results > 0 && result->len >= max_results)
            break;

        /* One row per address: the same page can be an open tab, a history
         * entry and a bookmark all at once, and three identical rows would
         * only push other matches off the list. */
        gboolean duplicate = FALSE;
        for (guint j = 0; j < result->len; j++) {
            NionAutocompleteItem *seen = g_ptr_array_index(result, j);
            if (g_strcmp0(seen->uri, entry->item->uri) == 0) {
                duplicate = TRUE;
                break;
            }
        }
        if (!duplicate)
            g_ptr_array_add(result, entry->item);
    }

    g_array_unref(ranked);
    g_free(query_fold);
    g_free(trimmed);
    return result;
}

GPtrArray *
nion_autocomplete_collect(NionApp *app, GPtrArray *extra)
{
    GPtrArray *candidates = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);

    if (app && app->history) {
        for (guint i = 0; i < app->history->len; i++) {
            NionHistoryEntry *entry = g_ptr_array_index(app->history, i);
            if (!entry || !nion_history_uri_is_recordable(entry->uri))
                continue;
            g_ptr_array_add(candidates,
                            nion_autocomplete_item_new(entry->uri, entry->title,
                                                       NION_AUTOCOMPLETE_HISTORY,
                                                       entry->last_visit));
        }
    }

    if (app && app->bookmarks) {
        for (guint i = 0; i < app->bookmarks->len; i++) {
            NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
            if (!bookmark || !nion_history_uri_is_recordable(bookmark->uri))
                continue;
            g_ptr_array_add(candidates,
                            nion_autocomplete_item_new(bookmark->uri, bookmark->title,
                                                       NION_AUTOCOMPLETE_BOOKMARK, 0));
        }
    }

    if (extra) {
        for (guint i = 0; i < extra->len; i++) {
            NionAutocompleteItem *item = g_ptr_array_index(extra, i);
            if (!item || !nion_history_uri_is_recordable(item->uri))
                continue;
            g_ptr_array_add(candidates,
                            nion_autocomplete_item_new(item->uri, item->title,
                                                       NION_AUTOCOMPLETE_TAB, 0));
        }
    }

    return candidates;
}

GPtrArray *
nion_autocomplete_suggest(NionApp *app, GPtrArray *extra, const gchar *query,
                          guint max_results)
{
    GPtrArray *collected = nion_autocomplete_collect(app, extra);
    GPtrArray *ranked = nion_autocomplete_rank(collected, query, max_results);

    /* rank() borrows its items from `collected`, which owns them. Returning
     * `ranked` after dropping the collection would hand the caller pointers
     * into freed memory, so the winners are copied into an array that owns
     * them. At most max_results items are copied, so this costs nothing.
     * (This was a real use-after-free: the collection's free func destroys
     * every item on unref.) */
    GPtrArray *owned = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);
    for (guint i = 0; i < ranked->len; i++) {
        NionAutocompleteItem *item = g_ptr_array_index(ranked, i);
        g_ptr_array_add(owned, nion_autocomplete_item_new(item->uri, item->title,
                                                          item->source, item->recency));
    }

    g_ptr_array_unref(ranked);
    g_ptr_array_unref(collected);
    return owned;
}
