/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "navigation.h"
#include "per-site.h"
#include "content-filter.h"
#include "permission.h"
#include "settings.h"
#include "site-data.h"

/* UI/tab operations that still live in main.c (Phase 6, NiOn 2.0.0).
 * main.c registers implementations via nion_site_data_set_callbacks()
 * so this module never calls into main.c directly. */
static NionSiteDataCallbacks cb;

static NionTab *
nion_current_tab(NionApp *app)
{
    return cb.current_tab ? cb.current_tab(app) : NULL;
}

static void
nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

static void
nion_apply_content_filter_to_window(NionApp *app)
{
    if (cb.apply_content_filter_to_window)
        cb.apply_content_filter_to_window(app);
}

void
nion_site_data_set_callbacks(const NionSiteDataCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

gchar *nion_web_origin_key_for_uri(const gchar *uri)
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
    gchar *lower_scheme = g_ascii_strdown(scheme, -1);
    gint port = g_uri_get_port(parsed);
    gboolean default_port = port < 0 || (is_http && port == 80) || (is_https && port == 443);
    gchar *origin = NULL;
    if (default_port) {
        origin = strchr(lower_host, ':')
            ? g_strdup_printf("%s://[%s]", lower_scheme, lower_host)
            : g_strdup_printf("%s://%s", lower_scheme, lower_host);
    } else {
        origin = strchr(lower_host, ':')
            ? g_strdup_printf("%s://[%s]:%d", lower_scheme, lower_host, port)
            : g_strdup_printf("%s://%s:%d", lower_scheme, lower_host, port);
    }

    g_free(lower_scheme);
    g_free(lower_host);
    g_uri_unref(parsed);
    return origin;
}

gboolean nion_tab_has_mixed_content(const NionTab *tab)
{
    return tab && (tab->mixed_content_displayed || tab->mixed_content_run ||
                   tab->mixed_content_other);
}

GtkWidget *nion_site_info_row(const gchar *heading, GtkWidget **value_out)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(heading);
    GtkWidget *value = gtk_label_new("");

    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(value), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(value), TRUE);
    /* Site information often contains unbroken URLs, .onion hostnames, and
     * security/status tokens. PANGO_WRAP_WORD can let those strings dictate
     * an enormous minimum/natural width. Allow character fallback wrapping
     * and explicitly bound the label request so the transient window stays
     * inside small laptop screens. */
    gtk_label_set_wrap_mode(GTK_LABEL(value), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(value), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(value), 48);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_widget_add_css_class(title, "nion-site-info-key");
    gtk_widget_add_css_class(value, "nion-site-info-value");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), value);
    if (value_out)
        *value_out = value;
    return box;
}

static const gchar *nion_permission_status_text(NionApp *app,
                                                 const gchar *origin,
                                                 guint permission)
{
    if (app && app->security_level == NION_SECURITY_SAFEST &&
        (permission == NION_PERMISSION_CAMERA || permission == NION_PERMISSION_MICROPHONE))
        return "Disabled by Safest level";
    return nion_permission_is_temporarily_allowed(app, origin, permission)
        ? "Allowed temporarily"
        : "Blocked by default";
}

void on_site_content_blocking_switch_notify(GObject *object,
                                                    GParamSpec *pspec,
                                                    gpointer user_data)
{
    (void)pspec;
    NionApp *app = user_data;
    if (!app || app->updating_site_controls)
        return;

    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!tab || !key || nion_content_filter_has_failed(app)) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), TRUE);
        app->updating_site_controls = FALSE;
        g_free(key);
        return;
    }
    g_free(key);

    gboolean enabled = gtk_switch_get_active(GTK_SWITCH(object));
    gboolean changed = nion_set_content_blocking_enabled(app, uri, enabled);
    if (!changed && nion_content_blocking_enabled_for_uri(app, uri) != enabled) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), !enabled);
        app->updating_site_controls = FALSE;
        nion_set_status(app, "● TOR CONNECTED — CONTENT-BLOCKING EXCEPTION LIMIT REACHED");
        return;
    }

    if (!app->is_private)
        nion_save_content_blocking(app);
    nion_apply_content_blocking(tab, uri);
    nion_update_site_info(app);

    if (app->tor_ready) {
        nion_set_status(app, enabled
            ? "● TOR CONNECTED — CONTENT BLOCKING ENABLED FOR THIS SITE"
            : "● TOR CONNECTED — CONTENT BLOCKING DISABLED FOR THIS SITE");
    }

    if (!tab->home_page && !tab->error_page)
        webkit_web_view_reload(tab->web_view);
}

void on_site_javascript_switch_notify(GObject *object,
                                             GParamSpec *pspec,
                                             gpointer user_data)
{
    (void)pspec;
    NionApp *app = user_data;
    if (!app || app->updating_site_controls)
        return;

    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!tab || !key) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), TRUE);
        app->updating_site_controls = FALSE;
        g_free(key);
        return;
    }
    g_free(key);

    gboolean enabled = gtk_switch_get_active(GTK_SWITCH(object));
    gboolean changed = nion_set_site_javascript_enabled(app, uri, enabled);
    if (!changed && nion_site_javascript_enabled_for_uri(app, uri) != enabled) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), !enabled);
        app->updating_site_controls = FALSE;
        nion_set_status(app, "● TOR CONNECTED — SITE JAVASCRIPT RULE LIMIT REACHED");
        return;
    }

    if (!app->is_private)
        nion_save_site_javascript(app);
    nion_apply_site_javascript(tab, uri);
    nion_update_site_info(app);

    if (app->tor_ready) {
        nion_set_status(app, enabled
            ? "● TOR CONNECTED — JAVASCRIPT ENABLED FOR THIS SITE"
            : "● TOR CONNECTED — JAVASCRIPT DISABLED FOR THIS SITE");
    }

    /* Reload so the new rule applies from document start rather than after
     * scripts from the old document have already executed. */
    if (!tab->home_page && !tab->error_page)
        webkit_web_view_reload(tab->web_view);
}

