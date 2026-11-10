/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_READER_H
#define NION_READER_H

#include "types.h"

/* reader (NiOn 2.2.4).

   Reader mode is a WebKit user style sheet, not a page rewrite. It is attached
   to the tab's user content manager, so it survives navigation until it is
   switched off, and turning it off restores the site untouched. */

/* Returns the stylesheet source, or NULL if the resource is missing. */
const gchar *nion_reader_css(void);

/* Turn reader mode on or off for a tab. Safe to call with NULL. */
void nion_reader_set_enabled(NionTab *tab, gboolean enabled);

/* Flip reader mode for the tab and return the new state. */
gboolean nion_reader_toggle(NionTab *tab);

#endif /* NION_READER_H */
