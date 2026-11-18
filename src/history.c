/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "history.h"

/* Local browsing history (v2.3.0, roadmap T1-1).
 *
 * Storage is a GKeyFile written atomically, the same pattern bookmarks and
 * downloads already use, so a half-written file can never replace a good one.
 * Everything below is deliberately boring: no index, no database, no async. A
 * two thousand entry list is small enough that a full rewrite costs nothing.
 */

static NionHistoryCallbacks cb;

/* Defined with the UI at the bottom, used by the deletion paths above so the
 * window never shows an entry that no longer exists. */
static void nion_history_refresh_window(NionApp *app);

void
nion_history_set_callbacks(const NionHistoryCallbacks *callbacks)
{
    if (callbacks)
        cb = *callbacks;
}

static void
nion_set_status(NionApp *app, const gchar *text)
{
    if (cb.set_status)
        cb.set_status(app, text);
}

static NionTab *
nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return cb.new_tab ? cb.new_tab(app, uri, select) : NULL;
}

void
nion_history_free_entry(gpointer data)
{
    NionHistoryEntry *entry = data;
    if (!entry)
        return;
    g_free(entry->title);
    g_free(entry->uri);
    g_free(entry);
}

/* ---- Model ---- */

static gchar *
nion_history_host_of(const gchar *uri)
{
    if (!uri || !*uri)
        return NULL;

    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!parsed)
        return NULL;

    const gchar *host = g_uri_get_host(parsed);
    gchar *copy = (host && *host) ? g_ascii_strdown(host, -1) : NULL;
    g_uri_unref(parsed);
    return copy;
}

gboolean
nion_history_uri_is_recordable(const gchar *uri)
{
    if (!uri || !*uri)
        return FALSE;
    if (strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return FALSE;
    if (!g_utf8_validate(uri, -1, NULL))
        return FALSE;

    /* A control byte would break the key file line format, and a bare newline in
     * a stored URL is never legitimate. Reject rather than escape and hope. */
    for (const guchar *p = (const guchar *)uri; *p; p++) {
        if (*p < 0x20 || *p == 0x7f)
            return FALSE;
    }

    GUri *parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!parsed)
        return FALSE;

    const gchar *scheme = g_uri_get_scheme(parsed);
    const gchar *host = g_uri_get_host(parsed);
    gboolean allowed = scheme && host && *host &&
        (g_ascii_strcasecmp(scheme, "http") == 0 ||
         g_ascii_strcasecmp(scheme, "https") == 0);
    g_uri_unref(parsed);
    return allowed;
}

