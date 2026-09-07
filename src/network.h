/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_NETWORK_H
#define NION_NETWORK_H

#include "types.h"


/* Extracted from src/main.c during Phase 3 modularization (NiOn 2.0.0). */

void nion_validate_cookie_store(NionApp *app);
void nion_apply_network_proxy(NionApp *app);
gboolean nion_prepare_network(NionApp *app);

#endif /* NION_NETWORK_H */
