/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for the privacy dashboard (roadmap T2-8).
 *
 * The rules under test are not "does the page look nice" but "is every claim on
 * it backed by something NiOn knows". A dashboard is the one page where an
 * invented number is a betrayal rather than a cosmetic bug, so the honesty
 * rules get their own tests: an unmeasured fact must say so, a private window
 * must not receive a figure it cannot have, and a failure must be visible rather
 * than quietly reported as fine.
 */

#include <glib.h>
#include <string.h>

#include "privacy-dashboard.h"

static gint failures = 0;
static gint checks = 0;

static void
ok(gboolean condition, const gchar *what)
{
    checks++;
    if (!condition) {
        failures++;
        g_printerr("FAIL: %s\n", what);
    }
}

/* A healthy, ordinary window: the shape most runs look like. */
static NionDashFacts
healthy_facts(void)
{
    NionDashFacts facts = { 0 };
    facts.is_private = FALSE;
    facts.tor_ready = TRUE;
    facts.tor_bootstrap_percent = 100;
    facts.tor_socks_port = "127.0.0.1:19050";
    facts.security_level = "Standard";
    facts.block_third_party_cookies = TRUE;
    facts.content_filter_ready = TRUE;
    facts.site_overrides = 0;
    facts.temporary_permissions = 0;
    facts.history_entries = 12;
    facts.reading_list_entries = 0;
    facts.active_tabs = 1;
    facts.suspended_tabs = 0;
    return facts;
}

static const NionDashTile *
find_tile(const NionDashboard *dashboard, const gchar *label)
{
    for (guint i = 0; i < dashboard->count; i++) {
        if (g_strcmp0(dashboard->tiles[i].label, label) == 0)
            return &dashboard->tiles[i];
    }
    return NULL;
}

/* ---------------------------------------------------------------- *
 * Nothing is invented
 * ---------------------------------------------------------------- */

static void
test_blocked_requests_is_never_a_number(void)
{
    /* NiOn does not count blocked requests. The tile must say so, and must never
     * come back with a figure, whatever the rest of the facts look like. */
    NionDashFacts facts = healthy_facts();
    NionDashboard *dashboard = nion_dashboard_build(&facts);

    const NionDashTile *tile = find_tile(dashboard, "Requests blocked");
    ok(tile != NULL, "the dashboard is honest about blocked requests");
    ok(tile->kind == NION_DASH_UNKNOWN,
       "blocked requests are reported as unknown, not as a measurement");
    ok(tile->value != NULL && strstr(tile->value, "Not counted") != NULL,
       "the tile says the number is not counted");
    /* And critically: no digits posing as a count. */
    ok(tile->value != NULL && !g_ascii_isdigit(tile->value[0]),
       "the value does not start with a number, so nobody reads it as a count");

    nion_dashboard_free(dashboard);
}

static void
test_unknown_site_overrides_says_so(void)
{
    NionDashFacts facts = healthy_facts();
    facts.site_overrides = -1; /* the caller does not know */

    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tile = find_tile(dashboard, "Site overrides");
    ok(tile->kind == NION_DASH_UNKNOWN, "an unmeasured override count is marked unknown");
    ok(strstr(tile->value, "Not tracked") != NULL,
       "the tile says it is not tracked instead of showing zero");
    nion_dashboard_free(dashboard);
}

static void
test_unknown_permissions_says_so(void)
{
    NionDashFacts facts = healthy_facts();
    facts.temporary_permissions = -1;
    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tile = find_tile(dashboard, "Temporary permissions");
    ok(tile->kind == NION_DASH_UNKNOWN, "an unmeasured permission count is marked unknown");
    ok(strstr(tile->value, "Not tracked") != NULL,
       "an unmeasured permission count is not shown as zero");
    nion_dashboard_free(dashboard);
}

static void
test_missing_security_level_is_unknown(void)
{
    NionDashFacts facts = healthy_facts();
    facts.security_level = NULL;
    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tile = find_tile(dashboard, "Security level");
    ok(tile->kind == NION_DASH_UNKNOWN, "a missing security level is not reported as known");
    nion_dashboard_free(dashboard);
}

/* ---------------------------------------------------------------- *
 * Failures are visible, never smoothed over
 * ---------------------------------------------------------------- */

