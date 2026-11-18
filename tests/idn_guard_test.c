/* Copyright (C) 2026 Jeannes Bryan */

/* Behavioural tests for homograph/IDN protection (roadmap T2-7).
 *
 * Two failure modes matter here and they pull in opposite directions:
 *
 *   - Missing an imitation: a Cyrillic "а" that should have been exposed
 *     passes as a plain ASCII address. That is the attack.
 *   - Crying wolf: every legitimate international name gets an alarming
 *     warning, so people learn to dismiss it, and the warning stops working.
 *
 * Both directions are tested. A test that only proves the first would push the
 * implementation straight into the second.
 */

#include <glib.h>

#include "idn-guard.h"

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
 * Host extraction
 * ---------------------------------------------------------------- */

static void
test_host_from_uri(void)
{
    struct {
        const gchar *uri;
        const gchar *expected;
    } cases[] = {
        { "https://example.com/", "example.com" },
        { "https://example.com", "example.com" },
        { "http://example.com/path?q=1#frag", "example.com" },
        { "https://example.com:8443/path", "example.com" },
        { "https://user:pass@example.com/path", "example.com" },
        { "https://user@example.com/", "example.com" },
        { "example.com", "example.com" },
        { "example.com:8080/x", "example.com" },
        { "https://example.com./", "example.com" },
        { "https://bücher.example/", "bücher.example" },
        { "http://[::1]:8080/", "[::1]" },
        { "http://127.0.0.1:8080/admin", "127.0.0.1" },
        { "http://abcdef.onion/", "abcdef.onion" },
    };

    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        gchar *host = nion_idn_host_from_uri(cases[i].uri);
        gchar *label = g_strdup_printf("%s yields host %s", cases[i].uri, cases[i].expected);
        ok(g_strcmp0(host, cases[i].expected) == 0, label);
        g_free(label);
        g_free(host);
    }

    ok(nion_idn_host_from_uri(NULL) == NULL, "NULL has no host");
    ok(nion_idn_host_from_uri("") == NULL, "an empty address has no host");
    ok(nion_idn_host_from_uri("https://") == NULL, "a scheme with no host yields nothing");
    ok(nion_idn_host_from_uri("/path/only") == NULL, "a bare path has no host");
}

/* ---------------------------------------------------------------- *
 * ASCII form: checked against published vectors, not assumed
 * ---------------------------------------------------------------- */

static void
test_punycode_vectors(void)
{
    /* RFC 3492 section 7.1 style vectors plus the IDNA examples that browsers
     * are expected to get right. If GLib's implementation ever changed, or if
     * this code started producing its own ASCII form, these would fail. */
    struct {
        const gchar *unicode;
        const gchar *ascii;
    } cases[] = {
        { "bücher.example", "xn--bcher-kva.example" },
        { "münchen.de", "xn--mnchen-3ya.de" },
        { "пример.рф", "xn--e1afmkfd.xn--p1ai" },
        { "例子.测试", "xn--fsqu00a.xn--0zwm56d" },
        { "例え.テスト", "xn--r8jz45g.xn--zckzah" },
    };

    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        NionIdnVerdict *verdict = nion_idn_inspect(cases[i].unicode);
        gchar *label = g_strdup_printf("%s encodes to %s", cases[i].unicode,
                                       cases[i].ascii);
        ok(g_strcmp0(verdict->ascii, cases[i].ascii) == 0, label);
        g_free(label);
        nion_idn_verdict_free(verdict);
    }

    /* An ASCII host must come back unchanged: the ASCII form is the input. */
    NionIdnVerdict *plain = nion_idn_inspect("example.com");
    ok(g_strcmp0(plain->ascii, "example.com") == 0, "an ASCII host is its own ASCII form");
    nion_idn_verdict_free(plain);
}

/* ---------------------------------------------------------------- *
 * Plain ASCII stays silent
 * ---------------------------------------------------------------- */

