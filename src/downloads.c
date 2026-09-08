/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "navigation.h"
#include "downloads.h"

/* UI/tab operations that still live in main.c (Phase 6, NiOn 2.0.0).
 * main.c registers implementations via nion_downloads_set_callbacks()
 * so this module never calls into main.c directly. */
static NionDownloadCallbacks cb;

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

void
nion_downloads_set_callbacks(const NionDownloadCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

static gchar *nion_safe_download_filename(const gchar *suggested)
{
    const gchar *source = (suggested && *suggested) ? suggested : "download";
    gchar *base = g_path_get_basename(source);

    if (!base || !*base || g_str_equal(base, ".") || g_str_equal(base, "..")) {
        g_free(base);
        return g_strdup("download");
    }

    for (gchar *p = base; *p; p++) {
        if (*p == '/' || *p == '\\' || ((guchar)*p < 0x20) || *p == 0x7f)
            *p = '_';
    }

    return base;
}

static gchar *nion_unique_download_path(NionApp *app, const gchar *suggested)
{
    gchar *filename = nion_safe_download_filename(suggested);
    gchar *candidate = g_build_filename(app->download_dir, filename, NULL);

    if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        g_free(filename);
        return candidate;
    }

    const gchar *dot = strrchr(filename, '.');
    gchar *stem = NULL;
    gchar *extension = NULL;

    if (dot && dot != filename) {
        stem = g_strndup(filename, (gsize)(dot - filename));
        extension = g_strdup(dot);
    } else {
        stem = g_strdup(filename);
        extension = g_strdup("");
    }

    g_free(candidate);
    candidate = NULL;

    for (guint i = 1; i < G_MAXUINT; i++) {
        gchar *numbered = g_strdup_printf("%s (%u)%s", stem, i, extension);
        candidate = g_build_filename(app->download_dir, numbered, NULL);
        g_free(numbered);
        if (!g_file_test(candidate, G_FILE_TEST_EXISTS))
            break;
        g_clear_pointer(&candidate, g_free);
    }

    g_free(stem);
    g_free(extension);
    g_free(filename);
    return candidate;
}

static gchar *nion_download_time_text(gint64 unix_time)
{
    if (unix_time <= 0)
        return g_strdup("");

    GDateTime *dt = g_date_time_new_from_unix_local(unix_time);
    if (!dt)
        return g_strdup("");
    gchar *text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return text;
}

static void nion_download_update_panel_visibility(NionApp *app)
{
    if (!app || !app->downloads_list)
        return;

    gboolean has_rows = gtk_widget_get_first_child(app->downloads_list) != NULL;
    if (app->downloads_empty_label)
        gtk_widget_set_visible(app->downloads_empty_label, !has_rows);
}

static void nion_download_free(gpointer data)
{
    NionDownload *item = data;
    if (!item)
        return;

    if (item->download) {
        g_signal_handlers_disconnect_by_data(item->download, item);
        g_object_unref(item->download);
    }

    g_clear_pointer(&item->destination, g_free);
    g_clear_pointer(&item->filename, g_free);
    g_clear_pointer(&item->source_uri, g_free);
    g_clear_pointer(&item->history_id, g_free);
    g_clear_pointer(&item->history_status, g_free);
    g_clear_pointer(&item->history_detail, g_free);
    g_free(item);
}

static void nion_download_set_history(NionDownload *item,
                                      const gchar *status,
                                      const gchar *detail)
{
    if (!item)
        return;
    g_free(item->history_status);
    item->history_status = g_strdup(status ? status : "Unknown");
    g_free(item->history_detail);
    item->history_detail = g_strdup(detail ? detail : "");
    item->history_time = g_get_real_time() / G_USEC_PER_SEC;
}

static void nion_download_refresh_history_detail(NionDownload *item)
{
    if (!item || !item->detail_label)
        return;

    const gchar *detail = item->history_detail ? item->history_detail : "";
    gchar *when = nion_download_time_text(item->history_time);
    gchar *shown = (*when && *detail)
        ? g_strdup_printf("%s · %s", detail, when)
        : g_strdup((*detail) ? detail : when);
    gtk_label_set_text(GTK_LABEL(item->detail_label), shown);
    g_free(shown);
    g_free(when);
}

