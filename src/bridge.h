/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_BRIDGE_H
#define NION_BRIDGE_H

#include "types.h"
#include <gio/gio.h>

/* Censorship circumvention: bundled pluggable transports (v2.2).
 *
 * NiOn ships the Tor Expert Bundle, which already contains the supported
 * pluggable transports. This module owns the bridge list, its persistence and
 * the torrc fragment that turns bridge mode on -- it never talks to Tor
 * itself. Fail-closed is the whole point here: if the user asked for bridges
 * and NiOn cannot honour that request exactly, Tor must not be started at all,
 * because a silent bridge-less connection in a censored network is precisely
 * the outcome the user asked to avoid. */

typedef struct {
    void (*set_status)(NionApp *app, const gchar *text);
    /* Stop the bundled Tor and start it again through the fail-closed path so
     * a changed bridge configuration takes effect (implemented by app.c). */
    void (*restart_tor)(NionApp *app);
} NionBridgeCallbacks;

void nion_bridge_set_callbacks(const NionBridgeCallbacks *callbacks);

/* Load the persisted bridge list. Missing or corrupt state is quarantined and
 * bridge mode falls back to "disabled with no bridges", never to a partial
 * list. */
void nion_load_bridges(NionApp *app);

/* Persist the current list atomically (0600). Private windows never write. */
void nion_save_bridges(NionApp *app);

/* TRUE when the user enabled bridge mode AND at least one line is stored. */
gboolean nion_bridges_active(const NionApp *app);

/* Build the torrc fragment for the current bridge configuration.
 * Returns a newly allocated string (possibly empty when bridge mode is off).
 * Returns NULL and sets *error when bridge mode cannot be honoured; the caller
 * must then refuse to start Tor. */
gchar *nion_bridge_torrc_fragment(NionApp *app, gchar **error);

/* Human-readable single line describing the transport situation, for the
 * bridge dialog and the About/status surfaces. */
gchar *nion_bridge_transport_summary(void);

/* True when the argument is an acceptable bridge line (no torrc injection
 * surface). Exposed for the static regression suite. */
gboolean nion_bridge_line_is_valid(const gchar *line);

/* GAction handler: menu -> Bridges (Censorship Circumvention)…. */
void action_bridges(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Release test hook used by scripts/test-bridges-stage1.sh (see main.c):
 * exercises the real validator and the real torrc builder, prints a report and
 * returns TRUE when every case behaved as required. Read-only: no profile, no
 * network, no Tor. */
gboolean nion_bridge_selfcheck(void);

#endif /* NION_BRIDGE_H */
