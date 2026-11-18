/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <jsc/jsc.h>
#include <string.h>

#include "types.h"
#include "util.h"
#include "history.h"
#include "reading-list.h"

/* Reading list: pages saved for offline reading (v2.3.3, roadmap T1-5).
 *
 * Storage is a GKeyFile written atomically, the same pattern history and
 * bookmarks use, so a half-written file can never replace a good one.
 *
 * The reason a saved page is text rather than a copy of the page: keeping the
 * HTML keeps its external images, stylesheets and frames, so opening the saved
 * copy would reach for them over the network and "offline" would be a label
 * rather than a fact. Text is what can be read with no network at all, and
 * "read offline" renders it with a GtkTextView, so opening a saved page cannot
 * issue a request even by mistake.
 */

static NionReadingCallbacks cb;
static void nion_reading_refresh_window(NionApp *app);

void
nion_reading_list_set_callbacks(const NionReadingCallbacks *callbacks)
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

/* ---- Model ---- */

void
nion_reading_entry_free(gpointer data)
{
    NionReadingEntry *entry = data;
    if (!entry)
        return;
    g_free(entry->uri);
    g_free(entry->title);
    g_free(entry->text);
    g_free(entry);
}

NionReadingEntry *
nion_reading_entry_new(const gchar *uri, const gchar *title, const gchar *text,
                       gint64 saved_at)
{
    NionReadingEntry *entry = g_new0(NionReadingEntry, 1);
    entry->uri = g_strdup(uri);
    entry->title = nion_history_normalize_title(title, uri);
    entry->text = g_strdup(text);
    entry->saved_at = saved_at;
    entry->words = nion_reading_count_words(entry->text);
    return entry;
}

/* Whitespace normalization. Extraction from a live page produces a lot of
 * indentation and blank runs; keeping them would waste the byte budget and make
 * the saved page look broken when it is read back. Tabs and carriage returns
 * become spaces, trailing spaces go, and no more than one blank line survives
 * in a row. */
gchar *
nion_reading_normalize_text(const gchar *raw)
{
    if (!raw)
        return NULL;

    GString *out = g_string_sized_new(strlen(raw) + 1);
    gboolean pending_blank = FALSE;
    gboolean wrote_anything = FALSE;
    guint blank_run = 0;

    const gchar *p = raw;
    while (*p) {
        /* Find the current line. */
        const gchar *line_start = p;
        while (*p && *p != '\n')
            p++;
        gsize length = (gsize)(p - line_start);
        if (*p == '\n')
            p++;

        gchar *line = g_strndup(line_start, length);
        /* A tab inside a line is just spacing. */
        for (gchar *q = line; *q; q++) {
            if (*q == '\t' || *q == '\r')
                *q = ' ';
        }
        gchar *trimmed = g_strstrip(line);

        if (!*trimmed) {
            blank_run++;
            pending_blank = wrote_anything;
            g_free(line);
            continue;
        }

        if (pending_blank && blank_run > 0) {
            g_string_append_c(out, '\n');
            g_string_append_c(out, '\n');
            pending_blank = FALSE;
        } else if (wrote_anything) {
            g_string_append_c(out, '\n');
        }
        blank_run = 0;

        g_string_append(out, trimmed);
        wrote_anything = TRUE;

        /* Stop early rather than allocate past the entry limit. The line is
         * released first: breaking out of the loop would otherwise leak it,
         * which is exactly what the sanitizer build caught here. */
        gboolean capped = out->len >= NION_MAX_READING_TEXT_BYTES;
        g_free(line);
        if (capped)
            break;
    }

    if (!wrote_anything) {
        g_string_free(out, TRUE);
        return NULL;
    }

    if (out->len > NION_MAX_READING_TEXT_BYTES) {
        /* Cut on a UTF-8 boundary, never mid-character. */
        gsize cut = NION_MAX_READING_TEXT_BYTES;
        while (cut > 0 && ((guchar)out->str[cut] & 0xC0) == 0x80)
            cut--;
        g_string_truncate(out, cut);
    }

    return g_string_free(out, FALSE);
}