static gboolean nion_download_source_retryable(const gchar *uri)
{
    if (!uri || !*uri || strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return FALSE;

    GError *error = NULL;
    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (!parsed) {
        g_clear_error(&error);
        return FALSE;
    }

    const gchar *scheme = g_uri_get_scheme(parsed);
    gboolean web_scheme = scheme &&
        (g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0);
    g_uri_unref(parsed);

    return web_scheme && nion_validate_uri(uri, NULL);
}

static gboolean nion_download_parent_directory_exists(const gchar *destination)
{
    if (!destination || !*destination)
        return FALSE;

    gchar *parent = g_path_get_dirname(destination);
    gboolean exists = parent && g_file_test(parent, G_FILE_TEST_IS_DIR);
    g_free(parent);
    return exists;
}

static void nion_download_update_actions(NionDownload *item)
{
    if (!item || !item->more_button)
        return;

    gboolean file_ready = item->finished && item->destination &&
        g_file_test(item->destination, G_FILE_TEST_IS_REGULAR);
    gboolean folder_ready = nion_download_parent_directory_exists(item->destination);
    gboolean link_ready = item->source_uri && *item->source_uri;
    gboolean retry_ready = item->failed &&
        g_strcmp0(item->history_status, "Failed") == 0 &&
        nion_download_source_retryable(item->source_uri);

    gtk_widget_set_visible(item->open_button, file_ready);
    gtk_widget_set_visible(item->folder_button, folder_ready);
    gtk_widget_set_visible(item->copy_link_button, link_ready);
    gtk_widget_set_visible(item->retry_button, retry_ready);
    gtk_widget_set_visible(item->more_button,
                           file_ready || folder_ready || link_ready || retry_ready);
}

static void nion_download_action_status(NionDownload *item, const gchar *detail)
{
    if (!item || !item->app || !detail)
        return;

    const gchar *tor_state = item->app->tor_ready && !item->app->tor_failed
        ? "● TOR CONNECTED"
        : "○ TOR OFFLINE";
    gchar *message = g_strdup_printf("%s — %s", tor_state, detail);
    nion_set_status(item->app, message);
    g_free(message);
}

static gboolean nion_download_launch_uri(NionDownload *item,
                                         const gchar *uri,
                                         const gchar *success_detail)
{
    if (!item || !item->app || !uri || !*uri)
        return FALSE;

    GError *error = NULL;
    gboolean launched = g_app_info_launch_default_for_uri(uri, NULL, &error);
    if (!launched) {
        gchar *detail = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                        (error && error->message) ? error->message : "could not launch application");
        nion_download_action_status(item, detail);
        g_free(detail);
        g_clear_error(&error);
        return FALSE;
    }

    if (success_detail)
        nion_download_action_status(item, success_detail);
    return TRUE;
}

static void on_download_open_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->finished || !item->destination ||
        !g_file_test(item->destination, G_FILE_TEST_IS_REGULAR)) {
        if (item && item->app)
            nion_download_action_status(item, "DOWNLOADED FILE NOT FOUND");
        nion_download_update_actions(item);
        return;
    }

    GError *error = NULL;
    gchar *uri = g_filename_to_uri(item->destination, NULL, &error);
    if (!uri) {
        gchar *message = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                         (error && error->message) ? error->message : "invalid file path");
        nion_download_action_status(item, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    nion_download_launch_uri(item, uri, "OPENED DOWNLOADED FILE");
    g_free(uri);
}

static void on_download_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->destination)
        return;

    gchar *parent = g_path_get_dirname(item->destination);
    if (!parent || !g_file_test(parent, G_FILE_TEST_IS_DIR)) {
        if (item->app)
            nion_download_action_status(item, "DOWNLOAD FOLDER NOT FOUND");
        g_free(parent);
        nion_download_update_actions(item);
        return;
    }

    GError *error = NULL;
    gchar *uri = g_filename_to_uri(parent, NULL, &error);
    g_free(parent);
    if (!uri) {
        gchar *message = g_strdup_printf("DOWNLOAD ACTION FAILED: %s",
                                         (error && error->message) ? error->message : "invalid folder path");
        nion_download_action_status(item, message);
        g_free(message);
        g_clear_error(&error);
        return;
    }

    nion_download_launch_uri(item, uri, "OPENED DOWNLOAD FOLDER");
    g_free(uri);
}

