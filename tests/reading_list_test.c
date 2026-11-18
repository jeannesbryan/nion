/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for the reading list (roadmap T1-5).
 *
 * These call the production functions from src/reading-list.c and
 * src/history.c, not a copy of the logic. The harness builds a bare NionApp
 * with a reading list array and no widgets: every rule under test is decided in
 * the model, before any window exists.
 *
 * The point of this module is not "save a page" but "save something that can be
 * read with no network at all, and never save a trail from a Private Window".
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>

#include "types.h"
#include "history.h"
#include "reading-list.h"

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

static NionApp *
make_app(const gchar *store_path)
{
    NionApp *app = g_new0(NionApp, 1);
    app->reading_list = g_ptr_array_new_with_free_func(nion_reading_entry_free);
    app->reading_list_file = g_strdup(store_path);
    return app;
}

static void
free_app(NionApp *app)
{
    if (!app)
        return;
    if (app->reading_list)
        g_ptr_array_unref(app->reading_list);
    g_free(app->reading_list_file);
    g_free(app);
}

static gchar *
scratch_dir(void)
{
    return g_dir_make_tmp("nion-reading-XXXXXX", NULL);
}

/* ---------------------------------------------------------------- *
 * Whitespace normalization
 * ---------------------------------------------------------------- */

static void
test_normalize_collapses_runs(void)
{
    gchar *text = nion_reading_normalize_text("  first line  \n\n\n\n   second\tline   \n");
    ok(g_strcmp0(text, "first line\n\nsecond line") == 0,
       "indentation, tabs, trailing spaces and blank runs collapse");
    g_free(text);
}

static void
test_normalize_rejects_empty(void)
{
    gchar *text = nion_reading_normalize_text("   \n\t\n   \n");
    ok(text == NULL, "whitespace-only input yields nothing to save");
    g_free(text);

    ok(nion_reading_normalize_text(NULL) == NULL, "NULL input is refused");
}

static void
test_normalize_keeps_single_newlines(void)
{
    gchar *text = nion_reading_normalize_text("line one\nline two\nline three");
    ok(g_strcmp0(text, "line one\nline two\nline three") == 0,
       "a single newline between lines is preserved");
    g_free(text);
}

static void
test_normalize_caps_size(void)
{
    /* Far more text than the entry limit, on separate lines. */
    GString *big = g_string_new(NULL);
    for (gint i = 0; i < 40000; i++)
        g_string_append(big, "a line of text that is not tiny\n");

    gchar *text = nion_reading_normalize_text(big->str);
    ok(text != NULL, "oversized input still produces text");
    ok(strlen(text) <= NION_MAX_READING_TEXT_BYTES,
       "the result never exceeds NION_MAX_READING_TEXT_BYTES");
    /* Truncation must not split a UTF-8 character. */
    ok(g_utf8_validate(text, -1, NULL), "the truncated result is still valid UTF-8");

    g_free(text);
    g_string_free(big, TRUE);
}

static void
test_normalize_truncation_keeps_utf8_whole(void)
{
    /* Multi-byte characters past the cap, chosen so the cap does not land on a
     * character boundary: 262144 is not a multiple of 3, so a byte-wise cut of
     * three-byte characters leaves half a character behind. A two-byte fixture
     * would not prove anything, because the cap happens to divide evenly. */
    GString *big = g_string_new(NULL);
    for (gint i = 0; i < 100000; i++)
        g_string_append(big, "日");
    ok(strlen(big->str) > NION_MAX_READING_TEXT_BYTES,
       "the multi-byte fixture really does exceed the cap");

    gchar *text = nion_reading_normalize_text(big->str);
    ok(text != NULL, "multi-byte text produces a result");
    ok(g_utf8_validate(text, -1, NULL),
       "truncation never leaves half a multi-byte character");
    ok(strlen(text) <= NION_MAX_READING_TEXT_BYTES, "the cap still holds for multi-byte text");

    g_free(text);
    g_string_free(big, TRUE);
}

/* ---------------------------------------------------------------- *
 * Length-prefixed payload parsing
 * ---------------------------------------------------------------- */

