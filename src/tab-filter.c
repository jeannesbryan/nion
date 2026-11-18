/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <glib.h>
#include <string.h>

#include "tab-filter.h"

/* Filtering the open-tab list (v2.3.4, roadmap T2-6). */

gboolean
nion_tab_filter_is_active(const gchar *query)
{
    if (!query)
        return FALSE;
    for (const gchar *p = query; *p; p++) {
        if (!g_ascii_isspace(*p))
            return TRUE;
    }
    return FALSE;
}

/* Folded containment. Both sides are folded the same way, so the comparison
 * does not depend on which case the user typed or the page used. */
static gboolean
folded_contains(const gchar *haystack, const gchar *needle)
{
    if (!haystack || !needle || !*needle)
        return FALSE;

    gchar *folded_hay = g_utf8_casefold(haystack, -1);
    gchar *folded_needle = g_utf8_casefold(needle, -1);
    gboolean found = folded_hay && folded_needle &&
                     strstr(folded_hay, folded_needle) != NULL;
    g_free(folded_hay);
    g_free(folded_needle);
    return found;
}

gboolean
nion_tab_filter_matches(const gchar *query, const gchar *title, const gchar *uri)
{
    /* An inactive query keeps every row: the menu is "all tabs" until the user
     * asks for less. */
    if (!nion_tab_filter_is_active(query))
        return TRUE;

    /* Split the query on whitespace and require every term. Splitting is done
     * here rather than with g_strsplit so no allocation is needed for the
     * common single-term case, and so a run of spaces cannot produce an empty
     * term that would match everything. */
    const gchar *cursor = query;
    gboolean saw_term = FALSE;

    while (*cursor) {
        while (*cursor && g_ascii_isspace(*cursor))
            cursor++;
        if (!*cursor)
            break;

        const gchar *start = cursor;
        while (*cursor && !g_ascii_isspace(*cursor))
            cursor++;
        gchar *term = g_strndup(start, (gsize)(cursor - start));
        saw_term = TRUE;

        gboolean in_title = folded_contains(title, term);
        gboolean in_uri = !in_title && folded_contains(uri, term);
        g_free(term);

        /* One missing term is enough to drop the tab: every term must match
         * something, or the row would survive a query it does not satisfy. */
        if (!in_title && !in_uri)
            return FALSE;
    }

    /* Every term matched something, so the tab stays. (A query made only of
     * whitespace never reaches here: is_active() already answered TRUE above,
     * which is why saw_term is not needed for control flow. It is kept only so
     * a future edit cannot silently turn "no terms" into "drop everything".) */
    (void)saw_term;
    return TRUE;
}