static void
test_tor_failure_needs_attention(void)
{
    NionDashFacts facts = healthy_facts();
    facts.tor_ready = FALSE;
    facts.tor_failed = TRUE;
    facts.tor_socks_port = NULL;

    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tor = find_tile(dashboard, "Tor");
    ok(tor->attention, "a failed Tor is flagged as needing attention");
    ok(strstr(tor->value, "Unavailable") != NULL, "the tile says Tor is unavailable");

    /* The promise that matters: no direct connection is ever implied. */
    ok(strstr(tor->detail, "direct") != NULL,
       "the tile states that no direct connection replaces Tor");
    ok(strstr(tor->detail, "never") != NULL,
       "the tile is explicit that the fallback does not happen");

    const NionDashTile *path = find_tile(dashboard, "Network path");
    ok(path->kind == NION_DASH_UNAVAILABLE,
       "with Tor down the network path is reported as not active, not as Tor-only");

    gchar *summary = nion_dashboard_summary(dashboard);
    ok(strstr(summary, "needs attention") != NULL,
       "the summary mentions the problem instead of reporting everything fine");
    g_free(summary);

    nion_dashboard_free(dashboard);
}

static void
test_connecting_tor_is_not_connected(void)
{
    NionDashFacts facts = healthy_facts();
    facts.tor_ready = FALSE;
    facts.tor_bootstrap_percent = 45;

    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tor = find_tile(dashboard, "Tor");
    ok(strstr(tor->value, "Connecting") != NULL, "a partial bootstrap reads as connecting");
    ok(strstr(tor->value, "45%") != NULL, "the percentage is shown");
    ok(strstr(tor->value, "Connected") == NULL, "it never claims to be connected");
    ok(!tor->attention, "a bootstrap in progress is not a problem needing attention");
    nion_dashboard_free(dashboard);
}

static void
test_content_filter_failure_is_visible(void)
{
    /* A filter that failed to load must not be reported as active: that would be
     * a security claim the browser cannot keep. */
    NionDashFacts facts = healthy_facts();
    facts.content_filter_ready = FALSE;
    facts.content_filter_failed = TRUE;

    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tile = find_tile(dashboard, "Content filtering");
    ok(tile->attention, "a failed filter is flagged");
    ok(strstr(tile->value, "Not active") != NULL, "a failed filter does not read as active");
    ok(strstr(tile->detail, "did not load") != NULL, "the tile says why");
    nion_dashboard_free(dashboard);
}

/* ---------------------------------------------------------------- *
 * Private window
 * ---------------------------------------------------------------- */

static void
test_private_window_claims_nothing_it_cannot_have(void)
{
    NionDashFacts facts = healthy_facts();
    facts.is_private = TRUE;
    facts.site_overrides = 0;         /* the private window has none */
    facts.history_entries = 0;
    facts.reading_list_entries = 0;

    NionDashboard *dashboard = nion_dashboard_build(&facts);

    const NionDashTile *overrides = find_tile(dashboard, "Site overrides");
    ok(overrides->kind == NION_DASH_UNAVAILABLE,
       "a private window reports site overrides as not applicable");
    ok(strstr(overrides->value, "None kept") != NULL,
       "the private window explains that nothing is kept");

    const NionDashTile *disk = find_tile(dashboard, "What is written to disk");
    /* It must not claim "0 history entries", which reads as a measurement of a
     * store that does not exist in this window. */
    ok(strstr(disk->value, "history") == NULL,
       "the private window does not report a history count at all");
    /* The claim has to be specific: nothing from THIS window is written. A vague
     * "nothing unusual" would leave the reader thinking writes do happen, which
     * is the opposite of what a private window guarantees. */
    ok(strstr(disk->value, "this window") != NULL,
       "the private window says nothing from this window is written");
    ok(strstr(disk->detail, "no history") != NULL,
       "the tile explains that a private window keeps no history");

    nion_dashboard_free(dashboard);
}

/* ---------------------------------------------------------------- *
 * Counts are reported with correct wording
 * ---------------------------------------------------------------- */

static void
test_singular_and_plural(void)
{
    NionDashFacts facts = healthy_facts();

    facts.history_entries = 1;
    facts.reading_list_entries = 1;
    facts.active_tabs = 1;
    NionDashboard *one = nion_dashboard_build(&facts);
    ok(strstr(find_tile(one, "What is written to disk")->value, "1 history entry") != NULL,
       "one entry is described in the singular");
    ok(strstr(find_tile(one, "What is written to disk")->detail, "1 saved page") != NULL,
       "one saved page is described in the singular");
    ok(strstr(find_tile(one, "Open tabs")->value, "1 tab open") != NULL,
       "one tab is described in the singular");
    nion_dashboard_free(one);

    facts.history_entries = 2;
    facts.reading_list_entries = 3;
    facts.active_tabs = 5;
    NionDashboard *many = nion_dashboard_build(&facts);
    ok(strstr(find_tile(many, "What is written to disk")->value, "2 history entries") != NULL,
       "two entries are described in the plural");
    ok(strstr(find_tile(many, "Open tabs")->value, "5 tabs open") != NULL,
       "five tabs are described in the plural");
    nion_dashboard_free(many);
}

