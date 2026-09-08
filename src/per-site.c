/* Copyright (C) 2026 Jeannes Bryan */

/* per-site (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

#include "per-site.h"
#include "types.h"
#include "util.h"
#include "navigation.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

static gboolean nion_security_default_javascript_enabled(const NionApp *app)
{
    return !app || app->security_level != NION_SECURITY_SAFEST;
}

gint nion_zoom_percent(gdouble zoom)
{
    gint percent = (gint)(zoom * 100.0 + 0.5);
    return CLAMP(percent, NION_ZOOM_MIN_PERCENT, NION_ZOOM_MAX_PERCENT);
}

gchar *nion_site_zoom_key_for_uri(const gchar *uri)
{
    if (!uri || !*uri)
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean is_http = scheme && g_ascii_strcasecmp(scheme, "http") == 0;
    gboolean is_https = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    if ((!is_http && !is_https) || !host || !*host) {
        g_uri_unref(parsed);
        return NULL;
    }

    gchar *lower_host = g_ascii_strdown(host, -1);
    gint port = g_uri_get_port(parsed);
    gint default_port = is_https ? 443 : 80;
    gchar *key = NULL;

    /* Zoom is remembered by site rather than scheme: http://example.com and
     * https://example.com intentionally share a value. Explicit non-default
     * ports stay separate because they can represent a different web app. */
    if (port < 0 || port == default_port) {
        key = g_strdup(lower_host);
    } else if (strchr(lower_host, ':')) {
        key = g_strdup_printf("[%s]:%d", lower_host, port);
    } else {
        key = g_strdup_printf("%s:%d", lower_host, port);
    }

    g_free(lower_host);
    g_uri_unref(parsed);
    return key;
}

void nion_save_site_zoom(NionApp *app)
{
    if (!app || app->is_private || !app->site_zoom_file || !app->site_zoom)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_SITE_ZOOM_FORMAT);

    GList *keys = g_hash_table_get_keys(app->site_zoom);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    guint index = 0;
    for (GList *node = keys; node && index < NION_MAX_SITE_ZOOM_ENTRIES; node = node->next) {
        const gchar *key = node->data;
        gint percent = GPOINTER_TO_INT(g_hash_table_lookup(app->site_zoom, key));
        if (!key || !*key || percent < NION_ZOOM_MIN_PERCENT ||
            percent > NION_ZOOM_MAX_PERCENT || percent == NION_ZOOM_DEFAULT_PERCENT)
            continue;

        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_integer(key_file, group, "percent", percent);
        g_free(group);
    }
    g_list_free(keys);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);

    nion_write_key_file_atomic(key_file, app->site_zoom_file);
    g_key_file_free(key_file);
}

void nion_load_site_zoom(NionApp *app)
{
    if (!app)
        return;

    if (app->site_zoom)
        g_hash_table_unref(app->site_zoom);
    app->site_zoom = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (!app->site_zoom_file)
        return;
    if (!g_file_test(app->site_zoom_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->site_zoom_file,
                                        NION_MAX_SITE_ZOOM_FILE_BYTES)) {
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->site_zoom_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn site zoom: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
        return;
    }

    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_SITE_ZOOM_FORMAT) {
        valid = FALSE;
        g_clear_error(&error);
    }

    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_SITE_ZOOM_ENTRIES) {
            valid = FALSE;
            g_clear_error(&error);
        }
    }

    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *key = g_key_file_get_string(key_file, group, "key", &error);
        if (error || !key || !*key || strlen(key) > 1024) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        gint percent = g_key_file_get_integer(key_file, group, "percent", &error);
        if (error || percent < NION_ZOOM_MIN_PERCENT ||
            percent > NION_ZOOM_MAX_PERCENT ||
            g_hash_table_contains(app->site_zoom, key)) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        if (percent != NION_ZOOM_DEFAULT_PERCENT)
            g_hash_table_insert(app->site_zoom, key, GINT_TO_POINTER(percent));
        else
            g_free(key);
        g_free(group);
    }

    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->site_zoom);
        nion_quarantine_profile_file(app->site_zoom_file, "site zoom");
    } else {
        g_chmod(app->site_zoom_file, 0600);
    }
}

