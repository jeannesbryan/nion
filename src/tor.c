/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <libsoup/soup.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "types.h"
#include "util.h"
#include "tor.h"
#include "privacy.h"

/* Forward declaration: nion_read_tor_line is defined after on_tor_line_read. */
static void nion_read_tor_line(NionApp *app);

/* Forward declaration: nion_store_tor_log is defined after nion_tor_startup_timeout,
 * but called by the latter before its definition. */
static void nion_store_tor_log(NionApp *app, const gchar *line);

static gchar *nion_find_tor_binary(void)
{
    const gchar *override = g_getenv("NION_TOR_BINARY");
    if (override && g_file_test(override, G_FILE_TEST_IS_EXECUTABLE))
        return g_canonicalize_filename(override, NULL);

    const gchar *appdir = g_getenv("APPDIR");
    if (appdir) {
        const gchar *candidates[] = {
            "usr/lib/nion/tor/tor",
            "usr/lib/nion/tor/bin/tor",
            "usr/bin/tor",
            NULL,
        };
        for (guint i = 0; candidates[i]; i++) {
            gchar *bundled = g_build_filename(appdir, candidates[i], NULL);
            if (g_file_test(bundled, G_FILE_TEST_IS_EXECUTABLE))
                return bundled;
            g_free(bundled);
        }
    }

    /* Development tree: build/nion -> ../runtime/tor/tor. */
    GError *error = NULL;
    gchar *exe = g_file_read_link("/proc/self/exe", &error);
    g_clear_error(&error);
    if (exe) {
        gchar *exe_dir = g_path_get_dirname(exe);
        gchar *project_dir = g_path_get_dirname(exe_dir);
        gchar *bundled = g_build_filename(project_dir, "runtime", "tor", "tor", NULL);
        g_free(project_dir);
        g_free(exe_dir);
        g_free(exe);
        if (g_file_test(bundled, G_FILE_TEST_IS_EXECUTABLE))
            return bundled;
        g_free(bundled);
    }

    /* System Tor is intentionally opt-in from 0.6.0 onward. The normal
     * runtime path is the Tor Expert Bundle prepared by our scripts. */
    if (g_strcmp0(g_getenv("NION_ALLOW_SYSTEM_TOR"), "1") == 0)
        return g_find_program_in_path("tor");

    return NULL;
}

gboolean nion_pid_alive(gint64 pid)
{
    if (pid <= 1)
        return FALSE;
    if (kill((pid_t)pid, 0) == 0)
        return TRUE;
    return errno == EPERM;
}

static gboolean nion_pid_looks_like_our_tor(NionApp *app, gint64 pid)
{
    gchar *proc_path = g_strdup_printf("/proc/%" G_GINT64_FORMAT "/cmdline", pid);
    gchar *contents = NULL;
    gsize length = 0;
    gboolean ok = FALSE;

    if (g_file_get_contents(proc_path, &contents, &length, NULL) && length > 0) {
        for (gsize i = 0; i < length; i++) {
            if (contents[i] == '\0')
                contents[i] = ' ';
        }
        ok = strstr(contents, "tor") != NULL &&
             app->tor_dir && strstr(contents, app->tor_dir) != NULL;
    }

    g_free(contents);
    g_free(proc_path);
    return ok;
}

void nion_wait_for_pid_exit(gint64 pid, guint timeout_ms)
{
    const guint step_ms = 50;
    guint waited = 0;
    while (nion_pid_alive(pid) && waited < timeout_ms) {
        while (g_main_context_pending(NULL))
            g_main_context_iteration(NULL, FALSE);
        g_usleep(step_ms * 1000);
        waited += step_ms;
    }
}

