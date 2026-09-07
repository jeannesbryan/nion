/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_SESSION_H
#define NION_SESSION_H

#include <glib.h>
#include "types.h"

/* Session persistence: save/restore browsing session state.
 *
 * Extracted from src/main.c during Phase 1 modularization (NiOn 2.0.0).
 * These functions form a self-contained core: they depend only on util.h
 * helpers and types.h, not on any tab/UI construction functions.
 */

void nion_save_session(NionApp *app, gboolean clean_shutdown);
void nion_schedule_session_save(NionApp *app);

#endif /* NION_SESSION_H */
