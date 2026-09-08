# NiOn v2.1.0 Roadmap

> Status: **Draft for review** · Target: NiOn 2.1.0 · Platform: GNU/Linux x86_64,
> dev constraint: **4 GB physical RAM**, Tor-only, fail-closed, strictly minimal.

This roadmap proposes the feature set for the release after the 2.0.0 modular
architecture. Every item is evaluated against NiOn's core philosophy:

- **Tor-only** — nothing may weaken the "everything through Tor or nothing" rule;
- **fail-closed** — new features must reuse (never bypass) the existing
  fail-closed state machine in `tor-core.c`/`main.c`;
- **minimalist** — each feature must earn its place; no new runtime bloat;
- **4 GB memory ceiling** — features must *reduce* or *bound* memory, not add to it;
- **modular seams** — new code must hang off the callback/API surfaces built in
  Phases 0–7, never reach back into `main.c` or create cycles.

Manifest baseline for this release cycle:
`NION_VERSION 2.0.0 → 2.1.0` · `WEBKITGTK_MIN 2.40` (tested 2.52.x) ·
`GTK_TESTED 4.22.4` · `GLIB_TESTED 2.88.2`.

---

## Headline features (v2.1 core)

### 1. Background Tab Discard (auto-suspend) — *the memory lever* — ✅ **DONE**

> **Status: implemented & merged to `main`** on `feature/v2.1-tab-discard`
> (commit `bf97e1c`). **Final shape** (revised by spike probes): WebKitGTK
> shares one WebProcess across tabs here, so terminating a process would take
> sibling tabs down, and in-place HTML unload alone does not return RSS.
> v1 ships a **soft unload** — a background tab idle past
> `NION_TAB_DISCARD_AFTER_MS` (5 min) is stopped and swapped to a tiny
> internal "💤 Suspended" document (releasing its page from the web process),
> keeping the `NionTab` shell + strip entry + real title alive; clicking the
> tab revives it through the Tor-gated loader. The 💤 chip, live dashboard
> "Discarded" count, per-15s sweep (`NION_TAB_DISCARD_POLL_MS`), session-save
> of the real URI, and full activity-touch guards all shipped. Memory is then
> actively shed by feature #4's pressure settings.

**Problem.** WebKitGTK is the dominant memory consumer: every real tab holds a
web process. On a 4 GB host, ~8–10 content-heavy tabs exhaust comfortable headroom
and the OS starts swapping.

**Idea.** A background tab that has not been viewed for N minutes is
*suspended*: NiOn destroys its `WebKitWebView` (releasing the web process and
its heap) while keeping the lightweight `NionTab` shell — URI, title, favicon,
pinned state, mute state, and a "was discarded" flag. Clicking the tab (or
Ctrl+Tab into it) recreates the WebView through the existing
`nion_new_tab_internal` hub (`main.c`) and reloads through Tor.

**Design notes (modular seam).**
- New module `src/discard.c/.h` + a small `NionDiscardCallbacks` surface
  (needs `new_tab_internal`, `current_tab`, `set_tab_state`, `notify`).
- Timer is per-`NionApp` idle/`g_timeout_add_seconds`; a preference
  ("Suspend background tabs after N minutes", default e.g. 5–10 min, 0 = off).
- Tab strip shows a muted "💤" indicator; suspended tabs are skipped by
  session-save's WebKit-state snapshot (they have none to snapshot).
- The web-process-crash recovery path in `session.c`/`webview.c` already
  models "tab shell alive, web content gone" — discard reuses that UX.

**Memory impact.** Near-linear memory ceiling on tab count: 20 tabs ≈ 2–3 real
processes instead of 20. This is the single largest RAM lever available.

**Honest caveat (v1 scope).** WebKitGTK does not expose Chrome-style
"freeze"; discard is destroy-and-recreate. Spike finding (2.52.6): session
state via `webkit_web_view_get_session_state()` +
`webkit_web_view_session_state_serialize()` (then `restore_session_state()` on
the recreated view) preserves the **URL and back/forward history** but **not
scroll position** or in-page form state. So v1 discard restores history, yet
the page reloads at the top; a tiny JS `scrollY` snapshot before teardown is a
possible v1.1 refinement — not a v1 promise.

