/* Copyright (C) 2026 Jeannes Bryan */

/* Tab model, notebook & context-menu logic (extracted from src/main.c,
 * Phase 5a, NiOn 2.0.0).

 * UI/navigation operations go through registered callbacks (see tabs.h) so
 * this module never calls into main.c code directly. */

#include "config.h"
#include "tabs.h"
#include "types.h"
#include "session.h"
#include "navigation.h"
#include "util.h"
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <glib.h>
#include <string.h>

static NionTabCallbacks s_tab_callbacks;

void nion_tabs_set_callbacks(const NionTabCallbacks *callbacks)
{
    if (callbacks)
        s_tab_callbacks = *callbacks;
}

static NionTab *nion_new_tab(NionApp *app, const gchar *uri, gboolean select)
{
    return s_tab_callbacks.new_tab ? s_tab_callbacks.new_tab(app, uri, select) : NULL;
}

static void nion_set_status(NionApp *app, const gchar *text)
{
    if (s_tab_callbacks.set_status) s_tab_callbacks.set_status(app, text);
}

static void nion_update_controls(NionApp *app)
{
    if (s_tab_callbacks.update_controls) s_tab_callbacks.update_controls(app);
}

static void nion_clear_retry(NionTab *tab)
{
    if (s_tab_callbacks.clear_retry) s_tab_callbacks.clear_retry(tab);
}

static void nion_load_home(NionTab *tab)
{
    if (s_tab_callbacks.load_home) s_tab_callbacks.load_home(tab);
}

static void nion_reload_crashed_tab(NionTab *tab)
{
    if (s_tab_callbacks.reload_crashed_tab) s_tab_callbacks.reload_crashed_tab(tab);
}

static void nion_load_uri(NionTab *tab, const gchar *uri)
{
    if (s_tab_callbacks.load_uri) s_tab_callbacks.load_uri(tab, uri);
}

static gboolean nion_tab_is_current(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook)
        return FALSE;
    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint cur = gtk_notebook_get_current_page(notebook);
    if (cur < 0)
        return FALSE;
    return gtk_notebook_get_nth_page(notebook, cur) == tab->page;
}

/* Static forward decls (source orderings preserved from main.c). */
static void nion_update_tab_context_menu(NionTab *tab);
static void nion_tab_context_popdown(NionTab *tab);

/* ---- Tab logic (source order preserved) ---- */

void nion_closed_tab_free(gpointer data)
{
    NionClosedTab *closed = data;
    if (!closed)
        return;

    g_free(closed->uri);
    g_free(closed);
}

static const gchar *nion_tab_reopenable_uri(NionTab *tab)
{
    if (!tab || tab->home_page)
        return NULL;

    const gchar *uri = NULL;
    if (tab->display_uri_override && *tab->display_uri_override)
        uri = tab->display_uri_override;
    else
        uri = webkit_web_view_get_uri(tab->web_view);

    if ((!uri || !*uri || g_str_equal(uri, "about:blank")) &&
        tab->restore_uri && *tab->restore_uri)
        uri = tab->restore_uri;

    if (!uri || !*uri || g_str_equal(uri, "about:blank") ||
        strlen(uri) > NION_MAX_SAVED_URI_BYTES)
        return NULL;

    gchar *validation = NULL;
    gboolean valid = nion_validate_uri(uri, &validation);
    g_free(validation);
    return valid ? uri : NULL;
}

static void nion_remember_closed_tab(NionTab *tab)
{
    if (!tab || !tab->app)
        return;

    const gchar *uri = nion_tab_reopenable_uri(tab);
    if (!uri)
        return;

    NionApp *app = tab->app;
    if (!app->closed_tabs)
        app->closed_tabs = g_queue_new();

    NionClosedTab *closed = g_new0(NionClosedTab, 1);
    closed->uri = g_strdup(uri);
    closed->muted = webkit_web_view_get_is_muted(tab->web_view);
    closed->pinned = tab->pinned;
    g_queue_push_tail(app->closed_tabs, closed);

    while (g_queue_get_length(app->closed_tabs) > NION_MAX_CLOSED_TABS)
        nion_closed_tab_free(g_queue_pop_head(app->closed_tabs));
}

