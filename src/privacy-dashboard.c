/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <glib.h>
#include <string.h>

#include "privacy-dashboard.h"

/* Privacy dashboard (v2.3.6, roadmap T2-8).
 *
 * Every tile below is built from a field of NionDashFacts, which the caller
 * fills from state NiOn already keeps. Nothing is estimated, sampled or
 * extrapolated. When a fact is not tracked, the tile says "not tracked" rather
 * than showing a number, because a privacy page is the last place that should
 * dress up a guess as a measurement.
 */

static NionDashTile *
add_tile(NionDashboard *dashboard, const gchar *label, gchar *value, gchar *detail,
         NionDashValueKind kind, gboolean attention)
{
    dashboard->tiles = g_renew(NionDashTile, dashboard->tiles, dashboard->count + 1);
    NionDashTile *tile = &dashboard->tiles[dashboard->count++];
    tile->label = g_strdup(label);
    tile->value = value;
    tile->detail = detail;
    tile->kind = kind;
    tile->attention = attention;
    return tile;
}

void
nion_dashboard_free(NionDashboard *dashboard)
{
    if (!dashboard)
        return;
    for (guint i = 0; i < dashboard->count; i++) {
        g_free(dashboard->tiles[i].label);
        g_free(dashboard->tiles[i].value);
        g_free(dashboard->tiles[i].detail);
    }
    g_free(dashboard->tiles);
    g_free(dashboard);
}

