/* Copyright (C) 2026 Jeannes Bryan */

/* privacy (extracted from src/main.c, Phase 2, NiOn 2.0.0). */

#include "privacy.h"
#include "types.h"

void nion_apply_cookie_policy(NionApp *app)
{
    if (!app->network_session)
        return;
    WebKitCookieManager *cookies = webkit_network_session_get_cookie_manager(app->network_session);
    /* WebKit ITP supersedes ACCEPT_NO_THIRD_PARTY. Preserve NiOn's older
     * strict option as an explicit alternative: default = ITP; strict =
     * blanket third-party-cookie blocking with ITP disabled. */
    if (app->block_third_party_cookies) {
        webkit_network_session_set_itp_enabled(app->network_session, FALSE);
        webkit_cookie_manager_set_accept_policy(cookies, WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    } else {
        webkit_network_session_set_itp_enabled(app->network_session, TRUE);
        webkit_cookie_manager_set_accept_policy(cookies, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    }
}

static void nion_set_boolean_setting_if_present(WebKitSettings *settings,
                                                const gchar *property_name,
                                                gboolean value)
{
    if (!settings || !property_name)
        return;

    GParamSpec *property = g_object_class_find_property(
        G_OBJECT_GET_CLASS(settings), property_name);
    if (property && G_IS_PARAM_SPEC_BOOLEAN(property))
        g_object_set(settings, property_name, value, NULL);
}

void nion_apply_privacy_settings(NionApp *app, WebKitSettings *settings)
{
    if (!settings)
        return;

    NionSecurityLevel level = app ? app->security_level : NION_SECURITY_STANDARD;

    /* Security Levels only tighten NiOn's existing baseline. Standard never
     * re-enables surfaces already hard-blocked by NiOn. */
    webkit_settings_set_enable_javascript(settings, level != NION_SECURITY_SAFEST);
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);

    webkit_settings_set_enable_webrtc(settings, FALSE);
    webkit_settings_set_enable_media_stream(settings, level != NION_SECURITY_SAFEST);
    webkit_settings_set_enable_dns_prefetching(settings, FALSE);

    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-accelerated-2d-canvas", FALSE);

    /* Page-controlled fullscreen is permitted only at Standard. Browser F11
     * fullscreen is a GTK window action and remains available at every level. */
    nion_set_boolean_setting_if_present(settings, "enable-fullscreen",
                                        level == NION_SECURITY_STANDARD);

    webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, FALSE);
    webkit_settings_set_allow_file_access_from_file_urls(settings, FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, FALSE);

    webkit_settings_set_enable_hyperlink_auditing(settings, FALSE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);
    webkit_settings_set_enable_encrypted_media(settings, FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-plugins", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-java", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-offline-web-application-cache", FALSE);
    nion_set_boolean_setting_if_present(settings, "enable-mock-capture-devices", FALSE);
    nion_set_boolean_setting_if_present(settings, "allow-top-navigation-to-data-urls", FALSE);
    nion_set_boolean_setting_if_present(settings, "disable-web-security", FALSE);
}