void nion_cleanup_stale_tor(NionApp *app)
{
    if (!app->tor_runtime_file || !g_file_test(app->tor_runtime_file, G_FILE_TEST_EXISTS))
        return;

    GKeyFile *state = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(state, app->tor_runtime_file, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_free(state);
        g_unlink(app->tor_runtime_file);
        return;
    }

    gint64 pid = g_key_file_get_int64(state, "Tor", "pid", NULL);
    if (nion_pid_alive(pid)) {
        if (nion_pid_looks_like_our_tor(app, pid)) {
            g_printerr("[NiOn] Found stale NiOn Tor PID %" G_GINT64_FORMAT "; terminating it.\n", pid);
            kill((pid_t)pid, SIGTERM);
            nion_wait_for_pid_exit(pid, 1500);
            if (nion_pid_alive(pid)) {
                g_printerr("[NiOn] Stale Tor did not stop gracefully; sending SIGKILL.\n");
                kill((pid_t)pid, SIGKILL);
                nion_wait_for_pid_exit(pid, 500);
            }
        } else {
            g_printerr("[NiOn] Runtime state references PID %" G_GINT64_FORMAT
                       " but it is not recognizably NiOn's Tor; leaving it untouched.\n", pid);
        }
    }

    g_key_file_free(state);
    g_unlink(app->tor_runtime_file);

    gchar *lock_file = g_build_filename(app->tor_dir, "lock", NULL);
    g_unlink(lock_file);
    g_free(lock_file);
}

static gboolean nion_tcp_port_available(guint16 port)
{
    GError *error = NULL;
    GSocket *socket = g_socket_new(G_SOCKET_FAMILY_IPV4,
                                   G_SOCKET_TYPE_STREAM,
                                   G_SOCKET_PROTOCOL_TCP,
                                   &error);
    if (!socket) {
        g_clear_error(&error);
        return FALSE;
    }

    GInetAddress *address = g_inet_address_new_from_string(NION_TOR_HOST);
    GSocketAddress *socket_address = g_inet_socket_address_new(address, port);
    gboolean ok = g_socket_bind(socket, socket_address, FALSE, &error);

    g_clear_error(&error);
    g_object_unref(socket_address);
    g_object_unref(address);
    g_object_unref(socket);
    return ok;
}

gboolean nion_choose_tor_port(NionApp *app)
{
    for (guint i = 0; i <= (NION_TOR_PORT_LAST - NION_TOR_PORT_FIRST); i++) {
        guint16 socks_port = (guint16)(NION_TOR_PORT_FIRST + i);
        if (nion_tcp_port_available(socks_port)) {
            app->tor_socks_port = socks_port;
            g_free(app->tor_proxy_uri);
            app->tor_proxy_uri = g_strdup_printf("socks://%s:%u", NION_TOR_HOST, socks_port);
            return TRUE;
        }
    }

    nion_set_tor_error(app,
        "No free local SOCKS port is available in NiOn's runtime range");
    return FALSE;
}

static gboolean nion_write_tor_runtime_state(NionApp *app)
{
    if (!app->tor_process || !app->tor_runtime_file)
        return FALSE;

    const gchar *identifier = g_subprocess_get_identifier(app->tor_process);
    if (!identifier || !*identifier)
        return FALSE;

    gchar *end = NULL;
    gint64 pid = g_ascii_strtoll(identifier, &end, 10);
    if (!end || *end != '\0' || pid <= 1)
        return FALSE;

    GKeyFile *state = g_key_file_new();
    g_key_file_set_int64(state, "Tor", "pid", pid);
    g_key_file_set_integer(state, "Tor", "socks-port", app->tor_socks_port);
    g_key_file_set_string(state, "Tor", "binary", app->tor_binary_path ? app->tor_binary_path : "");
    g_key_file_set_string(state, "Tor", "nion-version", NION_VERSION);
    gboolean ok = nion_write_key_file_atomic(state, app->tor_runtime_file);
    g_key_file_free(state);
    return ok;
}

static gboolean nion_tor_log_suggests_port_conflict(const gchar *line)
{
    if (!line || !*line)
        return FALSE;

    gchar *lower = g_ascii_strdown(line, -1);
    gboolean conflict = strstr(lower, "address already in use") ||
                        strstr(lower, "failed to bind") ||
                        strstr(lower, "could not bind");
    g_free(lower);
    return conflict;
}

