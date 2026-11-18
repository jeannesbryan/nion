/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Jeannes Bryan */

#include <glib.h>
#include <string.h>

#include "idn-guard.h"

/* Homograph and IDN protection (v2.3.5, roadmap T2-7).
 *
 * The ASCII form is produced by GLib's g_hostname_to_ascii(), which implements
 * IDNA/punycode. That is deliberate: RFC 3492 is a well-defined algorithm with
 * published test vectors, GLib already ships a tested implementation of it, and
 * a second hand-written copy here would be a place for a subtle bug to hide in
 * exactly the code that tells a user which host they are about to trust. The
 * test suite still checks the results against the RFC vectors, so the dependency
 * is verified rather than assumed.
 */

typedef enum {
    SCRIPT_NONE = 0,
    SCRIPT_LATIN,
    SCRIPT_GREEK,
    SCRIPT_CYRILLIC,
    SCRIPT_HAN,
    SCRIPT_HIRAGANA,
    SCRIPT_KATAKANA,
    SCRIPT_HANGUL,
    SCRIPT_ARABIC,
    SCRIPT_HEBREW,
    SCRIPT_OTHER,
} NionScript;

/* Script ranges. Only the ranges that matter for this decision are named; every
 * other non-ASCII character lands in SCRIPT_OTHER, which still counts as "not
 * Latin" so a Latin + Other mix is reported rather than ignored. */
static NionScript
script_of(gunichar ch)
{
    if ((ch >= 0x0041 && ch <= 0x005A) || (ch >= 0x0061 && ch <= 0x007A) ||
        (ch >= 0x00C0 && ch <= 0x024F) || (ch >= 0x1E00 && ch <= 0x1EFF) ||
        (ch >= 0xFB00 && ch <= 0xFB06))
        return SCRIPT_LATIN;
    if ((ch >= 0x0370 && ch <= 0x03FF) || (ch >= 0x1F00 && ch <= 0x1FFF))
        return SCRIPT_GREEK;
    if ((ch >= 0x0400 && ch <= 0x052F) || (ch >= 0x2DE0 && ch <= 0x2DFF) ||
        (ch >= 0xA640 && ch <= 0xA69F))
        return SCRIPT_CYRILLIC;
    if ((ch >= 0x3400 && ch <= 0x4DBF) || (ch >= 0x4E00 && ch <= 0x9FFF) ||
        (ch >= 0xF900 && ch <= 0xFAFF))
        return SCRIPT_HAN;
    if (ch >= 0x3040 && ch <= 0x309F)
        return SCRIPT_HIRAGANA;
    if ((ch >= 0x30A0 && ch <= 0x30FF) || (ch >= 0x31F0 && ch <= 0x31FF))
        return SCRIPT_KATAKANA;
    if ((ch >= 0xAC00 && ch <= 0xD7AF) || (ch >= 0x1100 && ch <= 0x11FF) ||
        (ch >= 0x3130 && ch <= 0x318F))
        return SCRIPT_HANGUL;
    if ((ch >= 0x0600 && ch <= 0x06FF) || (ch >= 0x0750 && ch <= 0x077F))
        return SCRIPT_ARABIC;
    if (ch >= 0x0590 && ch <= 0x05FF)
        return SCRIPT_HEBREW;
    return SCRIPT_OTHER;
}

static const gchar *
script_name(NionScript script)
{
    switch (script) {
    case SCRIPT_LATIN: return "Latin";
    case SCRIPT_GREEK: return "Greek";
    case SCRIPT_CYRILLIC: return "Cyrillic";
    case SCRIPT_HAN: return "Han";
    case SCRIPT_HIRAGANA: return "Hiragana";
    case SCRIPT_KATAKANA: return "Katakana";
    case SCRIPT_HANGUL: return "Hangul";
    case SCRIPT_ARABIC: return "Arabic";
    case SCRIPT_HEBREW: return "Hebrew";
    case SCRIPT_OTHER: return "another script";
    case SCRIPT_NONE:
    default: return NULL;
    }
}

/* Lookalike table: characters from other scripts that render like a Latin
 * letter. A label made entirely of these reads as Latin while resolving
 * somewhere else, which is the homograph this feature exists to expose.
 *
 * This is a curated subset, not the full Unicode confusables data. It covers the
 * Cyrillic and Greek letters used in the attacks that actually appear in the
 * wild; when a character is not in the table the label is simply not called a
 * lookalike, so the failure mode is silence rather than a false accusation. */
