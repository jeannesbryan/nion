/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for Tor bootstrap phrasing (roadmap T1-4).
 *
 * The fixtures are real lines captured from the bundled Tor binary
 * (0.4.9.16) on this machine, not lines written from memory. If a future Tor
 * changes the wording, this test says so instead of letting the parser drift.
 */

#include <glib.h>

#include "tor-status.h"

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

/* Timestamps kept in the fixture on purpose: the parser must find the marker
 * inside a full log line, not only at the start. */
static const gchar *const real_lines[] = {
    "Nov 18 22:06:04.000 [notice] Bootstrapped 0% (starting): Starting",
    "Nov 18 22:06:05.000 [notice] Bootstrapped 5% (conn): Connecting to a relay",
    "Nov 18 22:06:06.000 [notice] Bootstrapped 10% (conn_done): Connected to a relay",
    "Nov 18 22:06:07.000 [notice] Bootstrapped 14% (handshake): Handshaking with a relay",
    "Nov 18 22:06:07.000 [notice] Bootstrapped 15% (handshake_done): Handshake with a relay done",
    "Nov 18 22:06:07.000 [notice] Bootstrapped 20% (onehop_create): Establishing an encrypted directory connection",
    "Nov 18 22:06:07.000 [notice] Bootstrapped 25% (requesting_status): Asking for networkstatus consensus",
    "Nov 18 22:06:07.000 [notice] Bootstrapped 30% (loading_status): Loading networkstatus consensus",
    "Nov 18 22:06:11.000 [notice] Bootstrapped 40% (loading_keys): Loading authority key certs",
    "Nov 18 22:06:12.000 [notice] Bootstrapped 45% (requesting_descriptors): Asking for relay descriptors",
    "Nov 18 22:06:13.000 [notice] Bootstrapped 50% (loading_descriptors): Loading relay descriptors",
    "Nov 18 22:06:16.000 [notice] Bootstrapped 55% (loading_descriptors): Loading relay descriptors",
};

static const gint real_percents[] = { 0, 5, 10, 14, 15, 20, 25, 30, 40, 45, 50, 55 };

static const gchar *const real_tags[] = {
    "starting", "conn", "conn_done", "handshake", "handshake_done",
    "onehop_create", "requesting_status", "loading_status", "loading_keys",
    "requesting_descriptors", "loading_descriptors", "loading_descriptors",
};

static const gchar *const real_summaries[] = {
    "Starting",
    "Connecting to a relay",
    "Connected to a relay",
    "Handshaking with a relay",
    "Handshake with a relay done",
    "Establishing an encrypted directory connection",
    "Asking for networkstatus consensus",
    "Loading networkstatus consensus",
    "Loading authority key certs",
    "Asking for relay descriptors",
    "Loading relay descriptors",
    "Loading relay descriptors",
};

static void
test_real_lines(void)
{
    for (guint i = 0; i < G_N_ELEMENTS(real_lines); i++) {
        gint percent = -1;
        gchar *tag = NULL;
        gchar *summary = NULL;
        gboolean parsed = nion_tor_parse_bootstrap(real_lines[i], &percent, &tag, &summary);

        gchar *label = g_strdup_printf("line %u (%s) parses", i, real_tags[i]);
        ok(parsed, label);
        g_free(label);

        label = g_strdup_printf("line %u percentage is %d", i, real_percents[i]);
        ok(percent == real_percents[i], label);
        g_free(label);

        label = g_strdup_printf("line %u tag is %s", i, real_tags[i]);
        ok(g_strcmp0(tag, real_tags[i]) == 0, label);
        g_free(label);

        label = g_strdup_printf("line %u keeps Tor's own wording", i);
        ok(g_strcmp0(summary, real_summaries[i]) == 0, label);
        g_free(label);

        g_free(tag);
        g_free(summary);
    }
}