static void
test_parse_extraction_roundtrip(void)
{
    const gchar *text = "Hello there.\n\nA second paragraph with more words.";
    gchar *payload = g_strdup_printf("%u:%s%u:%s%u:%s",
                                     (guint)strlen("https://example.com/a"),
                                     "https://example.com/a",
                                     (guint)strlen("A title"), "A title",
                                     (guint)strlen(text), text);

    gchar *uri = NULL;
    gchar *title = NULL;
    gchar *body = NULL;
    ok(nion_reading_parse_extraction(payload, &uri, &title, &body),
       "a well formed payload parses");
    ok(g_strcmp0(uri, "https://example.com/a") == 0, "the address is recovered");
    ok(g_strcmp0(title, "A title") == 0, "the title is recovered");
    ok(g_strcmp0(body, text) == 0, "the body is recovered byte for byte");

    g_free(uri);
    g_free(title);
    g_free(body);
    g_free(payload);
}

static void
test_parse_extraction_handles_delimiter_inside_text(void)
{
    /* The whole reason for lengths instead of a delimiter. */
    const gchar *body = "3:abc|5:hello|0:";
    gchar *payload = g_strdup_printf("%u:%s%u:%s%u:%s",
                                     (guint)strlen("https://e.example/"),
                                     "https://e.example/",
                                     (guint)strlen("T"), "T",
                                     (guint)strlen(body), body);

    gchar *uri = NULL, *title = NULL, *text = NULL;
    ok(nion_reading_parse_extraction(payload, &uri, &title, &text),
       "text containing digits and colons still parses");
    ok(g_strcmp0(text, body) == 0, "the body keeps its own colons intact");

    g_free(uri);
    g_free(title);
    g_free(text);
    g_free(payload);
}

static void
test_parse_extraction_handles_multibyte(void)
{
    const gchar *body = "Kucing itu duduk di atas tikar. 日本語のテキスト。";
    gsize body_bytes = strlen(body);
    gchar *payload = g_strdup_printf("%u:%s%u:%s%u:%s",
                                     (guint)strlen("https://multi.example/"),
                                     "https://multi.example/",
                                     (guint)strlen("Judul"), "Judul",
                                     (guint)body_bytes, body);

    gchar *uri = NULL, *title = NULL, *text = NULL;
    ok(nion_reading_parse_extraction(payload, &uri, &title, &text),
       "a payload with multi-byte text parses (byte lengths, not characters)");
    ok(g_strcmp0(text, body) == 0, "multi-byte text survives exactly");

    g_free(uri);
    g_free(title);
    g_free(text);
    g_free(payload);
}

static void
test_parse_extraction_refuses_truncated(void)
{
    gchar *uri = NULL, *title = NULL, *text = NULL;

    /* Claims 100 bytes but carries 5. */
    ok(!nion_reading_parse_extraction("100:short", &uri, &title, &text),
       "a field longer than the payload is refused");
    ok(uri == NULL && title == NULL && text == NULL,
       "nothing is allocated for a refused payload");

    ok(!nion_reading_parse_extraction("4:abcd4:ab", &uri, &title, &text),
       "a payload missing its third field is refused");

    ok(!nion_reading_parse_extraction("", &uri, &title, &text), "an empty payload is refused");
    ok(!nion_reading_parse_extraction(NULL, &uri, &title, &text), "NULL is refused");

    ok(!nion_reading_parse_extraction("4:abcd4:abcd4:abcd trailing", &uri, &title, &text),
       "trailing bytes are refused rather than ignored");

    ok(!nion_reading_parse_extraction("x:abc", &uri, &title, &text),
       "a non-numeric length is refused");

    ok(!nion_reading_parse_extraction("99999999999999999999:abc", &uri, &title, &text),
       "an absurd length is refused instead of overflowing");
}

/* ---------------------------------------------------------------- *
 * Worth saving?
 * ---------------------------------------------------------------- */