static const gunichar confusable_latin[][2] = {
    /* Cyrillic lowercase */
    { 0x0430, 'a' }, { 0x0435, 'e' }, { 0x043E, 'o' }, { 0x0440, 'p' },
    { 0x0441, 'c' }, { 0x0443, 'y' }, { 0x0445, 'x' }, { 0x0456, 'i' },
    { 0x0455, 's' }, { 0x0458, 'j' }, { 0x0501, 'd' }, { 0x04BB, 'h' },
    { 0x04CF, 'l' }, { 0x051B, 'q' }, { 0x051D, 'w' }, { 0x043A, 'k' },
    { 0x043C, 'm' }, { 0x0442, 't' }, { 0x0432, 'b' }, { 0x043D, 'H' },
    /* Cyrillic uppercase */
    { 0x0410, 'A' }, { 0x0412, 'B' }, { 0x0415, 'E' }, { 0x041A, 'K' },
    { 0x041C, 'M' }, { 0x041D, 'H' }, { 0x041E, 'O' }, { 0x0420, 'P' },
    { 0x0421, 'C' }, { 0x0422, 'T' }, { 0x0425, 'X' }, { 0x0406, 'I' },
    { 0x0408, 'J' }, { 0x0405, 'S' }, { 0x0423, 'Y' },
    /* Greek lowercase */
    { 0x03BF, 'o' }, { 0x03B1, 'a' }, { 0x03BD, 'v' }, { 0x03C1, 'p' },
    { 0x03C4, 't' }, { 0x03C5, 'u' }, { 0x03B9, 'i' }, { 0x03BA, 'k' },
    { 0x03B5, 'e' }, { 0x03C7, 'x' }, { 0x03B3, 'y' }, { 0x03C9, 'w' },
    /* Greek uppercase */
    { 0x0391, 'A' }, { 0x0392, 'B' }, { 0x0395, 'E' }, { 0x0396, 'Z' },
    { 0x0397, 'H' }, { 0x0399, 'I' }, { 0x039A, 'K' }, { 0x039C, 'M' },
    { 0x039D, 'N' }, { 0x039F, 'O' }, { 0x03A1, 'P' }, { 0x03A4, 'T' },
    { 0x03A5, 'Y' }, { 0x03A7, 'X' },
};

/* TRUE when this character has a Latin letter it can be mistaken for. */
static gboolean
is_latin_lookalike(gunichar ch)
{
    for (guint i = 0; i < G_N_ELEMENTS(confusable_latin); i++) {
        if (confusable_latin[i][0] == ch)
            return TRUE;
    }
    return FALSE;
}

gboolean
nion_idn_has_non_ascii(const gchar *text)
{
    if (!text)
        return FALSE;
    for (const guchar *p = (const guchar *)text; *p; p++) {
        if (*p >= 0x80)
            return TRUE;
    }
    return FALSE;
}

gboolean
nion_idn_has_encoded_label(const gchar *host)
{
    if (!host)
        return FALSE;

    const gchar *label = host;
    while (label && *label) {
        if (g_ascii_strncasecmp(label, "xn--", 4) == 0)
            return TRUE;
        label = strchr(label, '.');
        if (label)
            label++;
    }
    return FALSE;
}

/* ---- Host extraction ---- */

/* Strip the parts of an address that are not the host: scheme, userinfo, port,
 * path, query, fragment. Written to tolerate what an address bar produces rather
 * than only what a validated URI contains. */
gchar *
nion_idn_host_from_uri(const gchar *uri)
{
    if (!uri || !*uri)
        return NULL;

    const gchar *start = uri;

    /* Scheme: letters+ followed by "://". A bare "host:port" must not be read as
     * a scheme, so the separator is required. */
    const gchar *scheme_end = strstr(uri, "://");
    if (scheme_end) {
        gboolean looks_like_scheme = scheme_end != uri;
        for (const gchar *p = uri; p < scheme_end; p++) {
            if (!g_ascii_isalnum(*p) && *p != '+' && *p != '-' && *p != '.') {
                looks_like_scheme = FALSE;
                break;
            }
        }
        if (looks_like_scheme)
            start = scheme_end + 3;
    }

    if (!*start)
        return NULL;

    const gchar *end = start;
    while (*end && *end != '/' && *end != '?' && *end != '#')
        end++;

    const gchar *host_start = start;

    /* Userinfo: everything before the last '@' is not the host. */
    const gchar *at = NULL;
    for (const gchar *p = start; p < end; p++) {
        if (*p == '@')
            at = p;
    }
    if (at)
        host_start = at + 1;

    /* Port: after the last ':' that follows the host, unless it is an IPv6
     * literal in brackets. */
    const gchar *host_end = end;
    if (host_start < end && *host_start == '[') {
        const gchar *close = memchr(host_start, ']', (gsize)(end - host_start));
        if (close)
            host_end = close + 1;
    } else {
        for (const gchar *p = host_start; p < end; p++) {
            if (*p == ':') {
                host_end = p;
                break;
            }
        }
    }

    if (host_end <= host_start)
        return NULL;

    gchar *host = g_strndup(host_start, (gsize)(host_end - host_start));

    /* A trailing dot is the same host (the root form), so it is dropped before
     * inspection: leaving it in would split the last label for nothing. */
    gsize length = strlen(host);
    while (length > 0 && host[length - 1] == '.')
        host[--length] = '\0';

    if (length == 0) {
        g_free(host);
        return NULL;
    }

    return host;
}

