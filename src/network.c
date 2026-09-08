/* Copyright (C) 2026 Jeannes Bryan */

/* Network: WebKitNetworkSession, proxy config, cookie store validation,
 * memory-pressure tuning (extracted from src/main.c, Phase 3, NiOn 2.0.0). */

#include "network.h"
#include "types.h"
#include "privacy.h"
#include "util.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>

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

/* ---- Memory-pressure tuning (v2.1 #4) ---- */

/* WebKit's periodic memory check is disabled unless custom settings are set,
 * and the settings must be installed before any WebKitNetworkSession exists.
 * NiOn tunes for its 4 GB physical-memory target: cap WebKit's working set,
 * start releasing non-critical memory early, and poll frequently so that
 * memory freed by a discarded tab (feature #1) is actually returned to the OS
 * within a couple of seconds instead of after WebKit's default 30 s poll.
 *
 * The memory_limit is capped well below the machine's RAM so the browser as a
 * whole (WebKit + Tor + GTK) stays comfortable inside the 4 GB ceiling. */
static void nion_apply_memory_pressure_settings(void)
{
    static gboolean applied = FALSE;
    if (applied)
        return;
    applied = TRUE;

    WebKitMemoryPressureSettings *mp = webkit_memory_pressure_settings_new();
    /* Cap the WebKit process working set at ~1.5 GB (leaves headroom for Tor
     * + the UI inside the 4 GB physical budget). */
    webkit_memory_pressure_settings_set_memory_limit(mp, 1536);
    /* Release non-critical memory (caches, buffers) from ~33% of the cap. */
    webkit_memory_pressure_settings_set_conservative_threshold(mp, 0.33);
    /* Release critical memory from ~50% of the cap. */
    webkit_memory_pressure_settings_set_strict_threshold(mp, 0.50);
    /* Never let WebKit hard-kill a web process on memory pressure alone: with
     * tabs sharing a WebProcess, a kill would take sibling tabs down. The
     * discard sweep + these thresholds bound memory without that. */
    webkit_memory_pressure_settings_set_kill_threshold(mp, 0.0);
    /* Poll every 2 s so freed memory is handed back quickly after a discard. */
    webkit_memory_pressure_settings_set_poll_interval(mp, 2.0);

    /* Caller-owned: WebKit keeps its own copy; we can free ours. */
    webkit_network_session_set_memory_pressure_settings(mp);
    webkit_memory_pressure_settings_free(mp);

    g_printerr("[NiOn] Memory pressure tuning active: 1.5 GB cap, 2 s poll.\n");
}

gboolean nion_prepare_network(NionApp *app)
{
    if (!app)
        return FALSE;

    /* Must run before the first WebKitNetworkSession is created. */
    nion_apply_memory_pressure_settings();

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
    return TRUE;
}

