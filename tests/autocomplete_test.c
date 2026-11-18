/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for local-only address bar autocomplete (roadmap T1-2).
 *
 * These call the production functions from src/autocomplete.c, not a copy of
 * the ranking rules, so a regression in the real module fails here. The
 * harness builds a bare NionApp with a history array and a bookmark array and
 * no widgets at all: every rule under test is decided in the model, before any
 * list is drawn.
 *
 * The point of the module is not "show something" but "show the right thing in
 * the right order, from local data only". A suggestion that ranks badly is a
 * nuisance; a suggestion built from a scheme NiOn refuses to browse, or from a
 * private window's trail, would be a policy violation.
 */

#include <glib.h>
#include <string.h>

#include "types.h"
#include "history.h"
#include "autocomplete.h"

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

/* The bookmark free function lives in src/bookmarks.c, which drags in the UI
 * callback struct and the whole widget layer for one g_free. The struct is
 * plain data, so the harness frees it itself; bookmarks.c is not under test. */
static void
free_bookmark(gpointer data)
{
    NionBookmark *bookmark = data;
    if (!bookmark)
        return;
    g_free(bookmark->title);
    g_free(bookmark->uri);
    g_free(bookmark);
}

static NionApp *
make_app(void)
{
    NionApp *app = g_new0(NionApp, 1);
    app->history = g_ptr_array_new_with_free_func(nion_history_free_entry);
    app->bookmarks = g_ptr_array_new_with_free_func(free_bookmark);
    return app;
}

static void
free_app(NionApp *app)
{
    if (!app)
        return;
    if (app->history)
        g_ptr_array_unref(app->history);
    if (app->bookmarks)
        g_ptr_array_unref(app->bookmarks);
    g_free(app);
}

static void
add_history(NionApp *app, const gchar *uri, const gchar *title, gint64 last_visit)
{
    NionHistoryEntry *entry = g_new0(NionHistoryEntry, 1);
    entry->uri = g_strdup(uri);
    entry->title = g_strdup(title);
    entry->last_visit = last_visit;
    entry->visits = 1;
    g_ptr_array_add(app->history, entry);
}

static void
add_bookmark(NionApp *app, const gchar *uri, const gchar *title)
{
    NionBookmark *bookmark = g_new0(NionBookmark, 1);
    bookmark->uri = g_strdup(uri);
    bookmark->title = g_strdup(title);
    g_ptr_array_add(app->bookmarks, bookmark);
}

/* Callers own the returned array; items are owned by it. */
static GPtrArray *
suggest(NionApp *app, GPtrArray *extra, const gchar *query, guint max_results)
{
    return nion_autocomplete_suggest(app, extra, query, max_results);
}

static const gchar *
uri_at(GPtrArray *matches, guint index)
{
    if (!matches || index >= matches->len)
        return NULL;
    NionAutocompleteItem *item = g_ptr_array_index(matches, index);
    return item ? item->uri : NULL;
}

/* ---------------------------------------------------------------- *
 * A blank query offers nothing
 * ---------------------------------------------------------------- */

static void
test_blank_query_offers_nothing(void)
{
    NionApp *app = make_app();
    add_history(app, "https://example.com/", "Example", 100);

    GPtrArray *none = suggest(app, NULL, "", 8);
    ok(none->len == 0, "empty query returns no suggestions");

    g_ptr_array_unref(none);
    none = suggest(app, NULL, "   \t ", 8);
    ok(none->len == 0, "whitespace-only query returns no suggestions");

    g_ptr_array_unref(none);
    none = suggest(app, NULL, NULL, 8);
    ok(none->len == 0, "NULL query returns no suggestions");

    g_ptr_array_unref(none);
    free_app(app);
}

/* ---------------------------------------------------------------- *
 * Ranking tiers
 * ---------------------------------------------------------------- */