/* ---- Verdict ---- */

/* Analyze one label. Fills the set of scripts seen and whether every non-ASCII
 * character in it is a Latin lookalike. */
static void
analyze_label(const gchar *label, gsize length, guint *scripts_seen,
              gboolean *all_lookalike, gboolean *has_non_ascii,
              gboolean *has_ascii)
{
    gchar *copy = g_strndup(label, length);
    const gchar *cursor = copy;

    while (*cursor) {
        gunichar ch = g_utf8_get_char(cursor);
        if (ch == 0)
            break;
        cursor = g_utf8_next_char(cursor);

        if (ch < 0x80) {
            /* An ASCII letter is Latin script. This has to be recorded in the
             * script mask, not only in has_ascii: the mixed-script check reads
             * the mask, so leaving it out made "Cyrillic letter + Latin letters"
             * look like a single-script label and hid the attack completely. */
            if (g_ascii_isalpha((gchar)ch)) {
                *has_ascii = TRUE;
                *scripts_seen |= (1u << SCRIPT_LATIN);
            }
            continue;
        }

        *has_non_ascii = TRUE;
        NionScript script = script_of(ch);
        *scripts_seen |= (1u << script);

        if (script == SCRIPT_LATIN)
            *has_ascii = TRUE;
        else if (!is_latin_lookalike(ch))
            *all_lookalike = FALSE;
    }

    /* A label with no non-ASCII character cannot be reported as "all lookalike":
     * that would be an empty claim. */
    if (!*has_non_ascii)
        *all_lookalike = FALSE;

    g_free(copy);
}

/* Script names present in a bitmask, joined for display. */
static gchar *
scripts_to_text(guint mask)
{
    GString *out = g_string_new(NULL);
    for (guint i = SCRIPT_NONE + 1; i <= SCRIPT_OTHER; i++) {
        if (!(mask & (1u << i)))
            continue;
        const gchar *name = script_name((NionScript)i);
        if (!name)
            continue;
        if (out->len)
            g_string_append(out, " + ");
        g_string_append(out, name);
    }
    return g_string_free(out, FALSE);
}

/* The result of looking at every label of a name. */
typedef struct {
    guint scripts;
    gboolean suspicious_mix;
    gchar *mix_detail;
    gboolean lookalike_label;
} NionIdnAnalysis;

static void
analyze_host(const gchar *host, NionIdnAnalysis *out)
{
    out->scripts = 0;
    out->suspicious_mix = FALSE;
    out->mix_detail = NULL;
    out->lookalike_label = FALSE;

    if (!host)
        return;

    const gchar *label = host;
    while (label && *label) {
        const gchar *dot = strchr(label, '.');
        gsize length = dot ? (gsize)(dot - label) : strlen(label);

        guint label_scripts = 0;
        gboolean all_lookalike = TRUE;
        gboolean has_non_ascii = FALSE;
        gboolean has_ascii = FALSE;
        analyze_label(label, length, &label_scripts, &all_lookalike,
                      &has_non_ascii, &has_ascii);

        out->scripts |= label_scripts;

        if (has_non_ascii) {
            gboolean has_latin = (label_scripts & (1u << SCRIPT_LATIN)) != 0;
            gboolean has_cyrillic = (label_scripts & (1u << SCRIPT_CYRILLIC)) != 0;
            gboolean has_greek = (label_scripts & (1u << SCRIPT_GREEK)) != 0;

            /* Latin beside Cyrillic, or beside Greek, inside one label is the
             * classic homograph construction: those scripts look alike and are
             * not written together. */
            if (has_latin && (has_cyrillic || has_greek)) {
                out->suspicious_mix = TRUE;
                if (!out->mix_detail) {
                    out->mix_detail = g_strdup(has_cyrillic ? "Latin + Cyrillic"
                                                            : "Latin + Greek");
                }
            }

            /* A label that is entirely non-Latin but where every character is a
             * Latin lookalike reads as Latin while resolving elsewhere. */
            if (!has_ascii && all_lookalike)
                out->lookalike_label = TRUE;
        }

        if (!dot)
            break;
        label = dot + 1;
    }
}

