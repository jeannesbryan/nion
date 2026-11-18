/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for the local browsing history store (roadmap T1-1).
 *
 * These call the production functions from src/history.c, not a copy of the
 * logic, so a regression in the real module fails here. The harness builds a
 * bare NionApp (no widgets, no Tor, no WebKit session) because every rule under
 * test is decided before any UI exists: what may be stored, what must expire,
 * what a Private Window may never write, and what deletion really deletes.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "history.h"

static gint failures = 0;
static gint checks = 0;

static void
ok(gboolean condition, const gchar *what)
{
    checks++;
    if (!condition) {
        failures++;
        g_printerr("FAIL: %s\n", what);
    }
}

static gchar *
scratch_dir(void)
{
    gchar *dir = g_dir_make_tmp("nion-history-XXXXXX", NULL);
    g_assert_nonnull(dir);
    return dir;
}

static NionApp *
make_app(const gchar *dir)
{
    NionApp *app = g_new0(NionApp, 1);
    app->history_file = g_build_filename(dir, "history.ini", NULL);
    return app;
}

static void
free_app(NionApp *app)
{
    if (!app)
        return;
    g_free(app->history_file);
    if (app->history)
        g_ptr_array_unref(app->history);
    g_free(app);
}

static gchar *
read_file(const gchar *path)
{
    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, NULL))
        return NULL;
    return contents;
}

static void
test_uri_gate(void)
{
    /* Accepted: the two schemes NiOn can actually browse, including .onion
     * hosts (they are plain http/https addresses served through Tor). */
    ok(nion_history_uri_is_recordable("https://example.com/a?b=1#c"), "accepts https");
    ok(nion_history_uri_is_recordable("http://example.com/"), "accepts http");
    ok(nion_history_uri_is_recordable("http://abcdefghijklmnop.onion/page"), "accepts onion host");
    ok(nion_history_uri_is_recordable("https://sub.example.com:8443/x"), "accepts port");

    /* Rejected: everything that is not a browsed page. */
    ok(!nion_history_uri_is_recordable(NULL), "rejects NULL");
    ok(!nion_history_uri_is_recordable(""), "rejects empty");
    ok(!nion_history_uri_is_recordable("javascript:alert(1)"), "rejects javascript:");
    ok(!nion_history_uri_is_recordable("file:///etc/passwd"), "rejects file:");
    ok(!nion_history_uri_is_recordable("data:text/html,hello"), "rejects data:");
    ok(!nion_history_uri_is_recordable("about:blank"), "rejects about:blank");
    ok(!nion_history_uri_is_recordable("nion://home"), "rejects internal scheme");
    ok(!nion_history_uri_is_recordable("http://"), "rejects missing host");
    ok(!nion_history_uri_is_recordable("https://exa\nmple.com/"), "rejects embedded newline");
    ok(!nion_history_uri_is_recordable("not a uri at all"), "rejects non-URI");

    gchar *huge = g_malloc0(NION_MAX_SAVED_URI_BYTES + 64);
    memset(huge, 'a', NION_MAX_SAVED_URI_BYTES + 32);
    memcpy(huge, "https://example.com/", 20);
    ok(!nion_history_uri_is_recordable(huge), "rejects over-long URI");
    g_free(huge);
}

static void
test_title_normalize(void)
{
    gchar *t1 = nion_history_normalize_title(NULL, "https://example.com/x");
    ok(g_strcmp0(t1, "https://example.com/x") == 0, "NULL title falls back to URI");
    g_free(t1);

    gchar *t2 = nion_history_normalize_title("   ", "https://example.com/y");
    ok(g_strcmp0(t2, "https://example.com/y") == 0, "blank title falls back to URI");
    g_free(t2);

    gchar *t3 = nion_history_normalize_title("  Real Title  ", "https://example.com/z");
    ok(g_strcmp0(t3, "Real Title") == 0, "title is stripped");
    g_free(t3);

    gchar *long_title = g_malloc0(600);
    memset(long_title, 'T', 500);
    gchar *t4 = nion_history_normalize_title(long_title, "https://example.com/w");
    ok(g_utf8_strlen(t4, -1) == NION_MAX_HISTORY_TITLE_CHARS, "long title truncated to limit");
    g_free(t4);
    g_free(long_title);
}

