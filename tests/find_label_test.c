/* Exercises src/find-label.c directly, the same file the browser links against.
 * Built and run by scripts/test-find-label-stage8.sh. */
#include "../src/find-label.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

static void expect_label(const char *what, guint index, guint total, const char *want)
{
    gchar *got = nion_find_format_label(index, total);
    checks++;
    if (g_strcmp0(got, want) != 0) {
        fprintf(stderr, "FAIL: %s: nion_find_format_label(%u, %u) = \"%s\", want \"%s\"\n",
                what, index, total, got, want);
        failures++;
    } else {
        printf("ok   %s (%u, %u) -> \"%s\"\n", what, index, total, got);
    }
    g_free(got);
}

static void expect_step(const char *what, guint index, guint total, int dir, guint want)
{
    guint got = nion_find_step_position(index, total, dir);
    checks++;
    if (got != want) {
        fprintf(stderr, "FAIL: %s: nion_find_step_position(%u, %u, %+d) = %u, want %u\n",
                what, index, total, dir, got, want);
        failures++;
    } else {
        printf("ok   %s (%u, %u, %+d) -> %u\n", what, index, total, dir, got);
    }
}

int main(void)
{
    /* The ordinary case, which is the whole point of the change. */
    expect_label("first of many", 1, 17, "1 of 17");
    expect_label("third of many", 3, 17, "3 of 17");
    expect_label("last of many", 17, 17, "17 of 17");

    /* Single match: the old label said "1 match", which hid the position. */
    expect_label("single match", 1, 1, "1 of 1");

    /* No matches: an empty string, not "0 of 0". */
    expect_label("no matches", 0, 0, "");
    expect_label("no matches with stale index", 5, 0, "");

    /* Clamping. A page that finishes loading mid-search can shrink the total
     * under a stale index; rendering 7 of 3 would be a visible lie. */
    expect_label("index past total", 7, 3, "3 of 3");
    expect_label("index zero with matches", 0, 9, "1 of 9");

    /* WebKit's own ceiling. It reports G_MAXUINT rather than a real count, so
     * the total must not be printed as a plain number. */
    expect_label("webkit ceiling", 3, G_MAXUINT, "3 of 1000+");
    expect_label("webkit ceiling at one", 1, G_MAXUINT, "1 of 1000+");

    /* Forward stepping, including the wraparound that has to match WebKit's
     * WRAP_AROUND so the label never reads "18 of 17". */
    expect_step("forward middle", 3, 17, 1, 4);
    expect_step("forward at last wraps", 17, 17, 1, 1);
    expect_step("forward at first", 1, 17, 1, 2);

    /* Backward stepping, including wrapping from the first match to the last. */
    expect_step("backward middle", 3, 17, -1, 2);
    expect_step("backward at first wraps", 1, 17, -1, 17);
    expect_step("backward at last", 17, 17, -1, 16);

    /* Stepping with no matches must not invent a position. */
    expect_step("forward with no matches", 0, 0, 1, 0);
    expect_step("backward with no matches", 0, 0, -1, 0);

    /* Stepping from a stale index clamps first, so the step is predictable. */
    expect_step("forward from stale", 99, 5, 1, 1);
    expect_step("backward from stale", 99, 5, -1, 4);

    /* Single match: stepping stays on the only match in both directions. */
    expect_step("forward single", 1, 1, 1, 1);
    expect_step("backward single", 1, 1, -1, 1);

    printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