gboolean nion_remember_site_zoom(NionApp *app, const gchar *key, gint percent)
{
    if (!app || app->is_private || !app->site_zoom || !key || !*key)
        return FALSE;

    percent = CLAMP(percent, NION_ZOOM_MIN_PERCENT, NION_ZOOM_MAX_PERCENT);
    if (percent == NION_ZOOM_DEFAULT_PERCENT)
        return g_hash_table_remove(app->site_zoom, key);

    gpointer current = g_hash_table_lookup(app->site_zoom, key);
    if (current && GPOINTER_TO_INT(current) == percent)
        return FALSE;

    if (!current && g_hash_table_size(app->site_zoom) >= NION_MAX_SITE_ZOOM_ENTRIES) {
        g_warning("NiOn site zoom limit reached; not persisting zoom for %s", key);
        return FALSE;
    }

    g_hash_table_replace(app->site_zoom, g_strdup(key), GINT_TO_POINTER(percent));
    return TRUE;
}

void nion_apply_site_zoom(NionTab *tab, const gchar *uri)
{
    if (!tab || !tab->web_view)
        return;

    gint percent = NION_ZOOM_DEFAULT_PERCENT;
    if (!tab->home_page && !tab->error_page) {
        gchar *key = nion_site_zoom_key_for_uri(uri);
        if (key && tab->app && !tab->app->is_private && tab->app->site_zoom) {
            gpointer stored = g_hash_table_lookup(tab->app->site_zoom, key);
            if (stored)
                percent = GPOINTER_TO_INT(stored);
        }
        g_free(key);
    }

    webkit_web_view_set_zoom_level(tab->web_view, (gdouble)percent / 100.0);
}

gboolean nion_site_javascript_enabled_for_uri(NionApp *app, const gchar *uri)
{
    if (!app || !uri || !*uri)
        return TRUE;

    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!key)
        return nion_security_default_javascript_enabled(app);

    gboolean enabled = nion_security_default_javascript_enabled(app);
    if (app->site_javascript_enabled &&
        g_hash_table_contains(app->site_javascript_enabled, key))
        enabled = TRUE;
    else if (app->site_javascript_disabled &&
             g_hash_table_contains(app->site_javascript_disabled, key))
        enabled = FALSE;
    g_free(key);
    return enabled;
}

gboolean nion_set_site_javascript_enabled(NionApp *app,
                                                   const gchar *uri,
                                                   gboolean enabled)
{
    if (!app || !app->site_javascript_disabled ||
        !app->site_javascript_enabled || !uri || !*uri)
        return FALSE;

    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!key)
        return FALSE;

    gboolean old_enabled = nion_site_javascript_enabled_for_uri(app, uri);
    gboolean default_enabled = nion_security_default_javascript_enabled(app);

    g_hash_table_remove(app->site_javascript_disabled, key);
    g_hash_table_remove(app->site_javascript_enabled, key);

    if (enabled != default_enabled) {
        guint total = g_hash_table_size(app->site_javascript_disabled) +
                      g_hash_table_size(app->site_javascript_enabled);
        if (total >= NION_MAX_SITE_JAVASCRIPT_ENTRIES) {
            if (old_enabled != default_enabled) {
                GHashTable *restore = old_enabled
                    ? app->site_javascript_enabled : app->site_javascript_disabled;
                g_hash_table_add(restore, g_strdup(key));
            }
            g_free(key);
            return FALSE;
        }
        GHashTable *target = enabled
            ? app->site_javascript_enabled : app->site_javascript_disabled;
        g_hash_table_add(target, g_strdup(key));
    }

    g_free(key);
    return old_enabled != enabled;
}

void nion_save_site_javascript(NionApp *app)
{
    if (!app || app->is_private || !app->site_javascript_file ||
        !app->site_javascript_disabled || !app->site_javascript_enabled)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_SITE_JAVASCRIPT_FORMAT);

    GList *disabled = g_hash_table_get_keys(app->site_javascript_disabled);
    GList *enabled = g_hash_table_get_keys(app->site_javascript_enabled);
    disabled = g_list_sort(disabled, (GCompareFunc)g_strcmp0);
    enabled = g_list_sort(enabled, (GCompareFunc)g_strcmp0);

    guint index = 0;
    for (GList *node = disabled; node && index < NION_MAX_SITE_JAVASCRIPT_ENTRIES;
         node = node->next) {
        const gchar *key = node->data;
        if (!key || !*key || strlen(key) > 1024)
            continue;
        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_boolean(key_file, group, "javascript-enabled", FALSE);
        g_free(group);
    }
    for (GList *node = enabled; node && index < NION_MAX_SITE_JAVASCRIPT_ENTRIES;
         node = node->next) {
        const gchar *key = node->data;
        if (!key || !*key || strlen(key) > 1024)
            continue;
        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_boolean(key_file, group, "javascript-enabled", TRUE);
        g_free(group);
    }
    g_list_free(disabled);
    g_list_free(enabled);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);

    nion_write_key_file_atomic(key_file, app->site_javascript_file);
    g_key_file_free(key_file);
}

