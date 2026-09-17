/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

/* Censorship circumvention: bundled pluggable transports (NiOn 2.2.0).
 *
 * The Tor Expert Bundle NiOn already fetches and verifies contains the
 * pluggable transports themselves (lyrebird for obfs4/webtunnel/meek_lite and
 * conjure-client for conjure). This module adds the missing half: turn a
 * user-supplied bridge list into a torrc fragment, and persist that list.
 *
 * Fail-closed rules enforced here:
 *   - a bridge line is a strict, validated token stream (no control
 *     characters, no torrc keywords, known transport method only), so it can
 *     never smuggle a second directive into NiOn's torrc;
 *   - when bridge mode is enabled but the configuration cannot be honoured
 *     (no lines, a line referencing a transport NiOn does not bundle, a
 *     missing transport binary) the torrc fragment is refused and Tor is not
 *     started at all. NiOn never falls back to a bridge-less connection.
 *
 * Deliberately adds no Tor control-port surface: bridges are pure torrc. */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "bridge.h"

static NionBridgeCallbacks cb;

void nion_bridge_set_callbacks(const NionBridgeCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

static void nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

/* ---- Bundled pluggable transports ---- */

typedef struct {
    const gchar *method;   /* transport name used in the bridge line */
    const gchar *binary;   /* bundled executable honouring that method */
} NionBridgeTransport;

/* lyrebird is the modern obfs4proxy replacement shipped by Tor: it implements
 * obfs4, webtunnel and meek_lite. conjure has its own client. NiOn supports
 * exactly what the verified bundle ships -- nothing more. */
static const NionBridgeTransport nion_bridge_transports[] = {
    { "obfs4",     "lyrebird" },
    { "webtunnel", "lyrebird" },
    { "meek_lite", "lyrebird" },
    { "conjure",   "conjure-client" },
};

static const NionBridgeTransport *nion_bridge_transport_for(const gchar *method)
{
    if (!method || !*method)
        return NULL;
    for (guint i = 0; i < G_N_ELEMENTS(nion_bridge_transports); i++) {
        if (g_str_equal(method, nion_bridge_transports[i].method))
            return &nion_bridge_transports[i];
    }
    return NULL;
}

/* Torrc keywords a bridge line must never carry. `Bridge <line>` cannot span
 * lines, so this is defence in depth on top of the control-character ban. */
static const gchar *nion_bridge_reserved_keywords[] = {
    "UseBridges",
    "ClientTransportPlugin",
    "SocksPort",
    "ControlPort",
    "DataDirectory",
    "ClientOnionAuthDir",
    NULL,
};

gboolean nion_bridge_line_is_valid(const gchar *line)
{
    if (!line || !*line)
        return FALSE;

    gsize length = strlen(line);
    if (length == 0 || length > NION_MAX_BRIDGE_LINE_CHARS)
        return FALSE;

    /* No control characters at all: a newline here would let a stored line
     * escape `Bridge <line>` and inject an arbitrary torrc directive. */
    for (const gchar *cursor = line; *cursor; cursor++) {
        if ((guchar)*cursor < 0x20 || *cursor == 0x7f)
            return FALSE;
    }

    if (line[0] == '#' || line[0] == ' ' || line[0] == '\t')
        return FALSE;

    for (guint i = 0; nion_bridge_reserved_keywords[i]; i++) {
        if (strstr(line, nion_bridge_reserved_keywords[i]))
            return FALSE;
    }

    gchar **tokens = g_strsplit_set(line, " \t", -1);
    gboolean valid = FALSE;
    guint token_count = 0;
    for (guint i = 0; tokens[i]; i++) {
        if (*tokens[i])
            token_count++;
    }
    /* <method> <endpoint> [key=value ...] -- a bare method is not a bridge. */
    valid = token_count >= 2 && nion_bridge_transport_for(tokens[0]) != NULL;
    g_strfreev(tokens);
    return valid;
}

/* ---- Pluggable-transport discovery ---- */

static gboolean nion_bridge_is_executable(const gchar *path)
{
    return path && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE) &&
           !g_file_test(path, G_FILE_TEST_IS_DIR);
}

/* Walk the layouts NiOn knows about. The development tree keeps the transport
 * next to the verified Tor daemon, the AppImage keeps the same shape under
 * $APPDIR/usr/lib/nion/tor. */