void on_site_autoplay_switch_notify(GObject *object,
                                            GParamSpec *pspec,
                                            gpointer user_data)
{
    (void)pspec;
    NionApp *app = user_data;
    if (!app || app->updating_site_controls)
        return;
    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gchar *key = nion_site_zoom_key_for_uri(uri);
    if (!tab || !key) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), FALSE);
        app->updating_site_controls = FALSE;
        g_free(key);
        return;
    }
    g_free(key);
    gboolean allowed = gtk_switch_get_active(GTK_SWITCH(object));
    gboolean changed = nion_set_autoplay_allowed_for_uri(app, uri, allowed);
    if (!changed && nion_autoplay_allowed_for_uri(app, uri) != allowed) {
        app->updating_site_controls = TRUE;
        gtk_switch_set_active(GTK_SWITCH(object), !allowed);
        app->updating_site_controls = FALSE;
        nion_set_status(app, "● TOR CONNECTED — AUTOPLAY EXCEPTION LIMIT REACHED");
        return;
    }
    if (!app->is_private)
        nion_save_autoplay(app);
    nion_update_site_info(app);
    if (app->tor_ready)
        nion_set_status(app, allowed
            ? "● TOR CONNECTED — AUTOPLAY WITH SOUND ALLOWED FOR THIS SITE"
            : "● TOR CONNECTED — AUDIBLE AUTOPLAY BLOCKED FOR THIS SITE");
    if (!tab->home_page && !tab->error_page)
        webkit_web_view_reload(tab->web_view);
}

void on_site_permissions_reset_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    NionTab *tab = nion_current_tab(app);
    if (!tab || !tab->web_view)
        return;

    gchar *origin = nion_web_origin_key_for_uri(webkit_web_view_get_uri(tab->web_view));
    if (!origin)
        return;

    nion_clear_temporary_permissions_for_origin(app, origin);

    /* A temporary grant is scoped to this NiOn window + origin, not to one
     * tab. Stop active camera/microphone capture in every matching tab when
     * the grant is reset so another tab cannot keep using a revoked grant. */
    gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *candidate = page
            ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!candidate || !candidate->web_view)
            continue;

        gchar *candidate_origin = nion_web_origin_key_for_uri(
            webkit_web_view_get_uri(candidate->web_view));
        gboolean same_origin = candidate_origin &&
            g_strcmp0(candidate_origin, origin) == 0;
        g_free(candidate_origin);
        if (!same_origin)
            continue;

        webkit_web_view_set_camera_capture_state(
            candidate->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
        webkit_web_view_set_microphone_capture_state(
            candidate->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
    }
    g_free(origin);

    nion_update_site_info(app);
    if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — TEMPORARY SITE PERMISSIONS RESET");
}

void nion_update_site_info_button(NionApp *app)
{
    if (!app || !app->site_info_button)
        return;

    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gboolean usable = tab && !tab->home_page && !tab->error_page && uri && *uri &&
                      !g_str_equal(uri, "about:blank");
    gtk_widget_set_sensitive(app->site_info_button, usable);
    gtk_widget_remove_css_class(app->site_info_button, "nion-site-info-warning");

    if (app->site_info_icon)
        gtk_image_set_from_icon_name(GTK_IMAGE(app->site_info_icon),
                                     "dialog-information-symbolic");
    if (!usable) {
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "No website connection information");
        return;
    }

    if (nion_tab_has_mixed_content(tab)) {
        if (app->site_info_icon)
            gtk_image_set_from_icon_name(GTK_IMAGE(app->site_info_icon),
                                         "dialog-warning-symbolic");
        gtk_widget_add_css_class(app->site_info_button, "nion-site-info-warning");
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "Site information — mixed content detected");
    } else if (nion_content_filter_has_failed(app)) {
        gtk_widget_set_tooltip_text(app->site_info_button,
                                    "Site information — content filter unavailable");
    } else {
        gtk_widget_set_tooltip_text(app->site_info_button, "Site information");
    }
}

gboolean on_site_info_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

void on_site_info_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app || !app->site_info_window)
        return;
    nion_update_site_info(app);
    gtk_window_present(GTK_WINDOW(app->site_info_window));
}