guint
nion_reading_count_words(const gchar *text)
{
    if (!text)
        return 0;

    guint words = 0;
    gboolean in_word = FALSE;
    for (const gchar *p = text; *p; p++) {
        gboolean space = g_ascii_isspace(*p);
        if (!space && !in_word) {
            words++;
            in_word = TRUE;
        } else if (space) {
            in_word = FALSE;
        }
    }
    return words;
}

gboolean
nion_reading_text_is_worth_saving(const gchar *text)
{
    if (!text)
        return FALSE;
    /* Byte length is a fast refusal; a page that is mostly whitespace is caught
     * by the word count right after it. */
    if (strlen(text) < NION_MIN_READABLE_CHARS)
        return FALSE;
    return nion_reading_count_words(text) >= NION_MIN_READABLE_CHARS / 10;
}

gint
nion_reading_find_uri(GPtrArray *entries, const gchar *uri)
{
    if (!entries || !uri)
        return -1;
    for (guint i = 0; i < entries->len; i++) {
        NionReadingEntry *entry = g_ptr_array_index(entries, i);
        if (entry && g_strcmp0(entry->uri, uri) == 0)
            return (gint)i;
    }
    return -1;
}

guint
nion_reading_prune(GPtrArray *entries, guint max_entries)
{
    if (!entries)
        return 0;
    guint removed = 0;
    while (entries->len > max_entries) {
        g_ptr_array_remove_index(entries, entries->len - 1);
        removed++;
    }
    return removed;
}

/* ---- Store ---- */

static gchar *
reading_group_name(guint index)
{
    return g_strdup_printf("entry-%u", index);
}

void
nion_reading_list_save(NionApp *app)
{
    if (!app || app->is_private || !app->reading_list_file || !app->reading_list)
        return;

    GKeyFile *key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "reading-list", "format", NION_READING_FORMAT);
    g_key_file_set_integer(key_file, "reading-list", "count",
                           (gint)app->reading_list->len);

    for (guint i = 0; i < app->reading_list->len; i++) {
        NionReadingEntry *entry = g_ptr_array_index(app->reading_list, i);
        if (!entry)
            continue;
        gchar *group = reading_group_name(i);
        g_key_file_set_string(key_file, group, "uri", entry->uri ? entry->uri : "");
        g_key_file_set_string(key_file, group, "title", entry->title ? entry->title : "");
        g_key_file_set_string(key_file, group, "text", entry->text ? entry->text : "");
        g_key_file_set_int64(key_file, group, "saved_at", entry->saved_at);
        g_key_file_set_integer(key_file, group, "words", (gint)entry->words);
        g_free(group);
    }

    gsize length = 0;
    gchar *data = g_key_file_to_data(key_file, &length, NULL);
    g_key_file_unref(key_file);

    if (!data || length == 0) {
        g_free(data);
        return;
    }

    gchar *dir = g_path_get_dirname(app->reading_list_file);
    if (dir) {
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
    }

    gchar *tmp = g_strdup_printf("%s.new", app->reading_list_file);
    if (!g_file_set_contents(tmp, data, (gssize)length, NULL)) {
        g_unlink(tmp);
        g_free(tmp);
        g_free(data);
        return;
    }
    if (g_rename(tmp, app->reading_list_file) != 0) {
        g_unlink(tmp);
        g_free(tmp);
        g_free(data);
        return;
    }
    g_free(tmp);
    g_free(data);
}