static void on_download_copy_link_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->source_uri || !*item->source_uri)
        return;

    GdkDisplay *display = gtk_widget_get_display(item->app->window);
    if (!display)
        return;

    GdkClipboard *clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, item->source_uri);
    nion_download_action_status(item, "DOWNLOAD LINK COPIED");
}

static void on_download_retry_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item || !item->app || !item->failed ||
        g_strcmp0(item->history_status, "Failed") != 0 ||
        !nion_download_source_retryable(item->source_uri))
        return;

    NionApp *app = item->app;
    if (!app->tor_ready || app->tor_failed) {
        nion_set_status(app, "○ TOR OFFLINE — DOWNLOAD RETRY BLOCKED");
        return;
    }

    NionTab *tab = nion_current_tab(app);
    if (!tab || !tab->web_view) {
        nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY FAILED: no active tab");
        return;
    }

    WebKitDownload *retry = webkit_web_view_download_uri(tab->web_view, item->source_uri);
    if (!retry) {
        nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY FAILED");
        return;
    }

    g_object_unref(retry);
    nion_set_status(app, "● TOR CONNECTED — DOWNLOAD RETRY STARTED (Ctrl+J)");
}

static GtkWidget *nion_download_create_row(NionDownload *item,
                                           const gchar *name,
                                           const gchar *detail,
                                           gboolean active)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name_label = gtk_label_new(name ? name : "Download");
    GtkWidget *detail_label = gtk_label_new(detail ? detail : "");
    GtkWidget *progress = gtk_progress_bar_new();
    GtkWidget *action = gtk_button_new_with_label(active ? "Cancel" : "Remove");
    GtkWidget *more = gtk_menu_button_new();
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *open = gtk_button_new_with_label("Open File");
    GtkWidget *folder = gtk_button_new_with_label("Open Containing Folder");
    GtkWidget *copy_link = gtk_button_new_with_label("Copy Download Link");
    GtkWidget *retry = gtk_button_new_with_label("Retry Failed Download");

    gtk_widget_add_css_class(row, "nion-download-row");
    gtk_widget_add_css_class(detail_label, "nion-download-detail");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(detail_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_ellipsize(GTK_LABEL(detail_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_widget_set_hexpand(progress, TRUE);
    gtk_widget_set_size_request(progress, 180, -1);
    gtk_widget_set_visible(progress, active);

    gtk_widget_set_tooltip_text(action, active ? "Cancel download" : "Remove from download history");
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(more), "view-more-symbolic");
    gtk_widget_set_tooltip_text(more, "Download actions");
    gtk_widget_set_halign(more, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(more, GTK_ALIGN_CENTER);

    gtk_widget_set_margin_top(actions, 6);
    gtk_widget_set_margin_bottom(actions, 6);
    gtk_widget_set_margin_start(actions, 6);
    gtk_widget_set_margin_end(actions, 6);
    gtk_box_append(GTK_BOX(actions), open);
    gtk_box_append(GTK_BOX(actions), folder);
    gtk_box_append(GTK_BOX(actions), copy_link);
    gtk_box_append(GTK_BOX(actions), retry);
    gtk_popover_set_child(GTK_POPOVER(popover), actions);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(more), popover);

    gtk_box_append(GTK_BOX(labels), name_label);
    gtk_box_append(GTK_BOX(labels), detail_label);
    gtk_box_append(GTK_BOX(row), labels);
    gtk_box_append(GTK_BOX(row), progress);
    gtk_box_append(GTK_BOX(row), action);
    gtk_box_append(GTK_BOX(row), more);

    item->row = row;
    item->name_label = name_label;
    item->detail_label = detail_label;
    item->progress_bar = progress;
    item->action_button = action;
    item->more_button = more;
    item->open_button = open;
    item->folder_button = folder;
    item->copy_link_button = copy_link;
    item->retry_button = retry;

    g_signal_connect(open, "clicked", G_CALLBACK(on_download_open_clicked), item);
    g_signal_connect(folder, "clicked", G_CALLBACK(on_download_folder_clicked), item);
    g_signal_connect(copy_link, "clicked", G_CALLBACK(on_download_copy_link_clicked), item);
    g_signal_connect(retry, "clicked", G_CALLBACK(on_download_retry_clicked), item);

    nion_download_update_actions(item);
    g_object_set_data_full(G_OBJECT(row), "nion-download", item, nion_download_free);
    return row;
}

void nion_save_download_history(NionApp *app)
{
    if (!app || app->is_private || !app->downloads_file || !app->downloads_list)
        return;

    GKeyFile *key_file = g_key_file_new();
    guint index = 0;
    for (GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (!item || (!item->finished && !item->failed))
            continue;
        if (index >= NION_MAX_DOWNLOAD_HISTORY)
            break;

        gchar *group = g_strdup_printf("Download-%u", index++);
        g_key_file_set_string(key_file, group, "name",
                              (item->filename && *item->filename) ? item->filename : "Download");
        g_key_file_set_string(key_file, group, "destination",
                              item->destination ? item->destination : "");
        g_key_file_set_string(key_file, group, "source-uri",
                              item->source_uri ? item->source_uri : "");
        g_key_file_set_string(key_file, group, "status",
                              item->history_status ? item->history_status : (item->finished ? "Completed" : "Failed"));
        g_key_file_set_string(key_file, group, "detail",
                              item->history_detail ? item->history_detail : "");
        g_key_file_set_int64(key_file, group, "time", item->history_time);
        g_free(group);
    }

    g_key_file_set_integer(key_file, "History", "count", (gint)index);
    nion_write_key_file_atomic(key_file, app->downloads_file);
    g_key_file_free(key_file);
}

static void nion_download_remove_row(NionDownload *item)
{
    if (!item || !item->row)
        return;

    NionApp *app = item->app;
    GtkWidget *row = item->row;
    item->row = NULL;
    gtk_box_remove(GTK_BOX(app->downloads_list), row);
    nion_download_update_panel_visibility(app);
    nion_save_download_history(app);
}

static void on_download_action_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionDownload *item = user_data;
    if (!item)
        return;

    if (!item->finished && !item->failed && item->download) {
        item->cancel_requested = TRUE;
        gtk_widget_set_sensitive(item->action_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(item->action_button), "Cancelling…");
        webkit_download_cancel(item->download);
        return;
    }

    nion_download_remove_row(item);
}

