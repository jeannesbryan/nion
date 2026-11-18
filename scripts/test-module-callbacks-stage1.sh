#!/usr/bin/env bash
# Guard: every module's callback hook must actually be filled in by main.c.
#
# Why this exists: the Phase 6 extraction gave four modules a `cb` struct and a
# `nion_<module>_set_callbacks()` hook, but main.c never called it. Every
# pointer stayed NULL, so the callbacks silently did nothing. Measured on the
# 2.2.6 binary: `objdump -d build/nion | grep -c 'call.*nion_bookmarks_set_callbacks'`
# returned 0. The bookmark star was permanently insensitive, Ctrl+D did
# nothing, "Open" in the bookmarks window did nothing, download "Retry" always
# failed with "no active tab", and the site-data switches read a NULL tab.
#
# The existing scripts all grep for source strings, so none of them could see a
# missing call. This one compares the hooks that exist against the hooks that
# are wired, and the fields of each struct against the fields that are filled.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

[[ -f "$SRC/main.c" ]] || fail "src/main.c missing"

python3 - "$SRC" <<'PY' || exit 1
import glob
import os
import re
import sys

src = sys.argv[1]
main_c = open(os.path.join(src, "main.c"), encoding="utf-8").read()

# 1. Every declared hook must be called from main.c (the composition root).
hooks = {}
for path in sorted(glob.glob(os.path.join(src, "*.h"))):
    text = open(path, encoding="utf-8").read()
    for m in re.finditer(
        r"void\s+(nion_(\w+)_set_callbacks)\s*\(\s*const\s+(\w+)\s*\*", text
    ):
        func, module, struct = m.group(1), m.group(2), m.group(3)
        hooks[func] = (module, struct, text)

if len(hooks) < 11:
    print(f"FAIL: only {len(hooks)} callback hooks found, expected at least 11 "
          f"(the scan is probably broken, which would make this guard blind)")
    sys.exit(1)

problems = []
for func, (module, struct, _) in sorted(hooks.items()):
    if not re.search(r"\b" + re.escape(func) + r"\s*\(\s*&", main_c):
        problems.append(
            f"{func}() is declared but main.c never calls it, so {struct} "
            f"stays zero-filled and module '{module}' silently does nothing"
        )

# 2. A registered struct must be filled field by field. Registering it with a
#    field left out is the same silent no-op with more code.
for func, (module, struct, header_text) in sorted(hooks.items()):
    if struct in ("NionTorCallbacks", "NionBridgeCallbacks", "NionSessionCallbacks",
                  "NionTabCallbacks", "NionWebviewCallbacks", "NionUiCallbacks",
                  "NionAppLifecycleCallbacks", "NionBookmarkCallbacks",
                  "NionDownloadCallbacks", "NionSettingsCallbacks",
                  "NionSiteDataCallbacks"):
        pass
    block = re.search(
        re.escape(struct) + r"\s+(\w+)\s*=\s*\{(.*?)\n\s*\};", main_c, re.S
    )
    if not block:
        problems.append(f"no '{struct} x = {{ ... }};' initializer found in main.c")
        continue

    fields = re.findall(r"\(\s*\*\s*(\w+)\s*\)", header_text)
    if not fields:
        problems.append(f"could not read fields of {struct} from {module}.h")
        continue

    filled = set(re.findall(r"\.(\w+)\s*=", block.group(2)))
    missing = [f for f in fields if f not in filled]
    if missing:
        problems.append(
            f"{struct} leaves {', '.join(missing)} NULL; those callbacks will "
            f"silently do nothing"
        )

if problems:
    for p in problems:
        print("FAIL: " + p)
    sys.exit(1)

print(f"PASS: {len(hooks)} module callback hooks are declared, called from main.c, "
      f"and fully initialised")
PY

# 3. The bug this guard was written for: prove the detector sees a real
#    regression by removing one registration in a scratch copy of the tree.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$SRC" "$WORK/src"
python3 - "$WORK/src/main.c" <<'PY' || fail "mutation setup failed"
import re, sys
path = sys.argv[1]
text = open(path, encoding="utf-8").read()
text2 = re.sub(r"\n\s*nion_bookmarks_set_callbacks\s*\([^;]*;", "\n", text, count=1)
if text2 == text:
    sys.exit(1)
open(path, "w", encoding="utf-8").write(text2)
PY

MUT_OUT="$(python3 - "$WORK/src" <<'PY'
import glob, os, re, sys
src = sys.argv[1]
main_c = open(os.path.join(src, "main.c"), encoding="utf-8").read()
missing = []
for path in sorted(glob.glob(os.path.join(src, "*.h"))):
    text = open(path, encoding="utf-8").read()
    for m in re.finditer(r"void\s+(nion_(\w+)_set_callbacks)\s*\(\s*const\s+(\w+)\s*\*", text):
        if not re.search(r"\b" + re.escape(m.group(1)) + r"\s*\(\s*&", main_c):
            missing.append(m.group(1))
print(" ".join(missing))
PY
)"

[[ "$MUT_OUT" == *nion_bookmarks_set_callbacks* ]] || \
    fail "mutation not detected: deleting a registration went unnoticed (guard is blind)"
pass "mutation caught: unwired nion_bookmarks_set_callbacks is reported"

echo "NiOn MODULE CALLBACK WIRING: PASS"
