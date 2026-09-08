/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_PER_SITE_H
#define NION_PER_SITE_H

#include "types.h"
#include <glib.h>

/* per-site (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

gint nion_zoom_percent(gdouble zoom);
gchar *nion_site_zoom_key_for_uri(const gchar *uri);
void nion_save_site_zoom(NionApp *app);
void nion_load_site_zoom(NionApp *app);
gboolean nion_remember_site_zoom(NionApp *app, const gchar *key, gint percent);
void nion_apply_site_zoom(NionTab *tab, const gchar *uri);
gboolean nion_site_javascript_enabled_for_uri(NionApp *app, const gchar *uri);
gboolean nion_set_site_javascript_enabled(NionApp *app, const gchar *uri, gboolean enabled);
void nion_save_site_javascript(NionApp *app);
void nion_load_site_javascript(NionApp *app);
void nion_apply_site_javascript(NionTab *tab, const gchar *uri);
gboolean nion_content_blocking_enabled_for_uri(NionApp *app, const gchar *uri);
gboolean nion_set_content_blocking_enabled(NionApp *app, const gchar *uri, gboolean enabled);
void nion_save_content_blocking(NionApp *app);
void nion_load_content_blocking(NionApp *app);
gboolean nion_autoplay_allowed_for_uri(NionApp *app, const gchar *uri);
gboolean nion_set_autoplay_allowed_for_uri(NionApp *app, const gchar *uri, gboolean allowed);
void nion_save_autoplay(NionApp *app);
void nion_load_autoplay(NionApp *app);
WebKitWebsitePolicies *nion_website_policies_for_uri(NionApp *app, const gchar *uri);
void nion_policy_decision_use_for_uri(NionTab *tab, WebKitPolicyDecision *decision, const gchar *uri);

/* New Identity clean slate (v2.1): wipe every per-site behavioral rule
 * (zoom, JavaScript, content-blocking, autoplay) for one window. Persistent
 * profiles also drop their on-disk rule files. */
void nion_wipe_all_site_rules(NionApp *app);

#endif /* NION_PER_SITE_H */