void
nion_reading_list_load(NionApp *app)
{
    if (!app || app->is_private || !app->reading_list_file)
        return;

    if (!app->reading_list)
        app->reading_list = g_ptr_array_new_with_free_func(nion_reading_entry_free);
    else
        g_ptr_array_set_size(app->reading_list, 0);

    gchar *data = NULL;
    gsize length = 0;
    if (!g_file_get_contents(app->reading_list_file, &data, &length, NULL))
        return;

    if (length > NION_MAX_READING_FILE_BYTES)
        length = NION_MAX_READING_FILE_BYTES;

    GKeyFile *key_file = g_key_file_new();
    if (!g_key_file_load_from_data(key_file, data, length, G_KEY_FILE_NONE, NULL)) {
        g_key_file_unref(key_file);
        g_free(data);
        return;
    }
    g_free(data);

    gint count = g_key_file_get_integer(key_file, "reading-list", "count", NULL);
    if (count < 0)
        count = 0;
    if (count > NION_MAX_READING_LIST)
        count = NION_MAX_READING_LIST;

    for (gint i = 0; i < count; i++) {
        gchar *group = reading_group_name((guint)i);
        gchar *uri = g_key_file_get_string(key_file, group, "uri", NULL);
        gchar *title = g_key_file_get_string(key_file, group, "title", NULL);
        gchar *text = g_key_file_get_string(key_file, group, "text", NULL);
        gint64 saved_at = g_key_file_get_int64(key_file, group, "saved_at", NULL);

        /* A stored entry still has to pass the same gate a fresh one does: a
         * hand-edited file must not smuggle in a scheme NiOn refuses to browse. */
        if (nion_history_uri_is_recordable(uri) && text && *text) {
            NionReadingEntry *entry = nion_reading_entry_new(uri, title, text, saved_at);
            g_ptr_array_add(app->reading_list, entry);
        }

        g_free(group);
        g_free(uri);
        g_free(title);
        g_free(text);
    }

    g_key_file_unref(key_file);
    nion_reading_prune(app->reading_list, NION_MAX_READING_LIST);
}

gboolean
nion_reading_list_add(NionApp *app, const gchar *uri, const gchar *title,
                      const gchar *text)
{
    if (!app || app->is_private || !app->reading_list_file || !app->reading_list)
        return FALSE;
    if (!nion_history_uri_is_recordable(uri))
        return FALSE;

    gchar *normalized = nion_reading_normalize_text(text);
    if (!nion_reading_text_is_worth_saving(normalized)) {
        g_free(normalized);
        return FALSE;
    }

    gint existing = nion_reading_find_uri(app->reading_list, uri);
    if (existing >= 0) {
        /* Saving the same page again refreshes it instead of adding a second
         * copy: the newer text may have the full article where the first save
         * caught a partial load. */
        g_ptr_array_remove_index(app->reading_list, (guint)existing);
    }

    NionReadingEntry *entry =
        nion_reading_entry_new(uri, title, normalized, g_get_real_time() / G_USEC_PER_SEC);
    g_free(normalized);

    /* Newest first, which is the order the window and the prune both assume. */
    g_ptr_array_insert(app->reading_list, 0, entry);
    nion_reading_prune(app->reading_list, NION_MAX_READING_LIST);
    nion_reading_list_save(app);
    nion_reading_refresh_window(app);
    return TRUE;
}

void
nion_reading_list_remove(NionApp *app, const gchar *uri)
{
    if (!app || !app->reading_list || !uri)
        return;

    gint index = nion_reading_find_uri(app->reading_list, uri);
    if (index < 0)
        return;

    g_ptr_array_remove_index(app->reading_list, (guint)index);
    nion_reading_list_save(app);
    nion_reading_refresh_window(app);
}

void
nion_reading_list_clear(NionApp *app)
{
    if (!app || !app->reading_list)
        return;

    g_ptr_array_set_size(app->reading_list, 0);
    nion_reading_list_save(app);
    nion_reading_refresh_window(app);
}

/* Parse the length-prefixed triple produced by nion_extract_script.
 *
 * Pure and byte-exact: every field must be preceded by its own UTF-8 byte
 * length, so a truncated payload is refused instead of being guessed at. This
 * is the reason the format carries lengths rather than a delimiter: page text
 * can contain any delimiter, and a wrong guess would silently save the wrong
 * bytes as someone's reading material.
 *
 * On success all three outputs are newly allocated. On failure nothing is
 * allocated and FALSE is returned. */