static void
test_record_and_dedupe(const gchar *dir)
{
    NionApp *app = make_app(dir);
    nion_history_load(app);

    nion_history_record(app, "https://one.example/a", "First page");
    ok(app->history->len == 1, "first visit stored");
    ok(g_file_test(app->history_file, G_FILE_TEST_EXISTS), "history file written on first visit");

    NionHistoryEntry *first = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(first->title, "First page") == 0, "title stored");
    ok(first->visits == 1, "visit counter starts at one");
    gint64 first_time = first->last_visit;

    nion_history_record(app, "https://one.example/a", "First page");
    ok(app->history->len == 1, "revisiting the same URI does not duplicate the row");
    first = g_ptr_array_index(app->history, 0);
    ok(first->visits == 2, "revisiting bumps the counter");
    ok(first->last_visit >= first_time, "revisiting updates the timestamp");

    nion_history_record(app, "https://two.example/b", "Second page");
    ok(app->history->len == 2, "second URI stored");
    NionHistoryEntry *head = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(head->uri, "https://two.example/b") == 0, "newest entry is first");

    nion_history_record(app, "https://one.example/a", "First page");
    head = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(head->uri, "https://one.example/a") == 0,
       "revisiting an older page moves it back to the front");
    ok(app->history->len == 2, "still two entries after the move");

    /* Not a page: must not enter the store at all. */
    nion_history_record(app, "javascript:alert(1)", "evil");
    ok(app->history->len == 2, "non-http URI never stored");

    free_app(app);
}

static void
test_private_window_never_writes(const gchar *dir)
{
    /* Shape 1: a Private Window, which has no history path at all. */
    NionApp *priv = g_new0(NionApp, 1);
    priv->is_private = TRUE;
    nion_history_record(priv, "https://secret.example/", "Secret");
    ok(priv->history == NULL || priv->history->len == 0,
       "private window keeps nothing in memory");

    gchar *path = g_build_filename(dir, "private-probe.ini", NULL);
    ok(!g_file_test(path, G_FILE_TEST_EXISTS), "private window created no file");
    g_free(path);
    g_free(priv);

    /* Shape 2: the defensive case, a private app that somehow has a path. The
     * module must still refuse, because the guard is not the missing path. */
    NionApp *priv2 = g_new0(NionApp, 1);
    priv2->is_private = TRUE;
    priv2->history_file = g_build_filename(dir, "should-not-exist.ini", NULL);
    nion_history_record(priv2, "https://secret.example/", "Secret");
    ok(priv2->history == NULL || priv2->history->len == 0,
       "private window records nothing even with a path set");
    nion_history_save(priv2);
    ok(!g_file_test(priv2->history_file, G_FILE_TEST_EXISTS),
       "private window cannot save even with a path set");
    g_free(priv2->history_file);
    g_free(priv2);
}

static void
test_persistence_round_trip(const gchar *dir)
{
    NionApp *app = make_app(dir);
    nion_history_load(app);
    nion_history_record(app, "https://alpha.example/1", "Alpha");
    nion_history_record(app, "https://beta.example/2", "Beta");
    nion_history_record(app, "https://gamma.example/3", "Gamma");
    free_app(app);

    NionApp *reloaded = make_app(dir);
    nion_history_load(reloaded);
    ok(reloaded->history->len == 3, "all entries reload from disk");

    NionHistoryEntry *head = g_ptr_array_index(reloaded->history, 0);
    ok(g_strcmp0(head->uri, "https://gamma.example/3") == 0,
       "newest stays first after reload");
    ok(g_strcmp0(head->title, "Gamma") == 0, "title survives reload");
    ok(head->visits >= 1, "visit counter survives reload");
    free_app(reloaded);
}