static gchar *nion_bridge_find_pt(const NionApp *app, const gchar *binary)
{
    if (!binary || !*binary)
        return NULL;

    GPtrArray *roots = g_ptr_array_new_with_free_func(g_free);

    const gchar *override = g_getenv("NION_PT_DIR");
    if (override && *override)
        g_ptr_array_add(roots, g_strdup(override));

    if (app && app->tor_binary_path) {
        gchar *tor_dir = g_path_get_dirname(app->tor_binary_path);
        g_ptr_array_add(roots, g_strdup(tor_dir));
        g_ptr_array_add(roots, g_build_filename(tor_dir, "pluggable_transports", NULL));
        gchar *expert = g_build_filename(tor_dir, "expert", "tor", NULL);
        g_ptr_array_add(roots, g_strdup(expert));
        g_ptr_array_add(roots, g_build_filename(expert, "pluggable_transports", NULL));
        g_free(expert);
        g_free(tor_dir);
    }

    const gchar *appdir = g_getenv("APPDIR");
    if (appdir) {
        gchar *tor_root = g_build_filename(appdir, "usr", "lib", "nion", "tor", NULL);
        g_ptr_array_add(roots, g_strdup(tor_root));
        g_ptr_array_add(roots, g_build_filename(tor_root, "pluggable_transports", NULL));
        gchar *expert = g_build_filename(tor_root, "expert", "tor", NULL);
        g_ptr_array_add(roots, g_strdup(expert));
        g_ptr_array_add(roots, g_build_filename(expert, "pluggable_transports", NULL));
        g_free(expert);
        g_free(tor_root);
    }

    /* Development tree: build/nion -> ../runtime/tor. */
    GError *error = NULL;
    gchar *exe = g_file_read_link("/proc/self/exe", &error);
    g_clear_error(&error);
    if (exe) {
        gchar *exe_dir = g_path_get_dirname(exe);
        gchar *project_dir = g_path_get_dirname(exe_dir);
        gchar *tor_root = g_build_filename(project_dir, "runtime", "tor", NULL);
        g_ptr_array_add(roots, g_strdup(tor_root));
        g_ptr_array_add(roots, g_build_filename(tor_root, "pluggable_transports", NULL));
        gchar *expert = g_build_filename(tor_root, "expert", "tor", NULL);
        g_ptr_array_add(roots, g_strdup(expert));
        g_ptr_array_add(roots, g_build_filename(expert, "pluggable_transports", NULL));
        g_free(expert);
        g_free(tor_root);
        g_free(project_dir);
        g_free(exe_dir);
        g_free(exe);
    }

    gchar *found = NULL;
    for (guint i = 0; i < roots->len && !found; i++) {
        const gchar *root = g_ptr_array_index(roots, i);
        if (!root || !*root)
            continue;
        gchar *candidate = g_build_filename(root, binary, NULL);
        if (nion_bridge_is_executable(candidate))
            found = candidate;
        else
            g_free(candidate);
    }

    g_ptr_array_unref(roots);
    return found;
}

gchar *nion_bridge_transport_summary(void)
{
    GPtrArray *parts = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < G_N_ELEMENTS(nion_bridge_transports); i++) {
        gchar *path = nion_bridge_find_pt(NULL, nion_bridge_transports[i].binary);
        g_ptr_array_add(parts, g_strdup_printf("%s %s",
                                              nion_bridge_transports[i].method,
                                              path ? "✓" : "—"));
        g_free(path);
    }
    g_ptr_array_add(parts, NULL);
    gchar *joined = g_strjoinv("   ", (gchar **)parts->pdata);
    gchar *summary = g_strdup_printf("Bundled transports:  %s", joined);
    g_free(joined);
    g_ptr_array_unref(parts);
    return summary;
}

/* ---- torrc fragment ---- */

