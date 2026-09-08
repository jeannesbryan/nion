#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
python3 - "$SRC" <<'PY'
from pathlib import Path
import re, sys
s='\n'.join(p.read_text() for p in sorted(Path(sys.argv[1]).glob('*')))
pat=re.compile(r'on_webview_context_menu\s*\(\s*WebKitWebView\s*\*\s*web_view\s*,\s*WebKitContextMenu\s*\*\s*context_menu\s*,(?P<middle>.*?)gpointer\s+user_data\s*\)', re.S)
ms=list(pat.finditer(s))
if len(ms) != 2:
    raise SystemExit(f'expected declaration + definition, found {len(ms)}')
for m in ms:
    middle=m.group('middle')
    if 'GdkEvent' in middle:
        raise SystemExit('legacy GdkEvent parameter is forbidden on WebKitGTK 6')
    if not re.search(r'WebKitHitTestResult\s*\*\s*hit_test_result\s*,', middle):
        raise SystemExit('missing WebKitHitTestResult parameter')
if '(void)event;' in s:
    raise SystemExit('legacy event variable remains')
if 'g_signal_connect(tab->web_view, "context-menu", G_CALLBACK(on_webview_context_menu), tab);' not in s:
    raise SystemExit('context-menu signal wiring missing')
print('CONTEXT MENU GTK4 SIGNATURE CHECK: PASS')
PY
