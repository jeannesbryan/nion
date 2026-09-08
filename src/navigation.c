/* Copyright (C) 2026 Jeannes Bryan */

#include "navigation.h"
#include "types.h"
#include <gio/gio.h>
#include <string.h>

/* Pure URI/host predicates & validation (extracted from src/main.c, Phase 1, NiOn 2.0.0). */

gboolean nion_looks_like_host_port(const gchar *text)
{
    if (!text || !*text || strpbrk(text, " \t\r\n"))
        return FALSE;

    const gchar *colon = strchr(text, ':');
    if (!colon || colon == text)
        return FALSE;

    /* Keep common host:port input such as localhost:8080 and
     * example.com:8443 from being mistaken for a URI scheme. */
    gchar *host = g_strndup(text, (gsize)(colon - text));
    gboolean hostish = g_ascii_strcasecmp(host, "localhost") == 0 || strchr(host, '.') != NULL;
    g_free(host);
    if (!hostish)
        return FALSE;

    const gchar *p = colon + 1;
    if (!g_ascii_isdigit(*p))
        return FALSE;
    while (g_ascii_isdigit(*p))
        p++;

    return *p == '\0' || *p == '/' || *p == '?' || *p == '#';
}

gboolean nion_string_has_scheme(const gchar *text)
{
    if (!text || !g_ascii_isalpha(text[0]) || nion_looks_like_host_port(text))
        return FALSE;

    for (const gchar *p = text + 1; *p; p++) {
        if (*p == ':')
            return TRUE;
        if (!(g_ascii_isalnum(*p) || *p == '+' || *p == '-' || *p == '.'))
            return FALSE;
    }

    return FALSE;
}