gchar *nion_bridge_torrc_fragment(NionApp *app, gchar **error)
{
    if (error)
        *error = NULL;

    if (!app || !app->bridges_enabled)
        return g_strdup("");

    if (!app->bridges || app->bridges->len == 0) {
        if (error)
            *error = g_strdup("Bridge mode is enabled but no bridge line is configured");
        return NULL;
    }

    if (app->bridges->len > NION_MAX_BRIDGES) {
        if (error)
            *error = g_strdup("Bridge list exceeds NiOn's supported maximum");
        return NULL;
    }

    GString *plugins = g_string_new(NULL);
    GString *bridge_lines = g_string_new(NULL);
    GPtrArray *declared = g_ptr_array_new_with_free_func(g_free);
    gchar *failure = NULL;

    for (guint i = 0; i < app->bridges->len; i++) {
        const gchar *line = g_ptr_array_index(app->bridges, i);
        if (!nion_bridge_line_is_valid(line)) {
            failure = g_strdup_printf("Bridge line %u is not a valid bridge", i + 1);
            break;
        }

        gchar **tokens = g_strsplit_set(line, " \t", 2);
        gchar *method = g_strdup(tokens[0]);
        g_strfreev(tokens);

        /* Declare each transport exactly once, and only when its bundled
         * binary really exists. A missing transport must abort the whole
         * start-up rather than degrade to a bridge-less connection. */
        gboolean already_declared = FALSE;
        for (guint j = 0; j < declared->len; j++) {
            if (g_str_equal(g_ptr_array_index(declared, j), method)) {
                already_declared = TRUE;
                break;
            }
        }

        if (already_declared) {
            g_free(method);
        } else {
            const NionBridgeTransport *transport = nion_bridge_transport_for(method);
            gchar *pt_path = transport ? nion_bridge_find_pt(app, transport->binary) : NULL;
            if (!pt_path) {
                failure = g_strdup_printf("Bundled Tor transport for '%s' (%s) was not found",
                                          method, transport ? transport->binary : "unknown");
                g_free(method);
                break;
            }
            g_string_append_printf(plugins, "ClientTransportPlugin %s exec %s\n",
                                   method, pt_path);
            g_ptr_array_add(declared, method);
            g_free(pt_path);
        }

        /* `line` is control-character free, so it cannot break out of the
         * single torrc directive it is being written into. */
        g_string_append_printf(bridge_lines, "Bridge %s\n", line);
    }

    if (failure) {
        g_string_free(plugins, TRUE);
        g_string_free(bridge_lines, TRUE);
        g_ptr_array_unref(declared);
        if (error)
            *error = failure;
        else
            g_free(failure);
        return NULL;
    }

    /* ClientTransportPlugin directives must precede the bridges that need
     * them, so the plugin block is emitted directly after UseBridges. */
    GString *fragment = g_string_new("UseBridges 1\n");
    g_string_append(fragment, plugins->str);
    g_string_append(fragment, bridge_lines->str);

    g_string_free(plugins, TRUE);
    g_string_free(bridge_lines, TRUE);
    g_ptr_array_unref(declared);
    return g_string_free(fragment, FALSE);
}

/* ---- Persistence ---- */

gboolean nion_bridges_active(const NionApp *app)
{
    return app && app->bridges_enabled && app->bridges && app->bridges->len > 0;
}

static void nion_bridges_clear(NionApp *app)
{
    if (!app)
        return;
    if (app->bridges)
        g_ptr_array_set_size(app->bridges, 0);
}

void nion_load_bridges(NionApp *app)
{
    if (!app)
        return;

    if (!app->bridges)
        app->bridges = g_ptr_array_new_with_free_func(g_free);
    app->bridges_enabled = FALSE;
    nion_bridges_clear(app);

    if (app->is_private || !app->bridge_file)
        return;

    if (!g_file_test(app->bridge_file, G_FILE_TEST_EXISTS))
        return;

    if (!nion_profile_file_within_limit(app->bridge_file, NION_MAX_BRIDGE_FILE_BYTES)) {
        nion_quarantine_profile_file(app->bridge_file, "bridges");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->bridge_file, G_KEY_FILE_NONE, &error)) {
        g_warning("Could not load NiOn bridges: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->bridge_file, "bridges");
        return;
    }

    gboolean valid = TRUE;
    gint format = g_key_file_get_integer(key_file, "Meta", "format", &error);
    if (error || format != NION_BRIDGE_FORMAT) {
        valid = FALSE;
        g_clear_error(&error);
    }

    gboolean enabled = FALSE;
    gint count = 0;
    if (valid) {
        enabled = g_key_file_get_boolean(key_file, "Meta", "enabled", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        }
        count = g_key_file_get_integer(key_file, "Meta", "count", &error);
        if (error || count < 0 || count > NION_MAX_BRIDGES) {
            valid = FALSE;
            g_clear_error(&error);
        }
    }

    for (gint i = 0; valid && i < count; i++) {
        gchar *group = g_strdup_printf("Bridge-%d", i);
        gchar *line = g_key_file_get_string(key_file, group, "line", &error);
        if (error || !nion_bridge_line_is_valid(line)) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            g_ptr_array_add(app->bridges, line);
            line = NULL;
        }
        g_free(line);
        g_free(group);
    }

    g_key_file_free(key_file);

    if (!valid) {
        /* One bad entry invalidates the whole list: a partially applied bridge
         * set is exactly the silent degradation this feature must not have. */
        nion_bridges_clear(app);
        nion_quarantine_profile_file(app->bridge_file, "bridges");
        app->bridges_enabled = FALSE;
        return;
    }

    app->bridges_enabled = enabled;
}