static void
test_non_bootstrap_lines(void)
{
    const gchar *others[] = {
        "Nov 18 22:06:00.000 [notice] Tor 0.4.9.16 opening log file.",
        "Nov 18 22:06:04.000 [notice] Opening Socks listener on 127.0.0.1:19177",
        "Nov 18 22:06:20.000 [warn] Circuit build timeout",
        "",
    };
    for (guint i = 0; i < G_N_ELEMENTS(others); i++) {
        gint percent = -1;
        gchar *tag = NULL;
        gchar *summary = NULL;
        ok(!nion_tor_parse_bootstrap(others[i], &percent, &tag, &summary),
           "a non-bootstrap line is not treated as progress");
        ok(percent == -1, "the percentage is untouched for a non-bootstrap line");
        ok(tag == NULL && summary == NULL, "nothing is allocated for a non-bootstrap line");
    }

    gint percent = -1;
    ok(!nion_tor_parse_bootstrap(NULL, &percent, NULL, NULL),
       "NULL input is refused without crashing");
}

static void
test_line_without_description(void)
{
    gint percent = -1;
    gchar *tag = NULL;
    gchar *summary = NULL;
    ok(nion_tor_parse_bootstrap("Bootstrapped 70% (loading_descriptors)", &percent,
                                &tag, &summary),
       "a line with a tag but no description still parses");
    ok(percent == 70, "the percentage is read");
    ok(g_strcmp0(tag, "loading_descriptors") == 0, "the tag is read");
    ok(summary == NULL || *summary == '\0', "the description is absent, not invented");
    g_free(tag);
    g_free(summary);
}

static void
test_malformed_percentages(void)
{
    gint percent = -1;
    /* "Bootstrapped abc%" must not be read as 0%. */
    ok(!nion_tor_parse_bootstrap("Bootstrapped abc% (conn): x", &percent, NULL, NULL),
       "a non-numeric percentage is refused");
    ok(percent == -1, "the percentage stays untouched on a refused line");

    ok(!nion_tor_parse_bootstrap("Bootstrapped 50 (conn): x", &percent, NULL, NULL),
       "a missing percent sign is refused");

    /* Out of range values are clamped rather than shown as nonsense. */
    percent = -1;
    ok(nion_tor_parse_bootstrap("Bootstrapped 150% (done): Done", &percent, NULL, NULL),
       "an out-of-range percentage still parses");
    ok(percent == 100, "an over-range percentage is clamped to 100");

    percent = -1;
    ok(nion_tor_parse_bootstrap("Bootstrapped 0% (starting): Starting", &percent, NULL, NULL),
       "zero percent parses");
    ok(percent == 0, "zero percent stays zero, not treated as missing");
}

static void
test_phase_classification(void)
{
    struct {
        const gchar *tag;
        NionTorPhase expected;
    } cases[] = {
        { "starting", NION_TOR_PHASE_STARTING },
        { "conn", NION_TOR_PHASE_CONNECTING },
        { "conn_done", NION_TOR_PHASE_CONNECTING },
        { "handshake", NION_TOR_PHASE_HANDSHAKE },
        { "handshake_done", NION_TOR_PHASE_HANDSHAKE },
        { "onehop_create", NION_TOR_PHASE_DIRECTORY },
        { "requesting_status", NION_TOR_PHASE_DIRECTORY },
        { "loading_status", NION_TOR_PHASE_DIRECTORY },
        { "loading_keys", NION_TOR_PHASE_DIRECTORY },
        { "requesting_descriptors", NION_TOR_PHASE_DESCRIPTORS },
        { "loading_descriptors", NION_TOR_PHASE_DESCRIPTORS },
        { "ap_conn", NION_TOR_PHASE_CIRCUIT },
        { "circuit_create", NION_TOR_PHASE_CIRCUIT },
        { "done", NION_TOR_PHASE_DONE },
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar *label = g_strdup_printf("tag %s classifies correctly", cases[i].tag);
        ok(nion_tor_phase_for_tag(cases[i].tag) == cases[i].expected, label);
        g_free(label);
    }

    /* An unknown tag must never be reported as finished: that would unlock
     * browsing on a state nobody understood. */
    ok(nion_tor_phase_for_tag("some_future_phase") == NION_TOR_PHASE_UNKNOWN,
       "an unknown tag is unknown, not done");
    ok(nion_tor_phase_for_tag("") == NION_TOR_PHASE_UNKNOWN, "an empty tag is unknown");
    ok(nion_tor_phase_for_tag(NULL) == NION_TOR_PHASE_UNKNOWN, "a NULL tag is unknown");
}

