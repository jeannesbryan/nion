/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_PRIVACY_H
#define NION_PRIVACY_H

#include "types.h"

/* privacy (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

void nion_apply_cookie_policy(NionApp *app);
void nion_apply_privacy_settings(NionApp *app, WebKitSettings *settings);

#endif /* NION_PRIVACY_H */