gboolean
nion_reading_parse_extraction(const gchar *payload, gchar **uri, gchar **title,
                              gchar **text)
{
    if (uri)
        *uri = NULL;
    if (title)
        *title = NULL;
    if (text)
        *text = NULL;
    if (!payload || !*payload)
        return FALSE;

    const gchar *cursor = payload;
    gchar *fields[3] = { NULL, NULL, NULL };

    for (gint i = 0; i < 3; i++) {
        if (!g_ascii_isdigit(*cursor))
            goto fail;

        gint64 length = 0;
        while (g_ascii_isdigit(*cursor)) {
            length = length * 10 + (*cursor - '0');
            if (length > NION_MAX_READING_TEXT_BYTES)
                goto fail;
            cursor++;
        }
        if (*cursor != ':')
            goto fail;
        cursor++;

        /* Refuse a field that claims more bytes than the payload holds, which is
         * what a truncated message looks like. */
        gsize available = strlen(cursor);
        if ((guint64)length > available)
            goto fail;

        fields[i] = g_strndup(cursor, (gsize)length);
        cursor += length;
    }

    /* Trailing bytes mean the payload does not match its own lengths. */
    if (*cursor != '\0')
        goto fail;

    if (uri)
        *uri = fields[0];
    else
        g_free(fields[0]);
    if (title)
        *title = fields[1];
    else
        g_free(fields[1]);
    if (text)
        *text = fields[2];
    else
        g_free(fields[2]);
    return TRUE;

fail:
    for (gint i = 0; i < 3; i++)
        g_free(fields[i]);
    return FALSE;
}

/* ---- Extraction ---- */

/* Runs in the page. Returns a JSON object with the title, the address and the
 * readable text. The text is taken from a copy of <body> with the parts that
 * are navigation or chrome removed, then read as textContent so no markup and
 * no attribute survives into the saved copy. */
/* Runs in the page and returns a length-prefixed triple: three fields, each
 * preceded by its UTF-8 byte length and a colon:
 *
 *   <len>:<uri><len>:<title><len>:<text>
 *
 * The byte lengths come from TextEncoder, and lengths are what makes this
 * parseable without a JSON library and without a separator that the page text
 * could contain. A page whose text is nothing but colons and digits is still
 * unambiguous, which a delimiter-based format could never promise.
 *
 * The text is taken from a copy of <body> with the navigation and chrome
 * removed, then read as innerText so no markup and no attribute survives into
 * the saved copy. */
static const gchar *const nion_extract_script =
    "(function(){"
    "  function bytes(s){ return new TextEncoder().encode(s).length; }"
    "  function pack(s){ return bytes(s) + ':' + s; }"
    "  try {"
    "    var body = document.body;"
    "    if (!body) return '0:' + pack('') + pack('');"
    "    var copy = body.cloneNode(true);"
    "    var drop = copy.querySelectorAll('script,style,noscript,iframe,svg,canvas,form,nav,footer,header,aside,button,select,textarea');"
    "    for (var i = 0; i < drop.length; i++) {"
    "      if (drop[i].parentNode) drop[i].parentNode.removeChild(drop[i]);"
    "    }"
    "    var text = copy.innerText || copy.textContent || '';"
    "    return pack(location.href || '') + pack(document.title || '') + pack(text);"
    "  } catch (e) { return '0:' + pack('') + pack(''); }"
    "})()";

typedef struct {
    NionApp *app;
    gchar *uri;
} NionReadingSaveRequest;