static gboolean nion_tor_log_suggests_corruption(const gchar *line)
{
    if (!line || !*line)
        return FALSE;

    gchar *lower = g_ascii_strdown(line, -1);
    gboolean has_damage_word = strstr(lower, "corrupt") ||
                               strstr(lower, "unparseable") ||
                               strstr(lower, "invalid") ||
                               strstr(lower, "parse error") ||
                               strstr(lower, "failed to parse") ||
                               strstr(lower, "failed to read");
    gboolean mentions_state = strstr(lower, "state") ||
                              strstr(lower, "cached-microdesc") ||
                              strstr(lower, "cached-consensus") ||
                              strstr(lower, "cached-descriptor") ||
                              strstr(lower, "unverified-consensus");
    gboolean corruption = has_damage_word && mentions_state;
    g_free(lower);
    return corruption;
}

static gboolean nion_quarantine_file(const gchar *source, const gchar *backup_dir)
{
    if (!g_file_test(source, G_FILE_TEST_EXISTS))
        return TRUE;

    gchar *base = g_path_get_basename(source);
    gchar *target = g_build_filename(backup_dir, base, NULL);
    gboolean ok = g_rename(source, target) == 0;
    if (!ok)
        g_warning("Could not quarantine Tor state file %s", source);
    g_free(target);
    g_free(base);
    return ok;
}

static gboolean nion_recover_tor_state(NionApp *app)
{
    GDateTime *now = g_date_time_new_now_local();
    gchar *stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    gchar *backup_name = g_strdup_printf("recovery-%s", stamp);
    gchar *backup_dir = g_build_filename(app->tor_dir, backup_name, NULL);
    if (g_mkdir_with_parents(backup_dir, 0700) != 0) {
        g_warning("Could not create Tor recovery directory %s: %s", backup_dir, g_strerror(errno));
        g_free(backup_dir);
        g_free(backup_name);
        g_free(stamp);
        g_date_time_unref(now);
        return FALSE;
    }
    g_chmod(backup_dir, 0700);

    const gchar *cache_files[] = {
        "lock",
        "cached-certs",
        "cached-consensus",
        "cached-consensus.new",
        "cached-microdesc-consensus",
        "cached-microdescs",
        "cached-microdescs.new",
        "cached-descriptors",
        "cached-descriptors.new",
        "unverified-consensus",
        NULL,
    };

    for (guint i = 0; cache_files[i]; i++) {
        gchar *path = g_build_filename(app->tor_dir, cache_files[i], NULL);
        nion_quarantine_file(path, backup_dir);
        g_free(path);
    }

    /* Only quarantine Tor's persistent state when the actual error points to
     * that file. This can reset entry-guard state, so do not do it for a
     * generic startup failure. */
    if (app->tor_last_log) {
        gchar *lower = g_ascii_strdown(app->tor_last_log, -1);
        if (strstr(lower, "state") &&
            (strstr(lower, "parse") || strstr(lower, "invalid") || strstr(lower, "corrupt"))) {
            gchar *state_file = g_build_filename(app->tor_dir, "state", NULL);
            nion_quarantine_file(state_file, backup_dir);
            g_free(state_file);
        }
        g_free(lower);
    }

    g_printerr("[NiOn] Tor cache/state recovery backup: %s\n", backup_dir);
    g_free(backup_dir);
    g_free(backup_name);
    g_free(stamp);
    g_date_time_unref(now);
    return TRUE;
}

static gboolean nion_restart_tor_delayed(gpointer user_data)
{
    NionApp *app = user_data;
    if (app->shutting_down)
        return G_SOURCE_REMOVE;

    g_clear_object(&app->tor_output);
    g_clear_object(&app->tor_process);
    g_unlink(app->tor_runtime_file);

    if (!nion_choose_tor_port(app))
        return G_SOURCE_REMOVE;

    nion_apply_network_proxy(app);
    nion_start_tor(app);
    return G_SOURCE_REMOVE;
}

