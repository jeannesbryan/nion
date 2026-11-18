/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TOR_CORE_H
#define NION_TOR_CORE_H

#include "types.h"

/* UI notification callbacks. The UI (main.c) registers implementations
 * so tor-core does not call into UI code directly. */
typedef struct {
    void (*set_tor_ready)(NionApp *app, gboolean ready);
    /* v2.3.2 (T1-4): the description Tor supplied with the percentage, or NULL
     * when the log line carried none. */
    void (*set_tor_progress)(NionApp *app, gint percent, const gchar *summary);
    void (*set_tor_error)(NionApp *app, const gchar *message);
    void (*set_status)(NionApp *app, const gchar *message);
} NionTorCallbacks;

void nion_tor_set_callbacks(const NionTorCallbacks *callbacks);


/* Extracted from src/main.c during Phase 3 modularization (NiOn 2.0.0). */

void nion_cleanup_stale_tor(NionApp *app);
gboolean nion_choose_tor_port(NionApp *app);
gboolean nion_start_tor(NionApp *app);
void nion_stop_tor_gracefully(NionApp *app);

/* v2.3.2 (roadmap T1-4): start Tor again after a failure the user can see.
 *
 * Before this existed, a failed or timed-out bootstrap left the window with no
 * way forward: browsing stayed blocked (correctly) and the only escape was to
 * quit the browser. Retrying must not stack a second Tor process on top of a
 * live one, so this tears the old runtime down first and reuses the same
 * delayed-restart path the port-conflict and corrupt-state recoveries use.
 * Returns FALSE when Tor could not even be started (missing bundle, no free
 * port, refused bridge configuration); the reason has already been surfaced by
 * then. */
gboolean nion_retry_tor(NionApp *app);

#endif /* NION_TOR_CORE_H */
