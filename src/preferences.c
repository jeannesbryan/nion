/* Copyright (C) 2026 Jeannes Bryan */

/* preferences (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

#include "preferences.h"
#include "types.h"
#include "util.h"

#include <glib.h>
#include <gio/gio.h>

static const gchar *nion_security_level_id(NionSecurityLevel level)
{
    switch (level) {
    case NION_SECURITY_SAFER: return "safer";
    case NION_SECURITY_SAFEST: return "safest";
    case NION_SECURITY_STANDARD:
    default: return "standard";
    }
}

static gboolean nion_security_level_parse(const gchar *value, NionSecurityLevel *out)
{
    if (!value || !out)
        return FALSE;
    if (g_str_equal(value, "standard"))
        *out = NION_SECURITY_STANDARD;
    else if (g_str_equal(value, "safer"))
        *out = NION_SECURITY_SAFER;
    else if (g_str_equal(value, "safest"))
        *out = NION_SECURITY_SAFEST;
    else
        return FALSE;
    return TRUE;
}

static void nion_load_preferences(NionApp *app)
{
    app->restore_session = TRUE;
    app->block_third_party_cookies = FALSE;
    app->security_level = NION_SECURITY_STANDARD;
    g_clear_pointer(&app->search_engine, g_free);
    app->search_engine = g_strdup("duckduckgo");

    if (g_file_test(app->preferences_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->preferences_file,
                                        NION_MAX_PREFERENCES_FILE_BYTES)) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->preferences_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn preferences: %s", error->message);
            nion_quarantine_profile_file(app->preferences_file, "preferences");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    gboolean valid = TRUE;
    if (g_key_file_has_key(key_file, "General", "restore-session", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "General", "restore-session", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->restore_session = value;
        }
    }

    if (g_key_file_has_key(key_file, "Privacy", "block-third-party-cookies", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "Privacy", "block-third-party-cookies", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->block_third_party_cookies = value;
        }
    }

    if (g_key_file_has_key(key_file, "Privacy", "security-level", NULL)) {
        error = NULL;
        gchar *level = g_key_file_get_string(key_file, "Privacy", "security-level", &error);
        NionSecurityLevel parsed = NION_SECURITY_STANDARD;
        if (error || !nion_security_level_parse(level, &parsed)) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->security_level = parsed;
        }
        g_free(level);
    }

    if (g_key_file_has_key(key_file, "Search", "engine", NULL)) {
        error = NULL;
        gchar *engine = g_key_file_get_string(key_file, "Search", "engine", &error);
        if (error || !engine || !(g_str_equal(engine, "duckduckgo") ||
                                  g_str_equal(engine, "brave") ||
                                  g_str_equal(engine, "startpage"))) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(engine);
        } else {
            g_free(app->search_engine);
            app->search_engine = engine;
        }
    }

    g_key_file_free(key_file);

    if (!valid) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        app->restore_session = TRUE;
        app->block_third_party_cookies = FALSE;
        app->security_level = NION_SECURITY_STANDARD;
        g_clear_pointer(&app->search_engine, g_free);
        app->search_engine = g_strdup("duckduckgo");
    }
}

static void nion_save_preferences(NionApp *app)
{
    if (!app || app->is_private || !app->preferences_file)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_boolean(key_file, "General", "restore-session", app->restore_session);
    g_key_file_set_boolean(key_file, "Privacy", "block-third-party-cookies", app->block_third_party_cookies);
    g_key_file_set_string(key_file, "Privacy", "security-level",
                          nion_security_level_id(app->security_level));
    g_key_file_set_string(key_file, "Search", "engine",
                          app->search_engine ? app->search_engine : "duckduckgo");
    nion_write_key_file_atomic(key_file, app->preferences_file);
    g_key_file_free(key_file);
}
