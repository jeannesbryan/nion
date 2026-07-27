#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail(){ echo "1.6.0 WEB PROCESS RECOVERY: FAIL: $*" >&2; exit 1; }
SRC=src/main.c

grep -Fq '"web-process-terminated"' "$SRC" || fail 'web-process-terminated signal is not connected'
grep -Fq 'WebKitWebProcessTerminationReason reason' "$SRC" || fail 'termination reason is not handled'
grep -Fq 'WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT' "$SRC" || fail 'memory-limit termination is not distinguished'
grep -Fq 'web_process_terminated' "$SRC" || fail 'per-tab recovery state missing'
grep -Fq 'nion://reload-crashed/' "$SRC" || fail 'local Reload Tab action missing'
grep -Fq 'nion_reload_crashed_tab(tab)' "$SRC" || fail 'crashed-tab reload path missing'
grep -Fq '!tab->web_process_terminated' "$SRC" || fail 'crash recovery page is not excluded from opaque session-state persistence'
grep -Fq 'RELOADING CRASHED TAB' "$SRC" || fail 'crash reload status missing'

printf 'NION 1.6.0 WEB PROCESS RECOVERY: PASS\n'