void nion_reopen_closed_tab(NionApp *app)
{
    if (!app || !app->closed_tabs || g_queue_is_empty(app->closed_tabs)) {
        if (app)
            nion_set_status(app, app->tor_ready
                ? "● TOR CONNECTED — NO CLOSED TAB TO REOPEN"
                : "○ TOR NOT READY — NO CLOSED TAB TO REOPEN");
        return;
    }

    if (!app->tor_ready) {
        nion_set_status(app, "○ TOR NOT READY — CLOSED TAB KEPT FOR LATER");
        return;
    }

    NionClosedTab *closed = g_queue_pop_tail(app->closed_tabs);

    /* If closing the last website left NiOn with its required replacement
     * New Tab, reuse that slot instead of leaving an unnecessary blank tab
     * beside the reopened page. */
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    if (gtk_notebook_get_n_pages(notebook) == 1) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, 0);
        NionTab *only = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (only && only->home_page)
            gtk_notebook_remove_page(notebook, 0);
    }

    NionTab *tab = nion_new_tab(app, closed->uri, TRUE);
    if (tab) {
        if (closed->muted)
            webkit_web_view_set_is_muted(tab->web_view, TRUE);
        nion_set_tab_pinned(tab, closed->pinned, TRUE);
    }

    nion_set_status(app, "● TOR CONNECTED — REOPENED CLOSED TAB");
    nion_closed_tab_free(closed);
}

static void nion_close_tab_now(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook)
        return;

    NionApp *app = tab->app;
    gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->page);
    if (page_num < 0)
        return;

    nion_remember_closed_tab(tab);

    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook)) == 1) {
        /* Closing the last tab keeps NiOn alive, just like a normal browser.
         * Remove the WebView entirely so the replacement blank tab has no
         * inherited Back/Forward history. */
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
        nion_new_tab(app, NULL, TRUE);
        nion_schedule_session_save(app);
        return;
    }

    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page_num);
    nion_update_controls(app);
    nion_schedule_session_save(app);
}

static guint nion_private_close_target_count(NionPrivateCloseRequest *request)
{
    if (!request || !request->app || !request->app->notebook || !request->anchor)
        return 0;
    if (request->mode == NION_PRIVATE_CLOSE_SINGLE)
        return gtk_notebook_page_num(GTK_NOTEBOOK(request->app->notebook), request->anchor->page) >= 0 ? 1u : 0u;

    GtkNotebook *notebook = GTK_NOTEBOOK(request->app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    gint anchor_index = gtk_notebook_page_num(notebook, request->anchor->page);
    guint count = 0;
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || tab == request->anchor || tab->pinned)
            continue;
        if (request->mode == NION_PRIVATE_CLOSE_RIGHT && (anchor_index < 0 || i <= anchor_index))
            continue;
        count++;
    }
    return count;
}

static void nion_private_apply_close_request(NionPrivateCloseRequest *request)
{
    if (!request || !request->app || !request->anchor)
        return;

    NionApp *app = request->app;
    if (request->mode == NION_PRIVATE_CLOSE_SINGLE) {
        nion_close_tab_now(request->anchor);
        return;
    }

    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    gint anchor_index = gtk_notebook_page_num(notebook, request->anchor->page);
    for (gint i = gtk_notebook_get_n_pages(notebook) - 1; i >= 0; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || page == request->anchor->page)
            continue;
        NionTab *other = g_object_get_data(G_OBJECT(page), "nion-tab");
        if (!other || other->pinned)
            continue;
        if (request->mode == NION_PRIVATE_CLOSE_RIGHT && (anchor_index < 0 || i <= anchor_index))
            continue;
        nion_close_tab_now(other);
    }

    gint current = gtk_notebook_page_num(notebook, request->anchor->page);
    if (current >= 0)
        gtk_notebook_set_current_page(notebook, current);
    nion_update_controls(app);
}