void nion_load_site_javascript(NionApp *app)
{
    if (!app)
        return;

    if (app->site_javascript_disabled)
        g_hash_table_unref(app->site_javascript_disabled);
    app->site_javascript_disabled = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (app->site_javascript_enabled)
        g_hash_table_unref(app->site_javascript_enabled);
    app->site_javascript_enabled = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (app->temporary_permissions)
        g_hash_table_unref(app->temporary_permissions);
    app->temporary_permissions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    if (app->is_private || !app->site_javascript_file)
        return;
    if (!g_file_test(app->site_javascript_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->site_javascript_file,
                                        NION_MAX_SITE_JAVASCRIPT_FILE_BYTES)) {
        nion_quarantine_profile_file(app->site_javascript_file, "site JavaScript");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->site_javascript_file,
                                   G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn site JavaScript rules: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->site_javascript_file, "site JavaScript");
        return;
    }

    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || (format != 1 && format != NION_SITE_JAVASCRIPT_FORMAT)) {
        valid = FALSE;
        g_clear_error(&error);
    }

    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_SITE_JAVASCRIPT_ENTRIES) {
            valid = FALSE;
            g_clear_error(&error);
        }
    }

    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *key = g_key_file_get_string(key_file, group, "key", &error);
        if (error || !key || !*key || strlen(key) > 1024 ||
            g_hash_table_contains(app->site_javascript_disabled, key) ||
            g_hash_table_contains(app->site_javascript_enabled, key)) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        gboolean enabled = g_key_file_get_boolean(key_file, group,
                                                   "javascript-enabled", &error);
        if (error || (format == 1 && enabled)) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }

        GHashTable *target = enabled
            ? app->site_javascript_enabled : app->site_javascript_disabled;
        g_hash_table_add(target, key);
        g_free(group);
    }

    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->site_javascript_disabled);
        g_hash_table_remove_all(app->site_javascript_enabled);
        nion_quarantine_profile_file(app->site_javascript_file, "site JavaScript");
    } else {
        g_chmod(app->site_javascript_file, 0600);
    }
}

void nion_apply_site_javascript(NionTab *tab, const gchar *uri)
{
    if (!tab || !tab->web_view)
        return;

    gboolean enabled = nion_security_default_javascript_enabled(tab->app);
    if (!tab->home_page && !tab->error_page && uri && *uri)
        enabled = nion_site_javascript_enabled_for_uri(tab->app, uri);

    WebKitSettings *settings = webkit_web_view_get_settings(tab->web_view);
    if (settings && webkit_settings_get_enable_javascript(settings) != enabled)
        webkit_settings_set_enable_javascript(settings, enabled);
}

gboolean nion_content_blocking_enabled_for_uri(NionApp *app, const gchar *uri)
{
    if (!app || !app->content_blocking_disabled || !uri || !*uri)
        return TRUE;

    gchar *key = nion_site_zoom_key_for_uri(uri);
    gboolean enabled = !key || !g_hash_table_contains(app->content_blocking_disabled, key);
    g_free(key);
    return enabled;
}

gboolean nion_set_content_blocking_enabled(NionApp *app,
                                                   const gchar *uri,
                                                   gboolean enabled)
{
    if (!app || !app->content_blocking_disabled || !uri || !*uri)
        return FALSE;

    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!key)
        return FALSE;

    gboolean changed = FALSE;
    if (enabled) {
        changed = g_hash_table_remove(app->content_blocking_disabled, key);
    } else if (!g_hash_table_contains(app->content_blocking_disabled, key)) {
        if (g_hash_table_size(app->content_blocking_disabled) >= NION_MAX_CONTENT_BLOCKING_EXCEPTIONS) {
            g_warning("NiOn content-blocking exception limit reached; not storing exception for %s", key);
        } else {
            g_hash_table_add(app->content_blocking_disabled, g_strdup(key));
            changed = TRUE;
        }
    }

    g_free(key);
    return changed;
}