**Effort.** Medium (~1 focused module + hub touch + pref wiring). No new deps.

---

### 2. Tor "New Identity" (circuit refresh)

**Problem.** Long-lived Tor circuits are linkable across tabs over time.
Today the only full reset is quitting NiOn.

**Idea.** A **New Identity** action (menu + optional toolbar button + start-page
button, see #3) that rotates the Tor circuit through the *existing* Tor
lifecycle in `tor-core.c`:
1. stop the child cleanly (`nion_stop_tor_gracefully`, already present);
2. purge runtime state exactly as `nion_cleanup_stale_tor` does (dead-SOCKS
   guard, control file, stale pid), and clear the cookie jar for the session;
3. relaunch (`nion_start_tor`) and run the full fail-closed dance:
   every window moves `tor_error → tor_progress → tor_ready` via the
   already-wired `NionTorCallbacks`;
4. private windows follow automatically through
   `nion_sync_private_windows_tor`.

**Why it fits & why this shape.** It reuses machinery NiOn already owns —
including the existing `nion_restart_tor_delayed` path — and deliberately adds
**no Tor control-port / SIGHUP surface** (a control port would be new attack
surface and violate minimalism). Navigation is already blocked during the swap
by the fail-closed path, so no new policy code is needed.

**Effort.** Small–medium. Mostly a thin orchestration function in
`tor-core.c`/`app.c` + a GAction in `ui.c` + button/menu wiring.

---

### 3. Start Page as a Privacy Dashboard (UI glue for #1 and #2)

**Problem.** The New Tab page is a lost opportunity — it is where a user lands
when unsure what to do, and today it is a plain search/home surface.

**Idea.** Enrich the internal home page (already built by `nion_home_html` in
`ui.c`, fed by the `NionTorCallbacks` progress stream) to display:
- real Tor bootstrap state / current status;
- current global Security Level;
- active-tab count vs. discarded-tab count (from #1);
- a short per-site permission summary;
- a prominent **New Identity** button (from #2).

**Why it fits.** All data already lives in `NionApp`/`tor-core`; this is a
static HTML generator change, not a new subsystem. Zero new runtime state.

**Effort.** Small. Pure `ui.c`/template work.

---

## Secondary / fast-follow candidates

### 4. "Low Memory" mode (native memory-pressure tuning) — ✅ **DONE**

> **Status: implemented on `feature/v2.1-tab-discard` (commit `f224e84`).**
> **Final decision: always-on, no Preferences toggle.** Minimalist call —
> NiOn users get the optimized lightweight experience by default; a toggle
> adds cognitive load for no benefit. `nion_prepare_network()` installs
> `WebKitMemoryPressureSettings` **before** the first session (the once-guarded
> seam through which every normal + private window builds its session):
> 1.5 GB working-set cap, conservative 0.33 / strict 0.50 release thresholds,
> **kill_threshold 0** (tabs share a WebProcess — never hard-kill), **2 s poll**
> (vs WebKit's default 30 s) so memory freed by a discard returns to the OS
> within seconds. Verified live: RSS drops promptly behind the 💤 indicator.

**Spike finding (revises earlier draft).** The old WebKitGTK knob to force a
**shared secondary-process** model (`webkit_web_context_set_process_model`) is
**removed in WebKitGTK 2.52** — no `process_model` API remains in the public
headers. Instead, 2.52 ships a first-class native pressure API:

```c
WebKitMemoryPressureSettings *mp = webkit_memory_pressure_settings_new();
webkit_memory_pressure_settings_set_memory_limit(mp, 1024);      /* MB budget */
webkit_memory_pressure_settings_set_conservative_threshold(mp, 0.35);
webkit_memory_pressure_settings_set_strict_threshold(mp, 0.65);
webkit_memory_pressure_settings_set_kill_threshold(mp, 0.95);
webkit_memory_pressure_settings_set_poll_interval(mp, 2.0);       /* seconds */
webkit_network_session_set_memory_pressure_settings(session, mp);
webkit_memory_pressure_settings_free(mp);   /* session keeps its own ref */
```

Verified present on **both** persistent and ephemeral `WebKitNetworkSession`
(WebKitNetworkSession.h:91), and linkable/runable at 2.52.6 (spike probe rc=0).

**Idea.** A "Low Memory" preference that:
- attaches `WebKitMemoryPressureSettings` (a 4 GB-tuned budget +
  conservative/strict/kill thresholds + poll interval) to each network session
  right where NiOn builds them (`network.c:83-85` — normal + private in one
  seam), so WebKit itself sheds caches/processes as pressure crosses thresholds;
- disables the GPU/compositing path (`WEBKIT_DISABLE_COMPOSITING_MODE`) on the
  lightweight X11 desktop NiOn targets (env-level, verify at implementation);
- keeps the WebKit cache pinned to `WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER`
  (already set globally in `app.c:649`).

**Why it fits.** Configuration, not new subsystems — directly answers "don't
bloat memory," and lets WebKit make eviction decisions against a budget NiOn
chooses. This is a *better* low-memory story than the removed process knob.

**Effort.** Small. No crash-isolation trade-off to document (shared-process
knob no longer exists); per-tab recovery UI unchanged.

### 5. Strict HTTPS + Onion-Location polish (hardening)

**Idea.** Two tight wins in `navigation.c`/`ui.c`/`per-site.c`:
- an opt-in **HTTPS-only** enforcement that upgrades or refuses clearnet HTTP
  beyond the current HTTPS-first behavior;
- richer **Onion-Location** surfacing: a "open .onion in new tab" affordance
  plus a remembered preferred-onion per site (in `per-site.c`) so repeat visits
  can offer the direct jump.

**Why it fits.** Pure boundary logic NiOn already owns; no new runtime cost;
deepens the "everything through Tor" promise.

**Effort.** Small–medium.

---

## Suggested sequencing

| Step | Scope | Headline |
|---|---|---|
| 2.1.0-a | **Spike** (branch `spike/v2.1-memory-apis`) ✅ done | Verified in `docs/spike-v2.1-memory-apis.md` — discard = destroy+recreate (session-state keeps URL/history, not scroll); `set_process_model` removed, native `WebKitMemoryPressureSettings` is the #4 approach |
| 2.1.0-1 | Feature #2 (New Identity) ✅ done | merged to `main` via `feature/v2.1-new-identity` (`17f3c37`, `11eb2a3`, `591a8af`) |
| 2.1.0-2 | Feature #1 (Tab Discard) ✅ done | `bf97e1c` — soft-unload discard + 💤 + sweep + dashboard count |
| 2.1.0-3 | Feature #3 (Privacy Dashboard) ✅ done | merged in `feature/v2.1-new-identity` (`0cc89d2`) |
| 2.1.0-4 | Feature #4 (Memory pressure) ✅ done | `f224e84` — always-on WebKitMemoryPressureSettings, no toggle |
| 2.1.0-5 | Feature #5 (HTTPS-only / Onion-Location) | **open** — next fast-follow candidate |

## Definition of done for v2.1.0

- New modules (if any) registered in `meson.build` and committed independently
  (AGENTS.md §5 discipline: move-only-when-moving, one-directional deps,
  callback structs for UI touchpoints, revertable commits).
- Memory: before/after `/proc` RSS measurement on a fixed 12-tab workload
  shows discard yields a *drop* (target ≥ 40% under load).
- Tor-only / fail-closed invariants untouched — `test-fail-closed.sh` still
  green live; static suite stays 100%; Private Window isolation intact.
- Manifest bumped to 2.1.0 only at release time; docs/landing updated in step.