static void
test_plain_ascii_is_safe(void)
{
    const gchar *hosts[] = {
        "example.com",
        "news.ycombinator.com",
        "127.0.0.1",
        "abcdef.onion",
        "sub.domain.co.uk",
    };

    for (guint i = 0; i < G_N_ELEMENTS(hosts); i++) {
        NionIdnVerdict *verdict = nion_idn_inspect(hosts[i]);
        gchar *label = g_strdup_printf("%s is not flagged", hosts[i]);
        ok(verdict->kind == NION_IDN_PLAIN && verdict->risk == NION_IDN_SAFE, label);
        g_free(label);
        ok(verdict->reason == NULL, "a safe host carries no warning text");
        nion_idn_verdict_free(verdict);
    }

    NionIdnVerdict *empty = nion_idn_inspect(NULL);
    ok(empty->kind == NION_IDN_PLAIN && empty->risk == NION_IDN_SAFE,
       "a missing host is not flagged");
    nion_idn_verdict_free(empty);
}

/* ---------------------------------------------------------------- *
 * The attack: Cyrillic and Latin in one label
 * ---------------------------------------------------------------- */

static void
test_mixed_latin_cyrillic_is_suspicious(void)
{
    /* "аpple.com" where the first letter is Cyrillic U+0430. This is the whole
     * point of the feature. */
    const gchar *attack = "\u0430pple.com";
    NionIdnVerdict *verdict = nion_idn_inspect(attack);
    ok(verdict->kind == NION_IDN_UNICODE, "a non-ASCII host is recognised as an IDN");
    ok(verdict->risk == NION_IDN_SUSPICIOUS,
       "Latin mixed with Cyrillic in one label is suspicious");
    ok(verdict->reason != NULL && strstr(verdict->reason, "xn--") != NULL,
       "the warning shows the ASCII form so the real name is visible");
    ok(g_strcmp0(verdict->ascii, "xn--pple-43d.com") == 0,
       "the ASCII form exposes the Cyrillic letter");
    ok(verdict->scripts != NULL && strstr(verdict->scripts, "Cyrillic") != NULL,
       "the scripts involved are named");
    nion_idn_verdict_free(verdict);

    /* The same construction with a Greek letter where Latin is expected. */
    NionIdnVerdict *greek = nion_idn_inspect("\u03BFne.example");
    ok(greek->risk == NION_IDN_SUSPICIOUS, "Latin mixed with Greek in one label is suspicious");
    nion_idn_verdict_free(greek);
}

/* A homograph does not have to be in the first label. "shop.аpple.com" puts the
 * dangerous one second, which is also how a subdomain-based imitation looks. */
static void
test_attack_in_a_later_label(void)
{
    NionIdnVerdict *later = nion_idn_inspect("shop.\u0430pple.com");
    ok(later->risk == NION_IDN_SUSPICIOUS,
       "a mixed-script label after a plain one is still caught");
    nion_idn_verdict_free(later);

    NionIdnVerdict *encoded_later = nion_idn_inspect("shop.xn--pple-43d.com");
    ok(encoded_later->risk == NION_IDN_SUSPICIOUS,
       "an encoded imitation in a later label is still caught");
    nion_idn_verdict_free(encoded_later);

    /* And the quiet direction for the same shape: a plain name stays plain even
     * though it has several labels. */
    NionIdnVerdict *plain = nion_idn_inspect("shop.example.com");
    ok(plain->risk == NION_IDN_SAFE, "a plain multi-label name stays quiet");
    nion_idn_verdict_free(plain);
}