void nion_update_site_info(NionApp *app)
{
    if (!app || !app->site_info_button)
        return;

    NionTab *tab = nion_current_tab(app);
    const gchar *uri = tab ? webkit_web_view_get_uri(tab->web_view) : NULL;
    gboolean usable = tab && !tab->home_page && !tab->error_page && uri && *uri &&
                      !g_str_equal(uri, "about:blank");

    if (!usable) {
        if (app->site_info_title_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_title_label), "Site information");
        if (app->site_info_host_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_host_label), "No website loaded");
        if (app->site_info_connection_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_connection_label), "—");
        if (app->site_info_route_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_route_label),
                               app->tor_ready ? "Bundled Tor — connected" : "Bundled Tor — not ready");
        if (app->site_info_mixed_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_mixed_label), "—");
        if (app->site_info_tracking_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_tracking_label), "—");
        if (app->site_info_security_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_security_label),
                               nion_security_level_label(app->security_level));
        if (app->site_info_uri_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_uri_label), "—");
        app->updating_site_controls = TRUE;
        if (app->site_info_javascript_switch) {
            gtk_switch_set_active(GTK_SWITCH(app->site_info_javascript_switch), TRUE);
            gtk_widget_set_sensitive(app->site_info_javascript_switch, FALSE);
        }
        if (app->site_info_content_blocking_switch) {
            gtk_switch_set_active(GTK_SWITCH(app->site_info_content_blocking_switch), TRUE);
            gtk_widget_set_sensitive(app->site_info_content_blocking_switch, FALSE);
        }
        if (app->site_info_autoplay_switch) {
            gtk_switch_set_active(GTK_SWITCH(app->site_info_autoplay_switch), FALSE);
            gtk_widget_set_sensitive(app->site_info_autoplay_switch, FALSE);
        }
        app->updating_site_controls = FALSE;
        if (app->site_info_javascript_status_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_javascript_status_label), "No website loaded");
        if (app->site_info_content_blocking_status_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_content_blocking_status_label), "No website loaded");
        if (app->site_info_autoplay_status_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_autoplay_status_label), "No website loaded");
        if (app->site_info_camera_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_camera_label), "—");
        if (app->site_info_microphone_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_microphone_label), "—");
        if (app->site_info_geolocation_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_geolocation_label), "—");
        if (app->site_info_notifications_label)
            gtk_label_set_text(GTK_LABEL(app->site_info_notifications_label), "—");
        if (app->site_info_permissions_reset_button)
            gtk_widget_set_sensitive(app->site_info_permissions_reset_button, FALSE);
        if (app->site_info_forget_button)
            gtk_widget_set_sensitive(app->site_info_forget_button, FALSE);
        gtk_widget_set_tooltip_text(app->site_info_button, "No website connection information");
        return;
    }

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gint port = g_uri_get_port(parsed);
    gboolean is_https = scheme && g_ascii_strcasecmp(scheme, "https") == 0;
    gboolean is_http = scheme && g_ascii_strcasecmp(scheme, "http") == 0;
    gboolean is_onion = nion_host_is_onion(host);
    gboolean default_port = port < 0 || (is_https && port == 443) || (is_http && port == 80);

    gchar *host_text = NULL;
    if (!host || !*host) {
        host_text = g_strdup("Unknown host");
    } else if (!default_port) {
        host_text = strchr(host, ':')
            ? g_strdup_printf("[%s]:%d", host, port)
            : g_strdup_printf("%s:%d", host, port);
    } else {
        host_text = g_strdup(host);
    }

    GTlsCertificate *certificate = NULL;
    GTlsCertificateFlags tls_errors = 0;
    gboolean has_tls = FALSE;
    if (is_https && tab->connection_committed)
        has_tls = webkit_web_view_get_tls_info(tab->web_view, &certificate, &tls_errors);
    (void)certificate;

    gchar *connection = NULL;
    if (is_onion && is_https) {
        if (!tab->connection_committed)
            connection = g_strdup("Onion Service + HTTPS — connecting");
        else if (has_tls && tls_errors == 0)
            connection = g_strdup("Onion Service + HTTPS — TLS verified");
        else
            connection = g_strdup("Onion Service + HTTPS — TLS information unavailable");
    } else if (is_onion) {
        connection = g_strdup("Onion Service — end-to-end encrypted by Tor");
    } else if (is_https) {
        if (!tab->connection_committed)
            connection = g_strdup("HTTPS — connecting");
        else if (has_tls && tls_errors == 0)
            connection = g_strdup("HTTPS — TLS verified");
        else
            connection = g_strdup("HTTPS — TLS information unavailable");
    } else if (is_http) {
        connection = g_strdup("HTTP — no TLS to the website");
    } else {
        connection = g_strdup("Unknown web connection");
    }

    const gchar *route = app->tor_ready
        ? "Bundled Tor — connected; browsing routed through SOCKS"
        : "Tor unavailable — new browsing is fail-closed";

    const gchar *mixed = NULL;
    if (tab->mixed_content_run && tab->mixed_content_displayed)
        mixed = "Warning — insecure active and display content detected";
    else if (tab->mixed_content_run)
        mixed = "Warning — insecure active content detected";
    else if (tab->mixed_content_displayed)
        mixed = "Warning — insecure display content detected";
    else if (tab->mixed_content_other)
        mixed = "Warning — insecure content event detected";
    else if (is_https)
        mixed = tab->connection_committed ? "None detected" : "Waiting for secure connection";
    else if (is_onion)
        mixed = "HTTPS mixed-content check not applicable to this HTTP onion page";
    else
        mixed = "Not applicable to this HTTP page";

    if (app->site_info_title_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_title_label),
                           is_onion ? "Onion site information" : "Site information");
    if (app->site_info_host_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_host_label), host_text);
    if (app->site_info_connection_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_connection_label), connection);
    if (app->site_info_route_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_route_label), route);
    if (app->site_info_mixed_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_mixed_label), mixed);
    if (app->site_info_tracking_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_tracking_label),
            app->block_third_party_cookies
                ? "Strict third-party cookie blocking (ITP disabled)"
                : "WebKit Intelligent Tracking Prevention (ITP) enabled");
    if (app->site_info_security_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_security_label),
                           nion_security_level_label(app->security_level));
    if (app->site_info_uri_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_uri_label), uri);

    gboolean javascript_enabled = nion_site_javascript_enabled_for_uri(app, uri);
    app->updating_site_controls = TRUE;
    if (app->site_info_javascript_switch) {
        gtk_widget_set_sensitive(app->site_info_javascript_switch, TRUE);
        gtk_switch_set_active(GTK_SWITCH(app->site_info_javascript_switch), javascript_enabled);
    }
    app->updating_site_controls = FALSE;
    if (app->site_info_javascript_status_label) {
        const gchar *js_status;
        if (javascript_enabled) {
            js_status = app->security_level == NION_SECURITY_SAFEST
                ? (app->is_private ? "Enabled — private Safest exception" : "Enabled — Safest exception")
                : (app->is_private ? "Enabled — private memory-only rule" : "Enabled");
        } else {
            js_status = app->security_level == NION_SECURITY_SAFEST
                ? "Disabled by Safest level"
                : (app->is_private ? "Disabled — private memory-only rule" : "Disabled for this site");
        }
        gtk_label_set_text(GTK_LABEL(app->site_info_javascript_status_label), js_status);
    }

    gboolean content_blocking_enabled = nion_content_blocking_enabled_for_uri(app, uri);
    gboolean filter_ready = nion_content_filter_is_ready(app);
    gboolean filter_failed = nion_content_filter_has_failed(app);
    app->updating_site_controls = TRUE;
    if (app->site_info_content_blocking_switch) {
        gtk_switch_set_active(GTK_SWITCH(app->site_info_content_blocking_switch),
                              content_blocking_enabled);
        gtk_widget_set_sensitive(app->site_info_content_blocking_switch,
                                 !filter_failed);
    }
    app->updating_site_controls = FALSE;
    if (app->site_info_content_blocking_status_label) {
        const gchar *blocking_status = filter_failed
            ? "Unavailable — bundled filter failed to compile"
            : (!filter_ready
                ? "Initializing bundled filter…"
                : (content_blocking_enabled
                    ? "Enabled — lightweight bundled rules"
                    : "Disabled for this site"));
        gtk_label_set_text(GTK_LABEL(app->site_info_content_blocking_status_label),
                           blocking_status);
    }

    gboolean autoplay_allowed = nion_autoplay_allowed_for_uri(app, uri);
    app->updating_site_controls = TRUE;
    if (app->site_info_autoplay_switch) {
        gtk_switch_set_active(GTK_SWITCH(app->site_info_autoplay_switch), autoplay_allowed);
        gtk_widget_set_sensitive(app->site_info_autoplay_switch, TRUE);
    }
    app->updating_site_controls = FALSE;
    if (app->site_info_autoplay_status_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_autoplay_status_label),
            autoplay_allowed
                ? (app->is_private ? "Allowed — private memory-only exception" : "Allowed for this site")
                : (app->security_level == NION_SECURITY_STANDARD
                    ? "Audible autoplay blocked; muted autoplay allowed"
                    : "Autoplay blocked by security level"));

    gchar *origin = nion_web_origin_key_for_uri(uri);
    if (app->site_info_camera_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_camera_label),
            origin ? nion_permission_status_text(app, origin, NION_PERMISSION_CAMERA) : "Blocked");
    if (app->site_info_microphone_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_microphone_label),
            origin ? nion_permission_status_text(app, origin, NION_PERMISSION_MICROPHONE) : "Blocked");
    if (app->site_info_geolocation_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_geolocation_label),
            origin ? nion_permission_status_text(app, origin, NION_PERMISSION_GEOLOCATION) : "Blocked");
    if (app->site_info_notifications_label)
        gtk_label_set_text(GTK_LABEL(app->site_info_notifications_label),
            origin ? nion_permission_status_text(app, origin, NION_PERMISSION_NOTIFICATIONS) : "Blocked");
    if (app->site_info_forget_button)
        gtk_widget_set_sensitive(app->site_info_forget_button, TRUE);
    if (app->site_info_permissions_reset_button) {
        gboolean has_temporary = origin && (
            nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_CAMERA) ||
            nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_MICROPHONE) ||
            nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_GEOLOCATION) ||
            nion_permission_is_temporarily_allowed(app, origin, NION_PERMISSION_NOTIFICATIONS));
        gtk_widget_set_sensitive(app->site_info_permissions_reset_button, has_temporary);
    }
    g_free(origin);

    g_free(connection);
    g_free(host_text);
    g_uri_unref(parsed);
}