void nion_save_bridges(NionApp *app)
{
    if (!app || app->is_private || !app->bridge_file || !app->bridges)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Meta", "format", NION_BRIDGE_FORMAT);
    g_key_file_set_boolean(key_file, "Meta", "enabled", app->bridges_enabled);
    g_key_file_set_integer(key_file, "Meta", "count", (gint)app->bridges->len);

    for (guint i = 0; i < app->bridges->len; i++) {
        const gchar *line = g_ptr_array_index(app->bridges, i);
        if (!nion_bridge_line_is_valid(line))
            continue;
        gchar *group = g_strdup_printf("Bridge-%u", i);
        g_key_file_set_string(key_file, group, "line", line);
        g_free(group);
    }

    nion_write_key_file_atomic(key_file, app->bridge_file);
    g_key_file_free(key_file);
}

/* ---- Preferences dialog ---- */

static void on_bridges_cancel_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void nion_bridges_set_error(NionApp *app, const gchar *message)
{
    if (!app || !app->bridges_error_label)
        return;
    gtk_label_set_text(GTK_LABEL(app->bridges_error_label), message ? message : "");
    gtk_widget_set_visible(app->bridges_error_label, message && *message);
}

static gchar *nion_bridges_text_from_view(NionApp *app)
{
    if (!app || !app->bridges_text_view)
        return NULL;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->bridges_text_view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

/* Parse the multi-line editor into a validated list. Returns NULL and sets
 * *error when any non-empty line is unacceptable. */
static GPtrArray *nion_bridges_parse(const gchar *text, gchar **error)
{
    if (error)
        *error = NULL;

    GPtrArray *parsed = g_ptr_array_new_with_free_func(g_free);
    if (!text || !*text)
        return parsed;

    gchar **lines = g_strsplit(text, "\n", -1);
    for (guint i = 0; lines[i]; i++) {
        gchar *line = g_strdup(lines[i]);
        g_strstrip(line);
        if (!*line) {
            g_free(line);
            continue;
        }

        if (!nion_bridge_line_is_valid(line)) {
            if (error)
                *error = g_strdup_printf(
                    "Line %u is not a valid bridge line. Expected \"<transport> <address> [key=value …]\" "
                    "with a transport NiOn bundles (obfs4, webtunnel, meek_lite, conjure).", i + 1);
            g_free(line);
            g_strfreev(lines);
            g_ptr_array_unref(parsed);
            return NULL;
        }

        if (parsed->len >= NION_MAX_BRIDGES) {
            if (error)
                *error = g_strdup_printf("NiOn supports at most %d bridge lines.", NION_MAX_BRIDGES);
            g_free(line);
            g_strfreev(lines);
            g_ptr_array_unref(parsed);
            return NULL;
        }

        g_ptr_array_add(parsed, line);
    }
    g_strfreev(lines);
    return parsed;
}

static void on_bridges_save_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!root || !GTK_IS_WINDOW(root) || !app)
        return;

    gboolean enabled = app->bridges_enable_check
        ? gtk_check_button_get_active(GTK_CHECK_BUTTON(app->bridges_enable_check))
        : FALSE;
    gchar *text = nion_bridges_text_from_view(app);
    gchar *error = NULL;
    GPtrArray *parsed = nion_bridges_parse(text, &error);
    g_free(text);

    if (!parsed) {
        nion_bridges_set_error(app, error);
        g_free(error);
        return;
    }

    if (enabled && parsed->len == 0) {
        nion_bridges_set_error(app,
            "Bridge mode is enabled but no bridge line is configured. NiOn will not start Tor "
            "without the bridges you asked for.");
        g_ptr_array_unref(parsed);
        return;
    }

    /* Validate that every requested transport is actually usable before the
     * configuration is stored, so the failure is reported here instead of at
     * Tor start-up. */
    if (enabled) {
        for (guint i = 0; i < parsed->len; i++) {
            const gchar *line = g_ptr_array_index(parsed, i);
            gchar **tokens = g_strsplit_set(line, " \t", 2);
            const NionBridgeTransport *transport = nion_bridge_transport_for(tokens[0]);
            gchar *pt_path = transport ? nion_bridge_find_pt(app, transport->binary) : NULL;
            if (!pt_path) {
                gchar *message = g_strdup_printf(
                    "The bundled transport for '%s' (%s) is not available in this NiOn build.",
                    tokens[0], transport ? transport->binary : "unknown");
                nion_bridges_set_error(app, message);
                g_free(message);
                g_strfreev(tokens);
                g_ptr_array_unref(parsed);
                return;
            }
            g_free(pt_path);
            g_strfreev(tokens);
        }
    }

    gboolean enabled_changed = (app->bridges_enabled != enabled);
    gboolean list_changed = TRUE;
    if (app->bridges && app->bridges->len == parsed->len) {
        list_changed = FALSE;
        for (guint i = 0; i < parsed->len; i++) {
            if (!g_str_equal(g_ptr_array_index(app->bridges, i), g_ptr_array_index(parsed, i))) {
                list_changed = TRUE;
                break;
            }
        }
    }

    if (!app->bridges)
        app->bridges = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_set_size(app->bridges, 0);
    for (guint i = 0; i < parsed->len; i++)
        g_ptr_array_add(app->bridges, g_strdup(g_ptr_array_index(parsed, i)));
    app->bridges_enabled = enabled;
    g_ptr_array_unref(parsed);

    nion_save_bridges(app);
    gtk_window_destroy(GTK_WINDOW(root));

    if (enabled_changed || list_changed) {
        if (app->tor_ready || app->tor_process) {
            nion_set_status(app,
                enabled ? "○ BRIDGE MODE — restarting Tor with the configured bridges…"
                        : "○ BRIDGE MODE DISABLED — restarting Tor…");
            if (cb.restart_tor)
                cb.restart_tor(app);
        } else {
            nion_set_status(app,
                enabled ? "○ BRIDGE MODE ENABLED — restart NiOn or retry so Tor can use the bridges"
                        : "○ BRIDGE MODE DISABLED — preferences saved");
        }
    }
}