static void
test_worth_saving_threshold(void)
{
    ok(!nion_reading_text_is_worth_saving(NULL), "NULL text is not worth saving");
    ok(!nion_reading_text_is_worth_saving(""), "empty text is not worth saving");
    ok(!nion_reading_text_is_worth_saving("short"),
       "a few words are not worth saving, so a login wall stays out of the list");

    /* Real prose, not one giant token: a single 250-character run with no
     * spaces is a blob, not something someone reads. */
    GString *prose = g_string_new(NULL);
    for (gint i = 0; i < 30; i++)
        g_string_append(prose, "kata yang wajar ");
    ok(nion_reading_text_is_worth_saving(prose->str),
       "text past the threshold and made of words is worth saving");
    g_string_free(prose, TRUE);

    /* The complementary case spelled out, so the word threshold cannot be
     * quietly dropped without a failure. */
    gchar *blob = g_strnfill(NION_MIN_READABLE_CHARS + 50, 'a');
    ok(!nion_reading_text_is_worth_saving(blob),
       "one long token with no spaces is not reading material");
    g_free(blob);

    /* Many short words, so the byte count passes but the words must too. */
    GString *words = g_string_new(NULL);
    for (gint i = 0; i < 60; i++)
        g_string_append(words, "kata ");
    ok(nion_reading_text_is_worth_saving(words->str),
       "a normal paragraph passes both the byte and word thresholds");
    g_string_free(words, TRUE);

    /* The other direction, which is what makes the byte threshold more than
     * decoration: enough words to pass the word test, far too few bytes to be
     * anything but a menu. Only the byte check refuses this, so dropping that
     * check has to fail here. */
    GString *menu = g_string_new(NULL);
    for (gint i = 0; i < 40; i++)
        g_string_append(menu, "a ");
    ok(strlen(menu->str) < NION_MIN_READABLE_CHARS,
       "the menu fixture is shorter than the byte threshold");
    ok(nion_reading_count_words(menu->str) >= NION_MIN_READABLE_CHARS / 10,
       "the menu fixture is wordy enough to pass the word test");
    ok(!nion_reading_text_is_worth_saving(menu->str),
       "a short menu with many one-letter words is refused by the byte threshold");
    g_string_free(menu, TRUE);
}

static void
test_word_count(void)
{
    ok(nion_reading_count_words(NULL) == 0, "NULL has no words");
    ok(nion_reading_count_words("") == 0, "empty text has no words");
    ok(nion_reading_count_words("one") == 1, "one word counts as one");
    ok(nion_reading_count_words("one two three") == 3, "three words count as three");
    ok(nion_reading_count_words("  spaced \n\n out  ") == 2, "runs of whitespace do not inflate the count");
    ok(nion_reading_count_words("line one\nline two") == 4, "newlines separate words");
}

/* ---------------------------------------------------------------- *
 * Saving
 * ---------------------------------------------------------------- */

static void
make_long_text(GString *out, gint paragraphs)
{
    for (gint i = 0; i < paragraphs; i++)
        g_string_append(out, "This is a paragraph of readable text for the test.\n\n");
}