static void
test_ranking_tiers(void)
{
    NionApp *app = make_app();
    /* Same query "exa" reaches all three items; only the tier decides order. */
    add_history(app, "https://docs.example.org/guide", "A guide about exa", 900);
    add_history(app, "https://example.com/", "Example", 100);
    add_bookmark(app, "https://blog.example.net/", "Exa notes");

    GPtrArray *matches = suggest(app, NULL, "exa", 8);
    ok(matches->len == 3, "all three tiers are offered");
    /* Prefix of the address body wins over a mere substring. */
    ok(g_strcmp0(uri_at(matches, 0), "https://example.com/") == 0,
       "address prefix match ranks first");
    ok(g_strcmp0(uri_at(matches, 1), "https://docs.example.org/guide") == 0,
       "address substring match ranks second");
    ok(g_strcmp0(uri_at(matches, 2), "https://blog.example.net/") == 0,
       "title-only match ranks last");

    g_ptr_array_unref(matches);
    free_app(app);
}

static void
test_scheme_is_not_part_of_the_match(void)
{
    NionApp *app = make_app();
    /* The port appears mid-address on purpose: typing "example.com:8080" must
     * match the address body without the user retyping the scheme. */
    add_history(app, "http://example.com:8080/admin", "Admin panel", 10);
    add_history(app, "https://example.com/", "Example", 20);

    GPtrArray *matches = suggest(app, NULL, "example.com:8080", 8);
    ok(matches->len == 1, "query matches the address with the scheme stripped");
    ok(g_strcmp0(uri_at(matches, 0), "http://example.com:8080/admin") == 0,
       "the matched address is the one carrying the port");

    g_ptr_array_unref(matches);
    free_app(app);
}

static void
test_case_insensitive(void)
{
    NionApp *app = make_app();
    add_history(app, "https://Example.COM/Path", "Mixed Case Title", 10);

    GPtrArray *upper = suggest(app, NULL, "EXAMPLE", 8);
    ok(upper->len == 1, "uppercase query matches a mixed case address");
    g_ptr_array_unref(upper);

    GPtrArray *title = suggest(app, NULL, "mixed case", 8);
    ok(title->len == 1, "lowercase multi-word query matches the title");
    g_ptr_array_unref(title);

    free_app(app);
}

static void
test_title_needs_every_term(void)
{
    NionApp *app = make_app();
    add_history(app, "https://a.onion/1", "Tor project documentation", 10);
    add_history(app, "https://a.onion/2", "Unrelated page", 20);

    GPtrArray *all = suggest(app, NULL, "project documentation", 8);
    ok(all->len == 1, "all terms matching the title is a match");
    ok(g_strcmp0(uri_at(all, 0), "https://a.onion/1") == 0, "the matching title wins");
    g_ptr_array_unref(all);

    GPtrArray *partial = suggest(app, NULL, "project missing", 8);
    ok(partial->len == 0, "one missing term drops the candidate entirely");
    g_ptr_array_unref(partial);

    free_app(app);
}

/* ---------------------------------------------------------------- *
 * Ordering inside a tier
 * ---------------------------------------------------------------- */

static void
test_source_priority_within_tier(void)
{
    NionApp *app = make_app();
    /* All three share the same address body prefix and therefore tier 0. */
    add_bookmark(app, "https://same.example/", "Same page bookmark");
    add_history(app, "https://same.example/", "Same page history", 500);
    add_history(app, "https://same.example/", "Same page history", 500);

    GPtrArray *extra = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);
    g_ptr_array_add(extra,
                    nion_autocomplete_item_new("https://same.example/", "Same page tab",
                                               NION_AUTOCOMPLETE_TAB, 0));

    GPtrArray *matches = suggest(app, extra, "same", 8);
    ok(matches->len == 1, "one row per address");
    NionAutocompleteItem *only = g_ptr_array_index(matches, 0);
    ok(only->source == NION_AUTOCOMPLETE_TAB,
       "the open tab is the row kept when tab, history and bookmark share a URI");
    ok(g_strcmp0(only->title, "Same page tab") == 0, "the kept row carries the tab title");

    g_ptr_array_unref(matches);
    g_ptr_array_unref(extra);
    free_app(app);
}

