/* Copyright (C) 2026 Jeannes Bryan */

/* Tor core runtime (extracted from src/main.c, Phase 3, NiOn 2.0.0).

 * Notification wrappers forward to UI callbacks registered by main.c so this
 * module never calls UI code directly. */

#include "config.h"
#include "tor-core.h"
#include "types.h"
#include "network.h"
#include "util.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

static NionTorCallbacks s_tor_callbacks;

void nion_tor_set_callbacks(const NionTorCallbacks *callbacks)
{
    if (callbacks)
        s_tor_callbacks = *callbacks;
}

static void nion_set_status(NionApp *app, const gchar *message)
{
    if (s_tor_callbacks.set_status)
        s_tor_callbacks.set_status(app, message);
}

static void nion_set_tor_progress(NionApp *app, gint percent)
{
    if (s_tor_callbacks.set_tor_progress)
        s_tor_callbacks.set_tor_progress(app, percent);
}

static void nion_set_tor_error(NionApp *app, const gchar *message)
{
    if (s_tor_callbacks.set_tor_error)
        s_tor_callbacks.set_tor_error(app, message);
}

/* Static forward decls for internal orderings preserved from main.c. */
static void nion_store_tor_log(NionApp *app, const gchar *line);
static void nion_read_tor_line(NionApp *app);

/* ---- Tor runtime (source order preserved) ---- */

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

static gboolean nion_pid_alive(gint64 pid)
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

static void nion_wait_for_pid_exit(gint64 pid, guint timeout_ms)
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

    /* Consume the async result so the wait is fully reaped. */
    gboolean finished = g_subprocess_wait_finish(process, result, &error);

    /* If this is the old Tor we stopped deliberately (normal shutdown or a
     * New Identity rotation), do nothing further: the error path must not run
     * and the new runtime's tor-runtime.ini / app->tor_* state must be left
     * untouched. */
    if (app->shutting_down || app->tor_switching_identity) {
        g_clear_error(&error);
        return;
    }

    if (!finished) {
        gchar *message = g_strdup_printf("Could not monitor Tor: %s",
                                         error ? error->message : "unknown error");
        nion_store_tor_log(app, message);
        nion_set_tor_error(app, message);
        g_free(message);
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

void nion_stop_tor_gracefully(NionApp *app)
{
    if (app->tor_startup_timeout_id) {
        g_source_remove(app->tor_startup_timeout_id);
        app->tor_startup_timeout_id = 0;
    }

    if (!app->tor_process) {
        if (app->tor_runtime_file)
            g_unlink(app->tor_runtime_file);
        return;
    }

    const gchar *identifier = g_subprocess_get_identifier(app->tor_process);
    gint64 pid = 0;
    if (identifier && *identifier) {
        gchar *end = NULL;
        pid = g_ascii_strtoll(identifier, &end, 10);
        if (!end || *end != '\0')
            pid = 0;
    }

    g_printerr("[NiOn] Stopping Tor gracefully…\n");
    g_subprocess_send_signal(app->tor_process, SIGTERM);

    if (pid > 1)
        nion_wait_for_pid_exit(pid, NION_TOR_GRACEFUL_SHUTDOWN_MS);

    if (pid > 1 && nion_pid_alive(pid)) {
        g_printerr("[NiOn] Tor did not stop within %d ms; forcing exit.\n",
                   NION_TOR_GRACEFUL_SHUTDOWN_MS);
        g_subprocess_force_exit(app->tor_process);
        nion_wait_for_pid_exit(pid, 500);
    }

    /* Reap the child if the async waiter has not already done so. */
    g_subprocess_wait(app->tor_process, NULL, NULL);

    if (app->tor_runtime_file)
        g_unlink(app->tor_runtime_file);
}