static gboolean on_download_decide_destination(WebKitDownload *download,
                                               const gchar *suggested_filename,
                                               gpointer user_data)
{
    NionDownload *item = user_data;
    NionApp *app = item->app;

    if (!app->tor_ready) {
        item->cancel_requested = TRUE;
        webkit_download_cancel(download);
        return TRUE;
    }

    gchar *destination = nion_unique_download_path(app, suggested_filename);
    if (!destination) {
        webkit_download_cancel(download);
        return TRUE;
    }

    g_free(item->destination);
    item->destination = destination;
    g_free(item->filename);
    item->filename = g_path_get_basename(destination);

    gtk_label_set_text(GTK_LABEL(item->name_label), item->filename);
    gtk_widget_set_tooltip_text(item->name_label, destination);
    nion_download_update_actions(item);
    webkit_download_set_allow_overwrite(download, FALSE);
    webkit_download_set_destination(download, destination);
    return TRUE;
}

static void on_download_progress_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    (void)pspec;
    NionDownload *item = user_data;
    WebKitDownload *download = WEBKIT_DOWNLOAD(object);

    gdouble progress = webkit_download_get_estimated_progress(download);
    if (progress < 0.0)
        progress = 0.0;
    if (progress > 1.0)
        progress = 1.0;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress_bar), progress);

    guint64 received = webkit_download_get_received_data_length(download);
    gchar *received_text = nion_format_bytes(received);
    gchar *detail = g_strdup_printf("%d%% · %s received",
                                    (gint)(progress * 100.0 + 0.5), received_text);
    gtk_label_set_text(GTK_LABEL(item->detail_label), detail);
    g_free(detail);
    g_free(received_text);
}

