# Spike Report — WebKitGTK 2.52 Memory / Process / Discard APIs

> Branch: `spike/v2.1-memory-apis` · Date: 2026-09-08
> Environment: WebKitGTK **2.52.6** headers (`/usr/include/webkitgtk-6.0`),
> GTK 4.22.4, GLib 2.88.2 — the release-test toolchain.
> Purpose: verify the v2.1.0 roadmap (see `docs/roadmap-2.1.md`) assumptions
> against the *actual* installed API before committing to implementation.

---

## Executive summary

| Roadmap item | Feasibility | Verdict |
|---|---|---|
| **#1 Background Tab Discard** | **CONFIRMED** | Destroy-and-recreate is the only mechanism (no freeze API); `session-state` gives partial preservation (URL + history), **not** scroll position. Plan as in roadmap. |
| **#2 Tor New Identity** | **CONFIRMED (no new API)** | Pure orchestration on existing `tor-core.c` (`nion_stop_tor_gracefully`, `nion_start_tor`, `nion_restart_tor_delayed`). No spike dependency. |
| **#3 Privacy Dashboard start page** | **CONFIRMED (no new API)** | `nion_home_html()` (ui.c:358) already renders Tor bootstrap % + security level; data all in `NionApp`. Pure template work. |
| **#4 "Low Memory" mode** | **⚠️ REVISED** | `set_process_model` is **REMOVED in 2.52** (shared-secondary-process knob is gone). Native replacement found: **`WebKitMemoryPressureSettings`** + existing cache-model control. See §3. |
| **#5 HTTPS-only / Onion-Location** | **CONFIRMED (no new API)** | Pure `navigation.c`/`per-site.c` boundary logic. No spike dependency. |

**Headline finding:** WebKitGTK 2.52 exposes a **first-class native memory-pressure
API** (`WebKitMemoryPressureSettings`), attachable per-network-session — a
better fit for NiOn's 4 GB constraint than the removed process-model knob.

---

## 1. Probe: compile + link verification

A standalone probe (`/tmp/spike_probe.c`) was compiled and linked against
`webkitgtk-6.0` 2.52.6 headers and **ran successfully** (rc=0):

```
memory-pressure: OK (limit=1024 MB kill=0.95)
session-state API: OK (symbols resolve)
process-control API: OK
cache-model enum: OK
```

All four candidate API families are present, exported, and linkable at 2.52.6.

## 2. Feature #1 (Tab Discard) — mechanism check

**No WebKit "freeze/suspend a web view" API exists** in 2.52. The only ways to
release a tab's web-process memory are:

1. **Destroy the `WebKitWebView`** (drop the last ref) → WebKit reaps the web
   process. This is the v1 discard mechanism.
2. `webkit_web_view_terminate_web_process()` (WebKitWebView.h:654) — kills the
   process but leaves a broken view needing reload; suited to crash handling,
   not clean suspend.

**Preservation via `WebKitWebViewSessionState`:**
- `webkit_web_view_get_session_state()` → `WebKitWebViewSessionState` →
  `webkit_web_view_session_state_serialize()` → `GBytes` (WebKitWebView.h:617,
  WebKitWebViewSessionState.h).
- `webkit_web_view_restore_session_state()` reconstructs URL + back/forward
  history on a fresh view.
- **Documented limitation:** session state does **not** preserve scroll
  position or in-page form state. Roadmap's "scroll position lost in v1" caveat
  stands; the JS `scrollY` snapshot remains a v1.1 candidate.

**NiOn seams confirmed:**
- Recreate hub: `nion_new_tab_internal()` (main.c:278) — takes `(uri, select,
  related_view)`; a discard-recreate call would pass `uri` + saved session
  bytes.
- Settings are re-applied fresh per tab via `nion_apply_privacy_settings()`
  (main.c:295/309) — a recreated tab inherits current per-site rules with no
  extra work.
