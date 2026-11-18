/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_PRIVACY_DASHBOARD_H
#define NION_PRIVACY_DASHBOARD_H

#include <glib.h>

/* Privacy dashboard for the start page (v2.3.6, roadmap T2-8).
 *
 * The rule this module exists to enforce: every number and every claim on the
 * dashboard has to come from something NiOn actually knows. A privacy page that
 * shows a plausible-looking "1,482 trackers blocked" that was never counted is
 * worse than no dashboard at all, because it teaches the user to trust a figure
 * that means nothing. Where NiOn does not measure something, the dashboard says
 * so in words instead of inventing a number.
 *
 * The module is GTK-free and returns plain data, so the wording and the
 * decisions are testable without opening a window. The page that draws it is
 * responsible only for layout.
 */

typedef enum {
    /* What the tile reports. */
    NION_DASH_KNOWN = 0,   /* a measured value or a definite setting */
    NION_DASH_UNKNOWN,     /* deliberately not measured; say so */
    NION_DASH_UNAVAILABLE, /* cannot be answered in the current state */
} NionDashValueKind;

typedef struct {
    gchar *label;   /* tile heading, for example "Tor" */
    gchar *value;   /* short value, for example "Connected" */
    gchar *detail;  /* one sentence explaining what that means */
    NionDashValueKind kind;
    /* A problem worth drawing attention to (Tor failed, filters failed). */
    gboolean attention;
} NionDashTile;

typedef struct {
    NionDashTile *tiles;
    guint count;
} NionDashboard;

/* What the caller knows. Everything here is something NiOn already tracks; the
 * struct exists so the module never reaches into the app and the rules can be
 * exercised with any combination. */
typedef struct {
    gboolean is_private;

    gboolean tor_ready;
    gboolean tor_failed;
    gint tor_bootstrap_percent;
    const gchar *tor_socks_port;

    const gchar *security_level; /* human wording, already localized by caller */

    gboolean block_third_party_cookies;

    gboolean content_filter_ready;
    gboolean content_filter_failed;

    /* Site data: how many sites have an override recorded. -1 when the store is
     * not available (a private window keeps none). */
    gint site_overrides;
    gint temporary_permissions;

    guint history_entries;
    guint reading_list_entries;

    guint active_tabs;
    guint suspended_tabs;
} NionDashFacts;

NionDashboard *nion_dashboard_build(const NionDashFacts *facts);
void nion_dashboard_free(NionDashboard *dashboard);

/* One-line summary used at the top of the page, for example
 * "3 protections active, 1 needs attention". Never claims a total it cannot
 * compute. */
gchar *nion_dashboard_summary(const NionDashboard *dashboard);

/* The full page as plain text, one line per tile. This exists so the wording is
 * testable and so a future text surface (terminal, log) has something to show;
 * the GTK page draws the same tiles as HTML. */
gchar *nion_dashboard_plain_text(const NionDashboard *dashboard);

#endif /* NION_PRIVACY_DASHBOARD_H */
