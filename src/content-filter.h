/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_CONTENT_FILTER_H
#define NION_CONTENT_FILTER_H

#include "types.h"

/* content-filter (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

WebKitUserContentFilter *nion_content_filter_for_app(NionApp *app);
gboolean nion_content_filter_is_ready(NionApp *app);
gboolean nion_content_filter_has_failed(NionApp *app);
void nion_apply_content_blocking(NionTab *tab, const gchar *uri);

#endif /* NION_CONTENT_FILTER_H */
