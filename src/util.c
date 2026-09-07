/* Copyright (C) 2026 Jeannes Bryan */

#include "util.h"
#include "types.h"
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <string.h>

/* Generic utilities (extracted from src/main.c, Phase 1, NiOn 2.0.0). */

gboolean nion_profile_file_within_limit(const gchar *path, goffset max_bytes)
{
    if (!path || max_bytes <= 0 || !g_file_test(path, G_FILE_TEST_EXISTS))
        return TRUE;

    GStatBuf st = {0};
    if (g_stat(path, &st) != 0)
        return FALSE;

    return S_ISREG(st.st_mode) && st.st_size >= 0 && st.st_size <= max_bytes;
}

void nion_quarantine_profile_file(const gchar *path, const gchar *label)
{
    if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
        return;

    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = now ? g_date_time_format(now, "%Y%m%d-%H%M%S") : g_strdup("unknown-time");
    gint64 nonce = g_get_real_time();
    gchar *target = g_strdup_printf("%s.corrupt-%s-%" G_GINT64_FORMAT, path, stamp, nonce);

    if (g_rename(path, target) == 0) {
        g_chmod(target, 0600);
        g_warning("Quarantined invalid NiOn %s file as %s",
                  label ? label : "profile", target);
    } else {
        g_warning("Could not quarantine invalid NiOn %s file %s",
                  label ? label : "profile", path);
    }

    g_free(target);
    g_free(stamp);
    if (now)
        g_date_time_unref(now);
}

gboolean nion_base64_state_looks_valid(const gchar *base64)
{
    if (!base64 || !*base64)
        return FALSE;

    gsize len = strlen(base64);
    if (len > NION_MAX_TAB_STATE_BASE64_BYTES || (len % 4) != 0)
        return FALSE;

    gboolean padding_seen = FALSE;
    guint padding = 0;
    for (gsize i = 0; i < len; i++) {
        const guchar c = (guchar)base64[i];
        if (c == '=') {
            padding_seen = TRUE;
            padding++;
            if (padding > 2 || i + 2 < len)
                return FALSE;
            continue;
        }
        if (padding_seen)
            return FALSE;
        if (!(g_ascii_isalnum(c) || c == '+' || c == '/'))
            return FALSE;
    }

    return TRUE;
}

gboolean nion_write_key_file_atomic(GKeyFile *key_file, const gchar *path)
{
    if (!key_file || !path)
        return FALSE;

    gsize length = 0;
    GError *error = NULL;
    gchar *contents = g_key_file_to_data(key_file, &length, &error);
    if (!contents) {
        g_warning("Could not serialize NiOn state: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        return FALSE;
    }

    gchar *tmp = g_strdup_printf("%s.tmp", path);
    gboolean ok = g_file_set_contents(tmp, contents, (gssize)length, &error);
    g_free(contents);

    if (!ok) {
        g_warning("Could not write %s: %s", tmp, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_unlink(tmp);
        g_free(tmp);
        return FALSE;
    }

    g_chmod(tmp, 0600);
    if (g_rename(tmp, path) != 0) {
        g_warning("Could not replace %s", path);
        g_unlink(tmp);
        g_free(tmp);
        return FALSE;
    }

    g_free(tmp);
    return TRUE;
}

gchar *nion_format_bytes(guint64 bytes)
{
    const gchar *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    gdouble value = (gdouble)bytes;
    guint unit = 0;

    while (value >= 1024.0 && unit + 1 < G_N_ELEMENTS(units)) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
        return g_strdup_printf("%" G_GUINT64_FORMAT " %s", bytes, units[unit]);
    return g_strdup_printf("%.1f %s", value, units[unit]);
}
