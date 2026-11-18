/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <glib.h>
#include <string.h>

#include "tor-status.h"

/* Tor bootstrap phrasing (v2.3.2, roadmap T1-4).
 *
 * The tags below are the ones this build of Tor actually emits. They were
 * captured from the bundled binary (0.4.9.16) rather than copied from a
 * document, so the table matches what NiOn ships:
 *
 *   Bootstrapped  0% (starting): Starting
 *   Bootstrapped  5% (conn): Connecting to a relay
 *   Bootstrapped 10% (conn_done): Connected to a relay
 *   Bootstrapped 14% (handshake): Handshaking with a relay
 *   Bootstrapped 15% (handshake_done): Handshake with a relay done
 *   Bootstrapped 20% (onehop_create): Establishing an encrypted directory connection
 *   Bootstrapped 25% (requesting_status): Asking for networkstatus consensus
 *   Bootstrapped 30% (loading_status): Loading networkstatus consensus
 *   Bootstrapped 40% (loading_keys): Loading authority key certs
 *   Bootstrapped 45% (requesting_descriptors): Asking for relay descriptors
 *   Bootstrapped 50% (loading_descriptors): Loading relay descriptors
 *
 * The fallback labels exist for the case where a line arrives with a tag but no
 * description. They are deliberately plain: when in doubt, say less rather than
 * invent a specific claim about what Tor is doing.
 */

/* Tor logs the percentage then the tag in parentheses, then optionally a
 * colon and a description. Parsing is done by hand instead of with sscanf
 * so the tag and the description keep their exact boundaries. */
gboolean
nion_tor_parse_bootstrap(const gchar *line, gint *percent, gchar **tag,
                         gchar **summary)
{
    if (tag)
        *tag = NULL;
    if (summary)
        *summary = NULL;
    if (!line)
        return FALSE;

    const gchar *marker = strstr(line, "Bootstrapped ");
    if (!marker)
        return FALSE;

    const gchar *cursor = marker + strlen("Bootstrapped ");

    /* Percentage: one or more digits followed by '%'. */
    if (!g_ascii_isdigit(*cursor))
        return FALSE;
    gint value = 0;
    while (g_ascii_isdigit(*cursor)) {
        value = value * 10 + (*cursor - '0');
        if (value > 1000) /* absurd, refuse rather than overflow */
            return FALSE;
        cursor++;
    }
    if (*cursor != '%')
        return FALSE;
    cursor++;

    if (value < 0)
        value = 0;
    if (value > 100)
        value = 100;
    if (percent)
        *percent = value;

    /* Tag: "(name)". Optional in principle, present in practice. */
    gchar *parsed_tag = NULL;
    if (*cursor == ' ') {
        cursor++;
    }
    if (*cursor == '(') {
        const gchar *close = strchr(cursor, ')');
        if (!close)
            return FALSE;
        parsed_tag = g_strndup(cursor + 1, (gsize)(close - cursor - 1));
        cursor = close + 1;
    }

    /* Description: everything after ": ", which is Tor's own wording. */
    gchar *parsed_summary = NULL;
    const gchar *colon = strstr(cursor, ": ");
    if (colon) {
        parsed_summary = g_strdup(colon + 2);
        g_strstrip(parsed_summary);
    }

    if (tag)
        *tag = parsed_tag;
    else
        g_free(parsed_tag);
    if (summary)
        *summary = parsed_summary;
    else
        g_free(parsed_summary);

    return TRUE;
}

NionTorPhase
nion_tor_phase_for_tag(const gchar *tag)
{
    if (!tag || !*tag)
        return NION_TOR_PHASE_UNKNOWN;

    struct {
        const gchar *tag;
        NionTorPhase phase;
    } table[] = {
        /* Connecting to the first relay: the network path is being built. */
        { "conn", NION_TOR_PHASE_CONNECTING },
        { "conn_done", NION_TOR_PHASE_CONNECTING },

        /* TLS and link handshakes. */
        { "handshake", NION_TOR_PHASE_HANDSHAKE },
        { "handshake_done", NION_TOR_PHASE_HANDSHAKE },

        /* Directory information: consensus and key material. */
        { "onehop_create", NION_TOR_PHASE_DIRECTORY },
        { "requesting_status", NION_TOR_PHASE_DIRECTORY },
        { "loading_status", NION_TOR_PHASE_DIRECTORY },
        { "loading_keys", NION_TOR_PHASE_DIRECTORY },

        /* Relay descriptors: usually the longest part on a fresh client. */
        { "requesting_descriptors", NION_TOR_PHASE_DESCRIPTORS },
        { "loading_descriptors", NION_TOR_PHASE_DESCRIPTORS },

        /* Circuit construction for application traffic. */
        { "ap_conn", NION_TOR_PHASE_CIRCUIT },
        { "ap_conn_done", NION_TOR_PHASE_CIRCUIT },
        { "circuit_create", NION_TOR_PHASE_CIRCUIT },

        /* Terminal states. */
        { "done", NION_TOR_PHASE_DONE },
        { "starting", NION_TOR_PHASE_STARTING },
    };

    for (guint i = 0; i < G_N_ELEMENTS(table); i++) {
        if (g_str_equal(table[i].tag, tag))
            return table[i].phase;
    }
    return NION_TOR_PHASE_UNKNOWN;
}

const gchar *
nion_tor_phase_fallback_label(NionTorPhase phase)
{
    switch (phase) {
    case NION_TOR_PHASE_STARTING:
        return "Starting";
    case NION_TOR_PHASE_CONNECTING:
        return "Connecting to a relay";
    case NION_TOR_PHASE_HANDSHAKE:
        return "Handshaking with a relay";
    case NION_TOR_PHASE_DIRECTORY:
        return "Loading directory information";
    case NION_TOR_PHASE_DESCRIPTORS:
        return "Loading relay descriptors";
    case NION_TOR_PHASE_CIRCUIT:
        return "Building a circuit";
    case NION_TOR_PHASE_DONE:
        return "Connected";
    case NION_TOR_PHASE_NONE:
    case NION_TOR_PHASE_UNKNOWN:
    default:
        /* Unknown phases deliberately say nothing specific: a wrong claim
         * about what Tor is doing is worse than no claim. */
        return NULL;
    }
}

/* Text for the status line while Tor works. Tor's own wording is preferred;
 * the percentage is always shown so progress is visible even when the phase is
 * unrecognised. */
gchar *
nion_tor_status_text(gint percent, const gchar *summary)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    const gchar *detail = (summary && *summary) ? summary : NULL;
    if (detail)
        return g_strdup_printf("○ CONNECTING TO TOR… %d%% — %s", percent, detail);
    return g_strdup_printf("○ CONNECTING TO TOR… %d%%", percent);
}

/* Short label for a tab that is waiting. Tabs are narrow, so the phase goes
 * here only when it is short enough to be useful; the full sentence belongs in
 * the tooltip. */
gchar *
nion_tor_tab_waiting_text(gint percent, const gchar *summary)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    (void)summary;
    return g_strdup_printf("Waiting for Tor — %d%%", percent);
}

gchar *
nion_tor_blocked_explanation(gint percent, const gchar *summary)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;

    if (summary && *summary) {
        return g_strdup_printf(
            "Browsing is blocked until Tor connects. Tor is at %d%%: %s.",
            percent, summary);
    }
    return g_strdup_printf(
        "Browsing is blocked until Tor connects. Tor is at %d%%.", percent);
}