gchar *
nion_history_normalize_title(const gchar *title, const gchar *uri)
{
    const gchar *source = (title && *title) ? title : uri;
    gchar *clean = g_strdup(source && *source ? source : "Untitled");
    g_strstrip(clean);

    if (!*clean) {
        g_free(clean);
        clean = g_strdup(uri && *uri ? uri : "Untitled");
    }

    if (g_utf8_validate(clean, -1, NULL)) {
        if (g_utf8_strlen(clean, -1) > NION_MAX_HISTORY_TITLE_CHARS) {
            gchar *shortened = g_utf8_substring(clean, 0, NION_MAX_HISTORY_TITLE_CHARS);
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

NionHistoryEntry *
nion_history_entry_new(const gchar *uri, const gchar *title, gint64 last_visit)
{
    NionHistoryEntry *entry = g_new0(NionHistoryEntry, 1);
    entry->uri = g_strdup(uri);
    entry->title = nion_history_normalize_title(title, uri);
    entry->last_visit = last_visit;
    entry->visits = 1;
    return entry;
}

static gint
nion_history_compare_newest_first(gconstpointer a, gconstpointer b)
{
    const NionHistoryEntry *left = *(NionHistoryEntry * const *)a;
    const NionHistoryEntry *right = *(NionHistoryEntry * const *)b;

    gint64 left_time = left ? left->last_visit : 0;
    gint64 right_time = right ? right->last_visit : 0;
    if (left_time != right_time)
        return left_time > right_time ? -1 : 1;
    return 0;
}

guint
nion_history_prune(GPtrArray *entries, gint64 now, guint max_entries)
{
    if (!entries)
        return 0;

    guint removed = 0;
    const gint64 retention = (gint64)NION_HISTORY_RETENTION_DAYS * 86400;

    /* Walk backwards so removing an index never shifts an index we still read. */
    for (gint i = (gint)entries->len - 1; i >= 0; i--) {
        NionHistoryEntry *entry = g_ptr_array_index(entries, (guint)i);
        gboolean unusable = !entry || !entry->uri || !*entry->uri || entry->last_visit <= 0;
        gboolean expired = entry && entry->last_visit > 0 &&
                           now > 0 && entry->last_visit + retention < now;
        if (unusable || expired) {
            g_ptr_array_remove_index(entries, (guint)i);
            removed++;
        }
    }

    if (max_entries > 0) {
        while (entries->len > max_entries) {
            g_ptr_array_remove_index(entries, entries->len - 1);
            removed++;
        }
    }
    return removed;
}

guint
nion_history_forget_host(GPtrArray *entries, const gchar *host)
{
    if (!entries || !host || !*host)
        return 0;

    gchar *wanted = g_ascii_strdown(host, -1);
    guint removed = 0;

    for (gint i = (gint)entries->len - 1; i >= 0; i--) {
        NionHistoryEntry *entry = g_ptr_array_index(entries, (guint)i);
        gchar *entry_host = entry ? nion_history_host_of(entry->uri) : NULL;
        if (entry_host && g_strcmp0(entry_host, wanted) == 0) {
            g_ptr_array_remove_index(entries, (guint)i);
            removed++;
        }
        g_free(entry_host);
    }

    g_free(wanted);
    return removed;
}

/* ---- Persistence ---- */

void
nion_history_load(NionApp *app)
{
    if (!app)
        return;

    if (app->history)
        g_ptr_array_unref(app->history);
    app->history = g_ptr_array_new_with_free_func(nion_history_free_entry);

    if (!app->history_file)
        return;

    if (g_file_test(app->history_file, G_FILE_TEST_EXISTS) &&
        !nion_profile_file_within_limit(app->history_file,
                                        NION_MAX_HISTORY_FILE_BYTES)) {
        g_warning("NiOn history file is larger than %d bytes; quarantining it",
                  NION_MAX_HISTORY_FILE_BYTES);
        nion_quarantine_profile_file(app->history_file, "history");
        return;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, app->history_file, G_KEY_FILE_NONE, &error)) {
        if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_warning("Could not load NiOn history: %s", error->message);
            nion_quarantine_profile_file(app->history_file, "history");
        }
        g_clear_error(&error);
        g_key_file_free(key_file);
        return;
    }

    error = NULL;
    gint count = g_key_file_get_integer(key_file, "History", "count", &error);
    if (error || count < 0 || count > NION_MAX_HISTORY_INPUT) {
        g_clear_error(&error);
        g_key_file_free(key_file);
        nion_quarantine_profile_file(app->history_file, "history");
        return;
    }

    for (gint i = 0; i < count; i++) {
        gchar *group = g_strdup_printf("Entry-%d", i);
        gchar *uri = g_key_file_get_string(key_file, group, "uri", NULL);
        gchar *title = g_key_file_get_string(key_file, group, "title", NULL);
        gint64 last_visit = g_key_file_get_int64(key_file, group, "last_visit", NULL);
        guint visits = (guint)g_key_file_get_integer(key_file, group, "visits", NULL);
        g_free(group);

        if (nion_history_uri_is_recordable(uri)) {
            NionHistoryEntry *entry = nion_history_entry_new(uri, title, last_visit);
            entry->visits = visits > 0 ? visits : 1;
            g_ptr_array_add(app->history, entry);
        }
        g_free(title);
        g_free(uri);
    }

    g_key_file_free(key_file);

    g_ptr_array_sort(app->history, nion_history_compare_newest_first);
    if (nion_history_prune(app->history, g_get_real_time() / G_USEC_PER_SEC,
                           NION_MAX_HISTORY) > 0)
        nion_history_save(app);
}

void
nion_history_save(NionApp *app)
{
    /* Fail closed: a Private Window has no history path, and even if one were
     * assigned by mistake this guard keeps private browsing out of the file. */
    if (!app || app->is_private || !app->history_file || !app->history)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "History", "format", NION_HISTORY_FORMAT);
    guint count = MIN(app->history->len, (guint)NION_MAX_HISTORY);
    g_key_file_set_integer(key_file, "History", "count", (gint)count);

    for (guint i = 0; i < count; i++) {
        NionHistoryEntry *entry = g_ptr_array_index(app->history, i);
        if (!entry || !entry->uri)
            continue;
        gchar *group = g_strdup_printf("Entry-%u", i);
        g_key_file_set_string(key_file, group, "uri", entry->uri);
        g_key_file_set_string(key_file, group, "title",
                              entry->title ? entry->title : entry->uri);
        g_key_file_set_int64(key_file, group, "last_visit", entry->last_visit);
        g_key_file_set_integer(key_file, group, "visits", (gint)entry->visits);
        g_free(group);
    }

    if (!nion_write_key_file_atomic(key_file, app->history_file))
        g_warning("Could not save NiOn history to %s", app->history_file);
    g_key_file_free(key_file);
}

