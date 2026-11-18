/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TAB_FILTER_H
#define NION_TAB_FILTER_H

#include <glib.h>

/* Filtering the open-tab list (v2.3.4, roadmap T2-6).
 *
 * The overflow menu names every open tab, which is enough for three tabs and
 * useless for thirty. This module decides which rows survive a query.
 *
 * Rules, chosen so the result is predictable while typing:
 *   - An empty or whitespace-only query is inactive and matches every tab. The
 *     menu then looks exactly as it did before this feature existed.
 *   - Matching is case-insensitive and folded with g_utf8_casefold, so a query
 *     in one case finds a title in another and non-ASCII titles behave the way
 *     a reader expects.
 *   - Every term must match, and each term may match either the title or the
 *     address. Requiring all terms is what makes typing more narrowing rather
 *     than less: with "all terms" the list only shrinks as you type, which is
 *     the one behaviour that makes a filter feel reliable.
 *   - A term matching neither field drops the tab. Nothing is fuzzy-matched:
 *     a tab shown must be a tab the query actually reaches.
 *
 * No GTK dependency, so the rules are tested directly instead of being
 * inferred from what a menu happened to draw.
 */

/* TRUE when the query would filter anything at all. */
gboolean nion_tab_filter_is_active(const gchar *query);

/* TRUE when this tab should stay listed. A NULL or inactive query matches
 * everything. title and uri may be NULL: a tab with neither matches nothing
 * once the query is active, because there is nothing to match against. */
gboolean nion_tab_filter_matches(const gchar *query, const gchar *title,
                                 const gchar *uri);

#endif /* NION_TAB_FILTER_H */