static void nion_clear_site_request_free(NionClearSiteRequest *request)
{
    if (!request)
        return;

    g_free(request->host);
    g_free(request->uri);
    g_clear_object(&request->web_view);
    g_list_free_full(request->website_data, (GDestroyNotify)webkit_website_data_unref);
    g_free(request);
}

static gchar *nion_current_site_host(NionApp *app, gchar **uri_out, WebKitWebView **web_view_out)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page)
        return NULL;

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    if (!uri || !*uri || g_str_equal(uri, "about:blank"))
        return NULL;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, &error);
    if (!parsed) {
        g_clear_error(&error);
        return NULL;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean supported = scheme && host &&
        (g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0);

    gchar *result = supported ? g_ascii_strdown(host, -1) : NULL;
    if (result && uri_out)
        *uri_out = g_strdup(uri);
    if (result && web_view_out)
        *web_view_out = g_object_ref(tab->web_view);

    g_uri_unref(parsed);
    g_clear_error(&error);
    return result;
}

static gboolean nion_website_data_matches_host(WebKitWebsiteData *data, const gchar *host)
{
    if (!data || !host || !*host)
        return FALSE;

    const gchar *name_raw = webkit_website_data_get_name(data);
    if (!name_raw || !*name_raw || g_str_equal(name_raw, "Local files"))
        return FALSE;

    while (*name_raw == '.')
        name_raw++;
    gchar *name = g_ascii_strdown(name_raw, -1);
    gboolean matches = g_strcmp0(name, host) == 0;

    /* WebKit normally groups website data by domain. If the current page is
     * a subdomain and WebKit reports the parent domain, treat that record as
     * belonging to the current site. Do not sweep sibling/third-party hosts. */
    if (!matches) {
        gsize host_len = strlen(host);
        gsize name_len = strlen(name);
        if (host_len > name_len &&
            g_str_has_suffix(host, name) &&
            host[host_len - name_len - 1] == '.')
            matches = TRUE;
    }

    g_free(name);
    return matches;
}

