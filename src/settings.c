/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "privacy.h"
#include "per-site.h"
#include "session.h"
#include "settings.h"

/* UI/status operations that still live in main.c (Phase 6, NiOn 2.0.0).
 * main.c registers implementations via nion_settings_set_callbacks() so
 * this module never calls into main.c directly. */
static NionSettingsCallbacks cb;

static void
nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

static void
nion_update_controls(NionApp *app)
{
    if (cb.update_controls)
        cb.update_controls(app);
}

static void
nion_update_site_info(NionApp *app)
{
    if (cb.update_site_info)
        cb.update_site_info(app);
}

void
nion_settings_set_callbacks(const NionSettingsCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

static const gchar *nion_security_level_id(NionSecurityLevel level)
{
    switch (level) {
    case NION_SECURITY_SAFER: return "safer";
    case NION_SECURITY_SAFEST: return "safest";
    case NION_SECURITY_STANDARD:
    default: return "standard";
    }
}

const gchar *nion_security_level_label(NionSecurityLevel level)
{
    switch (level) {
    case NION_SECURITY_SAFER: return "Safer";
    case NION_SECURITY_SAFEST: return "Safest";
    case NION_SECURITY_STANDARD:
    default: return "Standard";
    }
}

static gboolean nion_security_level_parse(const gchar *value, NionSecurityLevel *out)
{
    if (!value || !out)
        return FALSE;
    if (g_str_equal(value, "standard"))
        *out = NION_SECURITY_STANDARD;
    else if (g_str_equal(value, "safer"))
        *out = NION_SECURITY_SAFER;
    else if (g_str_equal(value, "safest"))
        *out = NION_SECURITY_SAFEST;
    else
        return FALSE;
    return TRUE;
}

void nion_load_preferences(NionApp *app)
{
    app->restore_session = TRUE;
    app->block_third_party_cookies = FALSE;
    app->security_level = NION_SECURITY_STANDARD;
    g_clear_pointer(&app->search_engine, g_free);
    app->search_engine = g_strdup("duckduckgo");

    if (g_file_test(app->preferences_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->preferences_file,
                                        NION_MAX_PREFERENCES_FILE_BYTES)) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->preferences_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn preferences: %s", error->message);
            nion_quarantine_profile_file(app->preferences_file, "preferences");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    gboolean valid = TRUE;
    if (g_key_file_has_key(key_file, "General", "restore-session", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "General", "restore-session", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->restore_session = value;
        }
    }

    if (g_key_file_has_key(key_file, "Privacy", "block-third-party-cookies", NULL)) {
        error = NULL;
        gboolean value = g_key_file_get_boolean(key_file, "Privacy", "block-third-party-cookies", &error);
        if (error) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->block_third_party_cookies = value;
        }
    }

    if (g_key_file_has_key(key_file, "Privacy", "security-level", NULL)) {
        error = NULL;
        gchar *level = g_key_file_get_string(key_file, "Privacy", "security-level", &error);
        NionSecurityLevel parsed = NION_SECURITY_STANDARD;
        if (error || !nion_security_level_parse(level, &parsed)) {
            valid = FALSE;
            g_clear_error(&error);
        } else {
            app->security_level = parsed;
        }
        g_free(level);
    }

    if (g_key_file_has_key(key_file, "Search", "engine", NULL)) {
        error = NULL;
        gchar *engine = g_key_file_get_string(key_file, "Search", "engine", &error);
        if (error || !engine || !(g_str_equal(engine, "duckduckgo") ||
                                  g_str_equal(engine, "brave") ||
                                  g_str_equal(engine, "startpage"))) {
            valid = FALSE;
            g_clear_error(&error);
            g_free(engine);
        } else {
            g_free(app->search_engine);
            app->search_engine = engine;
        }
    }

    g_key_file_free(key_file);

    if (!valid) {
        nion_quarantine_profile_file(app->preferences_file, "preferences");
        app->restore_session = TRUE;
        app->block_third_party_cookies = FALSE;
        app->security_level = NION_SECURITY_STANDARD;
        g_clear_pointer(&app->search_engine, g_free);
        app->search_engine = g_strdup("duckduckgo");
    }
}

