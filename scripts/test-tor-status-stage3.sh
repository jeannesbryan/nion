#!/usr/bin/env bash
# Tor bootstrap UX: phases, waiting tabs, retry (roadmap T1-4).
#
# Part 1 runs the real phrasing code: tests/tor_status_test.c is linked against
# src/tor-status.c from the production tree. Its fixtures are log lines captured
# from the bundled Tor binary, so the parser is checked against the wording NiOn
# actually ships rather than against remembered strings.
#
# Part 2 asserts the wiring a harness cannot see: the phase must reach the
# status line and the tab tooltips, the retry must not be able to stack a second
# daemon, and the retry link must require a user gesture.
#
# Part 3 mutation-tests part 1 so a detector that stopped detecting fails loud.
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
       "$ROOT/tests/tor_status_test.c" \
       "$dir/src/tor-status.c" \
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
}

# ---------------------------------------------------------------- part 1
build "$ROOT" "$BUILD_DIR/tor_status_test" || {
    cat "$BUILD_DIR/cc.log" >&2
    fail "tor status tests did not compile"
}
run_clean "tor bootstrap phrasing" "$BUILD_DIR/tor_status_test"
pass "bootstrap parsing and phrasing, compiled from src/tor-status.c"

# The module must stay GTK-free: if it ever grows an include of gtk, the tests
# above stop being runnable on their own and start dragging the widget layer in.
if grep -qE '#include\s*<(gtk|webkit)' "$SRC/tor-status.c" "$SRC/tor-status.h"; then
    fail "src/tor-status.c or its header includes GTK/WebKit"
fi
pass "the phrasing module has no GTK dependency"

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
tor_core = read("tor-core.c")
tor_core_h = read("tor-core.h")
tor_status_h = read("tor-status.h")
app_c = read("app.c")
app_h = read("app.h")
ui = read("ui.c")
tabs = read("tabs.c")
tabs_h = read("tabs.h")
webview = read("webview.c")

problems = []

# meson.build is the mechanical switch. This was actually forgotten once during
# this task and only the linker noticed.
if "'src/tor-status.c'" not in meson:
    problems.append("meson.build does not list src/tor-status.c")

# The phase description must survive from the log line to the UI.
if "nion_tor_parse_bootstrap" not in tor_core:
    problems.append("tor-core.c does not use the tested bootstrap parser")
if re.search(r'sscanf\s*\(\s*bootstrap\s*,\s*"Bootstrapped %d%%"', tor_core):
    problems.append("tor-core.c still parses the bootstrap line by hand")
if "tor_bootstrap_summary" not in types:
    problems.append("types.h has no place to keep Tor's phase description")
if "tor_bootstrap_summary" not in app_c:
    problems.append("app.c never stores Tor's phase description")
if "nion_tor_status_text" not in app_c:
    problems.append("app.c does not build the status line from the shared phrasing")
if re.search(r'g_strdup_printf\("○ CONNECTING TO TOR… %d%%",\s*percent\)', app_c):
    problems.append("app.c still builds its own percentage-only status line")

# The progress hook must carry the description, or the UI has nothing to show.
if "void (*set_tor_progress)(NionApp *app, gint percent, const gchar *summary)" not in tor_core_h:
    problems.append("the set_tor_progress hook does not carry the phase description")
if "void nion_set_tor_progress(NionApp *app, gint percent, const gchar *summary)" not in app_h:
    problems.append("nion_set_tor_progress does not carry the phase description")

# Retry: exists, refuses to stack daemons, and requires a gesture.
if "gboolean nion_retry_tor(NionApp *app)" not in tor_core_h:
    problems.append("nion_retry_tor is not declared")
retry = re.search(r"gboolean nion_retry_tor\(NionApp \*app\)(.*?)\n\}\n", tor_core, re.S)
if not retry:
    problems.append("tor-core.c has no nion_retry_tor body to inspect")
else:
    body = retry.group(1)
    if "app->shutting_down" not in body:
        problems.append("nion_retry_tor does not refuse during shutdown")
    if "app->tor_process && !app->tor_failed" not in body:
        problems.append("nion_retry_tor can start a second daemon while one is running")
    if "app->tor_ready && !app->tor_failed" not in body:
        problems.append("nion_retry_tor does not skip a healthy Tor")
    if "g_subprocess_force_exit" not in body:
        problems.append("nion_retry_tor does not stop the failed daemon first")
    if "nion_choose_tor_port" not in body:
        problems.append("nion_retry_tor reuses a port that the failed daemon may still hold")
    if "nion_start_tor" not in body:
        problems.append("nion_retry_tor never starts Tor")

if 'nion://retry-tor' not in ui:
    problems.append("no surface offers nion://retry-tor")
retry_link = re.search(r'nion://retry-tor', webview)
if not retry_link:
    problems.append("webview.c does not handle nion://retry-tor")
handler = re.search(r'nion://retry-tor(.*?)\n    \}\n', webview, re.S)
if not handler:
    problems.append("the retry-tor handler has no body to inspect")
else:
    body = handler.group(1)
    if "user_gesture" not in body:
        problems.append("the retry-tor handler does not require a user gesture")
    if "nion_retry_tor" not in body:
        problems.append("the retry-tor handler never calls nion_retry_tor")

# The retry link must appear only when it can help: on a failed Tor, never
# while Tor is still working.
start_page = re.search(r"nion_home_html\(NionApp \*app\)(.*?)\n\}\n", ui, re.S)
if not start_page:
    problems.append("ui.c has no nion_home_html to inspect")