static gboolean on_private_tab_close_confirm_request(GtkWindow *window, gpointer user_data)
{
    (void)window;
    NionPrivateCloseRequest *request = user_data;
    if (request && request->app) {
        request->app->private_tab_close_confirm_open = FALSE;
        request->app->private_tab_close_confirm_window = NULL;
    }
    return FALSE;
}

static void on_private_tab_close_no(GtkButton *button, gpointer user_data)
{
    NionPrivateCloseRequest *request = user_data;
    if (request && request->app) {
        request->app->private_tab_close_confirm_open = FALSE;
        request->app->private_tab_close_confirm_window = NULL;
    }
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void on_private_tab_close_yes(GtkButton *button, gpointer user_data)
{
    NionPrivateCloseRequest *request = user_data;
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(button));
    if (request && request->app) {
        request->app->private_tab_close_confirm_open = FALSE;
        request->app->private_tab_close_confirm_window = NULL;
    }
    nion_private_apply_close_request(request);
    if (root && GTK_IS_WINDOW(root))
        gtk_window_destroy(GTK_WINDOW(root));
}

static void nion_request_private_tab_close(NionTab *tab, NionPrivateCloseMode mode)
{
    if (!tab || !tab->app || !tab->app->is_private)
        return;
    NionApp *app = tab->app;
    if (app->private_tab_close_confirm_open)
        return;

    NionPrivateCloseRequest *request = g_new0(NionPrivateCloseRequest, 1);
    request->app = app;
    request->anchor = tab;
    request->mode = mode;
    guint count = nion_private_close_target_count(request);
    if (count == 0) {
        g_free(request);
        return;
    }

    app->private_tab_close_confirm_open = TRUE;
    GtkWidget *window = gtk_window_new();
    app->private_tab_close_confirm_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "Close private tab?");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 430, -1);
    g_object_set_data_full(G_OBJECT(window), "nion-private-close-request", request, g_free);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_window_set_child(GTK_WINDOW(window), box);

    GtkWidget *heading = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(heading),
                         mode == NION_PRIVATE_CLOSE_SINGLE
                            ? "<b>Close this private tab?</b>"
                            : "<b>Close private tabs?</b>");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(box), heading);

    gchar *message_text = NULL;
    if (mode == NION_PRIVATE_CLOSE_SINGLE)
        message_text = g_strdup("This tab will be closed.");
    else
        message_text = g_strdup_printf(count == 1
            ? "1 private tab will be closed."
            : "%u private tabs will be closed.", count);
    GtkWidget *message = gtk_label_new(message_text);
    g_free(message_text);
    gtk_label_set_wrap(GTK_LABEL(message), TRUE);
    gtk_label_set_xalign(GTK_LABEL(message), 0.0f);
    gtk_box_append(GTK_BOX(box), message);

    GtkWidget *note = gtk_label_new(
        "Private website data is discarded when the entire Private Window closes, not merely when one tab closes.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
    gtk_widget_add_css_class(note, "nion-muted");
    gtk_box_append(GTK_BOX(box), note);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(buttons, GTK_ALIGN_END);
    GtkWidget *no = gtk_button_new_with_label("No");
    GtkWidget *yes = gtk_button_new_with_label("Yes");
    gtk_widget_add_css_class(yes, "destructive-action");
    gtk_box_append(GTK_BOX(buttons), no);
    gtk_box_append(GTK_BOX(buttons), yes);
    gtk_box_append(GTK_BOX(box), buttons);

    g_signal_connect(no, "clicked", G_CALLBACK(on_private_tab_close_no), request);
    g_signal_connect(yes, "clicked", G_CALLBACK(on_private_tab_close_yes), request);
    g_signal_connect(window, "close-request",
                     G_CALLBACK(on_private_tab_close_confirm_request), request);
    gtk_window_present(GTK_WINDOW(window));
}

