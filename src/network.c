/* Copyright (C) 2026 Jeannes Bryan */

/* Network: WebKitNetworkSession, proxy config, cookie store validation
 * (extracted from src/main.c, Phase 3, NiOn 2.0.0). */

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
    return TRUE;
}

