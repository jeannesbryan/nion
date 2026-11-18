#!/usr/bin/env bash
# Homograph and IDN protection (roadmap T2-7).
#
# Part 1 runs the real detection rules: tests/idn_guard_test.c is linked against
# src/idn-guard.c from the production tree. The ASCII forms are checked against
# published punycode vectors rather than against whatever the code produces.
#
# Part 2 asserts the wiring a unit test cannot see: the notice must reach the
# address bar, the site information row must stay hidden for plain hosts, and
# nothing in this feature may reach the network.
#
# Part 3 drives the real window and checks the log line the notice emits, because
# a CSS class is not observable from outside the process.
#
# Part 4 mutation-tests part 1.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

export TMPDIR="$BUILD_DIR/tmp"
mkdir -p "$TMPDIR"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

command -v cc >/dev/null 2>&1 || fail "no C compiler"
pkg-config --exists glib-2.0 || fail "glib dev files missing"
GLIB_CFLAGS=$(pkg-config --cflags glib-2.0)
GLIB_LIBS=$(pkg-config --libs glib-2.0)

build() {
    local dir="$1" out="$2"
    shift 2
    cc -std=c11 -O1 -Wall -Wextra \
       -I"$dir/src" \
       "$@" \
       $GLIB_CFLAGS \
       -o "$out" \
       "$ROOT/tests/idn_guard_test.c" \
       "$dir/src/idn-guard.c" \
       $GLIB_LIBS 2>"$BUILD_DIR/cc.log"
}

run_clean() {
    local label="$1" bin="$2"
    "$bin" >"$BUILD_DIR/run.log" 2>&1
    local rc=$?
    cat "$BUILD_DIR/run.log"
    [[ $rc -eq 0 ]] || { cat "$BUILD_DIR/cc.log" >&2; fail "$label failed (exit $rc)"; }
    grep -qE '[0-9]+ checks, 0 failed' "$BUILD_DIR/run.log" || \
        fail "$label did not report a clean run"
    if grep -q "AddressSanitizer" "$BUILD_DIR/run.log"; then
        fail "$label reported an AddressSanitizer finding"
    fi
}

# ---------------------------------------------------------------- part 1
build "$ROOT" "$BUILD_DIR/idn_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "idn guard tests did not compile"
}
run_clean "homograph detection" "$BUILD_DIR/idn_test"
pass "detection rules, compiled from src/idn-guard.c"

# The rules are pure computation, checked as such: reading a host must never
# involve the network, and the module must not drag the widget layer in.
if grep -qE '#include\s*<(gtk|webkit)' "$SRC/idn-guard.c" "$SRC/idn-guard.h"; then
    fail "src/idn-guard.c or its header includes GTK/WebKit"
fi
for needle in soup_session g_resolver g_network curl http_get; do
    if grep -q "$needle" "$SRC/idn-guard.c"; then
        fail "src/idn-guard.c references ${needle}: the guard must stay offline"
    fi
done
pass "the guard is a pure local computation (no GTK, no network)"

# ---------------------------------------------------------------- part 2
python3 - "$SRC" "$ROOT/meson.build" <<'PY' || exit 1
import os
import re
import sys

src = sys.argv[1]
meson = open(sys.argv[2], encoding="utf-8").read()

def read(name):
    with open(os.path.join(src, name), encoding="utf-8") as handle:
        return handle.read()

types = read("types.h")
ui = read("ui.c")
ui_h = read("ui.h")
site_data = read("site-data.c")
guard = read("idn-guard.c")

problems = []

if "'src/idn-guard.c'" not in meson:
    problems.append("meson.build does not list src/idn-guard.c")

# The notice must reach the address bar itself, not only a panel.
if "nion_update_idn_notice" not in ui_h:
    problems.append("nion_update_idn_notice is not declared")
notice = re.search(r"void nion_update_idn_notice\(NionApp \*app\)(.*?)\n\}\n", ui, re.S)
if not notice:
    problems.append("ui.c has no nion_update_idn_notice body")
else:
    body = notice.group(1)
    if "nion_idn_host_from_uri" not in body:
        problems.append("the address bar notice never extracts the host")
    if "nion_idn_inspect" not in body:
        problems.append("the address bar notice never inspects the host")
    if "nion-idn-suspicious" not in body:
        problems.append("a suspected homograph is not marked differently from a notice")
    if "nion-idn-notice" not in body:
        problems.append("an international address is not marked at all")
    # The notice must read the address bar, not the loaded page: a failed or
    # blocked load is exactly when the name is most likely to be believed.
    if "gtk_editable_get_text" not in body:
        problems.append("the notice reads the page instead of the address bar")
    if "webkit_web_view_get_uri" in body:
        problems.append("the notice depends on the page having loaded, so it vanishes when the load fails")
    # Clearing on a plain host is what keeps ordinary browsing quiet.
    if "gtk_widget_remove_css_class" not in body:
        problems.append("the notice never clears, so an old warning would stick")
