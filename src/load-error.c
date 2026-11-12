/* Copyright (C) 2026 Jeannes Bryan */

#include "load-error.h"

#include <webkit/webkit.h>

gboolean nion_load_error_is_navigation_interruption(const GError *error)
{
    if (!error)
        return FALSE;

    if (error->domain == WEBKIT_NETWORK_ERROR &&
        error->code == WEBKIT_NETWORK_ERROR_CANCELLED)
        return TRUE;

    if (error->domain == WEBKIT_POLICY_ERROR &&
        error->code == WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)
        return TRUE;

    return FALSE;
}