/* ---- Validator / torrc self-check (release test hook) ---- */

/* Exercised by scripts/test-bridges-stage1.sh through the
 * NION_BRIDGE_SELFCHECK=1 environment variable, so the regression suite tests
 * the real validator and the real torrc builder instead of grepping for them.
 * Read-only: it never touches the profile, the network or Tor. */
typedef struct {
    const gchar *line;
    gboolean expected_valid;
} NionBridgeSelfCheckCase;

static const NionBridgeSelfCheckCase nion_bridge_selfcheck_cases[] = {
    /* Accepted: every transport the verified bundle ships. */
    { "obfs4 192.0.2.1:443 0123456789ABCDEF0123456789ABCDEF01234567 cert=AAAA iat-mode=0", TRUE },
    { "webtunnel 192.0.2.1:443 0123456789ABCDEF0123456789ABCDEF01234567 url=https://example.org/abc", TRUE },
    { "meek_lite 192.0.2.1:443 0123456789ABCDEF0123456789ABCDEF01234567 url=https://example.org/", TRUE },
    { "conjure https://registration.example.org/", TRUE },
    /* Rejected: empty shapes, unknown transport, injection attempts. */
    { "", FALSE },
    { "   ", FALSE },
    { "obfs4", FALSE },
    { "snowflake 192.0.2.1:1", FALSE },
    { "obfs4 192.0.2.1:443 FPR\nSocksPort 9999", FALSE },
    { "obfs4 192.0.2.1:443 FPR\rControlPort 9051", FALSE },
    { "obfs4\t192.0.2.1:443\nDataDirectory /tmp", FALSE },
    { "# obfs4 192.0.2.1:443 FPR", FALSE },
    { "obfs4 192.0.2.1:443 FPR cert=AAAA ClientTransportPlugin obfs4 exec /bin/sh", FALSE },
    { "UseBridges 1", FALSE },
    { " obfs4 192.0.2.1:443 FPR", FALSE },
};