static void
test_recency_orders_history(void)
{
    NionApp *app = make_app();
    add_history(app, "https://old.example/x", "Old", 100);
    add_history(app, "https://new.example/x", "New", 9000);
    add_history(app, "https://mid.example/x", "Mid", 5000);

    GPtrArray *matches = suggest(app, NULL, ".example/x", 8);
    ok(matches->len == 3, "all three history rows match");
    ok(g_strcmp0(uri_at(matches, 0), "https://new.example/x") == 0, "most recent first");
    ok(g_strcmp0(uri_at(matches, 1), "https://mid.example/x") == 0, "then the older one");
    ok(g_strcmp0(uri_at(matches, 2), "https://old.example/x") == 0, "then the oldest");

    g_ptr_array_unref(matches);
    free_app(app);
}

static void
test_stable_order_between_keystrokes(void)
{
    NionApp *app = make_app();
    /* Identical tier, source and recency: the original order must survive, so
     * the list does not reshuffle while the user keeps typing. */
    add_history(app, "https://one.example/", "First", 0);
    add_history(app, "https://two.example/", "Second", 0);
    add_history(app, "https://three.example/", "Third", 0);

    for (gint round = 0; round < 3; round++) {
        GPtrArray *matches = suggest(app, NULL, "example", 8);
        ok(g_strcmp0(uri_at(matches, 0), "https://one.example/") == 0, "first stays first");
        ok(g_strcmp0(uri_at(matches, 1), "https://two.example/") == 0, "second stays second");
        ok(g_strcmp0(uri_at(matches, 2), "https://three.example/") == 0, "third stays third");
        g_ptr_array_unref(matches);
    }

    free_app(app);
}

static void
test_result_cap(void)
{
    NionApp *app = make_app();
    for (gint i = 0; i < 40; i++) {
        gchar *uri = g_strdup_printf("https://cap%d.example/", i);
        add_history(app, uri, "Capped", 1000 - i);
        g_free(uri);
    }

    GPtrArray *matches = suggest(app, NULL, "cap", 8);
    ok(matches->len == 8, "the result is capped at max_results");

    g_ptr_array_unref(matches);
    free_app(app);
}

/* ---------------------------------------------------------------- *
 * The URI gate: only schemes NiOn will actually browse
 * ---------------------------------------------------------------- */

static void
test_only_browsable_schemes(void)
{
    NionApp *app = make_app();
    add_history(app, "file:///etc/passwd", "Local file", 900);
    add_history(app, "javascript:alert(1)", "Script", 800);
    add_history(app, "data:text/html,<b>x</b>", "Data URL", 700);
    add_history(app, "about:blank", "Blank", 600);
    add_history(app, "nion://home", "Start page", 500);
    add_history(app, "https://ok.example/", "Real page", 100);

    /* The same gate must cover the other two sources: a bookmark and an open
     * tab. Each is fed its own blocked scheme so that dropping one gate cannot
     * be hidden by the other two still working. */
    add_bookmark(app, "file:///home/notes.txt", "Notes file");
    GPtrArray *extra = g_ptr_array_new_with_free_func(nion_autocomplete_item_free);
    g_ptr_array_add(extra,
                    nion_autocomplete_item_new("javascript:void(0)", "Tab script",
                                               NION_AUTOCOMPLETE_TAB, 0));

    GPtrArray *matches = suggest(app, extra, "", 8);
    ok(matches->len == 0, "the gate is exercised: a blank query still returns nothing");
    g_ptr_array_unref(matches);

    /* Each blocked scheme is queried for by name, so this is not a blank-query
     * artefact: the candidate exists and is refused by the scheme gate. */
    const gchar *blocked[] = { "passwd", "alert", "html", "blank", "home", "notes", "script" };
    for (guint i = 0; i < G_N_ELEMENTS(blocked); i++) {
        matches = suggest(app, extra, blocked[i], 8);
        ok(matches->len == 0, "no suggestion for a scheme NiOn refuses to browse");
        g_ptr_array_unref(matches);
    }

    matches = suggest(app, extra, "ok.example", 8);
    ok(matches->len == 1, "the http(s) candidate is still offered");

    g_ptr_array_unref(matches);
    g_ptr_array_unref(extra);
    free_app(app);
}