static void on_download_received_data(WebKitDownload *download,
                                      guint64 data_length,
                                      gpointer user_data)
{
    (void)data_length;
    NionDownload *item = user_data;
    on_download_progress_changed(G_OBJECT(download), NULL, item);
}

static void on_download_failed(WebKitDownload *download,
                               GError *error,
                               gpointer user_data)
{
    (void)download;
    NionDownload *item = user_data;
    item->failed = TRUE;

    if (item->destination && g_file_test(item->destination, G_FILE_TEST_IS_REGULAR))
        g_remove(item->destination);

    gtk_widget_set_sensitive(item->action_button, TRUE);
    gtk_button_set_label(GTK_BUTTON(item->action_button), "Remove");
    gtk_widget_set_visible(item->progress_bar, FALSE);

    if (item->cancel_requested) {
        nion_download_set_history(item, "Cancelled", "Cancelled");
        if (item->app->tor_ready)
            nion_set_status(item->app, "● TOR CONNECTED — DOWNLOAD CANCELLED");
    } else {
        gchar *detail = g_strdup_printf("Failed — %s",
                                        (error && error->message) ? error->message : "download interrupted");
        nion_download_set_history(item, "Failed", detail);
        g_free(detail);
        if (item->app->tor_ready)
            nion_set_status(item->app, "● TOR CONNECTED — DOWNLOAD FAILED");
    }
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    nion_save_download_history(item->app);
}

static void on_download_finished(WebKitDownload *download, gpointer user_data)
{
    (void)download;
    NionDownload *item = user_data;

    if (item->failed)
        return;

    item->finished = TRUE;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(item->progress_bar), 1.0);
    gtk_widget_set_visible(item->progress_bar, FALSE);
    gtk_widget_set_sensitive(item->action_button, TRUE);
    gtk_button_set_label(GTK_BUTTON(item->action_button), "Remove");

    guint64 received = webkit_download_get_received_data_length(item->download);
    gchar *size = nion_format_bytes(received);
    gchar *detail = g_strdup_printf("Completed · %s", size);
    nion_download_set_history(item, "Completed", detail);
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    g_free(detail);
    g_free(size);
    nion_save_download_history(item->app);

    const gchar *filename = (item->filename && *item->filename) ? item->filename : "Download";
    if (item->app->tor_ready) {
        gchar *status = g_strdup_printf("● TOR CONNECTED — DOWNLOAD COMPLETE: %s", filename);
        nion_set_status(item->app, status);
        g_free(status);
    }

    if (!item->app->is_private) {
        GNotification *notification = g_notification_new("NiOn download complete");
        g_notification_set_body(notification, filename);
        g_application_send_notification(G_APPLICATION(item->app->application), NULL, notification);
        g_object_unref(notification);
    }
}

void on_download_started(WebKitNetworkSession *session,
                                WebKitDownload *download,
                                gpointer user_data)
{
    (void)session;
    NionApp *app = user_data;

    if (!app->tor_ready) {
        webkit_download_cancel(download);
        nion_set_status(app, "○ TOR OFFLINE — DOWNLOAD BLOCKED");
        return;
    }

    NionDownload *item = g_new0(NionDownload, 1);
    item->app = app;
    item->download = g_object_ref(download);

    WebKitURIRequest *request = webkit_download_get_request(download);
    const gchar *request_uri = request ? webkit_uri_request_get_uri(request) : NULL;
    if (request_uri && *request_uri && strlen(request_uri) <= NION_MAX_SAVED_URI_BYTES)
        item->source_uri = g_strdup(request_uri);

    GtkWidget *row = nion_download_create_row(item, "Preparing download…", "Waiting for destination…", TRUE);

    g_signal_connect(item->action_button, "clicked", G_CALLBACK(on_download_action_clicked), item);
    g_signal_connect(download, "decide-destination", G_CALLBACK(on_download_decide_destination), item);
    g_signal_connect(download, "notify::estimated-progress", G_CALLBACK(on_download_progress_changed), item);
    g_signal_connect(download, "received-data", G_CALLBACK(on_download_received_data), item);
    g_signal_connect(download, "failed", G_CALLBACK(on_download_failed), item);
    g_signal_connect(download, "finished", G_CALLBACK(on_download_finished), item);

    gtk_box_prepend(GTK_BOX(app->downloads_list), row);
    nion_download_update_panel_visibility(app);
    nion_set_status(app, app->is_private
        ? "● TOR CONNECTED — PRIVATE DOWNLOAD STARTED (EPHEMERAL HISTORY)"
        : "● TOR CONNECTED — DOWNLOAD STARTED (Ctrl+J)");
}