static gboolean nion_tor_startup_timeout(gpointer user_data)
{
    NionApp *app = user_data;
    app->tor_startup_timeout_id = 0;
    if (app->shutting_down || app->tor_ready)
        return G_SOURCE_REMOVE;

    nion_store_tor_log(app, "Tor bootstrap timed out after 120 seconds");
    nion_set_tor_error(app, "Tor bootstrap timed out after 120 seconds");
    if (app->tor_process)
        g_subprocess_force_exit(app->tor_process);
    return G_SOURCE_REMOVE;
}

static void nion_read_tor_line(NionApp *app);

static void nion_store_tor_log(NionApp *app, const gchar *line)
{
    if (!line || !*line)
        return;

    g_free(app->tor_last_log);
    app->tor_last_log = g_strdup(line);

    g_printerr("[NiOn:Tor] %s\n", line);
}

static void on_tor_line_read(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    if (app->shutting_down)
        return;

    GError *error = NULL;
    gsize length = 0;
    GDataInputStream *stream = G_DATA_INPUT_STREAM(source);
    gchar *line = g_data_input_stream_read_line_finish(stream,
                                                       result, &length, &error);
    (void)length;

    /* A previous Tor instance can finish an outstanding async read after a
     * recovery restart. Never let that stale stream alter the new runtime. */
    if (stream != app->tor_output) {
        g_free(line);
        g_clear_error(&error);
        return;
    }

    if (error) {
        gchar *message = g_strdup_printf("Tor log read failed: %s", error->message);
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    if (!line)
        return;

    nion_store_tor_log(app, line);
    if (nion_tor_log_suggests_port_conflict(line))
        app->tor_saw_port_conflict = TRUE;
    if (nion_tor_log_suggests_corruption(line))
        app->tor_saw_corruption = TRUE;

    const gchar *bootstrap = strstr(line, "Bootstrapped ");
    if (bootstrap) {
        gint percent = -1;
        if (sscanf(bootstrap, "Bootstrapped %d%%", &percent) == 1)
            nion_set_tor_progress(app, percent);
    }

    g_free(line);
    nion_read_tor_line(app);
}

static void nion_read_tor_line(NionApp *app)
{
    if (!app->tor_output || app->shutting_down)
        return;

    g_data_input_stream_read_line_async(app->tor_output,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        on_tor_line_read,
                                        app);
}

static void on_tor_process_waited(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    GSubprocess *process = G_SUBPROCESS(source);
    GError *error = NULL;

    if (app->tor_startup_timeout_id) {
        g_source_remove(app->tor_startup_timeout_id);
        app->tor_startup_timeout_id = 0;
    }

    if (!g_subprocess_wait_finish(process, result, &error)) {
        if (!app->shutting_down) {
            gchar *message = g_strdup_printf("Could not monitor Tor: %s",
                                             error ? error->message : "unknown error");
            nion_store_tor_log(app, message);
            nion_set_tor_error(app, message);
            g_free(message);
        }
        g_clear_error(&error);
        return;
    }

    if (app->tor_runtime_file)
        g_unlink(app->tor_runtime_file);

    if (app->shutting_down)
        return;

    /* A port can be taken in the tiny race between our preflight bind test
     * and Tor actually binding. Retry with another port rather than failing
     * the whole browser. */
    if (!app->tor_ready &&
        (app->tor_saw_port_conflict || nion_tor_log_suggests_port_conflict(app->tor_last_log)) &&
        app->tor_port_retry_count < 3) {
        app->tor_port_retry_count++;
        app->tor_failed = FALSE;
        nion_set_status(app, "○ TOR PORT CONFLICT — selecting another SOCKS port…");
        g_timeout_add(200, nion_restart_tor_delayed, app);
        return;
    }

    /* Recover only from logs that actually look like damaged Tor cache/state.
     * The affected files are quarantined, never silently discarded. */
    if (!app->tor_ready && !app->tor_recovery_attempted &&
        (app->tor_saw_corruption || nion_tor_log_suggests_corruption(app->tor_last_log))) {
        app->tor_recovery_attempted = TRUE;
        app->tor_failed = FALSE;
        nion_set_status(app, "○ TOR STATE RECOVERY — quarantining damaged cache and retrying…");
        if (!nion_recover_tor_state(app)) {
            nion_set_tor_error(app, "Tor state recovery could not create its quarantine directory");
            return;
        }
        g_timeout_add(250, nion_restart_tor_delayed, app);
        return;
    }

    gchar *message = NULL;
    if (g_subprocess_get_if_signaled(process)) {
        message = g_strdup_printf("Tor terminated by signal %d",
                                  g_subprocess_get_term_sig(process));
    } else if (g_subprocess_get_if_exited(process)) {
        message = g_strdup_printf("Tor exited with status %d",
                                  g_subprocess_get_exit_status(process));
    } else {
        message = g_strdup("Tor stopped unexpectedly");
    }

    if (app->tor_last_log && *app->tor_last_log) {
        gchar *combined = g_strdup_printf("%s — %s", message, app->tor_last_log);
        g_free(message);
        message = combined;
    }

    nion_set_tor_error(app, message);
    g_free(message);
}

gboolean nion_start_tor(NionApp *app)
{
    if (!app->tor_binary_path)
        app->tor_binary_path = nion_find_tor_binary();

    if (!app->tor_binary_path) {
        nion_store_tor_log(app, "Bundled Tor runtime was not found");
        nion_set_tor_error(app,
            "Bundled Tor runtime not found — run scripts/fetch-tor-runtime.sh");
        return FALSE;
    }

    if (!app->tor_socks_port || !app->tor_proxy_uri) {
        nion_store_tor_log(app, "NiOn could not reserve a local Tor SOCKS port");
        nion_set_tor_error(app,
            "No free Tor SOCKS port was found in the 19050-19069 range");
        return FALSE;
    }

    app->tor_ready = FALSE;
    app->tor_failed = FALSE;
    app->tor_bootstrap_percent = 0;
    app->tor_saw_port_conflict = FALSE;
    app->tor_saw_corruption = FALSE;
    g_clear_pointer(&app->tor_last_log, g_free);

    GError *error = NULL;
    gchar *torrc_path = g_build_filename(app->tor_dir, "torrc", NULL);
    const gchar *torrc =
        "# NiOn private Tor configuration\n"
        "ClientOnly 1\n"
        "SafeSocks 1\n"
        "WarnUnsafeSocks 1\n"
        "ClientRejectInternalAddresses 1\n"
        "ClientDNSRejectInternalAddresses 1\n";

    if (!g_file_set_contents(torrc_path, torrc, -1, &error)) {
        gchar *message = g_strdup_printf("Could not create NiOn torrc: %s",
                                         error ? error->message : "unknown error");
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        g_free(torrc_path);
        return FALSE;
    }
    g_chmod(torrc_path, 0600);

    gchar *socks_endpoint = g_strdup_printf("%s:%u", NION_TOR_HOST, app->tor_socks_port);
    gchar *owner_pid = g_strdup_printf("%ld", (long)getpid());

    app->tor_process = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE,
        &error,
        app->tor_binary_path,
        "-f", torrc_path,
        "--DataDirectory", app->tor_dir,
        "--SocksPort", socks_endpoint,
        "--__OwningControllerProcess", owner_pid,
        "--Log", "notice stdout",
        NULL);

    g_free(owner_pid);
    g_free(socks_endpoint);

    if (!app->tor_process) {
        gchar *message = g_strdup_printf("Tor start failed: %s",
                                         error ? error->message : "unknown error");
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
        g_clear_error(&error);
        g_free(torrc_path);
        return FALSE;
    }

    g_printerr("[NiOn] Starting bundled Tor: %s\n", app->tor_binary_path);
    g_printerr("[NiOn] Tor data: %s\n", app->tor_dir);
    g_printerr("[NiOn] Tor config: %s\n", torrc_path);
    g_printerr("[NiOn] SOCKS: %s:%u\n", NION_TOR_HOST, app->tor_socks_port);
    g_printerr("[NiOn] Tor owner PID: %ld\n", (long)getpid());
    g_free(torrc_path);

    nion_write_tor_runtime_state(app);

    GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(app->tor_process);
    app->tor_output = g_data_input_stream_new(stdout_stream);

    nion_set_tor_progress(app, 0);
    nion_read_tor_line(app);
    g_subprocess_wait_async(app->tor_process, NULL, on_tor_process_waited, app);
    app->tor_startup_timeout_id = g_timeout_add_seconds(
        NION_TOR_STARTUP_TIMEOUT_SECONDS,
        nion_tor_startup_timeout,
        app);
    return TRUE;
}