static void
test_all_lookalike_label_is_suspicious(void)
{
    /* Every character is Cyrillic, and every one of them has a Latin lookalike:
     * "аррӏе" reads as "apple" but is entirely Cyrillic. A single-script check
     * alone would miss this, which is why the lookalike table exists. */
    const gchar *attack = "\u0430\u0440\u0440\u04CF\u0435.com";
    NionIdnVerdict *verdict = nion_idn_inspect(attack);
    ok(verdict->risk == NION_IDN_SUSPICIOUS,
       "a label built entirely from Latin lookalikes is suspicious");
    ok(verdict->reason != NULL && strstr(verdict->reason, "imitation") != NULL,
       "the warning says what the name may be imitating");
    nion_idn_verdict_free(verdict);

    /* A fully Cyrillic word that does NOT read as Latin must stay a notice, not
     * an accusation: "пример" is an ordinary Russian word. */
    NionIdnVerdict *ordinary = nion_idn_inspect("\u043F\u0440\u0438\u043C\u0435\u0440.example");
    ok(ordinary->risk == NION_IDN_NOTICE,
       "ordinary Cyrillic text is a notice, not an accusation");
    nion_idn_verdict_free(ordinary);
}

/* ---------------------------------------------------------------- *
 * Legitimate international names must not be accused
 * ---------------------------------------------------------------- */

static void
test_legitimate_idn_is_notice_only(void)
{
    const gchar *hosts[] = {
        "bücher.example",   /* German */
        "münchen.de",       /* German */
        "пример.рф",        /* Russian, single script */
        "例子.测试",         /* Chinese, Han only */
        "例え.テスト",       /* Japanese: Han + Hiragana + Katakana */
        "한국.kr",           /* Korean, Hangul */
        "παράδειγμα.gr",    /* Greek, single script */
    };

    for (guint i = 0; i < G_N_ELEMENTS(hosts); i++) {
        NionIdnVerdict *verdict = nion_idn_inspect(hosts[i]);
        gchar *label = g_strdup_printf("%s is a notice, not an accusation", hosts[i]);
        ok(verdict->risk == NION_IDN_NOTICE, label);
        g_free(label);
        ok(verdict->reason != NULL, "an IDN still explains which name it resolves to");
        nion_idn_verdict_free(verdict);
    }
}

static void
test_japanese_mixed_scripts_are_allowed(void)
{
    /* Japanese legitimately writes Han beside Hiragana and Katakana, and Korean
     * writes Han beside Hangul. Treating every script mix as an attack would
     * flag ordinary names in those languages, which is exactly the false
     * positive that makes a warning worthless. */
    const gchar *hosts[] = {
        "例え.テスト",
        "\u6F22\u5B57\u3042.example",  /* Han + Hiragana */
        "\u6F22\u5B57\u30AB.example",  /* Han + Katakana */
        "\u6F22\uAD6D.example",        /* Han + Hangul */
    };

    for (guint i = 0; i < G_N_ELEMENTS(hosts); i++) {
        NionIdnVerdict *verdict = nion_idn_inspect(hosts[i]);
        gchar *label = g_strdup_printf("%s stays a notice (legitimate script mix)", hosts[i]);
        ok(verdict->risk == NION_IDN_NOTICE, label);
        g_free(label);
        nion_idn_verdict_free(verdict);
    }
}

/* ---------------------------------------------------------------- *
 * Encoded names
 * ---------------------------------------------------------------- */