if "nion_update_idn_notice(app)" not in ui:
    problems.append("nothing calls nion_update_idn_notice")

# One place that decides: the panel and the address bar must agree.
if "nion_idn_inspect" not in site_data:
    problems.append("the site information panel does not use the shared verdict")
if "site_info_idn_row" not in site_data:
    problems.append("the site information panel does not hide the row for plain hosts")
if "NION_IDN_SAFE" not in site_data:
    problems.append("the panel does not treat a plain host as nothing to report")

# Every label must be examined, not just the first.
if "strchr(label, '.')" not in guard:
    problems.append("the guard does not walk every label of the host")
# The ASCII form comes from the library implementation, which the tests check
# against published vectors.
if "g_hostname_to_ascii" not in guard:
    problems.append("the guard does not produce the ASCII form that will be resolved")

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: IDN guard wiring (address bar notice, panel row hidden for plain hosts, "
      "verdict shared, every label examined)")
PY

# ---------------------------------------------------------------- part 3
if [[ -x "$ROOT/build/nion" ]] && command -v xdotool >/dev/null 2>&1 \
   && [[ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]]; then
    UI_TMP="$BUILD_DIR/ui"
    mkdir -p "$UI_TMP"
    UI_LOG="$UI_TMP/nion.log"
    UI_PID=""

    # A stand-in Tor that reports a finished bootstrap and then stays alive.
    # Without it the address bar is insensitive while Tor is down, and nothing
    # typed into it ever arrives; /bin/false is therefore not usable here even
    # though it is the right choice for tests that must not touch the network.
    FAKE_TOR="$UI_TMP/fake-tor.sh"
    {
        echo '#!/bin/sh'
        echo 'echo "Bootstrapped 100% (done): Done"'
        echo 'while :; do sleep 1; done'
    } > "$FAKE_TOR"
    chmod +x "$FAKE_TOR"

    ui_stop() {
        if [[ -n "$UI_PID" ]] && kill -0 "$UI_PID" 2>/dev/null; then
            kill -KILL "$UI_PID" 2>/dev/null || true
            wait "$UI_PID" 2>/dev/null || true
        fi
        UI_PID=""
    }

    # Tab 0 is ordinary on purpose, so this run proves both directions: quiet on
    # a plain name, loud on an imitation. The hostile name is spelled with a
    # Cyrillic first letter and the active tab is switched to it, because only
    # the active tab's address bar is inspected.
    ui_seed() {
        local uri="$1"
        export XDG_DATA_HOME="$UI_TMP/data" XDG_CONFIG_HOME="$UI_TMP/config" \
               XDG_CACHE_HOME="$UI_TMP/cache"
        rm -rf "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"
        mkdir -p "$XDG_DATA_HOME/nion" "$XDG_CONFIG_HOME/nion" "$XDG_CACHE_HOME/nion"
        printf '[General]\nrestore-session=true\n' > "$XDG_CONFIG_HOME/nion/preferences.ini"
        {
            echo "[Session]"
            echo "format=1"
            echo "nion-version=2.3.5"
            echo "tab-count=1"
            echo "active-tab=0"
            echo "clean-shutdown=true"
            echo
            echo "[Tab-0]"
            echo "home=false"
            echo "muted=false"
            echo "pinned=false"
            echo "uri=$uri"
        } > "$XDG_DATA_HOME/nion/session.ini"
    }

    ui_launch() {
        local uri="$1"
        ui_seed "$uri"
        NION_TAB_DEBUG=1 NION_TOR_BINARY="$FAKE_TOR" "$ROOT/build/nion" >"$UI_LOG" 2>&1 &
        UI_PID=$!
        local wid=""
        for _ in $(seq 1 100); do
            wid=$(xdotool search --pid "$UI_PID" --onlyvisible 2>/dev/null | tail -1 || true)
            [[ -n "$wid" ]] && break
            sleep 0.25
        done
        [[ -n "$wid" ]] || { cat "$UI_LOG" >&2; ui_stop; fail "NiOn never mapped a window"; }
        WID="$wid"
        xdotool windowactivate --sync "$wid" 2>/dev/null || true
        sleep 2
    }

    # Direction one: an ordinary ASCII address must be inspected and left alone.
    ui_launch "https://example.com/"
    plain_lines=$(grep 'idn-notice ' "$UI_LOG" 2>/dev/null || true)
    [[ -n "$plain_lines" ]] || { cat "$UI_LOG" >&2; ui_stop; fail "the address bar was never inspected"; }
    printf '%s\n' "$plain_lines" | grep -q 'host=example.com kind=0 risk=0' \
        || { printf '%s\n' "$plain_lines" | head -3 >&2; ui_stop; fail "an ordinary address was not treated as plain"; }
    if printf '%s\n' "$plain_lines" | grep -q 'host=example.com .*risk=[12]'; then
        ui_stop
        fail "an ordinary ASCII address was flagged"
    fi
    pass "an ordinary ASCII address is inspected and left alone (no notice)"
    ui_stop

    # Direction two: the same window, opened on the imitation. WebKit normalizes
    # the address to its punycode form, so this also proves the decoded-name
    # analysis is what catches it.
    # $'...' so bash expands \u0430 into the Cyrillic letter; inside plain
    # double quotes it would be written literally and the host would be nonsense.
    ui_launch $'https://\u0430pple.com/'
    hostile=""
    for _ in $(seq 1 80); do
        hostile=$(grep 'idn-notice ' "$UI_LOG" | grep 'risk=2' | tail -1 || true)
        [[ -n "$hostile" ]] && break
        sleep 0.25
    done
    [[ -n "$hostile" ]] || {
        grep 'idn-notice ' "$UI_LOG" | tail -5 >&2
        ui_stop
        fail "a Cyrillic homograph in the address bar was not reported as suspicious"
    }
    [[ "$hostile" == *"xn--"* ]] || { ui_stop; fail "the report does not name the ASCII form: $hostile"; }
    pass "a Cyrillic homograph is reported suspicious in the real address bar"

    # Only criticals that come from NiOn's own widget code count. This session's
    # GDK also emits a Vulkan driver complaint about /dev/dri (the AMD driver
    # refusing the render node), which has nothing to do with this feature and
    # would otherwise make the check fail on a machine issue rather than a bug.
    criticals=$(grep 'CRITICAL' "$UI_LOG" 2>/dev/null | grep -v 'Vulkan\|radv\|/dev/dri' || true)
    ui_stop
    [[ -z "$criticals" ]] || {
        printf '%s\n' "$criticals" | head -3 >&2
        fail "NiOn's widgets reported a critical during the IDN check"
    }
    pass "no widget criticals during the IDN check (a GDK Vulkan driver line is ignored, not our code)"