void nion_prepare_dirs(NionApp *app)
{
    app->data_dir = g_build_filename(g_get_user_data_dir(), "nion", NULL);
    app->cache_dir = g_build_filename(g_get_user_cache_dir(), "nion", NULL);
    app->tor_dir = g_build_filename(app->data_dir, "tor", NULL);
    app->cookie_file = g_build_filename(app->data_dir, "cookies.sqlite", NULL);
    app->config_dir = g_build_filename(g_get_user_config_dir(), "nion", NULL);
    app->preferences_file = g_build_filename(app->config_dir, "preferences.ini", NULL);
    app->session_file = g_build_filename(app->data_dir, "session.ini", NULL);
    app->downloads_file = g_build_filename(app->data_dir, "downloads.ini", NULL);
    app->bookmarks_file = g_build_filename(app->data_dir, "bookmarks.ini", NULL);
    app->site_zoom_file = g_build_filename(app->config_dir, "site-zoom.ini", NULL);
    app->site_javascript_file = g_build_filename(app->config_dir, "site-javascript.ini", NULL);
    app->content_blocking_file = g_build_filename(app->config_dir, "content-blocking.ini", NULL);
    app->autoplay_file = g_build_filename(app->config_dir, "autoplay.ini", NULL);
    app->content_filter_store_dir = g_build_filename(app->cache_dir, "content-filters", NULL);
    app->tor_runtime_file = g_build_filename(app->data_dir, "tor-runtime.ini", NULL);

    const gchar *downloads = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    app->download_dir = (downloads && *downloads)
        ? g_strdup(downloads)
        : g_build_filename(g_get_home_dir(), "Downloads", NULL);

    const gchar *private_dirs[] = {
        app->data_dir,
        app->cache_dir,
        app->config_dir,
        app->tor_dir,
        NULL,
    };
    for (guint i = 0; private_dirs[i]; i++) {
        if (g_mkdir_with_parents(private_dirs[i], 0700) != 0)
            g_warning("Could not create NiOn private directory %s: %s", private_dirs[i], g_strerror(errno));
        else
            g_chmod(private_dirs[i], 0700);
    }

    if (g_mkdir_with_parents(app->download_dir, 0755) != 0)
        g_warning("Could not create download directory %s: %s", app->download_dir, g_strerror(errno));
}

