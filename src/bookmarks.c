/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "navigation.h"
#include "bookmarks.h"

/* UI/tab operations that still live in main.c (Phase 6, NiOn 2.0.0).
 * main.c registers implementations via nion_bookmarks_set_callbacks() so
 * this module never calls into main.c directly. */
static NionBookmarkCallbacks cb;

/* Local forward declarations (definitions below use them before the
 * definition; main.c used to provide these at file scope). */
static void nion_refresh_bookmarks_window(NionApp *app);
static void nion_add_current_bookmark(NionApp *app);

static NionTab *
nion_current_tab(NionApp *app)
{
    return cb.current_tab ? cb.current_tab(app) : NULL;
}

static NionTab *
nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return cb.new_tab ? cb.new_tab(app, uri, select) : NULL;
}

static void
nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

void
nion_bookmarks_set_callbacks(const NionBookmarkCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

void nion_bookmark_free(gpointer data)
{
    NionBookmark *bookmark = data;
    if (!bookmark)
        return;
    g_free(bookmark->title);
    g_free(bookmark->uri);
    g_free(bookmark);
}

static gchar *nion_bookmark_title_normalize(const gchar *title, const gchar *uri)
{
    const gchar *source = (title && *title) ? title : uri;
    gchar *clean = g_strdup(source && *source ? source : "Bookmark");
    g_strstrip(clean);
    if (!*clean) {
        g_free(clean);
        clean = g_strdup(uri && *uri ? uri : "Bookmark");
    }

    if (g_utf8_validate(clean, -1, NULL)) {
        glong chars = g_utf8_strlen(clean, -1);
        if (chars > NION_MAX_BOOKMARK_TITLE_CHARS) {
            gchar *shortened = g_utf8_substring(clean, 0, NION_MAX_BOOKMARK_TITLE_CHARS);
            g_free(clean);
            clean = shortened;
        }
    } else if (strlen(clean) > 1024) {
        gchar *shortened = g_strndup(clean, 1024);
        g_free(clean);
        clean = shortened;
    }
    return clean;
}

static gboolean nion_bookmark_uri_exists(NionApp *app, const gchar *uri)
{
    if (!app || !app->bookmarks || !uri)
        return FALSE;
    for (guint i = 0; i < app->bookmarks->len; i++) {
        NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
        if (bookmark && g_strcmp0(bookmark->uri, uri) == 0)
            return TRUE;
    }
    return FALSE;
}

static gint nion_bookmark_index(NionApp *app, NionBookmark *bookmark)
{
    if (!app || !app->bookmarks || !bookmark)
        return -1;
    for (guint i = 0; i < app->bookmarks->len; i++) {
        if (g_ptr_array_index(app->bookmarks, i) == bookmark)
            return (gint)i;
    }
    return -1;
}

void nion_save_bookmarks(NionApp *app)
{
    if (!app || !app->bookmarks_file || !app->bookmarks)
        return;

    GKeyFile *key_file = g_key_file_new();
    guint count = MIN(app->bookmarks->len, (guint)NION_MAX_BOOKMARKS);
    g_key_file_set_integer(key_file, "Bookmarks", "count", (gint)count);

    for (guint i = 0; i < count; i++) {
        NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
        if (!bookmark || !bookmark->uri)
            continue;
        gchar *group = g_strdup_printf("Bookmark-%u", i);
        g_key_file_set_string(key_file, group, "title",
                              bookmark->title ? bookmark->title : bookmark->uri);
        g_key_file_set_string(key_file, group, "uri", bookmark->uri);
        g_free(group);
    }

    nion_write_key_file_atomic(key_file, app->bookmarks_file);
    g_key_file_free(key_file);
}

void nion_load_bookmarks(NionApp *app)
{
    if (!app)
        return;

    if (app->bookmarks)
        g_ptr_array_unref(app->bookmarks);
    app->bookmarks = g_ptr_array_new_with_free_func(nion_bookmark_free);

    if (!app->bookmarks_file)
        return;

    if (g_file_test(app->bookmarks_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->bookmarks_file,
                                        NION_MAX_BOOKMARKS_FILE_BYTES)) {
        nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->bookmarks_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn bookmarks: %s", error->message);
            nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    error = NULL;
    gint count = g_key_file_get_integer(key_file, "Bookmarks", "count", &error);
    if (error || count < 0 || count > NION_MAX_BOOKMARKS_INPUT) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->bookmarks_file, "bookmarks");
        return;
    }
    if (count > NION_MAX_BOOKMARKS)
        count = NION_MAX_BOOKMARKS;

    for (gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Bookmark-%d", i);
        gchar *title = g_key_file_get_string(key_file, group, "title", NULL);
        gchar *uri = g_key_file_get_string(key_file, group, "uri", NULL);
        g_free(group);

        gchar *validation = NULL;
        gboolean valid = uri && *uri && strlen(uri) <= NION_MAX_SAVED_URI_BYTES &&
                         nion_validate_uri(uri, &validation);
        g_free(validation);
        if (valid && !nion_bookmark_uri_exists(app, uri)) {
            NionBookmark *bookmark = g_new0(NionBookmark, 1);
            bookmark->uri = g_strdup(uri);
            bookmark->title = nion_bookmark_title_normalize(title, uri);
            g_ptr_array_add(app->bookmarks, bookmark);
        }
        g_free(title);
        g_free(uri);
    }

    g_key_file_free(key_file);
}