/* ---- Recording ---- */

void
nion_history_record(NionApp *app, const gchar *uri, const gchar *title)
{
    if (!app || app->is_private || !app->history_file)
        return;
    if (!nion_history_uri_is_recordable(uri))
        return;

    if (!app->history)
        app->history = g_ptr_array_new_with_free_func(nion_history_free_entry);

    const gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    gchar *clean_title = nion_history_normalize_title(title, uri);
    NionHistoryEntry *found = NULL;
    gint found_index = -1;

    for (guint i = 0; i < app->history->len; i++) {
        NionHistoryEntry *entry = g_ptr_array_index(app->history, i);
        if (entry && g_strcmp0(entry->uri, uri) == 0) {
            found = entry;
            found_index = (gint)i;
            break;
        }
    }

    if (found) {
        /* steal_index, not remove_index: remove_index would run the free func
         * and leave a dangling pointer in `found`. */
        g_ptr_array_steal_index(app->history, (guint)found_index);
        found->last_visit = now;
        if (found->visits < G_MAXUINT)
            found->visits += 1;
        g_free(found->title);
        found->title = clean_title;
        clean_title = NULL;
        g_ptr_array_insert(app->history, 0, found);
    } else {
        g_ptr_array_insert(app->history, 0,
                           nion_history_entry_new(uri, clean_title, now));
    }

    g_free(clean_title);
    nion_history_prune(app->history, now, NION_MAX_HISTORY);
    nion_history_save(app);
}

void
nion_history_upgrade_title(NionApp *app, const gchar *uri, const gchar *title)
{
    if (!app || app->is_private || !app->history || !uri)
        return;
    if (!title || !*title)
        return;

    for (guint i = 0; i < app->history->len; i++) {
        NionHistoryEntry *entry = g_ptr_array_index(app->history, i);
        if (!entry || g_strcmp0(entry->uri, uri) != 0)
            continue;

        /* Only replace a placeholder. A page that later reports a worse or
         * truncated title must not clobber one that was already good. */
        gboolean placeholder = !entry->title || !*entry->title ||
                               g_strcmp0(entry->title, entry->uri) == 0;
        if (!placeholder)
            return;

        gchar *clean = nion_history_normalize_title(title, uri);
        if (g_strcmp0(clean, entry->title) == 0) {
            g_free(clean);
            return;
        }
        g_free(entry->title);
        entry->title = clean;
        nion_history_save(app);
        return;
    }
}

/* ---- Deletion ---- */

void
nion_history_clear(NionApp *app)
{
    if (!app)
        return;

    if (app->history)
        g_ptr_array_set_size(app->history, 0);

    if (app->history_file && g_file_test(app->history_file, G_FILE_TEST_EXISTS)) {
        if (g_unlink(app->history_file) != 0)
            g_warning("Could not remove NiOn history file %s: %s",
                      app->history_file, g_strerror(errno));
    }

    if (app->history_list)
        nion_history_refresh_window(app);

    if (!app->is_private)
        nion_set_status(app, app->tor_ready
            ? "● TOR CONNECTED — HISTORY CLEARED"
            : "○ TOR NOT READY — HISTORY CLEARED");
}