void nion_save_content_blocking(NionApp *app)
{
    if (!app || app->is_private || !app->content_blocking_file ||
        !app->content_blocking_disabled)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_CONTENT_BLOCKING_FORMAT);

    GList *keys = g_hash_table_get_keys(app->content_blocking_disabled);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    guint index = 0;
    for (GList *node = keys; node && index < NION_MAX_CONTENT_BLOCKING_EXCEPTIONS;
         node = node->next) {
        const gchar *key = node->data;
        if (!key || !*key || strlen(key) > 1024)
            continue;

        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_boolean(key_file, group, "content-blocking-enabled", FALSE);
        g_free(group);
    }
    g_list_free(keys);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);
    nion_write_key_file_atomic(key_file, app->content_blocking_file);
    g_key_file_free(key_file);
}

void nion_load_content_blocking(NionApp *app)
{
    if (!app)
        return;

    if (app->content_blocking_disabled)
        g_hash_table_unref(app->content_blocking_disabled);
    app->content_blocking_disabled = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* Private Windows get a clean memory-only exception set. They never read
     * the normal content-blocking exception profile. */
    if (app->is_private || !app->content_blocking_file)
        return;
    if (!g_file_test(app->content_blocking_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->content_blocking_file,
                                        NION_MAX_CONTENT_BLOCKING_FILE_BYTES)) {
        nion_quarantine_profile_file(app->content_blocking_file, "content blocking");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->content_blocking_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn content-blocking exceptions: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->content_blocking_file, "content blocking");
        return;
    }

    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_CONTENT_BLOCKING_FORMAT) {
        valid = FALSE;
        g_clear_error(&error);
    }
    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_CONTENT_BLOCKING_EXCEPTIONS) {
            valid = FALSE;
            g_clear_error(&error);
        }
    }

    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *key = g_key_file_get_string(key_file, group, "key", &error);
        gboolean enabled = g_key_file_get_boolean(key_file, group,
                                                   "content-blocking-enabled", &error);
        if (error || !key || !*key || strlen(key) > 1024 || enabled ||
            g_hash_table_contains(app->content_blocking_disabled, key)) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(key);
            g_free(group);
            break;
        }
        g_hash_table_add(app->content_blocking_disabled, key);
        g_free(group);
    }

    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->content_blocking_disabled);
        nion_quarantine_profile_file(app->content_blocking_file, "content blocking");
    } else {
        g_chmod(app->content_blocking_file, 0600);
    }
}

gboolean nion_autoplay_allowed_for_uri(NionApp *app, const gchar *uri)
{
    if (!app || !app->autoplay_allowed_sites || !uri || !*uri)
        return FALSE;
    gchar *key = nion_site_zoom_key_for_uri(uri);
    gboolean allowed = key && g_hash_table_contains(app->autoplay_allowed_sites, key);
    g_free(key);
    return allowed;
}

gboolean nion_set_autoplay_allowed_for_uri(NionApp *app, const gchar *uri, gboolean allowed)
{
    if (!app || !app->autoplay_allowed_sites || !uri || !*uri)
        return FALSE;
    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!key)
        return FALSE;
    gboolean changed = FALSE;
    if (!allowed)
        changed = g_hash_table_remove(app->autoplay_allowed_sites, key);
    else if (!g_hash_table_contains(app->autoplay_allowed_sites, key)) {
        if (g_hash_table_size(app->autoplay_allowed_sites) >= NION_MAX_AUTOPLAY_EXCEPTIONS)
            g_warning("NiOn autoplay exception limit reached; not storing exception for %s", key);
        else {
            g_hash_table_add(app->autoplay_allowed_sites, g_strdup(key));
            changed = TRUE;
        }
    }
    g_free(key);
    return changed;
}

void nion_save_autoplay(NionApp *app)
{
    if (!app || app->is_private || !app->autoplay_file || !app->autoplay_allowed_sites)
        return;
    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_AUTOPLAY_FORMAT);
    GList *keys = g_hash_table_get_keys(app->autoplay_allowed_sites);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    guint index = 0;
    for (GList *node = keys; node && index < NION_MAX_AUTOPLAY_EXCEPTIONS; node = node->next) {
        const gchar *key = node->data;
        if (!key || !*key || strlen(key) > 1024)
            continue;
        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", key);
        g_key_file_set_boolean(key_file, group, "autoplay-with-sound", TRUE);
        g_free(group);
    }
    g_list_free(keys);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);
    nion_write_key_file_atomic(key_file, app->autoplay_file);
    g_key_file_free(key_file);
}