static void nion_open_bookmark(NionApp *app, NionBookmark *bookmark)
{
    if (!app || !bookmark || !bookmark->uri)
        return;
    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — BOOKMARK OPEN BLOCKED");
        return;
    }
    nion_new_tab(app, bookmark->uri, TRUE);
}

static void on_bookmark_open_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    nion_open_bookmark(app, bookmark);
}

static void on_bookmark_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    (void)box;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(row), "nion-bookmark");
    nion_open_bookmark(user_data, bookmark);
}

static void on_bookmark_rename_cancel(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void on_bookmark_rename_save(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (!root || !GTK_IS_WINDOW(root))
        return;

    GtkWindow *window = GTK_WINDOW(root);
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(window), "nion-bookmark");
    GtkEntry *entry = g_object_get_data(G_OBJECT(window), "nion-title-entry");
    if (!bookmark || !entry || nion_bookmark_index(app, bookmark) < 0) {
        gtk_window_destroy(window);
        return;
    }

    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    gchar *title = nion_bookmark_title_normalize(text, bookmark->uri);
    g_free(bookmark->title);
    bookmark->title = title;
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK RENAMED");
    gtk_window_destroy(window);
}

static void on_bookmark_rename_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    if (!bookmark || nion_bookmark_index(app, bookmark) < 0)
        return;

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Rename Bookmark");
    gtk_window_set_transient_for(GTK_WINDOW(window),
        app->bookmarks_window ? GTK_WINDOW(app->bookmarks_window) : GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 460, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading), "<b>Rename bookmark</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), bookmark->title ? bookmark->title : bookmark->uri);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *uri = gtk_label_new(bookmark->uri);
    gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(uri, "nion-muted");
    gtk_box_append(GTK_BOX(box), uri);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel");
    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(save, "suggested-action");
    gtk_box_append(GTK_BOX(buttons), cancel);
    gtk_box_append(GTK_BOX(buttons), save);
    gtk_box_append(GTK_BOX(box), buttons);

    g_object_set_data(G_OBJECT(window), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(window), "nion-title-entry", entry);
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_bookmark_rename_cancel), app);
    g_signal_connect(save, "clicked", G_CALLBACK(on_bookmark_rename_save), app);
    g_signal_connect_swapped(entry, "activate", G_CALLBACK(gtk_widget_activate), save);

    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(entry);
}

static void on_bookmark_delete_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionBookmark *bookmark = g_object_get_data(G_OBJECT(button), "nion-bookmark");
    gint index = nion_bookmark_index(app, bookmark);
    if (index < 0)
        return;

    g_ptr_array_remove_index(app->bookmarks, (guint)index);
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    nion_update_bookmark_button(app);
    if (app->tor_ready)
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK REMOVED");
}

static gchar *nion_bookmark_search_fold(const gchar *text)
{
    if (!text || !*text)
        return g_strdup("");
    if (g_utf8_validate(text, -1, NULL))
        return g_utf8_casefold(text, -1);
    return g_ascii_strdown(text, -1);
}