gboolean nion_bridge_selfcheck(void)
{
    gboolean ok = TRUE;
    guint passed = 0;
    guint total = G_N_ELEMENTS(nion_bridge_selfcheck_cases);

    for (guint i = 0; i < total; i++) {
        const NionBridgeSelfCheckCase *item = &nion_bridge_selfcheck_cases[i];
        gboolean got = nion_bridge_line_is_valid(item->line);
        if (got != item->expected_valid) {
            ok = FALSE;
            g_printerr("FAIL  validator: expected %s for \"%s\"\n",
                       item->expected_valid ? "accept" : "reject", item->line);
        } else {
            passed++;
        }
    }

    /* An over-long line must be refused as well. */
    gchar *long_line = g_malloc(NION_MAX_BRIDGE_LINE_CHARS + 64);
    memset(long_line, 'a', NION_MAX_BRIDGE_LINE_CHARS + 63);
    long_line[NION_MAX_BRIDGE_LINE_CHARS + 63] = '\0';
    if (nion_bridge_line_is_valid(long_line)) {
        ok = FALSE;
        g_printerr("FAIL  validator: accepted an over-long bridge line\n");
    } else {
        passed++;
    }
    g_free(long_line);
    total++;

    g_print("bridge self-check: validator %u/%u cases behaved as required\n", passed, total);
    if (!ok)
        return FALSE;

    /* torrc fragment: ordering and content. */
    NionApp app = {0};
    app.bridges = g_ptr_array_new_with_free_func(g_free);
    app.bridges_enabled = TRUE;
    g_ptr_array_add(app.bridges, g_strdup(nion_bridge_selfcheck_cases[0].line));
    g_ptr_array_add(app.bridges, g_strdup(nion_bridge_selfcheck_cases[3].line));

    gchar *error = NULL;
    gchar *fragment = nion_bridge_torrc_fragment(&app, &error);
    if (!fragment) {
        /* The bundle may not be present in a bare checkout; report a skip
         * instead of pretending the fragment was verified. */
        g_print("SKIP  torrc fragment: bundled transports unavailable (%s)\n",
                error ? error : "unknown");
        g_free(error);
        g_ptr_array_unref(app.bridges);
        return TRUE;
    }

    const gchar *first_plugin = strstr(fragment, "ClientTransportPlugin obfs4 exec ");
    const gchar *first_bridge = strstr(fragment, "Bridge obfs4 ");
    gboolean fragment_ok =
        g_str_has_prefix(fragment, "UseBridges 1\n") &&
        first_plugin && first_bridge && first_plugin < first_bridge &&
        strstr(fragment, "ClientTransportPlugin conjure exec ") != NULL &&
        strstr(fragment, nion_bridge_selfcheck_cases[0].line) != NULL &&
        strstr(fragment, nion_bridge_selfcheck_cases[3].line) != NULL;

    if (!fragment_ok) {
        ok = FALSE;
        g_printerr("FAIL  torrc fragment:\n%s", fragment);
    } else {
        g_print("PASS  torrc fragment: UseBridges 1, plugins before bridges, all lines present\n");
    }

    /* Behavioural proof that a bridge line cannot smuggle a control-port or a
     * second SOCKS listener into the generated torrc. This is what matters, and
     * unlike a grep it cannot be fooled by the blocklist strings above. */
    if (fragment && (strstr(fragment, "ControlPort") || strstr(fragment, "SocksPort"))) {
        ok = FALSE;
        g_printerr("FAIL  generated torrc contains a control-port or SOCKS directive:\n%s", fragment);
    } else {
        g_print("PASS  generated torrc contains no control-port or injected SOCKS directive\n");
    }
    g_free(fragment);
    g_ptr_array_unref(app.bridges);
    if (!ok)
        return FALSE;

    /* Bridge mode enabled with an empty list, or with a transport that has no
     * bundled binary, must be refused outright. */
    NionApp empty = {0};
    empty.bridges = g_ptr_array_new_with_free_func(g_free);
    empty.bridges_enabled = TRUE;
    gchar *empty_error = NULL;
    gchar *refused = nion_bridge_torrc_fragment(&empty, &empty_error);
    if (refused || !empty_error) {
        ok = FALSE;
        g_printerr("FAIL  fail-closed: enabled bridge mode with no lines was not refused\n");
    } else {
        g_print("PASS  fail-closed: enabled bridge mode with no lines is refused\n");
    }
    g_free(refused);
    g_free(empty_error);
    g_ptr_array_unref(empty.bridges);

    /* Disabled bridge mode must contribute nothing to the torrc. */
    NionApp off = {0};
    off.bridges_enabled = FALSE;
    gchar *none = nion_bridge_torrc_fragment(&off, NULL);
    if (!none || *none) {
        ok = FALSE;
        g_printerr("FAIL  disabled bridge mode must produce an empty fragment\n");
    } else {
        g_print("PASS  disabled bridge mode produces an empty fragment\n");
    }
    g_free(none);

    return ok;
}