static void
on_extraction_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    NionReadingSaveRequest *request = user_data;
    NionApp *app = request->app;
    GError *error = NULL;

    JSCValue *value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);
    if (!value) {
        nion_set_status(app, "Could not read this page to save it");
        g_clear_error(&error);
        g_free(request->uri);
        g_free(request);
        return;
    }

    gchar *payload = jsc_value_to_string(value);
    g_object_unref(value);

    gchar *uri = NULL;
    gchar *title = NULL;
    gchar *text = NULL;
    if (!nion_reading_parse_extraction(payload, &uri, &title, &text)) {
        nion_set_status(app, "Could not read this page to save it");
        g_free(payload);
        g_free(request->uri);
        g_free(request);
        return;
    }
    g_free(payload);

    /* The address the page reports is only trusted when it still passes the
     * same gate a typed address does; otherwise the address that was actually
     * loaded is used. */
    const gchar *effective_uri =
        nion_history_uri_is_recordable(uri) ? uri : request->uri;

    gchar *normalized = nion_reading_normalize_text(text);
    if (!nion_reading_text_is_worth_saving(normalized)) {
        nion_set_status(app,
                        "Nothing worth saving on this page: it has too little "
                        "text to read offline");
    } else if (nion_reading_list_add(app, effective_uri, title, normalized)) {
        gchar *message = g_strdup_printf(
            "Saved for later: %s",
            (title && *title) ? title : effective_uri);
        nion_set_status(app, message);
        g_free(message);
    } else {
        nion_set_status(app, "Could not save this page for later");
    }

    g_free(normalized);
    g_free(uri);
    g_free(title);
    g_free(text);
    g_free(request->uri);
    g_free(request);
}

void
nion_reading_list_save_current(NionApp *app)
{
    if (!app)
        return;

    if (app->is_private) {
        /* Deliberate: saving a page from a Private Window would keep a copy of
         * what was read in private, which is the trail the window exists to
         * avoid. The refusal is visible rather than silent. */
        nion_set_status(app, "Private Windows do not keep a reading list");
        return;
    }

    /* The current tab is reached through the app-level notebook, so this
     * module never needs main.c's tab lookup. */
    NionTab *current = NULL;
    if (app->notebook) {
        gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
        if (page >= 0) {
            GtkWidget *widget =
                gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), page);
            current = widget ? g_object_get_data(G_OBJECT(widget), "nion-tab") : NULL;
        }
    }
    if (!current || !current->web_view || current->home_page || current->error_page) {
        nion_set_status(app, "There is no page to save in this tab");
        return;
    }

    const gchar *uri = webkit_web_view_get_uri(current->web_view);
    if (!nion_history_uri_is_recordable(uri)) {
        nion_set_status(app, "Only http and https pages can be saved for later");
        return;
    }

    NionReadingSaveRequest *request = g_new0(NionReadingSaveRequest, 1);
    request->app = app;
    request->uri = g_strdup(uri);

    webkit_web_view_evaluate_javascript(current->web_view, nion_extract_script, -1,
                                        NULL, NULL, NULL, on_extraction_done, request);
}

/* ---- UI ---- */

static void
on_reading_row_open(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(button), "nion-uri");
    if (app && uri)
        nion_new_tab(app, uri, TRUE);
}

/* The offline reader. A GtkTextView, not a WebView: a saved page is text, and
 * text cannot request anything. */