static gboolean nion_tab_matches_site_key(NionTab *tab, const gchar *site_key)
{
    if (!tab || !tab->web_view || !site_key)
        return FALSE;
    gchar *candidate = nion_site_zoom_key_for_uri(webkit_web_view_get_uri(tab->web_view));
    gboolean matches = candidate && g_strcmp0(candidate, site_key) == 0;
    g_free(candidate);
    return matches;
}

static void nion_stop_capture_for_origin(NionApp *app, const gchar *origin)
{
    if (!app || !origin || !app->notebook)
        return;
    gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || !tab->web_view)
            continue;
        gchar *candidate_origin = nion_web_origin_key_for_uri(
            webkit_web_view_get_uri(tab->web_view));
        gboolean same_origin = candidate_origin &&
            g_strcmp0(candidate_origin, origin) == 0;
        g_free(candidate_origin);
        if (!same_origin)
            continue;
        webkit_web_view_set_camera_capture_state(
            tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
        webkit_web_view_set_microphone_capture_state(
            tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
    }
}

static void nion_forget_site_local_state(NionApp *app, const gchar *uri)
{
    if (!app || !uri || !*uri)
        return;

    gchar *site_key = nion_site_zoom_key_for_uri(uri);
    gchar *origin = nion_web_origin_key_for_uri(uri);

    if (site_key) {
        if (app->site_zoom)
            g_hash_table_remove(app->site_zoom, site_key);
        if (app->site_javascript_disabled)
            g_hash_table_remove(app->site_javascript_disabled, site_key);
        if (app->site_javascript_enabled)
            g_hash_table_remove(app->site_javascript_enabled, site_key);
        if (app->content_blocking_disabled)
            g_hash_table_remove(app->content_blocking_disabled, site_key);
        if (app->autoplay_allowed_sites)
            g_hash_table_remove(app->autoplay_allowed_sites, site_key);

        if (!app->is_private) {
            nion_save_site_zoom(app);
            nion_save_site_javascript(app);
            nion_save_content_blocking(app);
            nion_save_autoplay(app);
        }

        /* Apply reset defaults immediately to already-open matching tabs. */
        gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
        for (gint i = 0; i < n_pages; i++) {
            GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
            NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
            if (!nion_tab_matches_site_key(tab, site_key))
                continue;
            webkit_web_view_set_zoom_level(tab->web_view, 1.0);
            nion_apply_site_javascript(tab, webkit_web_view_get_uri(tab->web_view));
            nion_apply_content_blocking(tab, webkit_web_view_get_uri(tab->web_view));
            g_clear_pointer(&tab->http_allowed_origin, g_free);
        }
    }

    if (origin) {
        nion_clear_temporary_permissions_for_origin(app, origin);
        nion_stop_capture_for_origin(app, origin);
    }

    g_free(origin);
    g_free(site_key);
    nion_update_site_info(app);
}

static void on_clear_site_data_removed(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionClearSiteRequest *request = user_data;
    NionApp *app = request->app;
    GError *error = NULL;
    gboolean ok = webkit_website_data_manager_remove_finish(
        WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);

    if (!ok) {
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? "● TOR CONNECTED — SITE DATA CLEAR FAILED: %s"
                : "○ TOR OFFLINE — SITE DATA CLEAR FAILED: %s",
            error ? error->message : "unknown error");
        nion_set_status(app, status);
        g_free(status);
        g_clear_error(&error);
        nion_clear_site_request_free(request);
        return;
    }

    if (request->forget_site)
        nion_forget_site_local_state(app, request->uri);

    gchar *status = g_strdup_printf(
        app->tor_ready
            ? (request->forget_site
                ? "● TOR CONNECTED — FORGOT SITE %s"
                : "● TOR CONNECTED — DATA CLEARED FOR %s")
            : (request->forget_site
                ? "○ TOR OFFLINE — FORGOT LOCAL STATE FOR %s"
                : "○ TOR OFFLINE — DATA CLEARED FOR %s"),
        request->host);
    nion_set_status(app, status);
    g_free(status);

    /* Reload only if the same WebView is still the active tab. This makes
     * cookie/storage removal immediately visible without disturbing a tab
     * the user switched away from while the async operation ran. */
    NionTab *current = nion_current_tab(app);
    if (app->tor_ready && current && current->web_view == request->web_view &&
        !current->home_page && !current->error_page)
        webkit_web_view_reload_bypass_cache(request->web_view);

    nion_clear_site_request_free(request);
}