static void
test_add_and_dedupe(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);
    NionApp *app = make_app(path);

    GString *text = g_string_new(NULL);
    make_long_text(text, 4);

    ok(nion_reading_list_add(app, "https://one.example/a", "First", text->str),
       "a normal page is saved");
    ok(app->reading_list->len == 1, "the list holds one entry");

    /* Saving the same address again refreshes rather than duplicating. */
    ok(nion_reading_list_add(app, "https://one.example/a", "First again", text->str),
       "saving the same address twice succeeds");
    ok(app->reading_list->len == 1, "the same address does not create a second row");
    NionReadingEntry *entry = g_ptr_array_index(app->reading_list, 0);
    ok(g_strcmp0(entry->title, "First again") == 0, "the newer title replaces the older one");

    ok(nion_reading_list_add(app, "https://two.example/b", "Second", text->str),
       "a second, different page is saved");
    ok(app->reading_list->len == 2, "the list holds two entries");
    entry = g_ptr_array_index(app->reading_list, 0);
    ok(g_strcmp0(entry->uri, "https://two.example/b") == 0,
       "the newest save is first, which the window and the prune both assume");

    g_string_free(text, TRUE);
    free_app(app);
    g_free(path);

    gchar *stored = g_build_filename(dir, "reading.ini", NULL);
    g_remove(stored);
    g_free(stored);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_gate_refuses_non_browsable(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);
    NionApp *app = make_app(path);

    GString *text = g_string_new(NULL);
    make_long_text(text, 4);

    const gchar *blocked[] = {
        "file:///etc/passwd",
        "javascript:alert(1)",
        "data:text/html,<b>x</b>",
        "about:blank",
        "nion://home",
        "",
    };
    for (guint i = 0; i < G_N_ELEMENTS(blocked); i++) {
        ok(!nion_reading_list_add(app, blocked[i], "Blocked", text->str),
           "a scheme NiOn refuses to browse cannot be saved");
    }
    ok(app->reading_list->len == 0, "nothing was stored for any refused scheme");

    ok(!nion_reading_list_add(app, NULL, "No address", text->str), "a NULL address is refused");
    ok(!nion_reading_list_add(app, "https://ok.example/", "Too short", "tiny"),
       "text below the threshold is refused even for a valid address");
    ok(app->reading_list->len == 0, "still nothing stored");

    g_string_free(text, TRUE);
    free_app(app);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_private_window_never_saves(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);
    NionApp *app = make_app(path);
    app->is_private = TRUE;

    GString *text = g_string_new(NULL);
    make_long_text(text, 4);

    ok(!nion_reading_list_add(app, "https://secret.example/", "Private", text->str),
       "a Private Window refuses to save a page for later");
    ok(app->reading_list->len == 0, "nothing was stored from the private window");

    /* Even with a path somehow set, the private guard has to hold. */
    nion_reading_list_save(app);
    gboolean exists = g_file_test(path, G_FILE_TEST_EXISTS);
    ok(!exists, "a Private Window never writes the store, even if given a path");

    g_string_free(text, TRUE);
    free_app(app);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_private_window_without_store(void)
{
    /* The shape a real Private Window has: no path at all. */
    NionApp *app = make_app(NULL);
    app->is_private = TRUE;

    GString *text = g_string_new(NULL);
    make_long_text(text, 4);
    ok(!nion_reading_list_add(app, "https://secret.example/", "Private", text->str),
       "a private window with no store path cannot save either");
    g_string_free(text, TRUE);
    free_app(app);
}

static void
test_prune_caps_the_list(void)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func(nion_reading_entry_free);
    for (guint i = 0; i < NION_MAX_READING_LIST + 25; i++) {
        gchar *uri = g_strdup_printf("https://cap%u.example/", i);
        g_ptr_array_add(entries, nion_reading_entry_new(uri, "Capped", "text", 0));
        g_free(uri);
    }

    guint removed = nion_reading_prune(entries, NION_MAX_READING_LIST);
    ok(removed == 25, "exactly the overflow is removed");
    ok(entries->len == NION_MAX_READING_LIST, "the list is capped");

    NionReadingEntry *first = g_ptr_array_index(entries, 0);
    ok(g_strcmp0(first->uri, "https://cap0.example/") == 0,
       "the oldest saves are dropped, not the newest");

    g_ptr_array_unref(entries);
}

/* ---------------------------------------------------------------- *
 * Persistence
 * ---------------------------------------------------------------- */