static void
on_reading_row_read(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(button), "nion-uri");
    if (!app || !uri || !app->reading_list)
        return;

    gint index = nion_reading_find_uri(app->reading_list, uri);
    if (index < 0) {
        nion_set_status(app, "That saved page is no longer in the list");
        return;
    }
    NionReadingEntry *entry = g_ptr_array_index(app->reading_list, (guint)index);

    GtkWidget *window = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 640);
    gchar *title = g_strdup_printf("Read offline: %s",
                                   entry->title && *entry->title ? entry->title : entry->uri);
    gtk_window_set_title(GTK_WINDOW(window), title);
    g_free(title);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(header, 14);
    gtk_widget_set_margin_end(header, 14);
    gtk_widget_set_margin_top(header, 12);
    gtk_widget_set_margin_bottom(header, 8);

    GtkWidget *heading = gtk_label_new(entry->title && *entry->title ? entry->title
                                                                    : entry->uri);
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(heading), TRUE);
    gtk_widget_add_css_class(heading, "title-3");

    GtkWidget *address = gtk_label_new(entry->uri);
    gtk_label_set_xalign(GTK_LABEL(address), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(address), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(address, "nion-muted");
    gtk_widget_set_tooltip_text(address, entry->uri);

    GDateTime *saved = g_date_time_new_from_unix_local(entry->saved_at);
    gchar *stamp = saved ? g_date_time_format(saved, "%Y-%m-%d %H:%M") : g_strdup("");
    gchar *meta = g_strdup_printf("Saved %s · %u words · text only, images are not kept",
                                 stamp, entry->words);
    GtkWidget *detail = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
    gtk_widget_add_css_class(detail, "nion-muted");

    gtk_box_append(GTK_BOX(header), heading);
    gtk_box_append(GTK_BOX(header), address);
    gtk_box_append(GTK_BOX(header), detail);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 18);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 18);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(text_view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(text_view), 18);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view)),
                             entry->text ? entry->text : "", -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), text_view);

    gtk_box_append(GTK_BOX(box), header);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), scroller);
    gtk_window_set_child(GTK_WINDOW(window), box);

    g_free(meta);
    g_free(stamp);
    if (saved)
        g_date_time_unref(saved);

    gtk_window_present(GTK_WINDOW(window));
}

static void
on_reading_row_remove(GtkButton *button, gpointer user_data)
{
    NionApp *app = user_data;
    const gchar *uri = g_object_get_data(G_OBJECT(button), "nion-uri");
    if (app && uri)
        nion_reading_list_remove(app, uri);
}

static void
on_clear_reading_confirmed(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    NionApp *app = user_data;
    GError *error = NULL;

    gint choice = gtk_alert_dialog_choose_finish(dialog, result, &error);
    if (choice == 0) /* "Clear list" */
        nion_reading_list_clear(app);
    g_clear_error(&error);
}

static void
on_clear_reading_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionApp *app = user_data;
    if (!app)
        return;

    GtkAlertDialog *dialog = gtk_alert_dialog_new(
        "Delete every page saved for offline reading? This cannot be undone.");
    const gchar *buttons[] = { "Clear list", "Cancel", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    /* Cancel is the default: destructive choices should not be one Enter away. */
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(app->window), NULL,
                            on_clear_reading_confirmed, app);
    g_object_unref(dialog);
}