static void
test_encoded_label_is_reported(void)
{
    NionIdnVerdict *verdict = nion_idn_inspect("xn--bcher-kva.example");
    ok(verdict->kind == NION_IDN_ENCODED, "an xn-- label is recognised as an encoded name");
    ok(verdict->risk == NION_IDN_NOTICE, "an encoded name is a notice, not an accusation");
    ok(verdict->reason != NULL && strstr(verdict->reason, "bücher") != NULL,
       "the warning states what the encoded name decodes to");
    nion_idn_verdict_free(verdict);

    /* An encoded imitation must still be caught. This is the shape the address
     * bar actually holds, because WebKit normalizes a homograph URL to its
     * ASCII form: analyzing only the ASCII text would report a harmless
     * "encoded name" for an active attack. */
    NionIdnVerdict *encoded_attack = nion_idn_inspect("xn--pple-43d.com");
    ok(encoded_attack->risk == NION_IDN_SUSPICIOUS,
       "an encoded name that decodes to a mixed-script label is suspicious");
    /* The address bar already shows the ASCII form here, so the useful half is
     * the decoded one: it is what the name reads as, and the sentence says what
     * to suspect about it. */
    ok(encoded_attack->reason != NULL &&
       strstr(encoded_attack->reason, "imitation") != NULL,
       "the warning says the encoded name may be an imitation");
    ok(encoded_attack->unicode != NULL && nion_idn_has_non_ascii(encoded_attack->unicode),
       "the decoded form is available, which is the reading the user cannot see");
    ok(encoded_attack->scripts != NULL && strstr(encoded_attack->scripts, "Cyrillic") != NULL,
       "the scripts are named even though the host was already ASCII");
    nion_idn_verdict_free(encoded_attack);

    /* And an ordinary encoded name stays a notice: bücher is not an attack. */
    NionIdnVerdict *encoded_ordinary = nion_idn_inspect("xn--bcher-kva.example");
    ok(encoded_ordinary->risk == NION_IDN_NOTICE,
       "an ordinary encoded name stays a notice, not an accusation");
    nion_idn_verdict_free(encoded_ordinary);

    /* Detection has to look at every label, not only the first. */
    ok(nion_idn_has_encoded_label("www.xn--bcher-kva.example"),
       "an encoded label after a plain one is still found");
    ok(nion_idn_has_encoded_label("XN--BCHER-KVA.example"),
       "the xn-- prefix is matched without regard to case");
    ok(!nion_idn_has_encoded_label("example.com"),
       "a plain name has no encoded label");
    ok(!nion_idn_has_encoded_label("xn--notatstart.example") == FALSE,
       "a name starting with xn-- is an encoded label");
    ok(!nion_idn_has_encoded_label("notxn--here.example"),
       "xn-- inside a label but not at its start is not an encoded label");
    ok(!nion_idn_has_encoded_label(NULL), "NULL has no encoded label");
}

/* ---------------------------------------------------------------- *
 * Helpers
 * ---------------------------------------------------------------- */

static void
test_has_non_ascii(void)
{
    ok(!nion_idn_has_non_ascii("example.com"), "a plain address is ASCII");
    ok(!nion_idn_has_non_ascii(""), "an empty string is ASCII");
    ok(!nion_idn_has_non_ascii(NULL), "NULL is not non-ASCII");
    ok(nion_idn_has_non_ascii("bücher"), "a German name is non-ASCII");
    ok(nion_idn_has_non_ascii("\u0430pple"), "a Cyrillic letter is non-ASCII");
}

/* A verdict must always be usable by a caller, even for input it did not flag:
 * the ASCII form is what the UI shows, so it can never be NULL. */
static void
test_verdict_always_has_ascii(void)
{
    const gchar *inputs[] = { "example.com", "bücher.example", "xn--bcher-kva.example", "" };
    for (guint i = 0; i < G_N_ELEMENTS(inputs); i++) {
        NionIdnVerdict *verdict = nion_idn_inspect(inputs[i]);
        ok(verdict->ascii != NULL, "the ASCII form is always set");
        ok(verdict->unicode != NULL, "the unicode form is always set");
        nion_idn_verdict_free(verdict);
    }
    nion_idn_verdict_free(NULL); /* must not crash */
}

int
main(void)
{
    test_host_from_uri();
    test_punycode_vectors();
    test_plain_ascii_is_safe();
    test_mixed_latin_cyrillic_is_suspicious();
    test_attack_in_a_later_label();
    test_all_lookalike_label_is_suspicious();
    test_legitimate_idn_is_notice_only();
    test_japanese_mixed_scripts_are_allowed();
    test_encoded_label_is_reported();
    test_has_non_ascii();
    test_verdict_always_has_ascii();

    g_print("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