static gboolean nion_bookmark_matches_search(NionBookmark *bookmark, const gchar *query)
{
    if (!bookmark)
        return FALSE;
    if (!query || !*query)
        return TRUE;

    gchar *query_copy = g_strdup(query);
    g_strstrip(query_copy);
    if (!*query_copy) {
        g_free(query_copy);
        return TRUE;
    }

    gchar *query_fold = nion_bookmark_search_fold(query_copy);
    gchar *title_fold = nion_bookmark_search_fold(bookmark->title);
    gchar *uri_fold = nion_bookmark_search_fold(bookmark->uri);
    gchar **terms = g_strsplit_set(query_fold, " \t\r\n", -1);
    gboolean matches = TRUE;

    for (guint i = 0; terms && terms[i]; i++) {
        const gchar *term = terms[i];
        if (!term || !*term)
            continue;
        if (!g_strstr_len(title_fold, -1, term) &&
            !g_strstr_len(uri_fold, -1, term)) {
            matches = FALSE;
            break;
        }
    }

    g_strfreev(terms);
    g_free(uri_fold);
    g_free(title_fold);
    g_free(query_fold);
    g_free(query_copy);
    return matches;
}

static GtkWidget *nion_bookmark_row_new(NionApp *app, NionBookmark *bookmark)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);

    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(labels, TRUE);
    GtkWidget *title = gtk_label_new(bookmark->title ? bookmark->title : bookmark->uri);
    GtkWidget *uri = gtk_label_new(bookmark->uri ? bookmark->uri : "");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(title, "heading");
    gtk_widget_add_css_class(uri, "nion-muted");
    gtk_box_append(GTK_BOX(labels), title);
    gtk_box_append(GTK_BOX(labels), uri);

    GtkWidget *open = gtk_button_new_with_label("Open");
    GtkWidget *rename = gtk_button_new_with_label("Rename");
    GtkWidget *remove = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(remove, "destructive-action");
    gtk_widget_set_valign(open, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(rename, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);

    g_object_set_data(G_OBJECT(open), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(rename), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(remove), "nion-bookmark", bookmark);
    g_object_set_data(G_OBJECT(row), "nion-bookmark", bookmark);
    g_signal_connect(open, "clicked", G_CALLBACK(on_bookmark_open_clicked), app);
    g_signal_connect(rename, "clicked", G_CALLBACK(on_bookmark_rename_clicked), app);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_bookmark_delete_clicked), app);

    gtk_box_append(GTK_BOX(content), labels);
    gtk_box_append(GTK_BOX(content), open);
    gtk_box_append(GTK_BOX(content), rename);
    gtk_box_append(GTK_BOX(content), remove);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);
    return row;
}

static void nion_refresh_bookmarks_window(NionApp *app)
{
    if (!app || !app->bookmarks_list)
        return;

    const gchar *query_text = app->bookmarks_search_entry
        ? gtk_editable_get_text(GTK_EDITABLE(app->bookmarks_search_entry))
        : "";
    gchar *query = g_strdup(query_text ? query_text : "");
    g_strstrip(query);
    gboolean searching = *query != '\0';
    guint total = app->bookmarks ? app->bookmarks->len : 0;
    guint matches = 0;

    GtkWidget *row = gtk_widget_get_first_child(app->bookmarks_list);
    while (row) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        gtk_list_box_remove(GTK_LIST_BOX(app->bookmarks_list), row);
        row = next;
    }

    if (app->bookmarks) {
        for (guint i = 0; i < app->bookmarks->len; i++) {
            NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
            if (bookmark && nion_bookmark_matches_search(bookmark, query)) {
                gtk_list_box_append(GTK_LIST_BOX(app->bookmarks_list),
                                    nion_bookmark_row_new(app, bookmark));
                matches++;
            }
        }
    }

    if (app->bookmarks_empty_label) {
        if (total == 0)
            gtk_label_set_text(GTK_LABEL(app->bookmarks_empty_label),
                               "No bookmarks yet. Press Ctrl+D on a website to add one.");
        else if (searching && matches == 0)
            gtk_label_set_text(GTK_LABEL(app->bookmarks_empty_label),
                               "No bookmarks match your search.");
        gtk_widget_set_visible(app->bookmarks_empty_label, matches == 0);
    }

    if (app->bookmarks_result_label) {
        gchar *summary = NULL;
        if (searching)
            summary = g_strdup_printf("%u of %u", matches, total);
        else
            summary = g_strdup_printf("%u bookmark%s", total, total == 1 ? "" : "s");
        gtk_label_set_text(GTK_LABEL(app->bookmarks_result_label), summary);
        g_free(summary);
    }
    g_free(query);
}

static void on_bookmarks_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    (void)entry;
    nion_refresh_bookmarks_window(user_data);
}