static void on_clear_site_data_fetched(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionClearSiteRequest *request = user_data;
    NionApp *app = request->app;
    WebKitWebsiteDataManager *manager = WEBKIT_WEBSITE_DATA_MANAGER(source);
    GError *error = NULL;
    GList *all_data = webkit_website_data_manager_fetch_finish(manager, result, &error);

    if (error) {
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? "● TOR CONNECTED — SITE DATA LOOKUP FAILED: %s"
                : "○ TOR OFFLINE — SITE DATA LOOKUP FAILED: %s",
            error->message);
        nion_set_status(app, status);
        g_free(status);
        g_clear_error(&error);
        nion_clear_site_request_free(request);
        return;
    }

    for (GList *item = all_data; item; item = item->next) {
        WebKitWebsiteData *data = item->data;
        if (nion_website_data_matches_host(data, request->host))
            request->website_data = g_list_prepend(
                request->website_data, webkit_website_data_ref(data));
    }
    g_list_free_full(all_data, (GDestroyNotify)webkit_website_data_unref);

    if (!request->website_data) {
        if (request->forget_site)
            nion_forget_site_local_state(app, request->uri);
        gchar *status = g_strdup_printf(
            app->tor_ready
                ? (request->forget_site
                    ? "● TOR CONNECTED — FORGOT SITE %s (NO WEBKIT DATA WAS STORED)"
                    : "● TOR CONNECTED — NO STORED DATA FOR %s")
                : (request->forget_site
                    ? "○ TOR OFFLINE — FORGOT LOCAL STATE FOR %s"
                    : "○ TOR OFFLINE — NO STORED DATA FOR %s"),
            request->host);
        nion_set_status(app, status);
        g_free(status);
        if (request->forget_site && app->tor_ready) {
            NionTab *current = nion_current_tab(app);
            if (current && current->web_view == request->web_view &&
                !current->home_page && !current->error_page)
                webkit_web_view_reload_bypass_cache(request->web_view);
        }
        nion_clear_site_request_free(request);
        return;
    }

    request->website_data = g_list_reverse(request->website_data);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — CLEARING SITE DATA…"
        : "○ TOR OFFLINE — CLEARING SITE DATA…");

    webkit_website_data_manager_remove(manager,
                                       WEBKIT_WEBSITE_DATA_ALL,
                                       request->website_data,
                                       NULL,
                                       on_clear_site_data_removed,
                                       request);
}

static void on_clear_site_data_confirm_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    const gchar *host = g_object_get_data(G_OBJECT(button), "nion-site-host");
    const gchar *uri = g_object_get_data(G_OBJECT(button), "nion-site-uri");
    WebKitWebView *web_view = g_object_get_data(G_OBJECT(button), "nion-site-webview");

    if (!host || !uri || !web_view)
        return;

    NionClearSiteRequest *request = g_new0(NionClearSiteRequest, 1);
    request->app = app;
    request->host = g_strdup(host);
    request->uri = g_strdup(uri);
    request->web_view = g_object_ref(web_view);
    request->forget_site = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(button), "nion-forget-site")) != 0;

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));

    WebKitWebsiteDataManager *manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — LOOKING UP SITE DATA…"
        : "○ TOR OFFLINE — LOOKING UP SITE DATA…");

    webkit_website_data_manager_fetch(manager,
                                      WEBKIT_WEBSITE_DATA_ALL,
                                      NULL,
                                      on_clear_site_data_fetched,
                                      request);
}

static void nion_show_site_data_dialog(NionApp *app, gboolean forget_site)
{

    gchar *uri = NULL;
    WebKitWebView *web_view = NULL;
    gchar *host = nion_current_site_host(app, &uri, &web_view);
    if (!host) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NO WEBSITE DATA ON THIS TAB"
            : "○ TOR OFFLINE — NO WEBSITE DATA ON THIS TAB");
        g_free(uri);
        g_clear_object(&web_view);
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window),
                         forget_site ? "Forget this site" : "Clear data for this site");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 500, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gchar *heading_markup = g_markup_printf_escaped(
        forget_site ? "<b>Forget %s?</b>" : "<b>Clear data for %s?</b>", host);
    gtk_label_set_markup(GTK_LABEL(heading), heading_markup);
    g_free(heading_markup);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *message = gtk_label_new(forget_site
        ? "NiOn will remove this site's WebKit data and reset NiOn's saved state for it: zoom, JavaScript rule, content-blocking exception, autoplay exception, temporary permissions, and temporary HTTP allowance. Bookmarks and downloaded files are not removed."
        : "NiOn will remove WebKit data attributed to this site, including cookies, cache, local/session storage, IndexedDB and other stored website data. Other websites are left alone. You may be signed out of this site.");
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *url_label = gtk_label_new(uri);
    gtk_label_set_wrap(GTK_LABEL(url_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(url_label), PANGO_WRAP_CHAR);
    gtk_label_set_width_chars(GTK_LABEL(url_label), 1);
    gtk_label_set_max_width_chars(GTK_LABEL(url_label), 60);
    gtk_label_set_selectable(GTK_LABEL(url_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(url_label), 0.0f);
    gtk_widget_add_css_class(url_label, "nion-muted");
    gtk_box_append(GTK_BOX(box), url_label);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *clear = gtk_button_new_with_label(
        forget_site ? "Forget This Site" : "Clear Site Data");
    gtk_widget_add_css_class(clear, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), clear);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data_full(G_OBJECT(clear), "nion-site-host", g_strdup(host), g_free);
    g_object_set_data_full(G_OBJECT(clear), "nion-site-uri", g_strdup(uri), g_free);
    g_object_set_data_full(G_OBJECT(clear), "nion-site-webview", g_object_ref(web_view), g_object_unref);
    g_object_set_data(G_OBJECT(clear), "nion-forget-site", GINT_TO_POINTER(forget_site));

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_site_data_confirm_clicked), app);
    gtk_window_present(GTK_WINDOW(window));

    g_free(host);
    g_free(uri);
    g_object_unref(web_view);
}

void action_clear_site_data(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_site_data_dialog(user_data, FALSE);
}

void action_forget_site(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_site_data_dialog(user_data, TRUE);
}