static void nion_download_add_history_row(NionApp *app,
                                          const gchar *name,
                                          const gchar *destination,
                                          const gchar *source_uri,
                                          const gchar *status,
                                          const gchar *detail,
                                          gint64 timestamp)
{
    NionDownload *item = g_new0(NionDownload, 1);
    item->app = app;
    item->filename = g_strdup((name && *name) ? name : "Download");
    item->destination = g_strdup(destination ? destination : "");
    if (source_uri && *source_uri && strlen(source_uri) <= NION_MAX_SAVED_URI_BYTES)
        item->source_uri = g_strdup(source_uri);
    item->history_status = g_strdup(status ? status : "Unknown");
    item->history_detail = g_strdup(detail ? detail : "");
    item->history_time = timestamp;
    item->finished = g_strcmp0(status, "Completed") == 0;
    item->failed = !item->finished;

    GtkWidget *row = nion_download_create_row(item, item->filename, "", FALSE);
    if (item->destination && *item->destination)
        gtk_widget_set_tooltip_text(item->name_label, item->destination);
    nion_download_refresh_history_detail(item);
    nion_download_update_actions(item);
    g_signal_connect(item->action_button, "clicked", G_CALLBACK(on_download_action_clicked), item);
    gtk_box_append(GTK_BOX(app->downloads_list), row);
}

static void nion_load_download_history(NionApp *app)
{
    if (!app || app->is_private || !app->downloads_file || !app->downloads_list)
        return;

    if (g_file_test(app->downloads_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->downloads_file,
                                        NION_MAX_DOWNLOADS_FILE_BYTES)) {
        nion_quarantine_profile_file(app->downloads_file, "download history");
        nion_download_update_panel_visibility(app);
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->downloads_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn download history: %s", error->message);
            nion_quarantine_profile_file(app->downloads_file, "download history");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_download_update_panel_visibility(app);
        return;
    }

    error = NULL;
    gint count = g_key_file_get_integer(key_file, "History", "count", &error);
    if (error || count < 0 || count > NION_MAX_DOWNLOAD_HISTORY_INPUT) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->downloads_file, "download history");
        nion_download_update_panel_visibility(app);
        return;
    }
    if (count > NION_MAX_DOWNLOAD_HISTORY)
        count = NION_MAX_DOWNLOAD_HISTORY;

    for (gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Download-%d", i);
        gchar *name = g_key_file_get_string(key_file, group, "name", NULL);
        gchar *destination = g_key_file_get_string(key_file, group, "destination", NULL);
        gchar *source_uri = g_key_file_get_string(key_file, group, "source-uri", NULL);
        gchar *status = g_key_file_get_string(key_file, group, "status", NULL);
        gchar *detail = g_key_file_get_string(key_file, group, "detail", NULL);
        gint64 timestamp = g_key_file_get_int64(key_file, group, "time", NULL);
        if (name && status)
            nion_download_add_history_row(app, name, destination, source_uri, status, detail, timestamp);
        g_free(name);
        g_free(destination);
        g_free(source_uri);
        g_free(status);
        g_free(detail);
        g_free(group);
    }
    g_key_file_free(key_file);
    nion_download_update_panel_visibility(app);
}

static void on_clear_downloads_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app || !app->downloads_list)
        return;

    GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
    while (row) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (item && (item->finished || item->failed)) {
            item->row = NULL;
            gtk_box_remove(GTK_BOX(app->downloads_list), row);
        }
        row = next;
    }
    nion_download_update_panel_visibility(app);
    nion_save_download_history(app);
}

