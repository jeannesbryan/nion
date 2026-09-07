/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TOR_CORE_H
#define NION_TOR_CORE_H

#include "types.h"

/* UI notification callbacks. The UI (main.c) registers implementations
 * so tor-core does not call into UI code directly. */
typedef struct {
    void (*set_tor_ready)(NionApp *app, gboolean ready);
    void (*set_tor_progress)(NionApp *app, gint percent);
    void (*set_tor_error)(NionApp *app, const gchar *message);
    void (*set_status)(NionApp *app, const gchar *message);
} NionTorCallbacks;

void nion_tor_set_callbacks(const NionTorCallbacks *callbacks);


/* Extracted from src/main.c during Phase 3 modularization (NiOn 2.0.0). */

void nion_cleanup_stale_tor(NionApp *app);
gboolean nion_choose_tor_port(NionApp *app);
gboolean nion_start_tor(NionApp *app);
void nion_stop_tor_gracefully(NionApp *app);

#endif /* NION_TOR_CORE_H */