void
nion_history_forget_site(NionApp *app, const gchar *uri)
{
    if (!app || !app->history || !uri)
        return;

    gchar *host = nion_history_host_of(uri);
    if (!host)
        return;

    if (nion_history_forget_host(app->history, host) > 0) {
        nion_history_save(app);
        if (app->history_list)
            nion_history_refresh_window(app);
    }
    g_free(host);
}

/* ---- UI ---- */

static gchar *
nion_history_search_fold(const gchar *text)
{
    if (!text || !*text)
        return g_strdup("");
    if (g_utf8_validate(text, -1, NULL))
        return g_utf8_casefold(text, -1);
    return g_ascii_strdown(text, -1);
}

static gboolean
nion_history_matches_search(NionHistoryEntry *entry, const gchar *query)
{
    if (!entry)
        return FALSE;
    if (!query || !*query)
        return TRUE;

    gchar *query_copy = g_strdup(query);
    g_strstrip(query_copy);
    if (!*query_copy) {
        g_free(query_copy);
        return TRUE;
    }

    gchar *query_fold = nion_history_search_fold(query_copy);
    gchar *title_fold = nion_history_search_fold(entry->title);
    gchar *uri_fold = nion_history_search_fold(entry->uri);
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

static gchar *
nion_history_format_when(gint64 last_visit)
{
    if (last_visit <= 0)
        return g_strdup("");

    GDateTime *moment = g_date_time_new_from_unix_local(last_visit);
    if (!moment)
        return g_strdup("");
    gchar *text = g_date_time_format(moment, "%Y-%m-%d %H:%M");
    g_date_time_unref(moment);
    return text;
}

static void
on_history_open_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionHistoryEntry *entry = g_object_get_data(G_OBJECT(button), "nion-history-entry");
    if (!app || !entry || !entry->uri)
        return;

    if (!app->tor_ready) {
        /* Fail closed, same rule as opening a bookmark: Tor owns every
         * navigation in NiOn, so a stored URL is not a bypass. */
        nion_set_status(app, "○ TOR NOT READY — HISTORY OPEN BLOCKED");
        return;
    }
    nion_new_tab(app, entry->uri, TRUE);
}

static void
on_history_delete_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    NionHistoryEntry *entry = g_object_get_data(G_OBJECT(button), "nion-history-entry");
    if (!app || !app->history || !entry)
        return;

    for (guint i = 0; i < app->history->len; i++) {
        if (g_ptr_array_index(app->history, i) == entry) {
            g_ptr_array_remove_index(app->history, i);
            nion_history_save(app);
            nion_history_refresh_window(app);
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — HISTORY ENTRY DELETED"
                : "○ TOR NOT READY — HISTORY ENTRY DELETED");
            return;
        }
    }
}

static void
on_history_row_activated(GtkListBox *list, GtkListBoxRow *row, gpointer user_data)
{
    (void)list;
    NionApp *app = user_data;
    NionHistoryEntry *entry = row
        ? g_object_get_data(G_OBJECT(row), "nion-history-entry") : NULL;
    if (!app || !entry || !entry->uri)
        return;

    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — HISTORY OPEN BLOCKED");
        return;
    }
    nion_new_tab(app, entry->uri, TRUE);
}

static GtkWidget *
nion_history_row_new(NionApp *app, NionHistoryEntry *entry)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(content, 8);
    gtk_widget_set_margin_bottom(content, 8);
    gtk_widget_set_margin_start(content, 10);
    gtk_widget_set_margin_end(content, 10);

    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(labels, TRUE);

    GtkWidget *title = gtk_label_new(entry->title ? entry->title : entry->uri);
    GtkWidget *uri = gtk_label_new(entry->uri ? entry->uri : "");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(title, "heading");
    gtk_widget_add_css_class(uri, "nion-muted");

    gchar *when = nion_history_format_when(entry->last_visit);
    gchar *detail = entry->visits > 1
        ? g_strdup_printf("%s · %u visits", when, entry->visits)
        : g_strdup(when);
    GtkWidget *meta = gtk_label_new(detail);
    gtk_label_set_xalign(GTK_LABEL(meta), 0.0f);
    gtk_widget_add_css_class(meta, "nion-muted");

    gtk_box_append(GTK_BOX(labels), title);
    gtk_box_append(GTK_BOX(labels), uri);
    gtk_box_append(GTK_BOX(labels), meta);

    GtkWidget *open = gtk_button_new_with_label("Open");
    GtkWidget *remove = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(remove, "destructive-action");
    gtk_widget_set_valign(open, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);

    g_object_set_data(G_OBJECT(open), "nion-history-entry", entry);
    g_object_set_data(G_OBJECT(remove), "nion-history-entry", entry);
    g_object_set_data(G_OBJECT(row), "nion-history-entry", entry);

    g_signal_connect(open, "clicked", G_CALLBACK(on_history_open_clicked), app);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_history_delete_clicked), app);

    gtk_box_append(GTK_BOX(content), labels);
    gtk_box_append(GTK_BOX(content), open);
    gtk_box_append(GTK_BOX(content), remove);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), content);

    g_free(detail);
    g_free(when);
    return row;
}

