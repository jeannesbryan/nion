/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_NAVIGATION_H
#define NION_NAVIGATION_H

#include <glib.h>

/* Pure URI/host predicates & validation (extracted from src/main.c, Phase 1, NiOn 2.0.0). */

gboolean nion_looks_like_host_port(const gchar *text);
gboolean nion_string_has_scheme(const gchar *text);
gboolean nion_ipv4_bytes_are_private(const guint8 *bytes);
gboolean nion_ipv6_bytes_are_private(const guint8 *bytes);
gboolean nion_host_is_local_or_private(const gchar *host);
gboolean nion_host_is_onion(const gchar *host);
gboolean nion_is_valid_v3_onion_host(const gchar *host);
gboolean nion_uri_is_onion(const gchar *uri);
gboolean nion_uri_is_http_clearnet(const gchar *uri);
gboolean nion_uri_is_https_clearnet(const gchar *uri);
gboolean nion_scheme_is_internal_only(const gchar *scheme);
gchar *nion_https_upgrade_uri(const gchar *uri);
gchar *nion_external_protocol_scheme(const gchar *uri);
gboolean nion_validate_uri(const gchar *uri, gchar **message);

#endif /* NION_NAVIGATION_H */