static void nion_clear_data_request_free(NionClearDataRequest *request)
{
    g_free(request);
}

static void nion_clear_all_temporary_permissions(NionApp *app)
{
    if (!app)
        return;
    if (app->temporary_permissions)
        g_hash_table_remove_all(app->temporary_permissions);

    if (!app->notebook)
        return;
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || !tab->web_view)
            continue;
        webkit_web_view_set_camera_capture_state(tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
        webkit_web_view_set_microphone_capture_state(tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
    }
}

static void nion_clear_selected_local_data(NionClearDataRequest *request)
{
    if (!request || !request->app)
        return;
    NionApp *app = request->app;

    if (request->clear_zoom && app->site_zoom) {
        g_hash_table_remove_all(app->site_zoom);
        if (!app->is_private && app->site_zoom_file)
            g_unlink(app->site_zoom_file);
        if (app->notebook) {
            GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
            gint pages = gtk_notebook_get_n_pages(notebook);
            for (gint i = 0; i < pages; i++) {
                GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
                NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
                if (tab && tab->web_view)
                    webkit_web_view_set_zoom_level(tab->web_view, 1.0);
            }
        }
    }

    if (request->clear_javascript && app->site_javascript_disabled) {
        g_hash_table_remove_all(app->site_javascript_disabled);
        if (app->site_javascript_enabled)
            g_hash_table_remove_all(app->site_javascript_enabled);
        if (!app->is_private && app->site_javascript_file)
            g_unlink(app->site_javascript_file);
        if (app->notebook) {
            GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
            gint pages = gtk_notebook_get_n_pages(notebook);
            for (gint i = 0; i < pages; i++) {
                GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
                NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
                if (!tab || !tab->web_view)
                    continue;
                nion_apply_site_javascript(tab, webkit_web_view_get_uri(tab->web_view));
            }
        }
    }

    if (request->clear_content_blocking && app->content_blocking_disabled) {
        g_hash_table_remove_all(app->content_blocking_disabled);
        if (!app->is_private && app->content_blocking_file)
            g_unlink(app->content_blocking_file);
        nion_apply_content_filter_to_window(app);
    }

    if (request->clear_autoplay && app->autoplay_allowed_sites) {
        g_hash_table_remove_all(app->autoplay_allowed_sites);
        if (!app->is_private && app->autoplay_file)
            g_unlink(app->autoplay_file);
    }

    if (request->clear_permissions)
        nion_clear_all_temporary_permissions(app);

    nion_update_site_info(app);
}

static void on_clear_data_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionClearDataRequest *request = user_data;
    NionApp *app = request ? request->app : NULL;
    GError *error = NULL;
    gboolean ok = webkit_website_data_manager_clear_finish(
        WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);

    if (!ok) {
        if (app) {
            gchar *status = g_strdup_printf("%s — WEBKIT DATA CLEAR FAILED: %s",
                app->tor_ready ? "● TOR CONNECTED" : "○ TOR OFFLINE",
                error ? error->message : "unknown error");
            nion_set_status(app, status);
            g_free(status);
        }
        g_clear_error(&error);
        nion_clear_data_request_free(request);
        return;
    }

    if (app)
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — SELECTED BROWSING DATA CLEARED"
            : "○ TOR OFFLINE — SELECTED BROWSING DATA CLEARED");
    nion_clear_data_request_free(request);
}

static void on_clear_data_confirm_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkCheckButton *website_check = g_object_get_data(G_OBJECT(button), "nion-clear-website");
    GtkCheckButton *cache_check = g_object_get_data(G_OBJECT(button), "nion-clear-cache");
    GtkCheckButton *zoom_check = g_object_get_data(G_OBJECT(button), "nion-clear-zoom");
    GtkCheckButton *javascript_check = g_object_get_data(G_OBJECT(button), "nion-clear-javascript");
    GtkCheckButton *content_blocking_check = g_object_get_data(G_OBJECT(button), "nion-clear-content-blocking");
    GtkCheckButton *autoplay_check = g_object_get_data(G_OBJECT(button), "nion-clear-autoplay");
    GtkCheckButton *permissions_check = g_object_get_data(G_OBJECT(button), "nion-clear-permissions");

    gboolean clear_website = website_check && gtk_check_button_get_active(website_check);
    gboolean clear_cache = cache_check && gtk_check_button_get_active(cache_check);
    gboolean clear_zoom = zoom_check && gtk_check_button_get_active(zoom_check);
    gboolean clear_javascript = javascript_check && gtk_check_button_get_active(javascript_check);
    gboolean clear_content_blocking = content_blocking_check && gtk_check_button_get_active(content_blocking_check);
    gboolean clear_autoplay = autoplay_check && gtk_check_button_get_active(autoplay_check);
    gboolean clear_permissions = permissions_check && gtk_check_button_get_active(permissions_check);

    if (!clear_website && !clear_cache && !clear_zoom && !clear_javascript &&
        !clear_content_blocking && !clear_autoplay && !clear_permissions) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NO BROWSING DATA SELECTED"
            : "○ TOR OFFLINE — NO BROWSING DATA SELECTED");
        return;
    }

    NionClearDataRequest *request = g_new0(NionClearDataRequest, 1);
    request->app = app;
    request->clear_zoom = clear_zoom;
    request->clear_javascript = clear_javascript;
    request->clear_content_blocking = clear_content_blocking;
    request->clear_autoplay = clear_autoplay;
    request->clear_permissions = clear_permissions;

    if (clear_website && clear_cache) {
        request->types = WEBKIT_WEBSITE_DATA_ALL;
    } else if (clear_website) {
        request->types = (WebKitWebsiteDataTypes)(WEBKIT_WEBSITE_DATA_ALL &
            ~(WEBKIT_WEBSITE_DATA_DISK_CACHE | WEBKIT_WEBSITE_DATA_MEMORY_CACHE));
    } else if (clear_cache) {
        request->types = (WebKitWebsiteDataTypes)(WEBKIT_WEBSITE_DATA_DISK_CACHE |
                                                  WEBKIT_WEBSITE_DATA_MEMORY_CACHE);
    }

    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));

    nion_clear_selected_local_data(request);

    if (request->types == 0) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — SELECTED LOCAL BROWSING DATA CLEARED"
            : "○ TOR OFFLINE — SELECTED LOCAL BROWSING DATA CLEARED");
        nion_clear_data_request_free(request);
        return;
    }

    WebKitWebsiteDataManager *manager =
        webkit_network_session_get_website_data_manager(app->network_session);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — CLEARING SELECTED BROWSING DATA…"
        : "○ TOR OFFLINE — CLEARING SELECTED BROWSING DATA…");

    webkit_website_data_manager_clear(manager,
                                      request->types,
                                      0,
                                      NULL,
                                      on_clear_data_finished,
                                      request);
}