void nion_close_tab(NionTab *tab)
{
    if (!tab || !tab->app)
        return;
    if (tab->app->is_private) {
        nion_request_private_tab_close(tab, NION_PRIVATE_CLOSE_SINGLE);
        return;
    }
    nion_close_tab_now(tab);
}

static void on_tab_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_close_tab(user_data);
}

void nion_tab_free(gpointer data)
{
    NionTab *tab = data;
    nion_clear_retry(tab);
    if (tab->http_warning_decision) {
        webkit_policy_decision_ignore(tab->http_warning_decision);
        g_clear_object(&tab->http_warning_decision);
    }
    if (tab->http_warning_window) {
        GtkWidget *warning = tab->http_warning_window;
        tab->http_warning_window = NULL;
        gtk_window_destroy(GTK_WINDOW(warning));
    }
    g_clear_pointer(&tab->display_uri_override, g_free);
    g_clear_pointer(&tab->onion_location, g_free);
    g_clear_pointer(&tab->http_warning_uri, g_free);
    g_clear_pointer(&tab->http_allowed_origin, g_free);
    g_clear_pointer(&tab->restore_uri, g_free);
    g_clear_object(&tab->home_return_item);
    g_clear_pointer(&tab->web_process_uri, g_free);
    g_clear_pointer(&tab->discard_uri, g_free);
    g_clear_pointer(&tab->discard_title, g_free);
    g_free(tab);
}

gint nion_count_pinned_tabs(NionApp *app)
{
    if (!app || !app->notebook)
        return 0;

    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    gint pinned = 0;
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (tab && tab->pinned)
            pinned++;
    }
    return pinned;
}

static void nion_update_tab_pinned_ui(NionTab *tab)
{
    if (!tab)
        return;

    if (tab->tab_close_button)
        gtk_widget_set_visible(tab->tab_close_button, !tab->pinned);
    if (tab->pin_indicator)
        gtk_widget_set_visible(tab->pin_indicator, tab->pinned);

    if (tab->tab_label_box) {
        if (tab->pinned)
            gtk_widget_add_css_class(tab->tab_label_box, "nion-tab-pinned");
        else
            gtk_widget_remove_css_class(tab->tab_label_box, "nion-tab-pinned");
        gtk_widget_set_tooltip_text(tab->tab_label_box,
                                    tab->pinned ? "Pinned tab" : NULL);
    }
}

void nion_set_tab_pinned(NionTab *tab, gboolean pinned, gboolean schedule_save)
{
    if (!tab || !tab->app || !tab->app->notebook)
        return;

    NionApp *app = tab->app;
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    if (tab->pinned == pinned) {
        nion_update_tab_pinned_ui(tab);
        nion_update_tab_context_menu(tab);
        if (schedule_save)
            nion_schedule_session_save(app);
        return;
    }

    tab->pinned = pinned;
    gint pinned_count = nion_count_pinned_tabs(app);
    gint target = pinned ? MAX(pinned_count - 1, 0) : pinned_count;
    gint current = gtk_notebook_page_num(notebook, tab->page);
    if (current >= 0 && current != target) {
        app->normalizing_tab_order = TRUE;
        gtk_notebook_reorder_child(notebook, tab->page, target);
        app->normalizing_tab_order = FALSE;
    }

    nion_update_tab_pinned_ui(tab);
    nion_update_tab_context_menu(tab);
    if (schedule_save)
        nion_schedule_session_save(app);
}

static gboolean nion_has_unpinned_tab_other_than(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook)
        return FALSE;
    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *other = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (other && other != tab && !other->pinned)
            return TRUE;
    }
    return FALSE;
}

static gboolean nion_has_unpinned_tab_to_right(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook)
        return FALSE;
    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint index = gtk_notebook_page_num(notebook, tab->page);
    gint pages = gtk_notebook_get_n_pages(notebook);
    for (gint i = index + 1; index >= 0 && i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *other = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (other && !other->pinned)
            return TRUE;
    }
    return FALSE;
}