static void
nion_history_refresh_window(NionApp *app)
{
    if (!app || !app->history_list)
        return;

    const gchar *query_text = app->history_search_entry
        ? gtk_editable_get_text(GTK_EDITABLE(app->history_search_entry))
        : "";
    gchar *query = g_strdup(query_text ? query_text : "");
    g_strstrip(query);
    gboolean searching = *query != '\0';
    guint total = app->history ? app->history->len : 0;
    guint matches = 0;

    GtkWidget *row = gtk_widget_get_first_child(app->history_list);
    while (row) {
        GtkWidget *next = gtk_widget_get_next_sibling(row);
        gtk_list_box_remove(GTK_LIST_BOX(app->history_list), row);
        row = next;
    }

    if (app->history) {
        for (guint i = 0; i < app->history->len; i++) {
            NionHistoryEntry *entry = g_ptr_array_index(app->history, i);
            if (entry && nion_history_matches_search(entry, query)) {
                gtk_list_box_append(GTK_LIST_BOX(app->history_list),
                                    nion_history_row_new(app, entry));
                matches++;
            }
        }
    }

    if (app->history_empty_label) {
        if (total == 0)
            gtk_label_set_text(GTK_LABEL(app->history_empty_label),
                               "No history yet. Pages you visit are listed here.");
        else if (searching && matches == 0)
            gtk_label_set_text(GTK_LABEL(app->history_empty_label),
                               "No history entries match your search.");
        gtk_widget_set_visible(app->history_empty_label, matches == 0);
    }

    if (app->history_result_label) {
        gchar *summary = searching
            ? g_strdup_printf("%u of %u", matches, total)
            : g_strdup_printf("%u entr%s", total, total == 1 ? "y" : "ies");
        gtk_label_set_text(GTK_LABEL(app->history_result_label), summary);
        g_free(summary);
    }
    g_free(query);
}

static void
on_history_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    (void)entry;
    nion_history_refresh_window(user_data);
}

static gboolean
on_history_window_close_request(GtkWindow *window, gpointer user_data)
{
    NionApp *app = user_data;
    if (app && app->history_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->history_search_entry), "");
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void
on_history_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (app && app->history_search_entry)
        gtk_editable_set_text(GTK_EDITABLE(app->history_search_entry), "");
    if (app && app->history_window)
        gtk_widget_set_visible(app->history_window, FALSE);
}

static void
on_history_clear_confirmed(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionApp *app = user_data;
    GError *error = NULL;
    gint choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
    g_clear_error(&error);

    /* Button 0 is Cancel, so only an explicit "Clear All History" wipes. */
    if (choice == 1 && app && !app->shutting_down)
        nion_history_clear(app);
}

static void
on_history_clear_clicked(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    if (!app || !app->history_window)
        return;

    GtkAlertDialog *dialog = gtk_alert_dialog_new("Clear all browsing history?");
    gtk_alert_dialog_set_detail(dialog,
        "Every stored address and title will be deleted from this device. "
        "Bookmarks, open tabs and site data are not affected.");
    const char *buttons[] = { "Cancel", "Clear All History", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(app->history_window), NULL,
                            on_history_clear_confirmed, app);
    g_object_unref(dialog);
    (void)button;
}