static void nion_save_preferences(NionApp *app)
{
    if (!app || app->is_private || !app->preferences_file)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_boolean(key_file, "General", "restore-session", app->restore_session);
    g_key_file_set_boolean(key_file, "Privacy", "block-third-party-cookies", app->block_third_party_cookies);
    g_key_file_set_string(key_file, "Privacy", "security-level",
                          nion_security_level_id(app->security_level));
    g_key_file_set_string(key_file, "Search", "engine",
                          app->search_engine ? app->search_engine : "duckduckgo");
    nion_write_key_file_atomic(key_file, app->preferences_file);
    g_key_file_free(key_file);
}

static void nion_apply_security_level_to_window(NionApp *app, gboolean reload_pages)
{
    if (!app || !app->notebook)
        return;

    /* Level changes revoke temporary grants so a stricter level never carries
     * forward camera/microphone/location/notification decisions unexpectedly. */
    if (app->temporary_permissions)
        g_hash_table_remove_all(app->temporary_permissions);

    gint n_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (gint i = 0; i < n_pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || !tab->web_view)
            continue;

        WebKitSettings *settings = webkit_web_view_get_settings(tab->web_view);
        nion_apply_privacy_settings(app, settings);
        nion_apply_site_javascript(tab, webkit_web_view_get_uri(tab->web_view));
        webkit_web_view_set_camera_capture_state(tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);
        webkit_web_view_set_microphone_capture_state(tab->web_view, WEBKIT_MEDIA_CAPTURE_STATE_NONE);

        if (reload_pages && !tab->home_page && !tab->error_page && app->tor_ready)
            webkit_web_view_reload(tab->web_view);
    }

    nion_update_site_info(app);
    nion_update_controls(app);
}

void on_preferences_cancel_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void on_preferences_save_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!root || !GTK_IS_WINDOW(root))
        return;

    GtkWindow *window = GTK_WINDOW(root);
    GtkCheckButton *restore_check = g_object_get_data(G_OBJECT(window), "nion-restore-check");
    GtkCheckButton *third_party_check = g_object_get_data(G_OBJECT(window), "nion-third-party-check");
    GtkDropDown *security_dropdown = g_object_get_data(G_OBJECT(window), "nion-security-dropdown");
    GtkDropDown *search_dropdown = g_object_get_data(G_OBJECT(window), "nion-search-dropdown");
    if (!restore_check || !third_party_check || !security_dropdown || !search_dropdown)
        return;

    gboolean old_restore = app->restore_session;
    NionSecurityLevel old_security = app->security_level;
    app->restore_session = gtk_check_button_get_active(restore_check);
    app->block_third_party_cookies = gtk_check_button_get_active(third_party_check);
    guint security_selected = gtk_drop_down_get_selected(security_dropdown);
    app->security_level = security_selected <= NION_SECURITY_SAFEST
        ? (NionSecurityLevel)security_selected : NION_SECURITY_STANDARD;

    const gchar *search_ids[] = { "duckduckgo", "brave", "startpage" };
    guint selected = gtk_drop_down_get_selected(search_dropdown);
    if (selected >= G_N_ELEMENTS(search_ids))
        selected = 0;
    g_free(app->search_engine);
    app->search_engine = g_strdup(search_ids[selected]);

    nion_save_preferences(app);
    nion_apply_cookie_policy(app);
    if (old_security != app->security_level)
        nion_apply_security_level_to_window(app, TRUE);
    if (!app->is_private && app->private_windows) {
        for (guint i = 0; i < app->private_windows->len; i++) {
            NionApp *private_app = g_ptr_array_index(app->private_windows, i);
            if (!private_app)
                continue;
            private_app->block_third_party_cookies = app->block_third_party_cookies;
            private_app->security_level = app->security_level;
            g_free(private_app->search_engine);
            private_app->search_engine = g_strdup(app->search_engine);
            nion_apply_cookie_policy(private_app);
            if (old_security != app->security_level)
                nion_apply_security_level_to_window(private_app, TRUE);
        }
    }

    if (!app->restore_session) {
        if (app->session_save_source_id) {
            g_source_remove(app->session_save_source_id);
            app->session_save_source_id = 0;
        }
        g_unlink(app->session_file);
    } else {
        nion_save_session(app, FALSE);
    }

    if (app->tor_ready) {
        if (old_security != app->security_level) {
            gchar *status = g_strdup_printf("● TOR CONNECTED — SECURITY LEVEL: %s",
                                            nion_security_level_label(app->security_level));
            nion_set_status(app, status);
            g_free(status);
        } else if (old_restore != app->restore_session) {
            nion_set_status(app, app->restore_session
                ? "● TOR CONNECTED — TAB RESTORE ENABLED"
                : "● TOR CONNECTED — TAB RESTORE DISABLED");
        } else {
            nion_set_status(app, "● TOR CONNECTED — PREFERENCES SAVED");
        }
    } else {
        nion_set_status(app, "○ TOR NOT READY — PREFERENCES SAVED");
    }

    gtk_window_destroy(window);
}