void nion_update_tab_audio_button(NionTab *tab)
{
    if (!tab || !tab->audio_button || !tab->web_view)
        return;

    gboolean muted = webkit_web_view_get_is_muted(tab->web_view);
    gtk_button_set_icon_name(GTK_BUTTON(tab->audio_button),
                             muted ? "audio-volume-muted-symbolic"
                                   : "audio-volume-high-symbolic");
    gtk_widget_set_tooltip_text(tab->audio_button,
                                muted ? "Unmute tab" : "Mute tab");
}

static void on_tab_audio_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->web_view)
        return;
    nion_tab_context_popdown(tab);

    webkit_web_view_set_is_muted(tab->web_view,
                                 !webkit_web_view_get_is_muted(tab->web_view));
    nion_schedule_session_save(tab->app);
}

void on_webview_muted_changed(WebKitWebView *web_view,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
    (void)web_view;
    (void)pspec;
    NionTab *tab = user_data;
    nion_update_tab_audio_button(tab);
    nion_update_tab_context_menu(tab);
}

static void nion_tab_context_popdown(NionTab *tab)
{
    if (tab && tab->tab_menu_popover)
        gtk_popover_popdown(GTK_POPOVER(tab->tab_menu_popover));
}

static void on_tab_context_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app || !tab->app->tor_ready)
        return;
    nion_tab_context_popdown(tab);

    if (tab->web_process_terminated) {
        nion_reload_crashed_tab(tab);
        return;
    }

    if (tab->home_page) {
        nion_load_home(tab);
        return;
    }

    nion_clear_retry(tab);
    tab->load_failed = FALSE;
    tab->error_page = FALSE;
    g_clear_pointer(&tab->display_uri_override, g_free);
    webkit_web_view_reload(tab->web_view);
}

static void on_tab_context_duplicate_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);
    if (!tab->app->tor_ready) {
        nion_set_status(tab->app, "○ TOR NOT READY — DUPLICATE TAB BLOCKED");
        return;
    }

    const gchar *uri = nion_tab_reopenable_uri(tab);
    NionTab *duplicate = nion_new_tab(tab->app, uri, TRUE);
    if (duplicate && uri)
        webkit_web_view_set_zoom_level(duplicate->web_view,
                                       webkit_web_view_get_zoom_level(tab->web_view));
}

static void on_tab_context_mute_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->web_view)
        return;
    nion_tab_context_popdown(tab);

    webkit_web_view_set_is_muted(tab->web_view,
                                 !webkit_web_view_get_is_muted(tab->web_view));
    nion_update_tab_context_menu(tab);
    nion_schedule_session_save(tab->app);
}

static void on_tab_context_pin_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab)
        return;
    nion_tab_context_popdown(tab);
    nion_set_tab_pinned(tab, !tab->pinned, TRUE);
}

static void on_tab_context_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    nion_close_tab(user_data);
}

static void on_tab_context_close_others_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);
    if (tab->app->is_private) {
        nion_request_private_tab_close(tab, NION_PRIVATE_CLOSE_OTHERS);
        return;
    }

    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    for (gint i = gtk_notebook_get_n_pages(notebook) - 1; i >= 0; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        if (!page || page == tab->page)
            continue;
        NionTab *other = g_object_get_data(G_OBJECT(page), "nion-tab");
        if (other && !other->pinned)
            nion_close_tab(other);
    }

    gtk_notebook_set_current_page(notebook, gtk_notebook_page_num(notebook, tab->page));
    nion_update_controls(tab->app);
}

static void on_tab_context_close_right_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    NionTab *tab = user_data;
    if (!tab || !tab->app)
        return;
    nion_tab_context_popdown(tab);
    if (tab->app->is_private) {
        nion_request_private_tab_close(tab, NION_PRIVATE_CLOSE_RIGHT);
        return;
    }

    GtkNotebook *notebook = GTK_NOTEBOOK(tab->app->notebook);
    gint tab_index = gtk_notebook_page_num(notebook, tab->page);
    if (tab_index < 0)
        return;

    for (gint i = gtk_notebook_get_n_pages(notebook) - 1; i > tab_index; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *other = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (other && !other->pinned)
            nion_close_tab(other);
    }

    nion_update_controls(tab->app);
}

