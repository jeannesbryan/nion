/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for tab filtering (roadmap T2-6).
 *
 * These call the production functions from src/tab-filter.c. The rules decide
 * which rows a menu draws, so a rule that is "almost right" hides tabs the user
 * is looking for, which reads as lost work rather than as a bad filter.
 */

#include <glib.h>

#include "tab-filter.h"

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

/* ---------------------------------------------------------------- *
 * Nothing typed means nothing filtered
 * ---------------------------------------------------------------- */

static void
test_inactive_query_keeps_everything(void)
{
    ok(!nion_tab_filter_is_active(NULL), "NULL is not an active query");
    ok(!nion_tab_filter_is_active(""), "an empty query is not active");
    ok(!nion_tab_filter_is_active("   "), "spaces alone are not an active query");
    ok(!nion_tab_filter_is_active("\t\n"), "tabs and newlines alone are not active");

    ok(nion_tab_filter_matches(NULL, "Anything", "https://a.example/"),
       "a NULL query keeps every tab");
    ok(nion_tab_filter_matches("", "Anything", "https://a.example/"),
       "an empty query keeps every tab");
    ok(nion_tab_filter_matches("   ", "Anything", "https://a.example/"),
       "a whitespace query keeps every tab");

    ok(nion_tab_filter_is_active("a"), "a single character is an active query");
    ok(nion_tab_filter_is_active("  docs  "), "a padded word is active");
}

/* ---------------------------------------------------------------- *
 * Case folding
 * ---------------------------------------------------------------- */

static void
test_case_insensitive(void)
{
    ok(nion_tab_filter_matches("DOCS", "Documents", "https://docs.example/"),
       "an uppercase query finds a lowercase title and address");
    ok(nion_tab_filter_matches("docs", "DOCUMENTS", "https://DOCS.example/"),
       "a lowercase query finds an uppercase title and address");
    ok(nion_tab_filter_matches("DoCs", "Documents", "https://docs.example/"),
       "mixed case works in both directions");
}

static void
test_non_ascii_case_folding(void)
{
    /* Case folding has to be UTF-8 aware, not ASCII only: a title in another
     * script must still be found by a differently cased query. */
    ok(nion_tab_filter_matches("ÄRGER", "Ärger im Büro", NULL),
       "an uppercase non-ASCII query finds a mixed case title");
    ok(nion_tab_filter_matches("ärger", "Ärger im Büro", NULL),
       "a lowercase non-ASCII query finds it too");
    ok(nion_tab_filter_matches("büro", "Ärger im Büro", NULL),
       "a non-ASCII term matches mid-title");
}

/* ---------------------------------------------------------------- *
 * Which fields are searched
 * ---------------------------------------------------------------- */

static void
test_matches_title_or_address(void)
{
    ok(nion_tab_filter_matches("documents", "Documents", "https://docs.example/"),
       "a title term matches");
    ok(nion_tab_filter_matches("docs.example", "Documents", "https://docs.example/"),
       "an address term matches even when the title does not contain it");
    ok(!nion_tab_filter_matches("unrelated", "Documents", "https://docs.example/"),
       "a term in neither field drops the tab");
}

static void
test_missing_fields(void)
{
    /* A tab with neither field has nothing to match against. */
    ok(!nion_tab_filter_matches("documents", NULL, NULL),
       "a tab with no title and no address cannot satisfy an active query");
    ok(nion_tab_filter_matches(NULL, NULL, NULL),
       "but it still shows while nothing is typed");

    ok(nion_tab_filter_matches("untitled", NULL, "https://untitled.example/"),
       "a tab with no title still matches on its address");
    ok(nion_tab_filter_matches("just a title", "Just a title", NULL),
       "a tab with no address still matches on its title");
}

/* ---------------------------------------------------------------- *
 * Every term must match
 * ---------------------------------------------------------------- */

static void
test_all_terms_required(void)
{
    const gchar *title = "Tor Project documentation";
    const gchar *uri = "https://community.torproject.org/docs";

    ok(nion_tab_filter_matches("tor", title, uri), "one matching term keeps the tab");
    ok(nion_tab_filter_matches("tor documentation", title, uri),
       "two terms that both match keep the tab");
    ok(nion_tab_filter_matches("documentation tor", title, uri),
       "the order of the terms does not matter");

    /* This is the property that makes typing narrow: one missing term is
     * enough to drop the row, so the list can only shrink. */
    ok(!nion_tab_filter_matches("tor missing", title, uri),
       "one missing term drops the tab");
    ok(!nion_tab_filter_matches("tor project missing", title, uri),
       "a single missing term among several still drops it");

    /* Terms may be satisfied by different fields. */
    ok(nion_tab_filter_matches("documentation community", title, uri),
       "one term may match the title while another matches the address");
}

static void
test_whitespace_in_query(void)
{
    const gchar *title = "Two Words";
    const gchar *uri = "https://two.example/";

    ok(nion_tab_filter_matches("  two   words  ", title, uri),
       "extra spaces between terms do not break matching");
    ok(nion_tab_filter_matches("two\twords", title, uri),
       "a tab character also separates terms");

    /* A trailing space must not become an empty term that matches everything,
     * which would keep tabs that do not match. */
    ok(!nion_tab_filter_matches("two missing ", title, uri),
       "a trailing space does not turn into a wildcard term");
}

static void
test_narrowing_property(void)
{
    /* The behaviour the feature exists for: as the query grows, the set of
     * matching tabs never grows. Asserted over a small population so a rule
     * change that makes typing widen the list fails here. */
    const gchar *titles[] = {
        "Tor Project",
        "Tor browser manual",
        "Mail",
        "Project notes",
    };
    const gchar *uris[] = {
        "https://torproject.org/",
        "https://tb-manual.torproject.org/",
        "https://mail.example/",
        "https://notes.example/project",
    };
    const guint count = G_N_ELEMENTS(titles);

    const gchar *queries[] = {
        "t", "to", "tor", "tor p", "tor pro", "tor proj", "tor proje", "tor projec",
    };
    guint previous = count + 1;
    for (guint q = 0; q < G_N_ELEMENTS(queries); q++) {
        guint matches = 0;
        for (guint i = 0; i < count; i++) {
            if (nion_tab_filter_matches(queries[q], titles[i], uris[i]))
                matches++;
        }
        gchar *label = g_strdup_printf("query \"%s\" matched %u tabs", queries[q], matches);
        ok(matches <= previous, label);
        g_free(label);
        previous = matches;
    }

    /* And the property has teeth: the longest query must actually narrow. */
    ok(previous < count, "typing a longer query really does narrow the list");
}

int
main(void)
{
    test_inactive_query_keeps_everything();
    test_case_insensitive();
    test_non_ascii_case_folding();
    test_matches_title_or_address();
    test_missing_fields();
    test_all_terms_required();
    test_whitespace_in_query();
    test_narrowing_property();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