static gboolean on_bookmarks_window_close_request(GtkWindow *window, gpointer user_data)
{
    NionApp *app = user_data;
    if (app && app->bookmarks_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->bookmarks_search_entry), "");
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void on_bookmarks_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (app && app->bookmarks_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->bookmarks_search_entry), "");
    if (app && app->bookmarks_window)
        gtk_widget_set_visible(app->bookmarks_window, FALSE);
}

static const gchar *nion_current_bookmarkable_uri(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page)
        return NULL;

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    if (!uri || !*uri || g_str_equal(uri, "about:blank") ||
        strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return NULL;

    gchar *validation = NULL;
    gboolean valid = nion_validate_uri(uri, &validation);
    g_free(validation);
    return valid ? uri : NULL;
}

void nion_update_bookmark_button(NionApp *app)
{
    if (!app || !app->bookmark_button)
        return;

    const gchar *uri = nion_current_bookmarkable_uri(app);
    gboolean bookmarkable = uri != NULL;
    gboolean bookmarked = bookmarkable && nion_bookmark_uri_exists(app, uri);

    gtk_widget_set_sensitive(app->bookmark_button, bookmarkable);
    gtk_button_set_icon_name(GTK_BUTTON(app->bookmark_button),
                             bookmarked ? "starred-symbolic" : "non-starred-symbolic");

    gtk_widget_remove_css_class(app->bookmark_button, "nion-bookmark-active");
    if (bookmarked)
        gtk_widget_add_css_class(app->bookmark_button, "nion-bookmark-active");

    if (!bookmarkable)
        gtk_widget_set_tooltip_text(app->bookmark_button, "This page cannot be bookmarked");
    else if (bookmarked)
        gtk_widget_set_tooltip_text(app->bookmark_button, "Remove this page from bookmarks");
    else
        gtk_widget_set_tooltip_text(app->bookmark_button, "Bookmark this page (Ctrl+D)");
}

static void nion_toggle_current_bookmark(NionApp *app)
{
    const gchar *uri = nion_current_bookmarkable_uri(app);
    if (!uri) {
        nion_update_bookmark_button(app);
        return;
    }

    if (nion_bookmark_uri_exists(app, uri)) {
        for (guint i = 0; app->bookmarks && i < app->bookmarks->len; i++) {
            NionBookmark *bookmark = g_ptr_array_index(app->bookmarks, i);
            if (bookmark && g_strcmp0(bookmark->uri, uri) == 0) {
                g_ptr_array_remove_index(app->bookmarks, i);
                nion_save_bookmarks(app);
                nion_refresh_bookmarks_window(app);
                nion_set_status(app, app->tor_ready
                    ? "● TOR CONNECTED — BOOKMARK REMOVED"
                    : "○ TOR NOT READY — BOOKMARK REMOVED");
                nion_update_bookmark_button(app);
                return;
            }
        }
    }

    nion_add_current_bookmark(app);
    nion_update_bookmark_button(app);
}

void on_bookmark_toolbar_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_toggle_current_bookmark(user_data);
}

static void nion_add_current_bookmark(NionApp *app)
{
    NionTab *tab = nion_current_tab(app);
    if (!tab || tab->home_page || tab->error_page) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — NOTHING TO BOOKMARK"
            : "○ TOR NOT READY — NOTHING TO BOOKMARK");
        return;
    }

    const gchar *uri = webkit_web_view_get_uri(tab->web_view);
    gchar *validation = NULL;
    if (!uri || !*uri || strlen(uri) > NION_MAX_SAVED_URI_BYTES ||
        !nion_validate_uri(uri, &validation)) {
        g_free(validation);
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — THIS PAGE CANNOT BE BOOKMARKED"
            : "○ TOR NOT READY — THIS PAGE CANNOT BE BOOKMARKED");
        return;
    }
    g_free(validation);

    if (!app->bookmarks)
        app->bookmarks = g_ptr_array_new_with_free_func(nion_bookmark_free);
    if (nion_bookmark_uri_exists(app, uri)) {
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — ALREADY BOOKMARKED"
            : "○ TOR NOT READY — ALREADY BOOKMARKED");
        return;
    }
    if (app->bookmarks->len >= NION_MAX_BOOKMARKS) {
        nion_set_status(app, "● TOR CONNECTED — BOOKMARK LIMIT REACHED");
        return;
    }

    NionBookmark *bookmark = g_new0(NionBookmark, 1);
    bookmark->uri = g_strdup(uri);
    bookmark->title = nion_bookmark_title_normalize(
        webkit_web_view_get_title(tab->web_view), uri);
    g_ptr_array_add(app->bookmarks, bookmark);
    nion_save_bookmarks(app);
    nion_refresh_bookmarks_window(app);
    nion_update_bookmark_button(app);
    nion_set_status(app, app->tor_ready
        ? "● TOR CONNECTED — BOOKMARK ADDED"
        : "○ TOR NOT READY — BOOKMARK ADDED");
}