void nion_load_autoplay(NionApp *app)
{
    if (!app)
        return;
    if (app->autoplay_allowed_sites)
        g_hash_table_unref(app->autoplay_allowed_sites);
    app->autoplay_allowed_sites = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    if (app->is_private || !app->autoplay_file)
        return;
    if (!g_file_test(app->autoplay_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->autoplay_file, NION_MAX_AUTOPLAY_FILE_BYTES)) {
        nion_quarantine_profile_file(app->autoplay_file, "autoplay exceptions");
        return;
    }
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->autoplay_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn autoplay exceptions: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->autoplay_file, "autoplay exceptions");
        return;
    }
    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_AUTOPLAY_FORMAT) { valid = FALSE; g_clear_error(&error); }
    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_AUTOPLAY_EXCEPTIONS) { valid = FALSE; g_clear_error(&error); }
    }
    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *key = g_key_file_get_string(key_file, group, "key", &error);
        gboolean allowed = g_key_file_get_boolean(key_file, group, "autoplay-with-sound", &error);
        if (error || !key || !*key || strlen(key) > 1024 || !allowed || g_hash_table_contains(app->autoplay_allowed_sites, key)) {
            valid = FALSE; g_clear_error(&error); g_free(key); g_free(group); break;
        }
        g_hash_table_add(app->autoplay_allowed_sites, key);
        g_free(group);
    }
    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->autoplay_allowed_sites);
        nion_quarantine_profile_file(app->autoplay_file, "autoplay exceptions");
    } else
        g_chmod(app->autoplay_file, 0600);
}

/* ---- Preferred Onion-Location per site (v2.1 #5) ---- */

gchar *nion_site_key_for_uri(const gchar *uri)
{
    /* Reuse the zoom key (clearnet http/https host, lowercase). */
    return nion_site_zoom_key_for_uri(uri);
}

void nion_save_preferred_onion(NionApp *app)
{
    if (!app || app->is_private || !app->preferred_onion_file || !app->preferred_onion)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_PREFERRED_ONION_FORMAT);

    GList *keys = g_hash_table_get_keys(app->preferred_onion);
    keys = g_list_sort(keys, (GCompareFunc)g_strcmp0);
    guint index = 0;
    for (GList *node = keys; node && index < NION_MAX_PREFERRED_ONION_ENTRIES;
         node = node->next) {
        const gchar *site_key = node->data;
        const gchar *onion = site_key ? g_hash_table_lookup(app->preferred_onion, site_key) : NULL;
        if (!site_key || !*site_key || !onion || !*onion ||
            strlen(site_key) > 1024 || strlen(onion) > 256)
            continue;

        gchar *group = g_strdup_printf("Site-%u", index++);
        g_key_file_set_string(key_file, group, "key", site_key);
        g_key_file_set_string(key_file, group, "onion", onion);
        g_free(group);
    }
    g_list_free(keys);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)index);
    nion_write_key_file_atomic(key_file, app->preferred_onion_file);
    g_key_file_free(key_file);
}

void nion_load_preferred_onion(NionApp *app)
{
    if (!app)
        return;
    if (app->preferred_onion)
        g_hash_table_unref(app->preferred_onion);
    app->preferred_onion = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (app->is_private || !app->preferred_onion_file)
        return;
    if (!g_file_test(app->preferred_onion_file, G_FILE_TEST_EXISTS))
        return;
    if (!nion_profile_file_within_limit(app->preferred_onion_file,
                                        NION_MAX_PREFERRED_ONION_FILE_BYTES)) {
        nion_quarantine_profile_file(app->preferred_onion_file, "preferred onions");
        return;
    }
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->preferred_onion_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn preferred onions: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->preferred_onion_file, "preferred onions");
        return;
    }
    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_PREFERRED_ONION_FORMAT) { valid = FALSE; g_clear_error(&error); }
    gint count = 0;
    if (valid) {
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_PREFERRED_ONION_ENTRIES) { valid = FALSE; g_clear_error(&error); }
    }
    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Site-%d", i);
        gchar *site_key = g_key_file_get_string(key_file, group, "key", &error);
        gchar *onion = g_key_file_get_string(key_file, group, "onion", &error);
        if (error || !site_key || !*site_key || !onion || !*onion ||
            strlen(site_key) > 1024 || strlen(onion) > 256 ||
            !nion_uri_is_onion(onion) ||
            g_hash_table_contains(app->preferred_onion, site_key)) {
            valid = FALSE; g_clear_error(&error); g_free(site_key); g_free(onion); g_free(group); break;
        }
        g_hash_table_insert(app->preferred_onion, site_key, onion);
        g_free(group);
    }
    g_key_file_free(key_file);
    if (!valid) {
        g_hash_table_remove_all(app->preferred_onion);
        nion_quarantine_profile_file(app->preferred_onion_file, "preferred onions");
    } else
        g_chmod(app->preferred_onion_file, 0600);
}

