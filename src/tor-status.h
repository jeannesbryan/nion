/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TOR_STATUS_H
#define NION_TOR_STATUS_H

#include <glib.h>

/* Human-readable Tor bootstrap state (v2.3.2, roadmap T1-4).
 *
 * Tor is slow by nature: a fresh client needs tens of seconds to pick guards
 * and fetch descriptors, and an .onion service can take minutes. A browser that
 * only says "connecting" for a minute looks broken, so NiOn shows what Tor is
 * doing right now.
 *
 * The wording comes from Tor itself. Its log line carries both a percentage and
 * a description:
 *
 *   Bootstrapped 45% (requesting_descriptors): Asking for relay descriptors
 *
 * Parsing the description out of the line is more durable than keeping a
 * private tag-to-English dictionary here: if Tor renames or adds a phase, the
 * text still arrives. The tag is only used to classify the phase, and the
 * small fallback table below covers a line that arrives without a description.
 *
 * This module has no GTK dependency so the parsing and the phrasing can be
 * tested directly.
 */

typedef enum {
    NION_TOR_PHASE_NONE = 0,
    NION_TOR_PHASE_STARTING,
    NION_TOR_PHASE_CONNECTING,
    NION_TOR_PHASE_HANDSHAKE,
    NION_TOR_PHASE_DIRECTORY,
    NION_TOR_PHASE_DESCRIPTORS,
    NION_TOR_PHASE_CIRCUIT,
    NION_TOR_PHASE_DONE,
    NION_TOR_PHASE_UNKNOWN
} NionTorPhase;

/* Parse one Tor log line. Returns TRUE when the line is a bootstrap progress
 * line, in which case *percent is 0..100 and *summary is the description Tor
 * supplied (may be an empty string if the line carried none). *summary is
 * newly allocated and must be freed by the caller; pass NULL if uninterested.
 * Lines that are not bootstrap lines return FALSE and change nothing. */
gboolean nion_tor_parse_bootstrap(const gchar *line, gint *percent,
                                  gchar **tag, gchar **summary);

/* Classify a bootstrap tag. Unknown tags return NION_TOR_PHASE_UNKNOWN, which
 * callers must treat as "keep showing the percentage" rather than "connected". */
NionTorPhase nion_tor_phase_for_tag(const gchar *tag);

/* Wording for a phase, used only when Tor's line carried no description. */
const gchar *nion_tor_phase_fallback_label(NionTorPhase phase);

/* The status line shown while Tor is still working, for example
 * "○ CONNECTING TO TOR… 45% — Asking for relay descriptors". When summary is
 * NULL or empty the percentage alone is used, never a guess. */
gchar *nion_tor_status_text(gint percent, const gchar *summary);

/* Short state for a tab that is waiting for Tor, for example
 * "Waiting for Tor — 45%" or "Waiting for Tor — Asking for relay descriptors".
 * Callers are expected to put the full sentence in a tooltip. */
gchar *nion_tor_tab_waiting_text(gint percent, const gchar *summary);

/* One sentence explaining why browsing is blocked, for the error surface:
 * "Browsing is blocked until Tor connects. Tor is at 45%: Asking for relay
 *  descriptors." Never implies the request went out over a direct connection. */
gchar *nion_tor_blocked_explanation(gint percent, const gchar *summary);

#endif /* NION_TOR_STATUS_H */
