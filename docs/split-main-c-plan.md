# NiOn 2.0.0 — `main.c` Split Plan (Completed)

> Status: **DONE** — the god-file is dead. `src/main.c` went from **10,822 lines**
> (~86% of `src/`, ~224 top-level functions) to a **599-line coordinator core**.
> This document is the permanent record of the module map, dependency policy,
> extraction order, and verification used during Phases 0–7.

## 1. Goal

The original NiOn was a single `src/main.c` monolith (~10.8k lines) that mixed
pure predicates, leaf state modules, UI chrome, Tor subprocess lifecycle, and
the GTK application entry point. The goal of 2.0.0 was a disciplined,
behavior-preserving decomposition:

- **move code, do not rewrite it** (no semantic drift, no opportunistic fixes);
- **head-first** modules (`.h` interface, then `.c` implementation);
- **one-directional dependencies** (leaf → UI layer → coordinator, never back);
- **UI decoupling through callbacks** so no module `#include`s `main.c`;
- **byte-identical bodies** — only `static` dropped on newly public symbols and
  local forward declarations added where main.c's old top-of-file decls had
  previously covered use-before-definition;
- every extraction committed independently so `git revert` rolls back one module;
- `meson.build` updated for every new `.c` (its `executable()` list is the
  mechanical switch that must never be missed);
- security invariants untouched: fail-closed Tor, Private Window isolation,
  all-permissions-denied baseline, URI protocol boundary, local-network lock-down.

## 2. Final module map (`src/`)

| Module | Role | Lines (approx) |
|---|---|---|
| `types.h` | shared types / enums / constants (`NionApp`, `NionTab`, `NionDownload`, …) | 336 |
| `util.c` | pure helpers: file-size bounds, atomic key-file writes, byte formatting, base64 sanity | 129 |
| `navigation.c` | URI/protocol predicates: internal schemes, external schemes, onion hosts, HTTPS-first | 358 |
| `per-site.c` | per-site zoom / JavaScript / autoplay / content-blocking rules | 695 |
| `content-filter.c` | lightweight native content-filter runtime state | 57 |
| `permission.c` | all-denied baseline + temporary per-origin permission grants | 95 |
| `privacy.c` | cookie/privacy settings application | 77 |
| `tor-core.c` | bundled-Tor subprocess lifecycle, Tor state, dead-SOCKS fail-closed | 674 |
| `network.c` | WebKit network-session preparation, proxy application, download wiring | 129 |
| `session.c` | session save/restore, crash recovery, private-window session audit | 457 |
| `tabs.c` | tab model, tab bar UI, close/pin/reorder/reopen-closed | 808 |
| `webview.c` | WebKitWebView signal handlers, per-tab policy glue | 803 |
| `bookmarks.c` | bookmarks model, window UI, toolbar state | 729 |
| `downloads.c` | downloads model, window UI, history, private cleanup | 920 |
| `settings.c` | preferences persistence, Security Levels, Preferences dialog | 414 |
| `site-data.c` | Site Information window, clear-site-data / browsing-data flows | 1,338 |
| `app.c` | application lifecycle: dirs, AppImage sandbox, Tor-state coordination, close/shutdown, private windows | 667 |
| `ui.c` | core chrome: window/toolbar builder, remaining actions, dialogs, find/zoom/print, status/title/controls | 2,272 |
| `main.c` | **coordinator core**: `main()`, `on_activate`, action table, webview-creation hub, content-filter/onion security glue | **599** |

**Total** ≈ 11.2k lines across 19 `.c`/`.h` units — the architecture stayed
roughly the same size while becoming navigable and individually auditable.

## 3. Dependency policy & callback surfaces

Modules never `#include` `main.c`. Cross-module UI touchpoints are injected as
small callback structs registered by `main.c`'s `on_activate`:

| Callback struct | Provides (from UI layer) | Used by |
|---|---|---|
| `NionTorCallbacks` | `set_status`, `set_tor_error`, `set_tor_progress` | `tor-core.c`, `network.c` |
| `NionSessionCallbacks` | `new_tab`, `set_tab_pinned`, … (session restore hooks) | `session.c` |
| `NionTabCallbacks` | `new_tab`, `set_status`, `update_controls`, `load_home`, … | `tabs.c` |
| `NionWebviewCallbacks` | `update_controls`, `show_error_page`, … (19 entries) | `webview.c` |
| `NionBookmarkCallbacks` | `current_tab`, `new_tab`, `set_status` | `bookmarks.c` |
| `NionDownloadCallbacks` | `current_tab`, `set_status` | `downloads.c` |
| `NionSettingsCallbacks` | `set_status`, `update_controls`, `update_site_info` | `settings.c` |
| `NionSiteDataCallbacks` | `current_tab`, `set_status`, `apply_content_filter_to_window` | `site-data.c` |
| `NionAppLifecycleCallbacks` | `set_status`, `update_controls`, `refresh_home_pages`, `stop_all_web_activity`, `build_ui` | `app.c` |
| `NionUiCallbacks` | `new_tab`, `current_tab`, `set_status`, `install_actions` | `ui.c` |

Each module exposes only the handful of public functions `main.c` (or the
WebKit/GTK signal layer) needs; everything else stays `static` inside the
module. `main.c` keeps the central wiring: it registers every callback struct
and hosts the `GActionEntry` table.

## 4. Extraction order (Phases 0–7)

1. **Phase 0** — types/constants → `types.h` (main.c −323 lines).
2. **Phase 1** — pure predicates/helpers → `util.c`, `navigation.c` (−486).
3. **Phase 2** — leaf state → `per-site.c`, `content-filter.c`, `permission.c`, `privacy.c` (−907).
4. **Phase 3** — Tor/network → `tor-core.c`, `network.c` (+ `NionTorCallbacks`) (−745).
5. **Phase 4** — session/crash recovery → `session.c` (−386).
6. **Phase 5a/5b** — tabs → `tabs.c`; webview handlers → `webview.c` (−1,429).
7. **Phase 6** — bookmarks → `bookmarks.c`; downloads → `downloads.c`; settings → `settings.c`; site-data → `site-data.c` (−3,236).
8. **Phase 7a/7b** — lifecycle → `app.c`; core chrome/actions → `ui.c` (main.c → **599**).

## 5. Verification

- Every phase: `ninja -C build` green, **zero new compiler warnings** (only
  pre-existing system-header pedantic noise and known WebKit deprecations).
- Binary smoke test after each phase (launches, Tor bootstrap starts,
  callbacks registered).
- Static test suite retargeted in Phase 7 to scan `src/*.c`/`src/*.h`
  recursively (see commit `439de8a`); all code-location guardrails green.
- Security invariants re-verified via the release preflight
  (`scripts/release-preflight.sh`) and the per-domain regression scripts.

## 6. Notes / follow-ups

- The `_NionApp` god object is intentionally **not** decomposed further: its
  fields are the shared state that every module reads through the callback/API
  surface, and splitting it would couple modules to fragments of each other.
- The UI-layer test scripts still rely on static greps of `src/` plus live
  runtime scenarios in `TESTING.md`; the static greps are now
  module-location-agnostic so future extractions do not blind them again.
- See `docs/main-c-function-inventory.md` for the detailed function → module
  mapping used to draw the boundaries.
