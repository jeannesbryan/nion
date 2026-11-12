/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_LOAD_ERROR_H
#define NION_LOAD_ERROR_H

#include <glib.h>

/* Classifies a WebKit load-failed error as a navigation interruption instead of
 * a page failure.
 *
 * Two different WebKit mistakes produce "the page could not be loaded", and
 * both are self-inflicted rather than the site's fault:
 *
 *  - WEBKIT_NETWORK_ERROR_CANCELLED: another navigation started while this one
 *    was still provisional.
 *  - WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE: a policy
 *    decision for this frame was ignored and a replacement load was started.
 *    NiOn's strict-HTTPS upgrade does exactly that (ignore the http://
 *    decision, then load the https:// twin), and WebKitGTK 2.52 then reports
 *    the dropped http:// frame with this error.
 *
 * Returning TRUE means "do not build an error page for this", because either a
 * replacement load is already on its way or the user left this page on purpose.
 *
 * Kept as a pure function so the classification can be tested directly against
 * the same source file the browser links.
 */
gboolean nion_load_error_is_navigation_interruption(const GError *error);

#endif
