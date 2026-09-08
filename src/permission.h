/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_PERMISSION_H
#define NION_PERMISSION_H

#include "types.h"
#include <glib.h>

/* permission (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

gboolean nion_permission_is_temporarily_allowed(NionApp *app, const gchar *origin, guint permission);
gboolean nion_permission_mask_is_temporarily_allowed(NionApp *app, const gchar *origin, guint mask);
void nion_allow_permission_mask_temporarily(NionApp *app, const gchar *origin, guint mask);
void nion_clear_temporary_permissions_for_origin(NionApp *app, const gchar *origin);

/* New Identity clean slate (v2.1): revoke every temporary permission grant
 * (camera/mic/geolocation/notifications) for all origins. */
void nion_clear_all_temporary_permissions(NionApp *app);

#endif /* NION_PERMISSION_H */