static void
test_fallback_labels(void)
{
    ok(g_strcmp0(nion_tor_phase_fallback_label(NION_TOR_PHASE_DESCRIPTORS),
                 "Loading relay descriptors") == 0,
       "the descriptors phase has plain wording");
    /* Unknown phases say nothing rather than guessing. */
    ok(nion_tor_phase_fallback_label(NION_TOR_PHASE_UNKNOWN) == NULL,
       "an unknown phase has no invented label");
    ok(nion_tor_phase_fallback_label(NION_TOR_PHASE_NONE) == NULL,
       "no phase means no label");
}

static void
test_status_text(void)
{
    gchar *text = nion_tor_status_text(45, "Asking for relay descriptors");
    ok(g_strcmp0(text, "○ CONNECTING TO TOR… 45% — Asking for relay descriptors") == 0,
       "the status line carries the percentage and Tor's own wording");
    g_free(text);

    text = nion_tor_status_text(45, NULL);
    ok(g_strcmp0(text, "○ CONNECTING TO TOR… 45%") == 0,
       "without a description the percentage is still shown");
    g_free(text);

    text = nion_tor_status_text(45, "");
    ok(g_strcmp0(text, "○ CONNECTING TO TOR… 45%") == 0,
       "an empty description is not shown as a dangling dash");
    g_free(text);

    text = nion_tor_status_text(-5, NULL);
    ok(g_strcmp0(text, "○ CONNECTING TO TOR… 0%") == 0, "a negative percentage clamps to 0");
    g_free(text);

    text = nion_tor_status_text(500, NULL);
    ok(g_strcmp0(text, "○ CONNECTING TO TOR… 100%") == 0, "an excessive percentage clamps to 100");
    g_free(text);

    /* The status line must never claim a connection exists before it does. */
    text = nion_tor_status_text(99, "Loading relay descriptors");
    ok(strstr(text, "CONNECTING") != NULL, "at 99% NiOn still says connecting");
    ok(strstr(text, "CONNECTED") == NULL, "at 99% NiOn does not claim a connection");
    g_free(text);
}

static void
test_tab_waiting_text(void)
{
    gchar *text = nion_tor_tab_waiting_text(45, "Asking for relay descriptors");
    ok(g_strcmp0(text, "Waiting for Tor — 45%") == 0,
       "the tab label stays short and carries the percentage");
    g_free(text);

    text = nion_tor_tab_waiting_text(-1, NULL);
    ok(g_strcmp0(text, "Waiting for Tor — 0%") == 0, "the tab label clamps a negative percentage");
    g_free(text);

    text = nion_tor_tab_waiting_text(300, NULL);
    ok(g_strcmp0(text, "Waiting for Tor — 100%") == 0, "the tab label clamps an excessive percentage");
    g_free(text);
}

static void
test_blocked_explanation(void)
{
    gchar *text = nion_tor_blocked_explanation(45, "Asking for relay descriptors");
    ok(strstr(text, "blocked") != NULL, "the explanation says browsing is blocked");
    ok(strstr(text, "45%") != NULL, "the explanation carries the percentage");
    ok(strstr(text, "Asking for relay descriptors") != NULL,
       "the explanation carries Tor's wording");
    /* Fail-closed phrasing: never imply the request went out another way. */
    ok(strstr(text, "direct") == NULL, "the explanation never mentions a direct connection");
    g_free(text);

    text = nion_tor_blocked_explanation(10, NULL);
    ok(strstr(text, "blocked") != NULL, "the explanation says blocked without a description");
    ok(strstr(text, "10%") != NULL, "the percentage is still shown");
    g_free(text);
}

int
main(void)
{
    test_real_lines();
    test_non_bootstrap_lines();
    test_line_without_description();
    test_malformed_percentages();
    test_phase_classification();
    test_fallback_labels();
    test_status_text();
    test_tab_waiting_text();
    test_blocked_explanation();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