static void on_bookmarks_add_current_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_add_current_bookmark(user_data);
}

static void nion_show_bookmarks(NionApp *app)
{
    if (!app)
        return;

    if (!app->bookmarks_window) {
        GtkWidget *window = gtk_window_new();
        app->bookmarks_window = window;
        gtk_window_set_title(GTK_WINDOW(window), "NiOn Bookmarks");
        gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
        gtk_window_set_default_size(GTK_WINDOW(window), 720, 520);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(root, 16);
        gtk_widget_set_margin_bottom(root, 16);
        gtk_widget_set_margin_start(root, 16);
        gtk_widget_set_margin_end(root, 16);
        gtk_window_set_child(GTK_WINDOW(window), root);

        GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *heading = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(heading), "<b>Bookmarks</b>");
        gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
        gtk_widget_set_hexpand(heading, TRUE);
        GtkWidget *add = gtk_button_new_with_label("Bookmark Current Page");
        gtk_box_append(GTK_BOX(header), heading);
        gtk_box_append(GTK_BOX(header), add);
        gtk_box_append(GTK_BOX(root), header);

        GtkWidget *note = gtk_label_new(
            "Bookmarks are stored locally in your NiOn profile. Double-click a row or use Open to launch it in a new tab.");
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_add_css_class(note, "nion-muted");
        gtk_box_append(GTK_BOX(root), note);

        GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        app->bookmarks_search_entry = gtk_search_entry_new();
        g_object_set(app->bookmarks_search_entry,
                     "placeholder-text", "Search bookmarks by title or URL",
                     NULL);
        gtk_widget_set_hexpand(app->bookmarks_search_entry, TRUE);
        gtk_search_entry_set_key_capture_widget(
            GTK_SEARCH_ENTRY(app->bookmarks_search_entry), window);
        app->bookmarks_result_label = gtk_label_new("0 bookmarks");
        gtk_widget_add_css_class(app->bookmarks_result_label, "nion-muted");
        gtk_widget_set_valign(app->bookmarks_result_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(search_row), app->bookmarks_search_entry);
        gtk_box_append(GTK_BOX(search_row), app->bookmarks_result_label);
        gtk_box_append(GTK_BOX(root), search_row);

        app->bookmarks_empty_label = gtk_label_new("No bookmarks yet. Press Ctrl+D on a website to add one.");
        gtk_widget_set_margin_top(app->bookmarks_empty_label, 28);
        gtk_widget_set_margin_bottom(app->bookmarks_empty_label, 28);
        gtk_widget_add_css_class(app->bookmarks_empty_label, "nion-muted");

        app->bookmarks_list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->bookmarks_list), GTK_SELECTION_SINGLE);
        gtk_list_box_set_show_separators(GTK_LIST_BOX(app->bookmarks_list), TRUE);
        gtk_list_box_set_placeholder(GTK_LIST_BOX(app->bookmarks_list), app->bookmarks_empty_label);
        gtk_widget_add_css_class(app->bookmarks_list, "boxed-list");

        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->bookmarks_list);
        gtk_box_append(GTK_BOX(root), scroll);

        GtkWidget *close = gtk_button_new_with_label("Close");
        gtk_widget_set_halign(close, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(root), close);

        g_signal_connect(add, "clicked", G_CALLBACK(on_bookmarks_add_current_clicked), app);
        g_signal_connect(close, "clicked", G_CALLBACK(on_bookmarks_close_clicked), app);
        g_signal_connect(app->bookmarks_search_entry, "search-changed",
                         G_CALLBACK(on_bookmarks_search_changed), app);
        g_signal_connect(app->bookmarks_list, "row-activated", G_CALLBACK(on_bookmark_row_activated), app);
        g_signal_connect(window, "close-request", G_CALLBACK(on_bookmarks_window_close_request), app);
    }

    nion_refresh_bookmarks_window(app);
    gtk_window_present(GTK_WINDOW(app->bookmarks_window));
}

void action_bookmark_page(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_add_current_bookmark(user_data);
}

void action_bookmarks(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_bookmarks(user_data);
}