NionDashboard *
nion_dashboard_build(const NionDashFacts *facts)
{
    NionDashboard *dashboard = g_new0(NionDashboard, 1);
    if (!facts)
        return dashboard;

    /* ---- Tor: the thing everything else depends on ---- */
    if (facts->tor_ready) {
        add_tile(dashboard, "Tor",
                 g_strdup("Connected"),
                 g_strdup("Browsing goes through the Tor network. Nothing leaves "
                          "this browser any other way."),
                 NION_DASH_KNOWN, FALSE);
    } else if (facts->tor_failed) {
        add_tile(dashboard, "Tor",
                 g_strdup("Unavailable"),
                 g_strdup("Tor is not running, so browsing is blocked. A failed "
                          "connection is never replaced by a direct one."),
                 NION_DASH_KNOWN, TRUE);
    } else {
        gchar *value = g_strdup_printf("Connecting — %d%%",
                                       facts->tor_bootstrap_percent);
        add_tile(dashboard, "Tor", value,
                 g_strdup("Still building its route. Browsing unlocks when Tor "
                          "reports it is done."),
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- Network path: stated or not claimed at all ---- */
    if (facts->tor_socks_port && *facts->tor_socks_port) {
        gchar *detail = g_strdup_printf(
            "Every page is fetched through the local Tor proxy on %s. "
            "There is no fallback to a direct connection.",
            facts->tor_socks_port);
        add_tile(dashboard, "Network path", g_strdup("Tor only"), detail,
                 NION_DASH_KNOWN, FALSE);
    } else {
        add_tile(dashboard, "Network path", g_strdup("Not active"),
                 g_strdup("No proxy is configured yet because Tor has not started."),
                 NION_DASH_UNAVAILABLE, FALSE);
    }

    /* ---- Cookies ---- */
    if (facts->block_third_party_cookies) {
        add_tile(dashboard, "Third-party cookies",
                 g_strdup("Blocked"),
                 g_strdup("A site cannot set a cookie for a domain you did not "
                          "visit."),
                 NION_DASH_KNOWN, FALSE);
    } else {
        add_tile(dashboard, "Third-party cookies",
                 g_strdup("Allowed"),
                 g_strdup("Sites can set cookies for other domains. This is the "
                          "less private of the two settings."),
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- Content filter: honest about failure ---- */
    if (facts->content_filter_failed) {
        add_tile(dashboard, "Content filtering",
                 g_strdup("Not active"),
                 g_strdup("The filter list did not load, so filtering is not "
                          "running. The rest of the protections are unaffected."),
                 NION_DASH_KNOWN, TRUE);
    } else if (facts->content_filter_ready) {
        add_tile(dashboard, "Content filtering",
                 g_strdup("Active"),
                 g_strdup("Blocking rules from the bundled list are applied to "
                          "every page."),
                 NION_DASH_KNOWN, FALSE);
    } else {
        add_tile(dashboard, "Content filtering",
                 g_strdup("Loading"),
                 g_strdup("The filter list is still being prepared."),
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- Security level ---- */
    add_tile(dashboard, "Security level",
             g_strdup(facts->security_level ? facts->security_level : "Unknown"),
             g_strdup("Controls whether scripting and other active content is "
                      "allowed to run."),
             facts->security_level ? NION_DASH_KNOWN : NION_DASH_UNKNOWN, FALSE);

    /* ---- Site overrides ---- */
    if (facts->is_private) {
        add_tile(dashboard, "Site overrides",
                 g_strdup("None kept"),
                 g_strdup("A private window keeps no per-site settings, so "
                          "nothing carries over to the next session."),
                 NION_DASH_UNAVAILABLE, FALSE);
    } else if (facts->site_overrides < 0) {
        add_tile(dashboard, "Site overrides", g_strdup("Not tracked"),
                 g_strdup("NiOn keeps per-site settings on disk but does not "
                          "count them for display."),
                 NION_DASH_UNKNOWN, FALSE);
    } else if (facts->site_overrides == 0) {
        add_tile(dashboard, "Site overrides",
                 g_strdup("None"),
                 g_strdup("No site has an exception recorded. The default "
                          "settings apply everywhere."),
                 NION_DASH_KNOWN, FALSE);
    } else if (facts->site_overrides == 1) {
        add_tile(dashboard, "Site overrides", g_strdup("1 site"),
                 g_strdup("One site has settings that differ from the default."),
                 NION_DASH_KNOWN, FALSE);
    } else {
        gchar *value = g_strdup_printf("%d sites", facts->site_overrides);
        add_tile(dashboard, "Site overrides", value,
                 g_strdup("These sites have settings that differ from the "
                          "default."),
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- Temporary permissions ---- */
    if (facts->temporary_permissions < 0) {
        add_tile(dashboard, "Temporary permissions", g_strdup("Not tracked"),
                 g_strdup("Permission grants given on this window are not "
                          "counted for display."),
                 NION_DASH_UNKNOWN, FALSE);
    } else if (facts->temporary_permissions == 0) {
        add_tile(dashboard, "Temporary permissions",
                 g_strdup("None"),
                 g_strdup("No site currently holds permission for the camera, "
                          "microphone, location or notifications."),
                 NION_DASH_KNOWN, FALSE);
    } else if (facts->temporary_permissions == 1) {
        add_tile(dashboard, "Temporary permissions", g_strdup("1 grant"),
                 g_strdup("One permission is held for this window and is dropped "
                          "when it closes."),
                 NION_DASH_KNOWN, FALSE);
    } else {
        gchar *value = g_strdup_printf("%d grants", facts->temporary_permissions);
        add_tile(dashboard, "Temporary permissions", value,
                 g_strdup("These permissions are held for this window and are "
                          "dropped when it closes."),
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- What is stored on this machine ---- */
    if (facts->is_private) {
        add_tile(dashboard, "What is written to disk",
                 g_strdup("Nothing from this window"),
                 g_strdup("Private windows keep no history and no reading list. "
                          "Cookies and site data for this window are discarded "
                          "when it closes."),
                 NION_DASH_KNOWN, FALSE);
    } else {
        gchar *value = facts->history_entries == 1
                           ? g_strdup("1 history entry")
                           : g_strdup_printf("%u history entries",
                                             facts->history_entries);
        gchar *detail = facts->reading_list_entries == 1
                            ? g_strdup("Kept on this device only. The reading "
                                       "list holds 1 saved page. Nothing is "
                                       "synced or uploaded.")
                            : g_strdup_printf("Kept on this device only. The "
                                              "reading list holds %u saved page(s). "
                                              "Nothing is synced or uploaded.",
                                              facts->reading_list_entries);
        add_tile(dashboard, "What is written to disk", value, detail,
                 NION_DASH_KNOWN, FALSE);
    }

    /* ---- Tabs ---- */
    {
        gchar *value = facts->active_tabs == 1
                           ? g_strdup("1 tab open")
                           : g_strdup_printf("%u tabs open", facts->active_tabs);
        gchar *detail = facts->suspended_tabs == 0
                            ? g_strdup("Nothing is suspended, so every tab is "
                                       "holding memory.")
                            : g_strdup_printf("%u suspended tab(s) have released "
                                              "their content and will reload when "
                                              "opened again.", facts->suspended_tabs);
        add_tile(dashboard, "Open tabs", value, detail, NION_DASH_KNOWN, FALSE);
    }

    /* ---- The honest gap ----
     * NiOn does not count blocked requests. Saying so is more useful than a
     * number nobody measured, and it keeps the dashboard's other figures
     * trustworthy by contrast. */
    add_tile(dashboard, "Requests blocked",
             g_strdup("Not counted"),
             g_strdup("NiOn does not keep a counter for blocked requests. It "
                      "would rather show nothing than a number that was not "
                      "measured."),
             NION_DASH_UNKNOWN, FALSE);

    return dashboard;
}

gchar *
nion_dashboard_summary(const NionDashboard *dashboard)
{
    if (!dashboard)
        return g_strdup("No dashboard available");

    guint attention = 0;
    guint known = 0;
    for (guint i = 0; i < dashboard->count; i++) {
        if (dashboard->tiles[i].attention)
            attention++;
        if (dashboard->tiles[i].kind == NION_DASH_KNOWN)
            known++;
    }

    /* Only counts what it actually has. "known" is the number of tiles backed by
     * a measured value, not a claim about the world. */
    if (attention == 0) {
        return g_strdup_printf("%u things reported, none needing attention", known);
    }
    if (attention == 1)
        return g_strdup_printf("%u things reported, 1 needs attention", known);
    return g_strdup_printf("%u things reported, %u need attention", known, attention);
}

gchar *
nion_dashboard_plain_text(const NionDashboard *dashboard)
{
    if (!dashboard)
        return g_strdup("");

    GString *out = g_string_new(NULL);
    for (guint i = 0; i < dashboard->count; i++) {
        const NionDashTile *tile = &dashboard->tiles[i];
        g_string_append_printf(out, "%s%s: %s\n  %s\n",
                               tile->attention ? "! " : "",
                               tile->label ? tile->label : "",
                               tile->value ? tile->value : "",
                               tile->detail ? tile->detail : "");
    }
    return g_string_free(out, FALSE);
}