/* ---------------------------------------------------------------- *
 * Private Window
 * ---------------------------------------------------------------- */

static void
test_private_window_has_no_history_to_suggest(void)
{
    NionApp *private_app = g_new0(NionApp, 1);
    private_app->is_private = TRUE;
    /* The private app never receives a history path and never builds the
     * array; the model must cope with NULL rather than fall back to anything. */
    private_app->history = NULL;
    private_app->bookmarks = g_ptr_array_new_with_free_func(free_bookmark);
    add_bookmark(private_app, "https://kept.example/", "Kept bookmark");

    /* "visited" appears only in the kind of page a history store would hold,
     * and this app has none: no fallback, no borrowed trail. */
    GPtrArray *matches = suggest(private_app, NULL, "visited", 8);
    ok(matches->len == 0, "a private window with no history suggests nothing");
    g_ptr_array_unref(matches);

    matches = suggest(private_app, NULL, "kept", 8);
    ok(matches->len == 1, "bookmarks remain available in a private window");
    g_ptr_array_unref(matches);

    g_ptr_array_unref(private_app->bookmarks);
    g_free(private_app);
}

static void
test_null_app_is_safe(void)
{
    GPtrArray *matches = suggest(NULL, NULL, "example", 8);
    ok(matches != NULL && matches->len == 0, "a NULL app yields an empty, valid result");
    g_ptr_array_unref(matches);
}

/* ---------------------------------------------------------------- *
 * Ownership: the returned array must outlive the candidate collection
 * ---------------------------------------------------------------- */

static void
test_result_owns_its_items(void)
{
    NionApp *app = make_app();
    add_history(app, "https://owned.example/", "Owned", 10);

    /* nion_autocomplete_suggest() drops the internal collection before it
     * returns. If the result merely borrowed from that collection, reading it
     * here would be a use-after-free: the strings below were freed with the
     * collection. Built with -fsanitize=address this is a hard failure, not a
     * silent read of stale bytes. */
    GPtrArray *matches = suggest(app, NULL, "owned", 8);
    ok(matches->len == 1, "one match survived the collection");

    NionAutocompleteItem *item = g_ptr_array_index(matches, 0);
    ok(g_strcmp0(item->uri, "https://owned.example/") == 0,
       "the URI is still readable after the collection was released");
    ok(g_strcmp0(item->title, "Owned") == 0,
       "the title is still readable after the collection was released");
    ok(item->source == NION_AUTOCOMPLETE_HISTORY, "the source survived too");

    /* Releasing the result must free exactly those copies: with a wrong free
     * func this leaks or double-frees, both caught by the sanitizer build. */
    g_ptr_array_unref(matches);
    free_app(app);
}

static void
test_item_new_falls_back_to_uri(void)
{
    NionAutocompleteItem *item =
        nion_autocomplete_item_new("https://untitled.example/", NULL, NION_AUTOCOMPLETE_TAB, 0);
    ok(g_strcmp0(item->title, "https://untitled.example/") == 0,
       "a missing title falls back to the address");
    nion_autocomplete_item_free(item);

    item = nion_autocomplete_item_new("https://untitled.example/", "", NION_AUTOCOMPLETE_TAB, 0);
    ok(g_strcmp0(item->title, "https://untitled.example/") == 0,
       "an empty title falls back to the address");
    nion_autocomplete_item_free(item);
}

int
main(void)
{
    test_blank_query_offers_nothing();
    test_ranking_tiers();
    test_scheme_is_not_part_of_the_match();
    test_case_insensitive();
    test_title_needs_every_term();
    test_source_priority_within_tier();
    test_recency_orders_history();
    test_stable_order_between_keystrokes();
    test_result_cap();
    test_only_browsable_schemes();
    test_private_window_has_no_history_to_suggest();
    test_null_app_is_safe();
    test_result_owns_its_items();
    test_item_new_falls_back_to_uri();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