void nion_validate_cookie_store(NionApp *app)
{
    if (!app || !app->cookie_file ||
        !g_file_test(app->cookie_file, G_FILE_TEST_IS_REGULAR))
        return;

    FILE *file = g_fopen(app->cookie_file, "rb");
    if (!file)
        return;

    unsigned char header[16] = {0};
    size_t read_bytes = fread(header, 1, sizeof(header), file);
    fclose(file);

    static const unsigned char sqlite_header[16] = {
        'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'
    };

    if (read_bytes == 0)
        return;
    if (read_bytes == sizeof(header) && memcmp(header, sqlite_header, sizeof(header)) == 0) {
        g_chmod(app->cookie_file, 0600);
        return;
    }

    g_warning("NiOn cookie database does not have a valid SQLite header; quarantining it");
    nion_quarantine_profile_file(app->cookie_file, "cookie database");

    gchar *wal = g_strdup_printf("%s-wal", app->cookie_file);
    gchar *shm = g_strdup_printf("%s-shm", app->cookie_file);
    nion_quarantine_profile_file(wal, "cookie database WAL");
    nion_quarantine_profile_file(shm, "cookie database SHM");
    g_free(wal);
    g_free(shm);
}

void nion_apply_network_proxy(NionApp *app)
{
    if (!app->network_session)
        return;

    const gchar *proxy_uri = app->tor_proxy_uri ? app->tor_proxy_uri : "socks://127.0.0.1:9";
    WebKitNetworkProxySettings *proxy = webkit_network_proxy_settings_new(proxy_uri, NULL);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "http", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "https", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "ws", proxy_uri);
    webkit_network_proxy_settings_add_proxy_for_scheme(proxy, "wss", proxy_uri);
    webkit_network_session_set_proxy_settings(app->network_session,
                                              WEBKIT_NETWORK_PROXY_MODE_CUSTOM,
                                              proxy);
    webkit_network_proxy_settings_free(proxy);
}