static void
test_retention_and_cap(const gchar *dir)
{
    const gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    const gint64 expired = now - ((gint64)NION_HISTORY_RETENTION_DAYS + 10) * 86400;

    gchar *path = g_build_filename(dir, "history.ini", NULL);
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "History", "count", 2);
    g_key_file_set_string(kf, "Entry-0", "uri", "https://old.example/");
    g_key_file_set_string(kf, "Entry-0", "title", "Old");
    g_key_file_set_int64(kf, "Entry-0", "last_visit", now);
    g_key_file_set_integer(kf, "Entry-0", "visits", 1);
    g_key_file_set_string(kf, "Entry-1", "uri", "https://ancient.example/");
    g_key_file_set_string(kf, "Entry-1", "title", "Ancient");
    g_key_file_set_int64(kf, "Entry-1", "last_visit", expired);
    g_key_file_set_integer(kf, "Entry-1", "visits", 1);
    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    g_file_set_contents(path, data, -1, NULL);
    g_free(data);
    g_key_file_free(kf);

    NionApp *app = make_app(dir);
    nion_history_load(app);
    ok(app->history->len == 1, "expired entry dropped on load");
    NionHistoryEntry *only = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(only->uri, "https://old.example/") == 0, "the surviving entry is the recent one");

    /* The pruned result is written back, so an idle profile cannot keep
     * expired addresses on disk forever. */
    gchar *on_disk = read_file(path);
    ok(on_disk && strstr(on_disk, "ancient.example") == NULL,
       "expired entry removed from the file, not only from memory");
    g_free(on_disk);
    free_app(app);

    /* Cap: more entries than the limit are cut down to the limit. */
    gchar *capdir = scratch_dir();
    gchar *cappath = g_build_filename(capdir, "history.ini", NULL);
    GKeyFile *cap = g_key_file_new();
    const guint extra = NION_MAX_HISTORY + 50;
    g_key_file_set_integer(cap, "History", "count", (gint)extra);
    for (guint i = 0; i < extra; i++) {
        gchar *group = g_strdup_printf("Entry-%u", i);
        gchar *uri = g_strdup_printf("https://cap.example/%u", i);
        g_key_file_set_string(cap, group, "uri", uri);
        g_key_file_set_string(cap, group, "title", uri);
        g_key_file_set_int64(cap, group, "last_visit", now - (gint64)i);
        g_key_file_set_integer(cap, group, "visits", 1);
        g_free(uri);
        g_free(group);
    }
    gchar *capdata = g_key_file_to_data(cap, NULL, NULL);
    g_file_set_contents(cappath, capdata, -1, NULL);
    g_free(capdata);
    g_key_file_free(cap);

    NionApp *capapp = g_new0(NionApp, 1);
    capapp->history_file = g_strdup(cappath);
    nion_history_load(capapp);
    ok(capapp->history->len == NION_MAX_HISTORY, "store capped at the maximum entry count");
    NionHistoryEntry *caphead = g_ptr_array_index(capapp->history, 0);
    ok(g_strcmp0(caphead->uri, "https://cap.example/0") == 0,
       "cap keeps the newest entries");
    free_app(capapp);
    g_free(cappath);
    g_rmdir(capdir);
    g_free(capdir);
    g_free(path);
}

static void
test_delete_paths(const gchar *dir)
{
    NionApp *app = make_app(dir);
    nion_history_load(app);
    nion_history_record(app, "https://keep.example/1", "Keep");
    nion_history_record(app, "https://drop.example/a", "Drop A");
    nion_history_record(app, "https://drop.example/b", "Drop B");
    ok(app->history->len == 3, "three entries before deletion");

    ok(nion_history_forget_host(app->history, "drop.example") == 2,
       "forget_host removes only that host");
    ok(app->history->len == 1, "other host survives forget_host");

    nion_history_record(app, "https://drop.example/c", "Drop C");
    nion_history_forget_site(app, "https://drop.example/c");
    ok(app->history->len == 1, "forget_site removes the site's entries via the host");

    /* clear removes both the entries and the file: New Identity relies on it. */
    nion_history_clear(app);
    ok(app->history->len == 0, "clear empties the entry list");
    ok(!g_file_test(app->history_file, G_FILE_TEST_EXISTS),
       "clear removes the history file from disk");
    free_app(app);
}

static void
test_title_upgrade(const gchar *dir)
{
    NionApp *app = make_app(dir);
    nion_history_load(app);

    /* Stored with a placeholder because the page had no title yet. */
    nion_history_record(app, "https://late.example/page", NULL);
    NionHistoryEntry *entry = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(entry->title, "https://late.example/page") == 0,
       "entry starts with the URI as placeholder");

    nion_history_upgrade_title(app, "https://late.example/page", "Real Late Title");
    entry = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(entry->title, "Real Late Title") == 0,
       "late title replaces the placeholder");

    nion_history_upgrade_title(app, "https://late.example/page", "Worse Title");
    entry = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(entry->title, "Real Late Title") == 0,
       "an already real title is not overwritten later");

    /* A private window must not be able to upgrade (and therefore write). */
    NionApp *priv = g_new0(NionApp, 1);
    priv->is_private = TRUE;
    priv->history = g_ptr_array_new_with_free_func(nion_history_free_entry);
    NionHistoryEntry *raw = nion_history_entry_new("https://late.example/page", NULL, 1);
    g_ptr_array_add(priv->history, raw);
    nion_history_upgrade_title(priv, "https://late.example/page", "Private Title");
    ok(g_strcmp0(raw->title, "https://late.example/page") == 0,
       "private window cannot upgrade a title");
    g_ptr_array_unref(priv->history);
    g_free(priv);

    free_app(app);
}