static gboolean on_downloads_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data;
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void nion_show_downloads(NionApp *app)
{
    if (!app || !app->downloads_window)
        return;
    gtk_window_present(GTK_WINDOW(app->downloads_window));
}

void nion_cancel_active_downloads(NionApp *app)
{
    if (!app->downloads_list)
        return;

    for (GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (!item || item->finished || item->failed || !item->download)
            continue;

        item->cancel_requested = TRUE;
        gtk_widget_set_sensitive(item->action_button, FALSE);
        gtk_button_set_label(GTK_BUTTON(item->action_button), "Cancelling…");
        webkit_download_cancel(item->download);
    }
}

void nion_private_cleanup_partial_downloads(NionApp *app)
{
    if (!app || !app->is_private || !app->downloads_list)
        return;

    for (GtkWidget *row = gtk_widget_get_first_child(app->downloads_list);
         row;
         row = gtk_widget_get_next_sibling(row)) {
        NionDownload *item = g_object_get_data(G_OBJECT(row), "nion-download");
        if (!item || item->finished || item->failed || !item->destination || !*item->destination)
            continue;
        if (g_file_test(item->destination, G_FILE_TEST_IS_REGULAR))
            g_remove(item->destination);
    }
}

void action_downloads(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    NionApp *app = user_data;
    nion_show_downloads(app);
}

void nion_build_downloads_window(NionApp *app)
{
    app->downloads_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(app->downloads_window),
                         app->is_private ? "Private Downloads — NiOn" : "Downloads — NiOn");
    gtk_window_set_transient_for(GTK_WINDOW(app->downloads_window), GTK_WINDOW(app->window));
    gtk_window_set_default_size(GTK_WINDOW(app->downloads_window), 760, 520);
    gtk_window_set_icon_name(GTK_WINDOW(app->downloads_window), NION_APP_ID);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(app->downloads_window), root);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(header, "nion-downloads-header");
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 12);
    gtk_widget_set_margin_start(header, 14);
    gtk_widget_set_margin_end(header, 14);

    GtkWidget *titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new(app->is_private ? "Private Downloads" : "Downloads");
    GtkWidget *path = gtk_label_new(app->download_dir);
    gtk_widget_add_css_class(title, "title-3");
    gtk_widget_add_css_class(path, "nion-muted");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(path), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(path), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_tooltip_text(path, app->download_dir);
    gtk_widget_set_hexpand(titles, TRUE);
    gtk_box_append(GTK_BOX(titles), title);
    gtk_box_append(GTK_BOX(titles), path);

    GtkWidget *clear = gtk_button_new_with_label("Clear Downloads");
    gtk_widget_set_valign(clear, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(clear, app->is_private
        ? "Remove entries from this private window only"
        : "Remove completed, failed and cancelled entries from history");
    gtk_box_append(GTK_BOX(header), titles);
    gtk_box_append(GTK_BOX(header), clear);
    gtk_box_append(GTK_BOX(root), header);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(root), separator);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, TRUE);
    gtk_box_append(GTK_BOX(root), overlay);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scroller);

    app->downloads_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(app->downloads_list, 8);
    gtk_widget_set_margin_bottom(app->downloads_list, 8);
    gtk_widget_set_margin_start(app->downloads_list, 14);
    gtk_widget_set_margin_end(app->downloads_list, 14);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), app->downloads_list);

    app->downloads_empty_label = gtk_label_new(app->is_private
        ? "No private downloads yet.\nHistory exists only while this Private Window is open."
        : "No downloads yet.\nDownloads made by NiOn will appear here.");
    gtk_label_set_justify(GTK_LABEL(app->downloads_empty_label), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(app->downloads_empty_label, "nion-muted");
    gtk_widget_set_halign(app->downloads_empty_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app->downloads_empty_label, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app->downloads_empty_label);

    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_downloads_clicked), app);
    g_signal_connect(app->downloads_window, "close-request",
                     G_CALLBACK(on_downloads_window_close_request), app);

    nion_load_download_history(app);
}