static void
test_suspended_tabs_are_reported(void)
{
    NionDashFacts facts = healthy_facts();
    facts.active_tabs = 4;
    facts.suspended_tabs = 2;
    NionDashboard *dashboard = nion_dashboard_build(&facts);
    const NionDashTile *tile = find_tile(dashboard, "Open tabs");
    ok(strstr(tile->detail, "2 suspended") != NULL,
       "suspended tabs are counted in the detail line");
    nion_dashboard_free(dashboard);

    facts.suspended_tabs = 0;
    NionDashboard *none = nion_dashboard_build(&facts);
    ok(strstr(find_tile(none, "Open tabs")->detail, "Nothing is suspended") != NULL,
       "with nothing suspended the tile says so plainly");
    nion_dashboard_free(none);
}

/* ---------------------------------------------------------------- *
 * Summary
 * ---------------------------------------------------------------- */

static void
test_summary_counts_only_what_it_has(void)
{
    NionDashFacts facts = healthy_facts();
    NionDashboard *dashboard = nion_dashboard_build(&facts);

    guint known = 0;
    for (guint i = 0; i < dashboard->count; i++) {
        if (dashboard->tiles[i].kind == NION_DASH_KNOWN)
            known++;
    }
    gchar *expected = g_strdup_printf("%u things reported, none needing attention",
                                     known);
    gchar *summary = nion_dashboard_summary(dashboard);
    ok(g_strcmp0(summary, expected) == 0,
       "the summary counts the tiles that carry a value and nothing else");
    g_free(expected);
    g_free(summary);
    nion_dashboard_free(dashboard);

    /* Two problems must both be mentioned. */
    facts.tor_ready = FALSE;
    facts.tor_failed = TRUE;
    facts.content_filter_failed = TRUE;
    NionDashboard *two = nion_dashboard_build(&facts);
    summary = nion_dashboard_summary(two);
    ok(strstr(summary, "2 need attention") != NULL,
       "two problems are counted, not collapsed into one");
    g_free(summary);
    nion_dashboard_free(two);
}

/* ---------------------------------------------------------------- *
 * Plain text surface
 * ---------------------------------------------------------------- */

static void
test_plain_text_lists_every_tile(void)
{
    NionDashFacts facts = healthy_facts();
    facts.tor_ready = FALSE;
    facts.tor_failed = TRUE;
    NionDashboard *dashboard = nion_dashboard_build(&facts);

    gchar *text = nion_dashboard_plain_text(dashboard);
    for (guint i = 0; i < dashboard->count; i++) {
        gchar *needle = g_strdup_printf("%s:", dashboard->tiles[i].label);
        gchar *label = g_strdup_printf("the text surface mentions %s", dashboard->tiles[i].label);
        ok(strstr(text, needle) != NULL, label);
        g_free(needle);
        g_free(label);
    }
    /* A problem is marked so a text reader can see it too. */
    ok(strstr(text, "! Tor:") != NULL, "a problem is marked in the text surface");

    g_free(text);
    nion_dashboard_free(dashboard);
}

static void
test_null_facts_is_safe(void)
{
    NionDashboard *dashboard = nion_dashboard_build(NULL);
    ok(dashboard != NULL && dashboard->count == 0,
       "a NULL fact set yields an empty dashboard, not a crash");
    gchar *summary = nion_dashboard_summary(dashboard);
    ok(summary != NULL && *summary, "an empty dashboard still has a summary");
    g_free(summary);
    gchar *text = nion_dashboard_plain_text(dashboard);
    ok(text != NULL, "an empty dashboard still renders text");
    g_free(text);
    nion_dashboard_free(dashboard);

    /* And the free function tolerates NULL, so an early return cannot leak. */
    nion_dashboard_free(NULL);
}

int
main(void)
{
    test_blocked_requests_is_never_a_number();
    test_unknown_site_overrides_says_so();
    test_unknown_permissions_says_so();
    test_missing_security_level_is_unknown();

    test_tor_failure_needs_attention();
    test_connecting_tor_is_not_connected();
    test_content_filter_failure_is_visible();

    test_private_window_claims_nothing_it_cannot_have();

    test_singular_and_plural();
    test_suspended_tabs_are_reported();

    test_summary_counts_only_what_it_has();
    test_plain_text_lists_every_tile();
    test_null_facts_is_safe();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