elif "app->tor_failed" not in start_page.group(1):
    problems.append("the start page offers retry regardless of Tor state")
if 'category && g_str_equal(category, "Tor error")' not in ui:
    problems.append("the error page does not limit the retry link to Tor failures")

# Waiting tabs: a spinner exists and its tooltip is built from the shared
# phrasing, so a tab opened mid-bootstrap explains itself.
if "tor_wait_indicator" not in types:
    problems.append("types.h has no per-tab waiting indicator")
if "gtk_spinner_new" not in tabs:
    problems.append("tabs.c does not create a waiting spinner")
if "nion_tabs_update_tor_waiting" not in tabs_h:
    problems.append("nion_tabs_update_tor_waiting is not declared")
refresh = re.search(r"void nion_tabs_update_tor_waiting\(NionApp \*app\)(.*?)\n\}\n", tabs, re.S)
if not refresh:
    problems.append("tabs.c has no nion_tabs_update_tor_waiting body")
else:
    body = refresh.group(1)
    if "nion_tor_blocked_explanation" not in body:
        problems.append("the waiting tooltip does not explain the block")
    if "gtk_spinner_start" not in body:
        problems.append("the waiting spinner never starts")
    if "gtk_spinner_stop" not in body:
        problems.append("the waiting spinner never stops")

# Every Tor state change must refresh the tabs, or a spinner stays on a window
# where Tor has already connected.
calls = app_c.count("nion_tabs_update_tor_waiting(app)")
if calls < 3:
    problems.append(f"app.c refreshes tab indicators {calls} time(s); ready, progress and error each need one")
if "nion_tabs_update_tor_waiting(app)" not in ui:
    problems.append("a tab created mid-bootstrap never gets its indicator")

# Fail-closed invariant: nothing in this feature may describe a bypass.
#
# Note the narrow wording on purpose. ui.c legitimately warns that an *external
# application* launched from a protocol prompt may reach the network without
# Tor; saying so is honest and required. What must never appear is a claim that
# NiOn itself will carry the request another way, so only those phrases are
# refused here.
bypass_phrases = [
    "fall back to direct",
    "fallback to direct",
    "direct connection",
    "continue without tor",
    "bypass tor",
    "connect directly",
]
for name, text in (("ui.c", ui), ("tabs.c", tabs), ("app.c", app_c)):
    lowered = text.lower()
    for phrase in bypass_phrases:
        if phrase in lowered:
            problems.append(f"{name} contains bypass phrasing: '{phrase}'")

if problems:
    for item in problems:
        print("FAIL: " + item)
    sys.exit(1)

print("PASS: Tor bootstrap wiring (phase reaches status line and tab tooltips, "
      "retry guarded, no direct-connection phrasing)")
PY

# ---------------------------------------------------------------- part 3
mutate() {
    local label="$1" expr="$2"
    local dir="$BUILD_DIR/mut-$(echo "$label" | tr ' %' '--')"
    rm -rf "$dir"; mkdir -p "$dir/src"
    cp "$ROOT/src/tor-status.c" "$ROOT/src/tor-status.h" "$dir/src/"
    python3 - "$dir/src/tor-status.c" "$expr" <<'PY' || { fail "mutation '$label' could not be applied"; }
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

# Without clamping, a nonsense percentage reaches the status line and the tab
# tooltip verbatim.
mutate "percentage not clamped" \
    "    if (value > 100)
        value = 100;|||    if (value > 100)
        value = value;"

# Without the marker check, every log line would be read as progress.
mutate "any line treated as bootstrap" \
    '    const gchar *marker = strstr(line, "Bootstrapped ");
    if (!marker)
        return FALSE;|||    const gchar *marker = line;'

# An unknown phase must say nothing. Inventing a label is the failure mode that
# tells the user something specific and wrong.
mutate "unknown phase invents a label" \
    "    case NION_TOR_PHASE_UNKNOWN:
    default:
        /* Unknown phases deliberately say nothing specific: a wrong claim
         * about what Tor is doing is worse than no claim. */
        return NULL;|||    case NION_TOR_PHASE_UNKNOWN:
    default:
        return \"Connecting\";"

# Tor's own wording must win over the fallback table.
mutate "Tor wording ignored" \
    '    if (detail)
        return g_strdup_printf("○ CONNECTING TO TOR… %d%% — %s", percent, detail);|||    if (FALSE)
        return g_strdup_printf("○ CONNECTING TO TOR… %d%% — %s", percent, detail);'

# A status line that claims a connection before there is one would contradict
# the fail-closed rule the whole browser is built on.
mutate "status claims connected early" \
    'return g_strdup_printf("○ CONNECTING TO TOR… %d%%", percent);|||return g_strdup_printf("● TOR CONNECTED… %d%%", percent);'

# The tag classification decides whether a tab is described as working or done.
mutate "descriptors phase misclassified" \
    '        { "loading_descriptors", NION_TOR_PHASE_DESCRIPTORS },|||        { "loading_descriptors", NION_TOR_PHASE_DONE },'

# A tab label without a percentage leaves the user unable to tell progress from
# a stall.
mutate "tab label drops the percentage" \
    'return g_strdup_printf("Waiting for Tor — %d%%", percent);|||return g_strdup("Waiting for Tor");'

echo "NiOn TOR BOOTSTRAP UX (T1-4): PASS"
