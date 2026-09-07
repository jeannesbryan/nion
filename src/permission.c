/* Copyright (C) 2026 Jeannes Bryan */

/* permission (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

#include "permission.h"
#include "types.h"

static const gchar *nion_permission_name(guint permission)
{
    switch (permission) {
    case NION_PERMISSION_CAMERA: return "camera";
    case NION_PERMISSION_MICROPHONE: return "microphone";
    case NION_PERMISSION_GEOLOCATION: return "location";
    case NION_PERMISSION_NOTIFICATIONS: return "notifications";
    default: return "unknown";
    }
}

static gchar *nion_permission_cache_key(const gchar *origin, guint permission)
{
    return origin && *origin
        ? g_strdup_printf("%s|%s", origin, nion_permission_name(permission))
        : NULL;
}

gboolean nion_permission_is_temporarily_allowed(NionApp *app,
                                                        const gchar *origin,
                                                        guint permission)
{
    if (!app || !app->temporary_permissions || !origin)
        return FALSE;
    gchar *key = nion_permission_cache_key(origin, permission);
    gboolean allowed = key && g_hash_table_contains(app->temporary_permissions, key);
    g_free(key);
    return allowed;
}

gboolean nion_permission_mask_is_temporarily_allowed(NionApp *app,
                                                             const gchar *origin,
                                                             guint mask)
{
    const guint permissions[] = {
        NION_PERMISSION_CAMERA,
        NION_PERMISSION_MICROPHONE,
        NION_PERMISSION_GEOLOCATION,
        NION_PERMISSION_NOTIFICATIONS,
    };
    for (guint i = 0; i < G_N_ELEMENTS(permissions); i++) {
        if ((mask & permissions[i]) &&
            !nion_permission_is_temporarily_allowed(app, origin, permissions[i]))
            return FALSE;
    }
    return mask != 0;
}

void nion_allow_permission_mask_temporarily(NionApp *app,
                                                    const gchar *origin,
                                                    guint mask)
{
    if (!app || !app->temporary_permissions || !origin)
        return;
    const guint permissions[] = {
        NION_PERMISSION_CAMERA,
        NION_PERMISSION_MICROPHONE,
        NION_PERMISSION_GEOLOCATION,
        NION_PERMISSION_NOTIFICATIONS,
    };
    for (guint i = 0; i < G_N_ELEMENTS(permissions); i++) {
        if (!(mask & permissions[i]))
            continue;
        gchar *key = nion_permission_cache_key(origin, permissions[i]);
        if (key)
            g_hash_table_add(app->temporary_permissions, key);
    }
}

void nion_clear_temporary_permissions_for_origin(NionApp *app,
                                                         const gchar *origin)
{
    if (!app || !app->temporary_permissions || !origin)
        return;
    const guint permissions[] = {
        NION_PERMISSION_CAMERA,
        NION_PERMISSION_MICROPHONE,
        NION_PERMISSION_GEOLOCATION,
        NION_PERMISSION_NOTIFICATIONS,
    };
    for (guint i = 0; i < G_N_ELEMENTS(permissions); i++) {
        gchar *key = nion_permission_cache_key(origin, permissions[i]);
        if (key) {
            g_hash_table_remove(app->temporary_permissions, key);
            g_free(key);
        }
    }
}