static void
test_corrupt_file_is_quarantined(const gchar *dir)
{
    gchar *path = g_build_filename(dir, "history.ini", NULL);
    g_file_set_contents(path, "[History\ncount = not a number\n\x01\x02", -1, NULL);

    NionApp *app = make_app(dir);
    nion_history_load(app);
    ok(app->history->len == 0, "corrupt file yields an empty store instead of a crash");
    ok(!g_file_test(app->history_file, G_FILE_TEST_EXISTS),
       "corrupt file moved aside rather than reused");

    GDir *scan = g_dir_open(dir, 0, NULL);
    gboolean quarantined = FALSE;
    if (scan) {
        const gchar *name;
        while ((name = g_dir_read_name(scan)) != NULL) {
            if (strstr(name, "history.ini.corrupt-") != NULL)
                quarantined = TRUE;
        }
        g_dir_close(scan);
    }
    ok(quarantined, "quarantine copy kept on disk");

    /* A store that was just quarantined must still be usable. */
    nion_history_record(app, "https://fresh.example/", "Fresh");
    ok(app->history->len == 1, "store usable after recovery");
    free_app(app);
    g_free(path);
}

static void
test_entry_from_file_with_bad_uri_is_dropped(const gchar *dir)
{
    gchar *sub = g_build_filename(dir, "baduri", NULL);
    g_mkdir_with_parents(sub, 0700);
    gchar *path = g_build_filename(sub, "history.ini", NULL);

    const gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "History", "count", 2);
    g_key_file_set_string(kf, "Entry-0", "uri", "https://good.example/");
    g_key_file_set_string(kf, "Entry-0", "title", "Good");
    g_key_file_set_int64(kf, "Entry-0", "last_visit", now);
    g_key_file_set_integer(kf, "Entry-0", "visits", 1);
    g_key_file_set_string(kf, "Entry-1", "uri", "javascript:alert(1)");
    g_key_file_set_string(kf, "Entry-1", "title", "Bad");
    g_key_file_set_int64(kf, "Entry-1", "last_visit", now);
    g_key_file_set_integer(kf, "Entry-1", "visits", 1);
    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    g_file_set_contents(path, data, -1, NULL);
    g_free(data);
    g_key_file_free(kf);

    NionApp *app = g_new0(NionApp, 1);
    app->history_file = g_strdup(path);
    nion_history_load(app);
    ok(app->history->len == 1, "file entry with a non-http URI is dropped on load");
    NionHistoryEntry *only = g_ptr_array_index(app->history, 0);
    ok(g_strcmp0(only->uri, "https://good.example/") == 0, "the valid entry survives");
    free_app(app);

    g_remove(path);
    g_rmdir(sub);
    g_free(path);
    g_free(sub);
}

/* Remove a scratch directory and everything in it, so a failing run leaves no
 * profile behind and no test can inherit another test's store. */
static void
cleanup_dir(const gchar *dir)
{
    GDir *scan = g_dir_open(dir, 0, NULL);
    if (!scan)
        return;

    const gchar *name;
    while ((name = g_dir_read_name(scan)) != NULL) {
        gchar *full = g_build_filename(dir, name, NULL);
        if (g_file_test(full, G_FILE_TEST_IS_DIR)) {
            GDir *inner = g_dir_open(full, 0, NULL);
            if (inner) {
                const gchar *child;
                while ((child = g_dir_read_name(inner)) != NULL) {
                    gchar *path = g_build_filename(full, child, NULL);
                    g_remove(path);
                    g_free(path);
                }
                g_dir_close(inner);
            }
            g_rmdir(full);
        } else {
            g_remove(full);
        }
        g_free(full);
    }
    g_dir_close(scan);
    g_rmdir(dir);
}

/* Every test gets its own empty profile. Sharing one directory made the
 * persistence and deletion tests inherit entries from earlier tests, which
 * looked like product failures until the counts were traced back to the shared
 * file. Isolation is the harness's job, not the store's. */
static void
run_isolated(void (*test)(const gchar *dir))
{
    gchar *dir = scratch_dir();
    test(dir);
    cleanup_dir(dir);
    g_free(dir);
}

int
main(void)
{
    test_uri_gate();
    test_title_normalize();
    run_isolated(test_record_and_dedupe);
    run_isolated(test_private_window_never_writes);
    run_isolated(test_persistence_round_trip);
    run_isolated(test_title_upgrade);
    run_isolated(test_delete_paths);
    run_isolated(test_retention_and_cap);
    run_isolated(test_corrupt_file_is_quarantined);
    run_isolated(test_entry_from_file_with_bad_uri_is_dropped);

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