/* Remember (and persist) the preferred .onion twin for a clearnet site key.
 * Returns TRUE when the mapping changed. Never used for Private Windows. */
gboolean nion_remember_preferred_onion(NionApp *app, const gchar *site_key,
                                       const gchar *onion_uri)
{
    if (!app || app->is_private || !site_key || !*site_key ||
        !onion_uri || !*onion_uri || !nion_uri_is_onion(onion_uri))
        return FALSE;
    if (!app->preferred_onion)
        nion_load_preferred_onion(app);
    if (!app->preferred_onion)
        return FALSE;

    const gchar *existing = g_hash_table_lookup(app->preferred_onion, site_key);
    if (existing && g_strcmp0(existing, onion_uri) == 0)
        return FALSE;

    if (!existing && g_hash_table_size(app->preferred_onion) >= NION_MAX_PREFERRED_ONION_ENTRIES) {
        g_warning("NiOn preferred-onion limit reached; not persisting %s", site_key);
        return FALSE;
    }
    g_hash_table_replace(app->preferred_onion, g_strdup(site_key), g_strdup(onion_uri));
    nion_save_preferred_onion(app);
    return TRUE;
}

/* Look up a remembered preferred .onion for a clearnet site key. */
gchar *nion_preferred_onion_for_site_key(NionApp *app, const gchar *site_key)
{
    if (!app || !site_key || !*site_key || !app->preferred_onion)
        return NULL;
    const gchar *onion = g_hash_table_lookup(app->preferred_onion, site_key);
    return onion ? g_strdup(onion) : NULL;
}

WebKitWebsitePolicies *nion_website_policies_for_uri(NionApp *app, const gchar *uri)
{
    WebKitAutoplayPolicy policy;
    if (nion_autoplay_allowed_for_uri(app, uri))
        policy = WEBKIT_AUTOPLAY_ALLOW;
    else if (app && app->security_level != NION_SECURITY_STANDARD)
        policy = WEBKIT_AUTOPLAY_DENY;
    else
        policy = WEBKIT_AUTOPLAY_ALLOW_WITHOUT_SOUND;
    return webkit_website_policies_new_with_policies("autoplay", policy, NULL);
}

void nion_policy_decision_use_for_uri(NionTab *tab, WebKitPolicyDecision *decision, const gchar *uri)
{
    if (!tab || !tab->app || !decision) {
        if (decision) webkit_policy_decision_use(decision);
        return;
    }
    WebKitWebsitePolicies *policies = nion_website_policies_for_uri(tab->app, uri);
    webkit_policy_decision_use_with_policies(decision, policies);
    g_object_unref(policies);
}

void nion_wipe_all_site_rules(NionApp *app)
{
    /* New Identity clean slate (v2.1): clear every per-site behavioral rule
     * for this window so no zoom level, JavaScript toggle, content-blocking
     * exception or autoplay allowance survives into the new identity.
     * Persistent profiles drop their on-disk rule files too; Private Windows
     * (memory-only rules) simply empty their tables. */
    if (!app)
        return;

    if (app->site_zoom) {
        g_hash_table_remove_all(app->site_zoom);
        if (!app->is_private && app->site_zoom_file)
            g_unlink(app->site_zoom_file);
    }

    if (app->site_javascript_disabled) {
        g_hash_table_remove_all(app->site_javascript_disabled);
        if (!app->is_private && app->site_javascript_file)
            g_unlink(app->site_javascript_file);
    }
    if (app->site_javascript_enabled)
        g_hash_table_remove_all(app->site_javascript_enabled);

    if (app->content_blocking_disabled) {
        g_hash_table_remove_all(app->content_blocking_disabled);
        if (!app->is_private && app->content_blocking_file)
            g_unlink(app->content_blocking_file);
    }

    if (app->autoplay_allowed_sites) {
        g_hash_table_remove_all(app->autoplay_allowed_sites);
        if (!app->is_private && app->autoplay_file)
            g_unlink(app->autoplay_file);
    }

    if (app->preferred_onion) {
        g_hash_table_remove_all(app->preferred_onion);
        if (!app->is_private && app->preferred_onion_file)
            g_unlink(app->preferred_onion_file);
    }
}
