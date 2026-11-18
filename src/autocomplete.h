/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_AUTOCOMPLETE_H
#define NION_AUTOCOMPLETE_H

#include "types.h"

/* Address bar autocomplete built ONLY from data NiOn already has locally:
 * the local history store, the bookmark list, and the tabs that are open right
 * now. There is deliberately no network path here.
 *
 * Browsers that suggest as you type usually send the partial query to a search
 * engine before the user presses Enter, which tells a third party what you were
 * considering reading. NiOn does not. A miss simply returns nothing.
 */

typedef enum {
    NION_AUTOCOMPLETE_TAB = 0,       /* an open tab: highest priority */
    NION_AUTOCOMPLETE_HISTORY = 1,   /* a page visited before */
    NION_AUTOCOMPLETE_BOOKMARK = 2,  /* an explicit bookmark */
} NionAutocompleteSource;

typedef struct {
    gchar *title;
    gchar *uri;
    NionAutocompleteSource source;
    gint64 recency;   /* history last_visit; 0 for tabs and bookmarks */
} NionAutocompleteItem;

NionAutocompleteItem *nion_autocomplete_item_new(const gchar *uri, const gchar *title,
                                                 NionAutocompleteSource source,
                                                 gint64 recency);
void nion_autocomplete_item_free(gpointer data);

/* Rank candidates against a query. Returns a new array of borrowed items (the
 * caller owns the array, not the items), newest and closest match first,
 * de-duplicated by URI and capped at `max_results`. An empty or blank query
 * returns an empty array: NiOn does not offer a menu just for focusing the bar.
 *
 * Ordering: host or address prefix match first, then address substring, then
 * title match, and within one tier the higher priority source (open tab, then
 * history, then bookmark) followed by the more recent visit. */
GPtrArray *nion_autocomplete_rank(GPtrArray *candidates, const gchar *query,
                                  guint max_results);

/* Collect candidates from the app's history and bookmarks, plus `extra` (the
 * open tabs, built by the UI layer which owns the notebook). Only http and
 * https addresses are considered, using the same gate the history store uses,
 * so a suggestion can never hand the address bar a scheme NiOn refuses to
 * browse. Returns a new array owning its items. */
GPtrArray *nion_autocomplete_collect(NionApp *app, GPtrArray *extra);

/* Convenience: collect, rank, drop the collection. Returns a new array that
 * OWNS its items (free func is nion_autocomplete_item_free); release it with
 * g_ptr_array_unref(). Unlike nion_autocomplete_rank(), the caller does not
 * have to keep a candidate array alive to keep the result valid. */
GPtrArray *nion_autocomplete_suggest(NionApp *app, GPtrArray *extra,
                                     const gchar *query, guint max_results);

#endif /* NION_AUTOCOMPLETE_H */
