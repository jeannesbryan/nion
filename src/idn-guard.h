/* Copyright (C) 2026 Jeannes Bryan */

#ifndef NION_IDN_GUARD_H
#define NION_IDN_GUARD_H

#include <glib.h>

/* Homograph and IDN protection, computed locally (v2.3.5, roadmap T2-7).
 *
 * The attack: an attacker registers a name that looks like a well-known one
 * using characters from another script. Cyrillic "а" (U+0430) and Latin "a"
 * are different characters that render almost identically, so "аpple.com"
 * with a Cyrillic first letter is a different host than "apple.com" and reads
 * the same in the address bar.
 *
 * Everything here is computed on the machine, from the address itself: no
 * blocklist is fetched, no reputation service is asked, nothing about the page
 * leaves the browser. That is the only kind of protection this browser can
 * honestly offer.
 *
 * Two facts make this decidable without a network:
 *   - The ASCII form of a name is well defined (IDNA/punycode), so NiOn can
 *     always show the user the characters that will actually be resolved.
 *   - Characters belong to scripts, and some script combinations inside a single
 *     label are normal while others are how homographs are built.
 *
 * What this module deliberately does NOT claim: it is not a complete confusables
 * database. It covers the character ranges and lookalikes that matter for the
 * attacks this browser is likely to meet, and it says nothing rather than
 * guessing when a name is outside that knowledge.
 */

typedef enum {
    /* A plain ASCII host: nothing to report, and nothing shown. Keeping this
     * the default matters, because a warning that fires on ordinary addresses
     * is a warning people learn to ignore. */
    NION_IDN_PLAIN = 0,
    /* ASCII, but at least one label starts with xn--: an encoded IDN. Readable
     * nowhere, so the UI can say what it decodes to. */
    NION_IDN_ENCODED,
    /* Contains non-ASCII characters: an IDN as served or typed. */
    NION_IDN_UNICODE,
} NionIdnKind;

typedef enum {
    /* Nothing worth telling the user. */
    NION_IDN_SAFE = 0,
    /* An IDN, but with no sign of a homograph. Show the ASCII form so the name
     * is not silently accepted at face value. */
    NION_IDN_NOTICE,
    /* Mixed scripts in one label, or a label built from lookalikes: this is what
     * an impersonation looks like. */
    NION_IDN_SUSPICIOUS,
} NionIdnRisk;

typedef struct {
    NionIdnKind kind;
    NionIdnRisk risk;
    /* The address with non-ASCII characters as an ASCII (punycode) form. Always
     * set for a non-empty input, and what the resolver actually uses. */
    gchar *ascii;
    /* The unicode form, for comparison next to the ASCII form. */
    gchar *unicode;
    /* Scripts found in the labels that carry them, for example
     * "Latin + Cyrillic". NULL when there is nothing to say. */
    gchar *scripts;
    /* One short sentence for a tooltip. NULL when risk is SAFE. */
    gchar *reason;
} NionIdnVerdict;

/* Inspect a host (not a full URI; callers extract the host first). A NULL or
 * empty host yields a verdict with kind PLAIN and risk SAFE. */
NionIdnVerdict *nion_idn_inspect(const gchar *host);
void nion_idn_verdict_free(NionIdnVerdict *verdict);

/* Extract the host from a URI for inspection, tolerating the shapes an address
 * bar produces (missing scheme, userinfo, port, trailing dot). Returns newly
 * allocated text, or NULL when there is no host at all. */
gchar *nion_idn_host_from_uri(const gchar *uri);

/* TRUE when the text contains a character outside ASCII. */
gboolean nion_idn_has_non_ascii(const gchar *text);

/* TRUE when any label of this ASCII host starts with "xn--". */
gboolean nion_idn_has_encoded_label(const gchar *host);

#endif /* NION_IDN_GUARD_H */