static GtkWidget *nion_browsing_data_check(const gchar *title,
                                            const gchar *detail,
                                            gboolean active)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *check = gtk_check_button_new_with_label(title);
    GtkWidget *description = gtk_label_new(detail);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), active);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_xalign(GTK_LABEL(description), 0.0f);
    gtk_widget_add_css_class(description, "nion-muted");
    gtk_widget_set_margin_start(description, 28);
    gtk_box_append(GTK_BOX(row), check);
    gtk_box_append(GTK_BOX(row), description);
    g_object_set_data(G_OBJECT(row), "nion-check", check);
    return row;
}

void action_clear_data(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), app->is_private
        ? "Private browsing data" : "Browsing data manager");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), app->is_private
        ? "<b>Clear private browsing data</b>"
        : "<b>Choose browsing data to clear</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *message = gtk_label_new(app->is_private
        ? "Private website data is already ephemeral, but you can clear selected state before closing this Private Window."
        : "Website data and cache are selected by default. Saved zoom, JavaScript, content-blocking, and autoplay exceptions are separate so they are never erased accidentally.");
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *website_row = nion_browsing_data_check(
        "Cookies and website storage",
        "Cookies, local/session storage, IndexedDB, service workers, DOM Cache, HSTS/ITP state, and other WebKit website data.",
        TRUE);
    GtkWidget *cache_row = nion_browsing_data_check(
        "Web cache",
        "HTTP disk and memory cache.",
        TRUE);
    GtkWidget *zoom_row = nion_browsing_data_check(
        "Saved site zoom levels",
        "Forget all normal per-site zoom overrides and reset open tabs to 100%.",
        FALSE);
    GtkWidget *javascript_row = nion_browsing_data_check(
        app->is_private ? "Private JavaScript site rules" : "Saved JavaScript site rules",
        app->is_private
            ? "Forget memory-only JavaScript overrides for this Private Window."
            : "Forget all persistent per-site JavaScript overrides. Open pages may need a reload before scripts run again.",
        FALSE);
    GtkWidget *content_blocking_row = nion_browsing_data_check(
        app->is_private ? "Private content-blocking exceptions" : "Saved content-blocking exceptions",
        app->is_private
            ? "Forget memory-only sites where content blocking was disabled in this Private Window."
            : "Forget all persistent sites where lightweight content blocking was disabled.",
        FALSE);
    GtkWidget *autoplay_row = nion_browsing_data_check(
        app->is_private ? "Private autoplay exceptions" : "Saved autoplay exceptions",
        app->is_private
            ? "Forget memory-only sites allowed to autoplay with sound in this Private Window."
            : "Forget all persistent sites allowed to autoplay with sound.",
        FALSE);
    GtkWidget *permissions_row = nion_browsing_data_check(
        "Temporary site permissions",
        "Revoke temporary camera, microphone, location, and notification grants in this window; active camera/microphone capture is stopped.",
        FALSE);

    gtk_box_append(GTK_BOX(box), website_row);
    gtk_box_append(GTK_BOX(box), cache_row);
    if (!app->is_private)
        gtk_box_append(GTK_BOX(box), zoom_row);
    gtk_box_append(GTK_BOX(box), javascript_row);
    gtk_box_append(GTK_BOX(box), content_blocking_row);
    gtk_box_append(GTK_BOX(box), autoplay_row);
    gtk_box_append(GTK_BOX(box), permissions_row);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *clear = gtk_button_new_with_label("Clear Selected");
    gtk_widget_add_css_class(clear, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), clear);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data(G_OBJECT(clear), "nion-clear-website",
                      g_object_get_data(G_OBJECT(website_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-cache",
                      g_object_get_data(G_OBJECT(cache_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-zoom",
                      app->is_private ? NULL : g_object_get_data(G_OBJECT(zoom_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-javascript",
                      g_object_get_data(G_OBJECT(javascript_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-content-blocking",
                      g_object_get_data(G_OBJECT(content_blocking_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-autoplay",
                      g_object_get_data(G_OBJECT(autoplay_row), "nion-check"));
    g_object_set_data(G_OBJECT(clear), "nion-clear-permissions",
                      g_object_get_data(G_OBJECT(permissions_row), "nion-check"));

    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_data_confirm_clicked), app);
    gtk_window_present(GTK_WINDOW(window));
}

void on_site_forget_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_show_site_data_dialog(user_data, TRUE);
}
