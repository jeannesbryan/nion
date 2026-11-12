/* Exercises src/load-error.c directly, the same file the browser links against.
 * Built and run by scripts/test-load-error-stage10.sh.
 *
 * The bug this guards: NiOn's strict-HTTPS upgrade ignores the http:// policy
 * decision and loads the https:// twin. WebKitGTK 2.52 then reports the dropped
 * frame as WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE, which
 * used to be treated as a page failure: the user got "Website unavailable
 * through Tor" on a site that was loading fine, and only a manual reload
 * recovered it.
 */
#include "../src/load-error.h"

#include <glib.h>
#include <stdio.h>
#include <webkit/webkit.h>

static int failures = 0;
static int checks = 0;

static void expect(const char *what, GQuark domain, gint code, gboolean want)
{
    GError *error = g_error_new_literal(domain, code, "synthetic");
    gboolean got = nion_load_error_is_navigation_interruption(error);

    checks++;
    if (got != want) {
        fprintf(stderr, "FAIL: %s: got %s, want %s\n",
                what, got ? "interruption" : "page failure",
                want ? "interruption" : "page failure");
        failures++;
    } else {
        printf("ok   %s -> %s\n", what, got ? "interruption (no error page)" : "page failure");
    }
    g_error_free(error);
}

int main(void)
{
    /* A NULL error must not crash and must not swallow a failure. */
    checks++;
    if (nion_load_error_is_navigation_interruption(NULL)) {
        fprintf(stderr, "FAIL: NULL error treated as an interruption\n");
        failures++;
    } else {
        printf("ok   NULL error -> page failure\n");
    }

    /* --- interruptions: no error page may be built --- */

    /* Another navigation replaced this one (pre-existing behaviour). */
    expect("network/cancelled",
           WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED, TRUE);

    /* The strict-HTTPS upgrade replacing a provisional http:// frame.
     * This is the regression that produced a false error page. */
    expect("policy/frame-load-interrupted-by-policy-change",
           WEBKIT_POLICY_ERROR,
           WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE, TRUE);

    /* --- real failures: the error page must still be shown --- */

    expect("network/unknown-host",
           WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL, FALSE);
    expect("network/transport",
           WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_TRANSPORT, FALSE);
    expect("network/generic-failure",
           WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_FAILED, FALSE);
    expect("policy/cannot-show-uri",
           WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_CANNOT_SHOW_URI, FALSE);
    expect("policy/cannot-use-restricted-port",
           WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_CANNOT_USE_RESTRICTED_PORT, FALSE);
    expect("network/file-does-not-exist",
           WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_FILE_DOES_NOT_EXIST, FALSE);

    /* A code that only exists in one domain must not leak across domains:
     * policy 102 is an interruption, but network 102 is not. */
    expect("cross-domain/network-102", WEBKIT_NETWORK_ERROR, 102, FALSE);
    /* ... and policy 302 (the cancelled code) is not an interruption either. */
    expect("cross-domain/policy-302", WEBKIT_POLICY_ERROR, 302, FALSE);

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("LOAD ERROR CLASSIFICATION: FAIL\n");
        return 1;
    }
    printf("LOAD ERROR CLASSIFICATION: PASS\n");
    return 0;
}