static GtkWidget *nion_tab_menu_button(const gchar *label,
                                       GCallback callback,
                                       NionTab *tab)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(button, "flat");
    g_signal_connect(button, "clicked", callback, tab);
    return button;
}

static void nion_update_tab_context_menu(NionTab *tab)
{
    if (!tab || !tab->app)
        return;

    if (tab->tab_menu_mute_button) {
        gtk_button_set_label(GTK_BUTTON(tab->tab_menu_mute_button),
                             webkit_web_view_get_is_muted(tab->web_view)
                                ? "Unmute Tab" : "Mute Tab");
    }
    if (tab->tab_menu_pin_button)
        gtk_button_set_label(GTK_BUTTON(tab->tab_menu_pin_button),
                             tab->pinned ? "Unpin Tab" : "Pin Tab");

    if (tab->tab_menu_close_others_button)
        gtk_widget_set_sensitive(tab->tab_menu_close_others_button,
                                 nion_has_unpinned_tab_other_than(tab));
    if (tab->tab_menu_close_right_button)
        gtk_widget_set_sensitive(tab->tab_menu_close_right_button,
                                 nion_has_unpinned_tab_to_right(tab));
}

static GtkWidget *nion_create_tab_context_menu(NionTab *tab)
{
    GtkWidget *popover = gtk_popover_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *reload = nion_tab_menu_button("Reload", G_CALLBACK(on_tab_context_reload_clicked), tab);
    GtkWidget *duplicate = nion_tab_menu_button("Duplicate Tab", G_CALLBACK(on_tab_context_duplicate_clicked), tab);
    GtkWidget *pin = nion_tab_menu_button("Pin Tab", G_CALLBACK(on_tab_context_pin_clicked), tab);
    GtkWidget *mute = nion_tab_menu_button("Mute Tab", G_CALLBACK(on_tab_context_mute_clicked), tab);
    GtkWidget *close = nion_tab_menu_button("Close Tab", G_CALLBACK(on_tab_context_close_clicked), tab);
    GtkWidget *close_others = nion_tab_menu_button("Close Other Tabs", G_CALLBACK(on_tab_context_close_others_clicked), tab);
    GtkWidget *close_right = nion_tab_menu_button("Close Tabs to the Right", G_CALLBACK(on_tab_context_close_right_clicked), tab);

    gtk_box_append(GTK_BOX(box), reload);
    gtk_box_append(GTK_BOX(box), duplicate);
    gtk_box_append(GTK_BOX(box), pin);
    gtk_box_append(GTK_BOX(box), mute);
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(box), close);
    gtk_box_append(GTK_BOX(box), close_others);
    gtk_box_append(GTK_BOX(box), close_right);
    gtk_popover_set_child(GTK_POPOVER(popover), box);

    tab->tab_menu_popover = popover;
    tab->tab_menu_mute_button = mute;
    tab->tab_menu_pin_button = pin;
    tab->tab_menu_close_others_button = close_others;
    tab->tab_menu_close_right_button = close_right;
    return popover;
}

static void on_tab_context_pressed(GtkGestureClick *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data)
{
    (void)n_press;
    NionTab *tab = user_data;
    if (!tab || !tab->tab_menu_popover)
        return;

    nion_update_tab_context_menu(tab);
    GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(tab->tab_menu_popover), &rect);
    gtk_popover_popup(GTK_POPOVER(tab->tab_menu_popover));
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

