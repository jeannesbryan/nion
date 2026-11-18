/* Copyright (C) 2026 Jeannes Bryan */

/* Proves the data saver rules actually stop requests (roadmap T2-10).
 *
 * Compiling a filter only proves the JSON parsed. This harness loads a real page
 * that references an image, a web font and a video, and lets the test script
 * count what the server received. With the filter off, everything must arrive;
 * with it on, only the document may.
 *
 * The control run matters as much as the blocked run: "no request reached the
 * server" is only meaningful if the same harness can show a request arriving.
 * Both are required by the script, so a harness that silently failed to load
 * anything could not pass as a success.
 *
 * Usage: data_saver_harness <url> <on|off>
 * Prints "HARNESS: loaded" once the page has finished loading.
 */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <stdlib.h>
#include <string.h>

#include "data-saver.h"

/* The harness deliberately does not link the window layer: it exists to prove
 * the rules block requests, and dragging in the whole UI would make it a second
 * program to keep working. The status line is therefore stubbed here, and this
 * stub is the only reason the harness can link data-saver.c at all. */
void
nion_show_status(NionApp *app, const gchar *text)
{
    (void)app;
    (void)text;
}

static GMainLoop *loop = NULL;
static int exit_code = 0;

static gboolean
quit_soon(gpointer data)
{
    (void)data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void
on_load_changed(WebKitWebView *view, WebKitLoadEvent event, gpointer user_data)
{
    (void)user_data;
    if (event == WEBKIT_LOAD_FINISHED) {
        g_print("HARNESS: loaded\n");
        fflush(stdout);
        /* Give the network a moment to finish any request it already started, so
         * an unblocked image has time to reach the server. */
        g_timeout_add(1200, quit_soon, NULL);
    }
    (void)view;
}

static void
on_filter_saved(GObject *source, GAsyncResult *result, gpointer user_data)
{
    WebKitWebView *view = WEBKIT_WEB_VIEW(user_data);
    GError *error = NULL;
    WebKitUserContentFilter *filter =
        webkit_user_content_filter_store_save_finish(
            WEBKIT_USER_CONTENT_FILTER_STORE(source), result, &error);

    if (!filter) {
        g_printerr("HARNESS: filter failed to compile: %s\n",
                   error ? error->message : "unknown error");
        g_clear_error(&error);
        exit_code = 1;
        g_main_loop_quit(loop);
        return;
    }

    g_print("HARNESS: filter compiled\n");
    webkit_user_content_manager_add_filter(
        webkit_web_view_get_user_content_manager(view), filter);
    g_print("HARNESS: filter applied\n");
    fflush(stdout);

    const char *url = g_object_get_data(G_OBJECT(view), "target-url");
    webkit_web_view_load_uri(view, url);
}

int
main(int argc, char **argv)
{
    if (argc < 3) {
        g_printerr("usage: %s <url> <on|off>\n", argv[0]);
        return 2;
    }
    const char *url = argv[1];
    gboolean use_filter = g_strcmp0(argv[2], "on") == 0;

    gtk_init();

    loop = g_main_loop_new(NULL, FALSE);

    /* No persistent session: this harness must not leave state behind, and it
     * must behave the same on every run. */
    WebKitNetworkSession *session = webkit_network_session_new_ephemeral();
    /* No webkit_web_view_new_with_network_session() in this WebKit: the session
     * is a construct property. */
    WebKitWebView *view = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW, "network-session", session, NULL));
    g_object_set_data(G_OBJECT(view), "target-url", (gpointer)url);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    gtk_window_new(); /* keep the widget tree alive without showing a window */

    if (use_filter) {
        gchar *dir = g_dir_make_tmp("nion-ds-harness-XXXXXX", NULL);
        if (!dir) {
            g_printerr("HARNESS: no temp dir\n");
            return 1;
        }
        WebKitUserContentFilterStore *store =
            webkit_user_content_filter_store_new(dir);
        const gchar *rules = nion_data_saver_rules_json();
        GBytes *bytes = g_bytes_new_static(rules, strlen(rules));
        /* The store is what the product uses too, so this exercises the same
         * compilation path rather than a hand-made filter object. */
        webkit_user_content_filter_store_save(store, "data-saver-harness", bytes,
                                              NULL, on_filter_saved, view);
        g_bytes_unref(bytes);
    } else {
        g_print("HARNESS: filter off (control run)\n");
        fflush(stdout);
        webkit_web_view_load_uri(view, url);
    }

    g_main_loop_run(loop);
    return exit_code;
}
