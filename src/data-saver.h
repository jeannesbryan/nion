/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_DATA_SAVER_H
#define NION_DATA_SAVER_H

#include "types.h"

/* Data saver: stop fetching images, fonts and media for one tab (v2.3.7, T2-10).
 *
 * The mechanism is WebKit's own resource-type filter, not a stylesheet that
 * hides things. Hiding an image with CSS still downloads it, so it would save
 * nothing while looking like it saved something. Blocking the request means the
 * bytes never travel, which is the only thing that helps on a slow connection
 * and the only thing that is honest to call a data saver.
 *
 * Three resource types are blocked and nothing else. Scripts and stylesheets
 * are left alone: blocking them would change what the page does rather than how
 * much it costs, and a page that breaks is not a page the user saved data on,
 * it is a page they have to reload without the feature.
 *
 * The choice is per tab, because "this page is heavy" is a property of a page,
 * not of a browsing session. Turning it on reloads the tab, since a filter only
 * affects requests made after it is applied.
 */

/* The rules, as WebKit content-blocker JSON. Pure and side-effect free so the
 * exact rules can be asserted in a test rather than described in a comment. */
const gchar *nion_data_saver_rules_json(void);

/* TRUE when this tab is currently asked to save data. */
gboolean nion_data_saver_is_on(NionTab *tab);

/* Turn it on or off for this tab, apply the change, and report the outcome in
 * the status line. Reloads the tab so the new state takes effect.
 * Returns the state after the toggle. */
gboolean nion_data_saver_toggle(NionTab *tab);

/* Apply the current choice to the tab's user content manager. Safe to call on
 * every navigation: the filter is removed and re-added, the same way the
 * content blocker is handled. */
void nion_data_saver_apply(NionTab *tab);

/* The compiled filter for this app (a private window borrows its owner's). NULL
 * when it is not ready, which callers must treat as "cannot save data", never as
 * "nothing to save". */
WebKitUserContentFilter *nion_data_saver_filter_for_app(NionApp *app);

/* TRUE when the rules failed to compile, so the feature cannot work and the UI
 * must say so rather than offer a toggle that does nothing. */
gboolean nion_data_saver_has_failed(NionApp *app);

#endif /* NION_DATA_SAVER_H */