- Crash-recovery UX already models "tab shell alive, web gone" (session.c) —
  discard can reuse its reload path.

**Verdict:** implement as planned — destroy shell's web_view, keep `NionTab`
metadata, recreate via hub on activate. New `src/discard.c` + callback struct.

## 3. Feature #4 ("Low Memory" mode) — **REVISED**

### `set_process_model` is gone
`grep -rn "set_process_model\|process_model" *.h` → **no matches**. The
WebKitGTK-era shared/secondary-process knob (pre-2.40) has been removed; 2.52
manages web processes internally per network-session/context. **Cannot force a
shared child via public API.**

### Native replacement: `WebKitMemoryPressureSettings`
New in the 2.5x surface, attachable per session:

```c
WebKitMemoryPressureSettings *mp = webkit_memory_pressure_settings_new();
webkit_memory_pressure_settings_set_memory_limit(mp, 1024);      /* MB */
webkit_memory_pressure_settings_set_conservative_threshold(mp, 0.35);
webkit_memory_pressure_settings_set_strict_threshold(mp, 0.65);
webkit_memory_pressure_settings_set_kill_threshold(mp, 0.95);
webkit_memory_pressure_settings_set_poll_interval(mp, 2.0);       /* s */
/* attach to a network session: */
webkit_network_session_set_memory_pressure_settings(session, mp);
webkit_memory_pressure_settings_free(mp);  /* session keeps its own ref */
```

- Host API: `webkit_network_session_set_memory_pressure_settings`
  (WebKitNetworkSession.h:91) — **present on both** persistent and ephemeral
  sessions.
- This is a *better* low-memory story than the removed knob: WebKit itself
  sheds caches/processes when crossing thresholds, tuned to NiOn's 4 GB budget.

### NiOn seams confirmed
- Both sessions are created in one place — `network.c:83-85`
  (`webkit_network_session_new_ephemeral()` for private, `_new(data, cache)` for
  normal). A `nion_apply_memory_pressure(app)` helper called right after
  creation applies the settings to whichever session was built.
- Cache model is **already** pinned to `WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER`
  globally (`app.c:649`) — the "aggressive cache" half of low-memory mode
  exists; the memory-pressure settings are the missing half.

### Verdict for roadmap
Replace the "force shared secondary process" bullet with
**"attach native `WebKitMemoryPressureSettings` (tunable thresholds) per
session"**. Drop the crash-isolation trade-off language (no longer applicable).
Compositing-mode disable (`WEBKIT_DISABLE_COMPOSITING_MODE`) remains a separate
env-level option to verify at implementation time on the X11 target.

## 4. Non-API items (no spike needed)

- **#2 New Identity** — `tor-core.c` already has `nion_stop_tor_gracefully`
  (:634), `nion_start_tor` (:536), `nion_restart_tor_delayed` (:353), and
  cleanup via `nion_cleanup_stale_tor` (:142). A New Identity action is a thin
  orchestration + GAction. No new Tor control surface (as designed).
- **#3 Privacy Dashboard** — `nion_home_html()` (ui.c:358) already renders Tor
  bootstrap % and state; `nion_security_level_label()` (settings.c) is
  available; `app->tor_bootstrap_percent`, `app->security_level` live on
  `NionApp`. Adding discard counts + a New Identity button is template-only.
- **#5** — pure boundary logic in existing modules.

## 5. Recommendations for v2.1.0

1. Proceed with **#1 (Discard)** and **#2 (New Identity)** as the headline pair.
2. **Amend roadmap §"4. Low Memory"** to the native `WebKitMemoryPressureSettings`
   approach (this report supersedes the process-model assumption).
3. Build #3 (dashboard) as the UI glue — its data hooks are all present.
4. Implementation-order note: #2 is the smallest shippable win (pure
   orchestration) and exercises the fail-closed dance end-to-end; #1 is the
   larger engineering piece and should follow with its own module + commit.