gboolean nion_prepare_network(NionApp *app)
{
    if (!app)
        return FALSE;

    if (app->network_session) {
        if (app->is_private && !webkit_network_session_is_ephemeral(app->network_session)) {
            g_critical("Refusing to use a non-ephemeral WebKit network session for Private Window");
            return FALSE;
        }
        nion_apply_network_proxy(app);
        return TRUE;
    }

    app->network_session = app->is_private
        ? webkit_network_session_new_ephemeral()
        : webkit_network_session_new(app->data_dir, app->cache_dir);
    if (!app->network_session)
        return FALSE;

    /* A Private Window is allowed to exist only when WebKit confirms that
     * the backing NetworkSession is ephemeral. This converts the private
     * storage promise into a runtime invariant instead of relying only on
     * the constructor call above. */
    if (app->is_private && !webkit_network_session_is_ephemeral(app->network_session)) {
        g_critical("Private Window WebKit session is not ephemeral; blocking window creation");
        g_clear_object(&app->network_session);
        return FALSE;
    }

    /* Site information relies on strict certificate verification. Make the
     * existing fail-on-TLS-errors behavior explicit instead of relying on a
     * library default. Private windows use the same strict policy. */
    webkit_network_session_set_tls_errors_policy(app->network_session,
                                                  WEBKIT_TLS_ERRORS_POLICY_FAIL);

    WebKitWebsiteDataManager *data_manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    webkit_website_data_manager_set_favicons_enabled(data_manager, TRUE);

    WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(app->network_session);
    if (!app->is_private) {
        webkit_cookie_manager_set_persistent_storage(cookies,
                                                      app->cookie_file,
                                                      WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    }
    nion_apply_cookie_policy(app);

    webkit_network_session_set_persistent_credential_storage_enabled(app->network_session,
                                                                      !app->is_private);
    if (app->is_private &&
        webkit_network_session_get_persistent_credential_storage_enabled(app->network_session)) {
        g_critical("Private Window persistent credential storage could not be disabled");
        g_clear_object(&app->network_session);
        return FALSE;
    }

    nion_apply_network_proxy(app);

    g_signal_connect(app->network_session, "download-started",
                     G_CALLBACK(on_download_started), app);
    return TRUE;
}

guint nion_count_nonblank_tabs(NionApp *app)
{
    if (!app || !app->notebook)
        return 0;

    guint count = 0;
    gint pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || tab->home_page)
            continue;

        const gchar *uri = tab->display_uri_override ? tab->display_uri_override
                                                     : webkit_web_view_get_uri(tab->web_view);
        if (uri && *uri && !g_str_equal(uri, "about:blank"))
            count++;
    }
    return count;
}