GtkWidget *nion_make_tab_label(NionTab *tab)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "nion-tab-label");
    GtkWidget *favicon = gtk_picture_new();
    GtkWidget *pin = gtk_label_new("📌");
    GtkWidget *label = gtk_label_new("New Tab");
    GtkWidget *audio = gtk_button_new_from_icon_name("audio-volume-high-symbolic");
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
    GtkWidget *suspend = gtk_label_new("💤");

    gtk_widget_set_size_request(favicon, 16, 16);
    gtk_picture_set_can_shrink(GTK_PICTURE(favicon), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(favicon), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_visible(favicon, FALSE);
    gtk_widget_add_css_class(pin, "nion-tab-pin");
    gtk_widget_set_tooltip_text(pin, "Pinned tab");
    gtk_widget_set_visible(pin, FALSE);
    gtk_widget_add_css_class(suspend, "nion-tab-suspend");
    gtk_widget_set_tooltip_text(suspend, "Suspended tab — click to restore");
    gtk_widget_set_visible(suspend, FALSE);

    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars(GTK_LABEL(label), 14);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 22);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_tooltip_text(audio, "Mute tab");
    gtk_widget_add_css_class(audio, "flat");
    gtk_widget_add_css_class(audio, "nion-tab-audio");
    gtk_widget_set_focus_on_click(audio, FALSE);
    gtk_widget_set_visible(audio, FALSE);

    gtk_widget_set_tooltip_text(close, "Close tab");
    gtk_widget_add_css_class(close, "flat");
    gtk_widget_add_css_class(close, "nion-tab-close");

    gtk_box_append(GTK_BOX(box), favicon);
    gtk_box_append(GTK_BOX(box), pin);
    gtk_box_append(GTK_BOX(box), suspend);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), audio);
    gtk_box_append(GTK_BOX(box), close);

    tab->title_label = label;
    tab->favicon_picture = favicon;
    tab->pin_indicator = pin;
    tab->suspend_indicator = suspend;
    tab->audio_button = audio;
    tab->tab_label_box = box;
    tab->tab_close_button = close;
    nion_update_tab_pinned_ui(tab);
    g_signal_connect(audio, "clicked", G_CALLBACK(on_tab_audio_clicked), tab);
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close_clicked), tab);

    GtkWidget *popover = nion_create_tab_context_menu(tab);
    gtk_widget_set_parent(popover, box);
    GtkGesture *context_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(context_click, "pressed", G_CALLBACK(on_tab_context_pressed), tab);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(context_click));

    return box;
}

/* ---- Background Tab Discard (v2.1 #1) ---- */

void nion_tab_touch(NionTab *tab)
{
    if (!tab)
        return;
    tab->last_active_msec = g_get_monotonic_time() / 1000;
}

static void nion_set_suspend_chip(NionTab *tab, gboolean show)
{
    if (!tab || !tab->suspend_indicator)
        return;
    gtk_widget_set_visible(tab->suspend_indicator, show);
}

static gboolean nion_tab_has_real_uri(NionTab *tab)
{
    if (!tab || !tab->web_view)
        return FALSE;
    const gchar *uri = tab->display_uri_override && *tab->display_uri_override
        ? tab->display_uri_override
        : webkit_web_view_get_uri(tab->web_view);
    if (!uri || !*uri || g_str_equal(uri, "about:blank"))
        return FALSE;
    gchar *validation = NULL;
    gboolean ok = nion_validate_uri(uri, &validation);
    g_free(validation);
    return ok;
}