gboolean nion_ipv4_bytes_are_private(const guint8 *bytes)
{
    if (!bytes)
        return FALSE;

    /* Unspecified/current-network, RFC1918, loopback, carrier-grade NAT,
     * link-local, benchmarking, multicast and reserved space. NiOn is a
     * Tor-only web browser, not a local-network browser. */
    if (bytes[0] == 0 ||
        bytes[0] == 10 ||
        bytes[0] == 127 ||
        (bytes[0] == 100 && bytes[1] >= 64 && bytes[1] <= 127) ||
        (bytes[0] == 169 && bytes[1] == 254) ||
        (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
        (bytes[0] == 192 && bytes[1] == 168) ||
        (bytes[0] == 198 && (bytes[1] == 18 || bytes[1] == 19)) ||
        bytes[0] >= 224)
        return TRUE;

    return FALSE;
}

gboolean nion_ipv6_bytes_are_private(const guint8 *bytes)
{
    if (!bytes)
        return FALSE;

    gboolean all_zero = TRUE;
    for (guint i = 0; i < 16; i++) {
        if (bytes[i] != 0) {
            all_zero = FALSE;
            break;
        }
    }
    if (all_zero)
        return TRUE;

    /* ::1 */
    gboolean loopback = TRUE;
    for (guint i = 0; i < 15; i++) {
        if (bytes[i] != 0) {
            loopback = FALSE;
            break;
        }
    }
    if (loopback && bytes[15] == 1)
        return TRUE;

    /* fc00::/7 (ULA), fe80::/10 (link-local), ff00::/8 (multicast). */
    if ((bytes[0] & 0xfe) == 0xfc ||
        (bytes[0] == 0xfe && (bytes[1] & 0xc0) == 0x80) ||
        bytes[0] == 0xff)
        return TRUE;

    /* IPv4-mapped IPv6 ::ffff:a.b.c.d */
    gboolean mapped = TRUE;
    for (guint i = 0; i < 10; i++) {
        if (bytes[i] != 0) {
            mapped = FALSE;
            break;
        }
    }
    if (mapped && bytes[10] == 0xff && bytes[11] == 0xff)
        return nion_ipv4_bytes_are_private(bytes + 12);

    return FALSE;
}

gboolean nion_host_is_local_or_private(const gchar *host)
{
    if (!host || !*host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    gboolean local_name =
        g_str_equal(lower, "localhost") ||
        g_str_has_suffix(lower, ".localhost") ||
        g_str_has_suffix(lower, ".local") ||
        g_str_has_suffix(lower, ".lan") ||
        g_str_equal(lower, "home.arpa") ||
        g_str_has_suffix(lower, ".home.arpa");

    if (local_name) {
        g_free(lower);
        return TRUE;
    }

    GInetAddress *address = g_inet_address_new_from_string(lower);
    g_free(lower);
    if (!address)
        return FALSE;

    const guint8 *bytes = g_inet_address_to_bytes(address);
    gboolean blocked = g_inet_address_get_family(address) == G_SOCKET_FAMILY_IPV4
        ? nion_ipv4_bytes_are_private(bytes)
        : nion_ipv6_bytes_are_private(bytes);

    g_object_unref(address);
    return blocked;
}

gboolean nion_host_is_onion(const gchar *host)
{
    if (!host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    gboolean result = g_str_has_suffix(lower, ".onion");
    g_free(lower);
    return result;
}

gboolean nion_is_valid_v3_onion_host(const gchar *host)
{
    if (!host)
        return FALSE;

    gchar *lower = g_ascii_strdown(host, -1);
    if (!g_str_has_suffix(lower, ".onion")) {
        g_free(lower);
        return FALSE;
    }

    gsize host_len = strlen(lower);
    if (host_len <= 6) {
        g_free(lower);
        return FALSE;
    }

    gchar *suffix = lower + host_len - 6; /* points to .onion */
    gchar *label_start = suffix;
    while (label_start > lower && *(label_start - 1) != '.')
        label_start--;

    gsize label_len = (gsize)(suffix - label_start);
    if (label_len != 56) {
        g_free(lower);
        return FALSE;
    }

    for (gsize i = 0; i < label_len; i++) {
        gchar c = label_start[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) {
            g_free(lower);
            return FALSE;
        }
    }

    g_free(lower);
    return TRUE;
}

gboolean nion_uri_is_onion(const gchar *uri)
{
    if (!uri)
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    gboolean result = nion_host_is_onion(g_uri_get_host(parsed));
    g_uri_unref(parsed);
    return result;
}

gboolean nion_uri_is_http_clearnet(const gchar *uri)
{
    if (!uri || !*uri || nion_uri_is_onion(uri))
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean ok = scheme && host && *host && g_ascii_strcasecmp(scheme, "http") == 0;
    g_uri_unref(parsed);
    return ok;
}

gboolean nion_uri_is_https_clearnet(const gchar *uri)
{
    if (!uri || !*uri || nion_uri_is_onion(uri))
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    gboolean ok = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    g_uri_unref(parsed);
    return ok;
}

/* HTTPS-only upgrade (v2.1 #5): return the https:// twin of a clearnet
 * http:// URI (scheme swapped, default port normalised), or NULL when the
 * URI is not a plain-clearnet-http URL that can be upgraded. .onion services
 * (plain http by design, but end-to-end inside Tor) are never upgraded. */
gchar *nion_https_upgrade_uri(const gchar *uri)
{
    if (!uri || !*uri || !nion_uri_is_http_clearnet(uri))
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *host = g_uri_get_host(parsed);
    const gchar *path = g_uri_get_path(parsed);
    const gchar *query = g_uri_get_query(parsed);
    const gchar *fragment = g_uri_get_fragment(parsed);
    gint port = g_uri_get_port(parsed);

    GString *upgraded = g_string_new("https://");
    if (host && *host)
        g_string_append(upgraded, host);
    if (port >= 0 && port != 80 && port != 443)
        g_string_append_printf(upgraded, ":%d", port);
    g_string_append(upgraded, (path && *path) ? path : "/");
    if (query && *query) {
        g_string_append_c(upgraded, '?');
        g_string_append(upgraded, query);
    }
    if (fragment && *fragment) {
        g_string_append_c(upgraded, '#');
        g_string_append(upgraded, fragment);
    }

    g_uri_unref(parsed);
    return g_string_free(upgraded, FALSE);
}

gboolean nion_scheme_is_internal_only(const gchar *scheme)
{
    if (!scheme || !*scheme)
        return FALSE;

    const gchar *blocked[] = {
        "file", "javascript", "data", "blob", "about", "nion"
    };
    for (guint i = 0; i < G_N_ELEMENTS(blocked); i++) {
        if (g_ascii_strcasecmp(scheme, blocked[i]) == 0)
            return TRUE;
    }
    return FALSE;
}

gchar *nion_external_protocol_scheme(const gchar *uri)
{
    if (!uri || !*uri || strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    gchar *result = NULL;
    if (scheme && *scheme &&
        g_ascii_strcasecmp(scheme, "http") != 0 &&
        g_ascii_strcasecmp(scheme, "https") != 0 &&
        !nion_scheme_is_internal_only(scheme))
        result = g_ascii_strdown(scheme, -1);

    g_uri_unref(parsed);
    return result;
}

gboolean nion_validate_uri(const gchar *uri, gchar **message)
{
    if (message)
        *message = NULL;

    if (!uri || !*uri) {
        if (message)
            *message = g_strdup("The address is empty.");
        return FALSE;
    }

    if (g_str_equal(uri, "about:blank"))
        return TRUE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        if (message)
            *message = g_strdup_printf("Invalid address: %s",
                                       error ? error->message : "could not parse URI");
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);

    if (!scheme || !(g_ascii_strcasecmp(scheme, "http") == 0 ||
                     g_ascii_strcasecmp(scheme, "https") == 0)) {
        if (message)
            *message = g_strdup("NiOn only opens http:// and https:// web addresses.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (!host || !*host) {
        if (message)
            *message = g_strdup("The web address does not contain a valid hostname.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (nion_host_is_local_or_private(host)) {
        if (message)
            *message = g_strdup(
                "Local, private, link-local, multicast, and reserved network addresses are blocked "
                "by NiOn's Tor-only privacy policy.");
        g_uri_unref(parsed);
        return FALSE;
    }

    if (nion_host_is_onion(host) && !nion_is_valid_v3_onion_host(host)) {
        if (message)
            *message = g_strdup(
                "Invalid .onion address. NiOn accepts Tor v3 onion addresses "
                "with a 56-character base32 service label before .onion.");
        g_uri_unref(parsed);
        return FALSE;
    }

    g_uri_unref(parsed);
    return TRUE;
}