void action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;

    if (app->is_private) {
        nion_set_status(app, "● PRIVATE WINDOW — PREFERENCES ARE GLOBAL; EDIT THEM FROM THE NORMAL WINDOW");
        return;
    }

    if (app->preferences_window) {
        gtk_window_present(GTK_WINDOW(app->preferences_window));
        return;
    }

    GtkWidget *window = gtk_window_new();
    app->preferences_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->preferences_window);

    gtk_window_set_title(GTK_WINDOW(window), "NiOn Preferences");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 560);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Browsing, Session &amp; Privacy</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *security_heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(security_heading), "<b>Security level</b>");
    gtk_label_set_xalign(GTK_LABEL(security_heading), 0.0f);
    gtk_box_append(GTK_BOX(box), security_heading);

    const gchar *security_names[] = { "Standard", "Safer", "Safest", NULL };
    GtkWidget *security_dropdown = gtk_drop_down_new_from_strings(security_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(security_dropdown), (guint)app->security_level);
    gtk_widget_set_tooltip_text(security_dropdown,
        "Standard preserves NiOn's compatibility baseline; Safer blocks page fullscreen and autoplay by default; Safest also disables JavaScript and MediaStream by default.");
    gtk_box_append(GTK_BOX(box), security_dropdown);

    GtkWidget *security_note = gtk_label_new(
        "Security Levels only tighten NiOn's existing hardening. WebRTC, WebGL, WebAudio, JavaScript clipboard access and automatic JavaScript popups stay blocked at every level. Changing level reloads open website tabs and revokes temporary permissions.");
    gtk_label_set_wrap(GTK_LABEL(security_note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(security_note), 0.0f);
    gtk_widget_add_css_class(security_note, "nion-muted");
    gtk_box_append(GTK_BOX(box), security_note);

    GtkWidget *restore = gtk_check_button_new_with_label("Restore tabs when NiOn starts");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(restore), app->restore_session);
    gtk_box_append(GTK_BOX(box), restore);

    GtkWidget *third_party = gtk_check_button_new_with_label("Strictly block third-party cookies (disables WebKit ITP)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(third_party), app->block_third_party_cookies);
    gtk_box_append(GTK_BOX(box), third_party);

    GtkWidget *search_heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(search_heading), "<b>Default search engine</b>");
    gtk_label_set_xalign(GTK_LABEL(search_heading), 0.0f);
    gtk_widget_set_margin_top(search_heading, 4);
    gtk_box_append(GTK_BOX(box), search_heading);

    const gchar *search_names[] = { "DuckDuckGo", "Brave Search", "Startpage", NULL };
    GtkWidget *search_dropdown = gtk_drop_down_new_from_strings(search_names);
    guint search_selected = 0;
    if (app->search_engine && g_str_equal(app->search_engine, "brave"))
        search_selected = 1;
    else if (app->search_engine && g_str_equal(app->search_engine, "startpage"))
        search_selected = 2;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(search_dropdown), search_selected);
    gtk_box_append(GTK_BOX(box), search_dropdown);

    GtkWidget *note = gtk_label_new(
        "Cookies and website data remain persistent between NiOn restarts so website logins can survive. "
        "By default NiOn enables WebKit Intelligent Tracking Prevention (ITP). The strict third-party-cookie option disables ITP and can break some sign-in flows.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data(G_OBJECT(window), "nion-restore-check", restore);
    g_object_set_data(G_OBJECT(window), "nion-third-party-check", third_party);
    g_object_set_data(G_OBJECT(window), "nion-security-dropdown", security_dropdown);
    g_object_set_data(G_OBJECT(window), "nion-search-dropdown", search_dropdown);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_preferences_cancel_clicked), app);
    g_signal_connect(save, "clicked", G_CALLBACK(on_preferences_save_clicked), app);

    gtk_window_present(GTK_WINDOW(window));
}