void nion_tab_discard(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook || !tab->web_view ||
        tab->discarded || tab->home_page || tab->error_page ||
        tab->web_process_terminated || tab->app->shutting_down)
        return;

    /* Never discard the active tab, pinned tabs, or audio-playing tabs. */
    if (nion_tab_is_current(tab) || tab->pinned ||
        webkit_web_view_is_playing_audio(tab->web_view))
        return;
    if (!nion_tab_has_real_uri(tab))
        return;

    const gchar *uri = tab->display_uri_override && *tab->display_uri_override
        ? tab->display_uri_override
        : webkit_web_view_get_uri(tab->web_view);

    g_clear_pointer(&tab->discard_uri, g_free);
    tab->discard_uri = g_strdup(uri);
    g_clear_pointer(&tab->discard_title, g_free);
    const gchar *title = webkit_web_view_get_title(tab->web_view);
    tab->discard_title = g_strdup(title && *title ? title : uri);
    tab->discard_muted = webkit_web_view_get_is_muted(tab->web_view);
    tab->discarded = TRUE;
    nion_set_suspend_chip(tab, TRUE);
    /* Keep the real page title in the strip (the suspended doc has none). */
    if (tab->title_label) {
        gtk_label_set_text(GTK_LABEL(tab->title_label), tab->discard_title);
        gtk_widget_set_tooltip_text(tab->title_label, tab->discard_title);
    }
    if (tab->favicon_picture)
        gtk_widget_set_visible(tab->favicon_picture, FALSE);

    /* Release the heavy page from the web process: stop the load and swap in
     * a tiny internal suspended document. The shell, tab-strip entry, pinned
     * state and saved URI all survive; the real page is re-fetched through
     * Tor when the user clicks the tab again. */
    webkit_web_view_stop_loading(tab->web_view);
    gchar *html = g_strdup_printf(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<style>body{font-family:system-ui,sans-serif;background:#f6f5f4;color:#414141;"
        "display:flex;align-items:center;justify-content:center;height:96vh;margin:0}"
        "div{text-align:center}.zz{font-size:46px}.m{font-size:15px;margin-top:12px}"
        ".s{font-size:12.5px;color:#8f8f8c;margin-top:6px}</style></head><body>"
        "<div><div class='zz'>💤</div><div class='m'>Suspended — click this tab to reload</div>"
        "<div class='s'>Background tab discarded to save memory</div></div></body></html>");
    webkit_web_view_set_zoom_level(tab->web_view, 1.0);
    webkit_web_view_load_html(tab->web_view, html, "about:blank");
    g_free(html);

    nion_schedule_session_save(tab->app);
}

void nion_resume_discarded_tab(NionTab *tab)
{
    if (!tab || !tab->app || !tab->app->notebook || !tab->web_view ||
        !tab->discarded)
        return;

    gchar *uri = g_strdup(tab->discard_uri);
    gboolean was_muted = tab->discard_muted;
    tab->discarded = FALSE;
    nion_set_suspend_chip(tab, FALSE);
    g_clear_pointer(&tab->discard_uri, g_free);
    g_clear_pointer(&tab->discard_title, g_free);

    if (was_muted)
        webkit_web_view_set_is_muted(tab->web_view, TRUE);

    if (uri && *uri && !g_str_equal(uri, "about:blank")) {
        nion_load_uri(tab, uri);
    } else {
        nion_load_home(tab);
    }
    g_free(uri);
    nion_tab_touch(tab);
    nion_schedule_session_save(tab->app);
}

gint nion_count_discarded_tabs(NionApp *app)
{
    if (!app || !app->notebook)
        return 0;
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    gint count = 0;
    for (gint i = 0; i < pages; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (tab && tab->discarded)
            count++;
    }
    return count;
}

void nion_discard_idle_tabs(NionApp *app)
{
    if (!app || !app->notebook || app->shutting_down)
        return;

    gint64 now = g_get_monotonic_time() / 1000;
    GtkNotebook *notebook = GTK_NOTEBOOK(app->notebook);
    gint pages = gtk_notebook_get_n_pages(notebook);
    gint current = gtk_notebook_get_current_page(notebook);

    for (gint i = 0; i < pages; i++) {
        if (i == current)
            continue;
        GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
        NionTab *tab = page ? g_object_get_data(G_OBJECT(page), "nion-tab") : NULL;
        if (!tab || tab->discarded || tab->pinned || tab->home_page ||
            tab->error_page || tab->web_process_terminated)
            continue;
        if (tab->last_active_msec > 0 &&
            (now - tab->last_active_msec) >= NION_TAB_DISCARD_AFTER_MS)
            nion_tab_discard(tab);
    }
}