static void
test_save_and_load_roundtrip(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);
    NionApp *app = make_app(path);

    GString *text = g_string_new(NULL);
    make_long_text(text, 5);
    ok(nion_reading_list_add(app, "https://kept.example/article", "Kept article", text->str),
       "the page is saved");

    NionReadingEntry *before = g_ptr_array_index(app->reading_list, 0);
    gchar *before_text = g_strdup(before->text);
    guint before_words = before->words;

    free_app(app);

    /* Fresh app, same file: this is what a restart looks like. */
    NionApp *reloaded = make_app(path);
    nion_reading_list_load(reloaded);
    ok(reloaded->reading_list->len == 1, "the entry survives a reload");
    NionReadingEntry *after = g_ptr_array_index(reloaded->reading_list, 0);
    ok(g_strcmp0(after->uri, "https://kept.example/article") == 0, "the address survives");
    ok(g_strcmp0(after->title, "Kept article") == 0, "the title survives");
    ok(g_strcmp0(after->text, before_text) == 0, "the text survives byte for byte");
    ok(after->words == before_words, "the word count survives");

    g_free(before_text);
    g_string_free(text, TRUE);
    free_app(reloaded);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_load_refuses_smuggled_uri(void)
{
    /* A hand-edited store must not smuggle in a scheme NiOn refuses to browse. */
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);

    GString *text = g_string_new(NULL);
    make_long_text(text, 5);

    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "reading-list", "format", NION_READING_FORMAT);
    g_key_file_set_integer(kf, "reading-list", "count", 2);
    g_key_file_set_string(kf, "entry-0", "uri", "file:///etc/passwd");
    g_key_file_set_string(kf, "entry-0", "title", "Not allowed");
    g_key_file_set_string(kf, "entry-0", "text", text->str);
    g_key_file_set_int64(kf, "entry-0", "saved_at", 1000);
    g_key_file_set_string(kf, "entry-1", "uri", "https://good.example/");
    g_key_file_set_string(kf, "entry-1", "title", "Allowed");
    g_key_file_set_string(kf, "entry-1", "text", text->str);
    g_key_file_set_int64(kf, "entry-1", "saved_at", 2000);
    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    g_key_file_unref(kf);
    g_file_set_contents(path, data, -1, NULL);
    g_free(data);

    NionApp *app = make_app(path);
    nion_reading_list_load(app);
    ok(app->reading_list->len == 1,
       "only the browsable address is loaded; the file scheme is dropped");
    NionReadingEntry *entry = g_ptr_array_index(app->reading_list, 0);
    ok(g_strcmp0(entry->uri, "https://good.example/") == 0, "the surviving entry is the http one");

    g_string_free(text, TRUE);
    free_app(app);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_load_missing_file_is_quiet(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "does-not-exist.ini", NULL);
    NionApp *app = make_app(path);

    nion_reading_list_load(app);
    ok(app->reading_list != NULL && app->reading_list->len == 0,
       "a missing store loads as an empty list instead of failing");

    free_app(app);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

static void
test_remove_and_clear(void)
{
    gchar *dir = scratch_dir();
    gchar *path = g_build_filename(dir, "reading.ini", NULL);
    NionApp *app = make_app(path);

    GString *text = g_string_new(NULL);
    make_long_text(text, 4);
    nion_reading_list_add(app, "https://one.example/", "One", text->str);
    nion_reading_list_add(app, "https://two.example/", "Two", text->str);
    ok(app->reading_list->len == 2, "two pages saved");

    nion_reading_list_remove(app, "https://one.example/");
    ok(app->reading_list->len == 1, "removing one address leaves the other");
    ok(nion_reading_find_uri(app->reading_list, "https://one.example/") < 0,
       "the removed address is gone");

    nion_reading_list_remove(app, "https://never-saved.example/");
    ok(app->reading_list->len == 1, "removing an unknown address changes nothing");

    nion_reading_list_clear(app);
    ok(app->reading_list->len == 0, "clearing empties the list");

    g_string_free(text, TRUE);
    free_app(app);
    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

int
main(void)
{
    test_normalize_collapses_runs();
    test_normalize_rejects_empty();
    test_normalize_keeps_single_newlines();
    test_normalize_caps_size();
    test_normalize_truncation_keeps_utf8_whole();

    test_parse_extraction_roundtrip();
    test_parse_extraction_handles_delimiter_inside_text();
    test_parse_extraction_handles_multibyte();
    test_parse_extraction_refuses_truncated();

    test_worth_saving_threshold();
    test_word_count();

    test_add_and_dedupe();
    test_gate_refuses_non_browsable();
    test_private_window_never_saves();
    test_private_window_without_store();
    test_prune_caps_the_list();

    test_save_and_load_roundtrip();
    test_load_refuses_smuggled_uri();
    test_load_missing_file_is_quiet();
    test_remove_and_clear();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