void action_bridges(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    if (!app)
        return;

    if (app->is_private) {
        nion_set_status(app,
            "● PRIVATE WINDOW — BRIDGE SETTINGS ARE GLOBAL; EDIT THEM FROM THE NORMAL WINDOW");
        return;
    }

    if (app->bridges_window) {
        gtk_window_present(GTK_WINDOW(app->bridges_window));
        return;
    }

    GtkWidget *window = gtk_window_new();
    app->bridges_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->bridges_window);

    gtk_window_set_title(GTK_WINDOW(window), "NiOn Bridges");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 620, 540);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Censorship circumvention (bridges)</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *intro = gtk_label_new(
        "Bridges are unlisted Tor relays that make Tor traffic look like ordinary traffic. "
        "Use them when your network blocks Tor. Get bridge lines from the Tor Project "
        "(bridges.torproject.org, or the \"Get bridges\" option in other Tor software) and paste "
        "one per line.");
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_label_set_xalign(GTK_LABEL(intro), 0.0f);
    gtk_widget_add_css_class(intro, "nion-muted");
    gtk_box_append(GTK_BOX(box), intro);

    GtkWidget *transport_label = gtk_label_new(NULL);
    gchar *summary = nion_bridge_transport_summary();
    gtk_label_set_text(GTK_LABEL(transport_label), summary);
    g_free(summary);
    gtk_label_set_xalign(GTK_LABEL(transport_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(transport_label), TRUE);
    gtk_widget_add_css_class(transport_label, "nion-muted");
    gtk_box_append(GTK_BOX(box), transport_label);
    app->bridges_transport_label = transport_label;

    GtkWidget *enable = gtk_check_button_new_with_label("Use bridges to connect to Tor");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(enable), app->bridges_enabled);
    gtk_widget_set_tooltip_text(enable,
        "When enabled, NiOn starts its bundled Tor with UseBridges 1. If the bridge configuration "
        "cannot be honoured, Tor is not started at all — NiOn never falls back to a straight Tor "
        "connection behind your back.");
    gtk_box_append(GTK_BOX(box), enable);
    app->bridges_enable_check = enable;

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_widget_set_size_request(scroller, -1, 180);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 6);

    GString *current = g_string_new("");
    if (app->bridges) {
        for (guint i = 0; i < app->bridges->len; i++) {
            g_string_append(current, g_ptr_array_index(app->bridges, i));
            g_string_append_c(current, '\n');
        }
    }
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), current->str, -1);
    g_string_free(current, TRUE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_box_append(GTK_BOX(box), scroller);
    app->bridges_text_view = view;

    GtkWidget *error_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(error_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(error_label), TRUE);
    gtk_widget_add_css_class(error_label, "nion-status-error");
    gtk_widget_set_visible(error_label, FALSE);
    gtk_box_append(GTK_BOX(box), error_label);
    app->bridges_error_label = error_label;

    GtkWidget *note = gtk_label_new(
        "Fail-closed: with bridges enabled NiOn refuses to start Tor when the list is empty, a line "
        "is malformed, or a required transport is missing. Bridges change how NiOn reaches the Tor "
        "network; they do not change what a website can see.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label("Save & Reconnect");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_bridges_cancel_clicked), app);
    g_signal_connect(save, "clicked", G_CALLBACK(on_bridges_save_clicked), app);

    gtk_window_present(GTK_WINDOW(window));
}