static void
nion_show_history(NionApp *app)
{
    if (!app)
        return;

    /* Fail closed: a Private Window keeps no history, so it must not present
     * the normal window's store either. The menu entry is hidden there too;
     * this guard covers the action being invoked another way. */
    if (app->is_private || !app->history_file) {
        nion_set_status(app, "○ PRIVATE WINDOW — HISTORY IS NOT KEPT");
        return;
    }

    if (!app->history_window) {
        GtkWidget *window = gtk_window_new();
        app->history_window = window;
        gtk_window_set_title(GTK_WINDOW(window), "NiOn History");
        gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
        gtk_window_set_default_size(GTK_WINDOW(window), 760, 560);

        GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(root, 16);
        gtk_widget_set_margin_bottom(root, 16);
        gtk_widget_set_margin_start(root, 16);
        gtk_widget_set_margin_end(root, 16);
        gtk_window_set_child(GTK_WINDOW(window), root);

        GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *heading = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(heading), "<b>History</b>");
        gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
        gtk_widget_set_hexpand(heading, TRUE);
        GtkWidget *clear_all = gtk_button_new_with_label("Clear All History");
        gtk_widget_add_css_class(clear_all, "destructive-action");
        gtk_box_append(GTK_BOX(header), heading);
        gtk_box_append(GTK_BOX(header), clear_all);
        gtk_box_append(GTK_BOX(root), header);

        GtkWidget *note = gtk_label_new(
            "Stored only in this NiOn profile, never uploaded. Entries expire "
            "after 90 days, are not recorded in Private Windows, and are erased "
            "by New Identity.");
        gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
        gtk_label_set_wrap(GTK_LABEL(note), TRUE);
        gtk_widget_add_css_class(note, "nion-muted");
        gtk_box_append(GTK_BOX(root), note);

        GtkWidget *search_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        app->history_search_entry = gtk_search_entry_new();
        g_object_set(app->history_search_entry,
                     "placeholder-text", "Search history by title or URL",
                     NULL);
        gtk_widget_set_hexpand(app->history_search_entry, TRUE);
        gtk_search_entry_set_key_capture_widget(
            GTK_SEARCH_ENTRY(app->history_search_entry), window);
        app->history_result_label = gtk_label_new("0 entries");
        gtk_widget_add_css_class(app->history_result_label, "nion-muted");
        gtk_widget_set_valign(app->history_result_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(search_row), app->history_search_entry);
        gtk_box_append(GTK_BOX(search_row), app->history_result_label);
        gtk_box_append(GTK_BOX(root), search_row);

        app->history_empty_label = gtk_label_new(
            "No history yet. Pages you visit are listed here.");
        gtk_widget_set_margin_top(app->history_empty_label, 28);
        gtk_widget_set_margin_bottom(app->history_empty_label, 28);
        gtk_widget_add_css_class(app->history_empty_label, "nion-muted");

        app->history_list = gtk_list_box_new();
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->history_list),
                                        GTK_SELECTION_SINGLE);
        gtk_list_box_set_show_separators(GTK_LIST_BOX(app->history_list), TRUE);
        gtk_list_box_set_placeholder(GTK_LIST_BOX(app->history_list),
                                     app->history_empty_label);
        gtk_widget_add_css_class(app->history_list, "boxed-list");

        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->history_list);
        gtk_box_append(GTK_BOX(root), scroll);

        GtkWidget *close = gtk_button_new_with_label("Close");
        gtk_widget_set_halign(close, GTK_ALIGN_END);
        gtk_box_append(GTK_BOX(root), close);

        g_signal_connect(clear_all, "clicked", G_CALLBACK(on_history_clear_clicked), app);
        g_signal_connect(close, "clicked", G_CALLBACK(on_history_close_clicked), app);
        g_signal_connect(app->history_search_entry, "search-changed",
                         G_CALLBACK(on_history_search_changed), app);
        g_signal_connect(app->history_list, "row-activated",
                         G_CALLBACK(on_history_row_activated), app);
        g_signal_connect(window, "close-request",
                         G_CALLBACK(on_history_window_close_request), app);
    }

    nion_history_refresh_window(app);
    gtk_window_present(GTK_WINDOW(app->history_window));
}

void
action_history(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_history(user_data);
}