static void
nion_reading_refresh_window(NionApp *app)
{
    if (!app || !app->reading_list_box)
        return;

    GtkWidget *child = gtk_widget_get_first_child(app->reading_list_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(app->reading_list_box), child);
        child = next;
    }

    guint count = app->reading_list ? app->reading_list->len : 0;

    if (app->reading_empty_label)
        gtk_widget_set_visible(app->reading_empty_label, count == 0);
    if (app->reading_count_label) {
        gchar *label = count == 1 ? g_strdup("1 page saved")
                                  : g_strdup_printf("%u pages saved", count);
        gtk_label_set_text(GTK_LABEL(app->reading_count_label), label);
        g_free(label);
    }

    for (guint i = 0; i < count; i++) {
        NionReadingEntry *entry = g_ptr_array_index(app->reading_list, i);
        if (!entry)
            continue;

        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(box, 10);
        gtk_widget_set_margin_end(box, 10);
        gtk_widget_set_margin_top(box, 6);
        gtk_widget_set_margin_bottom(box, 6);

        GtkWidget *stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
        gtk_widget_set_hexpand(stack, TRUE);
        GtkWidget *title = gtk_label_new(entry->title && *entry->title ? entry->title
                                                                      : entry->uri);
        gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        GtkWidget *uri = gtk_label_new(entry->uri);
        gtk_label_set_xalign(GTK_LABEL(uri), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(uri), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_add_css_class(uri, "nion-muted");

        GDateTime *saved = g_date_time_new_from_unix_local(entry->saved_at);
        gchar *stamp = saved ? g_date_time_format(saved, "%Y-%m-%d") : g_strdup("");
        gchar *meta = g_strdup_printf("saved %s · %u words", stamp, entry->words);
        GtkWidget *detail = gtk_label_new(meta);
        gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);
        gtk_widget_add_css_class(detail, "nion-muted");

        gtk_box_append(GTK_BOX(stack), title);
        gtk_box_append(GTK_BOX(stack), uri);
        gtk_box_append(GTK_BOX(stack), detail);
        g_free(meta);
        g_free(stamp);
        if (saved)
            g_date_time_unref(saved);

        GtkWidget *read = gtk_button_new_with_label("Read offline");
        g_object_set_data_full(G_OBJECT(read), "nion-uri", g_strdup(entry->uri), g_free);
        g_signal_connect(read, "clicked", G_CALLBACK(on_reading_row_read), app);
        GtkWidget *open = gtk_button_new_with_label("Open");
        g_object_set_data_full(G_OBJECT(open), "nion-uri", g_strdup(entry->uri), g_free);
        g_signal_connect(open, "clicked", G_CALLBACK(on_reading_row_open), app);
        GtkWidget *remove = gtk_button_new_with_label("Delete");
        g_object_set_data_full(G_OBJECT(remove), "nion-uri", g_strdup(entry->uri), g_free);
        g_signal_connect(remove, "clicked", G_CALLBACK(on_reading_row_remove), app);

        gtk_box_append(GTK_BOX(box), stack);
        gtk_box_append(GTK_BOX(box), read);
        gtk_box_append(GTK_BOX(box), open);
        gtk_box_append(GTK_BOX(box), remove);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
        gtk_list_box_append(GTK_LIST_BOX(app->reading_list_box), row);
    }
}

static void
nion_show_reading_list(NionApp *app)
{
    if (!app)
        return;

    if (app->reading_window) {
        nion_reading_refresh_window(app);
        gtk_window_present(GTK_WINDOW(app->reading_window));
        return;
    }

    GtkWidget *window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "NiOn — Reading List");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 560);
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    app->reading_window = window;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(header, 12);
    gtk_widget_set_margin_end(header, 12);
    gtk_widget_set_margin_top(header, 10);
    gtk_widget_set_margin_bottom(header, 10);

    app->reading_count_label = gtk_label_new("");
    gtk_widget_set_hexpand(app->reading_count_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->reading_count_label), 0.0f);
    GtkWidget *clear = gtk_button_new_with_label("Clear list");
    g_signal_connect(clear, "clicked", G_CALLBACK(on_clear_reading_clicked), app);
    gtk_box_append(GTK_BOX(header), app->reading_count_label);
    gtk_box_append(GTK_BOX(header), clear);

    app->reading_empty_label = gtk_label_new(
        "Nothing saved yet.\n\nOpen a page and choose Save for later, "
        "or press Ctrl+Shift+S. Only the text is kept, so a saved page can be "
        "read with no network at all.");
    gtk_label_set_wrap(GTK_LABEL(app->reading_empty_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(app->reading_empty_label), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(app->reading_empty_label, "nion-muted");
    gtk_widget_set_margin_start(app->reading_empty_label, 24);
    gtk_widget_set_margin_end(app->reading_empty_label, 24);
    gtk_widget_set_margin_top(app->reading_empty_label, 24);

    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroller, TRUE);
    app->reading_list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->reading_list_box),
                                    GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), app->reading_list_box);

    gtk_box_append(GTK_BOX(root), header);
    gtk_box_append(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(root), app->reading_empty_label);
    gtk_box_append(GTK_BOX(root), scroller);
    gtk_window_set_child(GTK_WINDOW(window), root);

    /* The window is reused rather than rebuilt, so the widget pointers above
     * stay valid while it is hidden. */
    gtk_window_present(GTK_WINDOW(window));
    nion_reading_refresh_window(app);
}

void
action_reading_list(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_show_reading_list(user_data);
}

void
action_save_for_later(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    nion_reading_list_save_current(user_data);
}
