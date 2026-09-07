/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_UTIL_H
#define NION_UTIL_H

#include <glib.h>

/* Generic utilities (extracted from src/main.c, Phase 1, NiOn 2.0.0). */

gboolean nion_profile_file_within_limit(const gchar *path, goffset max_bytes);
void nion_quarantine_profile_file(const gchar *path, const gchar *label);
gboolean nion_base64_state_looks_valid(const gchar *base64);
gboolean nion_write_key_file_atomic(GKeyFile *key_file, const gchar *path);
gchar *nion_format_bytes(guint64 bytes);

#endif /* NION_UTIL_H */
