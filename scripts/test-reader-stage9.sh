#!/usr/bin/env bash
# Guard for reader mode (NiOn 2.2.4).
#
# Reader mode is a WebKit user style sheet. The failure modes that matter are
# structural rather than visual: the resource not being bundled, the sheet never
# being attached, the accelerator colliding with another action (so the shortcut
# silently does nothing), or the menu entry pointing at an action that was never
# registered. This checks all of them against the real source files.

set -uo pipefail
cd "$(dirname "$0")/.." || exit 1

pass=0
fail=0

ok() { pass=$((pass + 1)); printf '  ok   %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL %s\n' "$1"; }
check() { if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }

echo "reader mode (stage 9)"

# --- stylesheet ---
check "data/reader.css exists" "[ -s data/reader.css ]"
check "stylesheet hides page navigation" "grep -q 'role=\"navigation\"' data/reader.css"
check "stylesheet hides aside" "grep -q '^aside,' data/reader.css"
check "stylesheet hides footer" "grep -q '^footer,' data/reader.css"
check "stylesheet sets a serif body font" "grep -q 'Georgia' data/reader.css"
check "stylesheet caps the measure" "grep -q 'max-width: 38rem' data/reader.css"

# --- resource bundling ---
check "reader.css is in the gresource manifest" "grep -q '<file>reader.css</file>' data/nion.gresource.xml"

# --- module ---
check "src/reader.c exists" "[ -s src/reader.c ]"
check "reader.c is in the build" "grep -q \"'src/reader.c'\" meson.build"
check "reader attaches a user style sheet" "grep -q 'webkit_user_content_manager_add_style_sheet' src/reader.c"
check "reader clears sheets before applying" "grep -q 'webkit_user_content_manager_remove_all_style_sheets' src/reader.c"
check "reader reads the bundled resource" "grep -q 'g_resources_lookup_data' src/reader.c"
check "reader records state on the tab" "grep -q 'tab->reader_mode = TRUE' src/reader.c"

# --- tab state ---
check "NionTab carries reader_mode" "grep -q 'gboolean reader_mode;' src/types.h"

# --- wiring ---
check "ui.c declares the action" "grep -q 'void action_reader_mode' src/ui.c"
check "ui.h exports the action" "grep -q 'void action_reader_mode' src/ui.h"
check "menu entry exists" "grep -q 'Reader Mode\", \"win.reader-mode' src/ui.c"
check "action is registered" "grep -q '{ \"reader-mode\", action_reader_mode' src/main.c"
check "accelerator is bound" "grep -q 'win.reader-mode\", reader_accels' src/main.c"

# --- the bug found while testing: a duplicate accelerator silently loses ---
hard=$(grep -oP 'hard_reload_accels\[\] = \{ "\K[^"]+' src/main.c | head -1)
read=$(grep -oP 'reader_accels\[\] = \{ "\K[^"]+' src/main.c | head -1)
if [ -n "$hard" ] && [ -n "$read" ] && [ "$hard" != "$read" ]; then
    ok "reader accelerator does not collide with hard reload ($read vs $hard)"
else
    bad "reader accelerator collides with hard reload ('$read' vs '$hard')"
fi

echo
echo "$pass ok, $fail gagal"
[ "$fail" -eq 0 ]
