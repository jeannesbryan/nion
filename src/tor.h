/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_TOR_H
#define NION_TOR_H

#include "types.h"

/* Exported tor functions (defined in tor.c, called from main.c) */
gboolean nion_start_tor(NionApp *app);
gboolean nion_prepare_network(NionApp *app);
void nion_apply_network_proxy(NionApp *app);
void nion_cleanup_stale_tor(NionApp *app);
void nion_prepare_dirs(NionApp *app);
void nion_validate_cookie_store(NionApp *app);
gboolean nion_choose_tor_port(NionApp *app);
gboolean nion_pid_alive(gint64 pid);
void nion_wait_for_pid_exit(gint64 pid, guint timeout_ms);
guint nion_count_nonblank_tabs(NionApp *app);

/* Dependencies defined in main.c (tor.c calls these across translation unit).
 * They are declared here so tor.c can link against them. */
void nion_set_status(NionApp *app, const gchar *text);
void nion_set_tor_error(NionApp *app, const gchar *message);
void nion_set_tor_progress(NionApp *app, gint percent);
void on_download_started(WebKitNetworkSession *session, WebKitDownload *download,
                         gpointer user_data);

#endif /* NION_TOR_H */