static void
analysis_clear(NionIdnAnalysis *analysis)
{
    g_free(analysis->mix_detail);
    analysis->mix_detail = NULL;
}

void
nion_idn_verdict_free(NionIdnVerdict *verdict)
{
    if (!verdict)
        return;
    g_free(verdict->ascii);
    g_free(verdict->unicode);
    g_free(verdict->scripts);
    g_free(verdict->reason);
    g_free(verdict);
}

NionIdnVerdict *
nion_idn_inspect(const gchar *host)
{
    NionIdnVerdict *verdict = g_new0(NionIdnVerdict, 1);
    verdict->kind = NION_IDN_PLAIN;
    verdict->risk = NION_IDN_SAFE;

    if (!host || !*host) {
        verdict->ascii = g_strdup("");
        verdict->unicode = g_strdup("");
        return verdict;
    }

    gboolean non_ascii = nion_idn_has_non_ascii(host);

    /* The ASCII form is what will actually be resolved. Asking GLib for it even
     * when the host is already ASCII keeps one code path and gives the caller a
     * value it can always rely on. */
    gchar *ascii = g_hostname_to_ascii(host);
    verdict->ascii = ascii ? ascii : g_strdup(host);

    gchar *unicode = g_hostname_to_unicode(host);
    verdict->unicode = unicode ? unicode : g_strdup(host);

    if (!non_ascii) {
        /* Plain ASCII needs no warning. An xn-- label is worth naming, because
         * an encoded name is unreadable and the user deserves to know what it
         * decodes to.
         *
         * It is also worth decoding for a second reason, which the window test
         * exposed: WebKit normalizes the address to its ASCII form, so the
         * address bar of a homograph page reads "xn--pple-43d.com" and the
         * Cyrillic letter is nowhere in the text. Analyzing only the ASCII form
         * would report a harmless "encoded name" for an active imitation. So the
         * decoded name is analyzed with the same rules. */
        if (!nion_idn_has_encoded_label(host))
            return verdict;

        verdict->kind = NION_IDN_ENCODED;

        NionIdnAnalysis decoded_analysis;
        analyze_host(verdict->unicode, &decoded_analysis);
        if (decoded_analysis.scripts)
            verdict->scripts = scripts_to_text(decoded_analysis.scripts);

        if (decoded_analysis.suspicious_mix || decoded_analysis.lookalike_label) {
            verdict->risk = NION_IDN_SUSPICIOUS;
            if (decoded_analysis.suspicious_mix) {
                verdict->reason = g_strdup_printf(
                    "This is an encoded name that mixes %s. Different scripts can "
                    "look identical, so this may be an imitation. It decodes to %s.",
                    decoded_analysis.mix_detail ? decoded_analysis.mix_detail
                                                : "scripts",
                    verdict->unicode);
            } else {
                verdict->reason = g_strdup_printf(
                    "This is an encoded name written with letters that look like "
                    "Latin but are not, so it may be an imitation. It decodes to %s.",
                    verdict->unicode);
            }
        } else {
            verdict->risk = NION_IDN_NOTICE;
            if (verdict->unicode && *verdict->unicode &&
                g_strcmp0(verdict->unicode, host) != 0) {
                verdict->reason = g_strdup_printf(
                    "This address is an encoded international name. It decodes to %s.",
                    verdict->unicode);
            } else {
                verdict->reason = g_strdup(
                    "This address is an encoded international name (xn--).");
            }
        }

        analysis_clear(&decoded_analysis);
        return verdict;
    }

    /* Non-ASCII: an IDN. The danger lives inside a single label, not across the
     * whole name, so every label is examined. */
    verdict->kind = NION_IDN_UNICODE;

    NionIdnAnalysis analysis;
    analyze_host(host, &analysis);
    if (analysis.scripts)
        verdict->scripts = scripts_to_text(analysis.scripts);

    if (analysis.suspicious_mix) {
        verdict->risk = NION_IDN_SUSPICIOUS;
        verdict->reason = g_strdup_printf(
            "This address mixes %s inside one name. Different scripts can look "
            "identical, so this may be an imitation. The name is actually %s.",
            analysis.mix_detail ? analysis.mix_detail : "scripts", verdict->ascii);
    } else if (analysis.lookalike_label) {
        verdict->risk = NION_IDN_SUSPICIOUS;
        verdict->reason = g_strdup_printf(
            "This address is written with letters that look like Latin but are "
            "not, so it may be an imitation. The name is actually %s.",
            verdict->ascii);
    } else {
        verdict->risk = NION_IDN_NOTICE;
        verdict->reason = g_strdup_printf(
            "This address contains non-Latin characters. It resolves to %s.",
            verdict->ascii);
    }

    analysis_clear(&analysis);
    return verdict;
}