else
    echo "SKIP: the window test needs build/nion, xdotool and a display"
fi

# ---------------------------------------------------------------- part 4
mutate() {
    local label="$1" expr="$2"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' %' '--')"
    rm -rf "$dir"; mkdir -p "$dir/src"
    cp "$ROOT/src/idn-guard.c" "$ROOT/src/idn-guard.h" "$dir/src/"
    python3 - "$dir/src/idn-guard.c" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
import sys
path, expr = sys.argv[1], sys.argv[2]
old, new = expr.split("|||")
text = open(path, encoding="utf-8").read()
if old not in text:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
PY
    build "$dir" "$dir/test" || fail "mutation '$label' did not compile"
    if "$dir/test" >"$dir/run.log" 2>&1; then
        cat "$dir/run.log" >&2
        fail "mutation '$label' survived: the harness does not catch it"
    fi
    pass "mutation caught: $label"
}

# If ASCII letters stop counting as Latin, the classic attack becomes invisible.
# This exact mistake was in the first version and the suite caught it.
mutate "ASCII letters are not Latin script" \
    "                *has_ascii = TRUE;
                *scripts_seen |= (1u << SCRIPT_LATIN);|||                *has_ascii = TRUE;"

# Crying wolf: treating every IDN as an attack makes the warning worthless.
mutate "every IDN is suspicious" \
    "        verdict->risk = NION_IDN_NOTICE;
        verdict->reason = g_strdup_printf(
            \"This address contains non-Latin characters. It resolves to %s.\",
            verdict->ascii);|||        verdict->risk = NION_IDN_SUSPICIOUS;
        verdict->reason = g_strdup_printf(
            \"This address contains non-Latin characters. It resolves to %s.\",
            verdict->ascii);"

# Silence: never reporting anything means the feature does nothing.
mutate "mixed scripts ignored" \
    "            if (has_latin && (has_cyrillic || has_greek)) {|||            if (FALSE) {"

# Only the first label is not enough: "www.xn--..." must be caught.
mutate "only the first label examined" \
    "        if (!dot)
            break;
        label = dot + 1;|||        break;"

# A lookalike label is the single-script attack.
mutate "lookalike label ignored" \
    "                out->lookalike_label = TRUE;|||                out->lookalike_label = FALSE;"

# The address bar holds the punycode form, so skipping the decoded analysis would
# downgrade an active imitation to a harmless note.
mutate "encoded name not decoded for analysis" \
    "        analyze_host(verdict->unicode, &decoded_analysis);|||        analyze_host(host, &decoded_analysis);"

# Plain ASCII must stay silent, or every page carries a warning.
# Plain ASCII must stay silent, or every page carries a warning.
mutate "plain ASCII flagged" \
    "        if (!nion_idn_has_encoded_label(host))
            return verdict;|||        if (FALSE)
            return verdict;"

echo "NiOn IDN / HOMOGRAPH GUARD (T2-7): PASS"
