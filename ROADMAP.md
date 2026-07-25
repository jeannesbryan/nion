# NiOn Roadmap

**NiOn — Minimal Onion**

## 0.1.0 — Foundation ✅
GTK/WebKit foundation, address bar, navigation, multi-tab, Tor SOCKS routing, bootstrap, fail-closed networking, persistent profile.

## 0.2.0 — Browsing Reliability ✅
Onion validation, internal error handling, loading state, titles/favicons, popup-to-tab behavior, URL/search handling, onion retry.

## 0.3.0 — Downloads ✅
Tor-routed downloads, progress/cancel, collision handling, completion/failure handling.

## 0.4.0 — Session & Usability ✅
Tab/session restore, persistent cookie policy, browsing-data clearing, preferences, abnormal-shutdown recovery.

## 0.5.0 — Privacy & Leak Audit ✅
WebRTC/DNS/media permission hardening, local-network restrictions, proxy/fail-closed checks, runtime audit tooling.

## 0.6.0 — Tor Runtime ✅
Verified bundled Tor Expert Bundle, isolated runtime/data, robust startup/shutdown, dynamic SOCKS port, recovery handling.

## 0.7.0 — UI Polish ✅
Final icon integration, cleaner toolbar/tab layout, keyboard navigation, system-aware internal pages, About dialog.

## 0.8.0 — Browser UX & Tor Integration ✅
Onion-Location badge, last-tab blank behavior, close confirmation, expanded About, Exit menu, persistent Downloads window, Home button. The experimental circuit viewer added here was removed in 0.10.0.

## 0.9.0 — AppImage Engineering ✅
Final AppDir layout, bundled Tor and WebKit subprocesses, recursive runtime deployment, portable AppRun, desktop/AppStream integration, diagnostics, profile replacement tests.

## 0.10.0 — Browsing Essentials ✅
Removed unreliable Tor Circuit/ControlPort functionality; added Find in Page, zoom, hard reload, fullscreen, and search-engine preferences.

## 0.11.0 — Release Candidate ✅
Feature freeze, fail-closed reinforcement, profile corruption/size handling, bounded histories, Tor recovery refinements, AppImage cleanup, recovery/fail-closed test scripts.

## 1.0.0 — Stable ✅

- production AppImage pipeline
- bundled Tor
- stable persistent browser profile
- `.desktop` integration
- final application icon
- application metadata
- public README
- GPL-3.0-or-later LICENSE
- CHANGELOG
- GitHub Release notes/workflow
- documented build process
- final privacy/network audit procedure
- stable upgrade path
- GitHub-ready `.gitignore`

**Goal:** Open websites. Open onions. Everything through Tor.
## 1.1.0 — Everyday Privacy & Browsing ✅

- **Stage 1 — HTTPS-First for clearnet ✅**
  - schemeless clearnet addresses prefer HTTPS
  - explicit/top-level plain HTTP clearnet navigation shows a Cancel/Continue warning
  - HTTP `.onion` navigation remains allowed
- **Stage 2 — Simple Bookmarks ✅**
  - `Ctrl+D` bookmarks the current HTTP(S)/`.onion` page
  - persistent local `bookmarks.ini`
  - Bookmarks window with Open / Rename / Delete
  - duplicate URL protection and bounded profile loading
  - toolbar bookmark button reflects/toggles the active URL
  - blank/New Tab/internal error pages remain non-bookmarkable
- **Stage 3 — Clear Data for This Site ✅**
  - current-site confirmation dialog
  - WebKit per-site website-data fetch/remove
  - cookies/cache/storage for matching host/domain records
  - unrelated website-data records are not deliberately selected
  - reload active page without cache after success
  - global Clear Browsing Data remains available
- **Stage 4 — Tab Audio Indicator & Mute ✅**
  - speaker indicator appears per tab while WebKit reports audio playback
  - one-click mute/unmute affects only that tab
  - muted icon tracks WebKit `is-muted` state
  - mute state is included in session restore
  - redundant **Bookmark This Page** hamburger item removed; `Ctrl+D` and toolbar bookmark button remain
## 1.2.0 — Browsing Convenience & Site UX ✅

- Stage 1 — Tab Recovery & Tab Context Menu ✅
- Stage 2 — Page Actions: Better Web Page Context Menu + Print / Save as PDF ✅
- Stage 3 — Per-Site Zoom Memory ✅
- Stage 4 — Site & Connection Information + Mixed Content Detection ✅
- Stage 5 — Download Improvements ✅
- Stage 6 — Bookmark Search ✅

## 1.2.1 — Version & Dependency Manifest ✅

- centralized `release/manifest/` source of truth for NiOn release and bundled Tor versions
- Meson reads the canonical NiOn version file directly
- About/Tor version macros are generated from the manifest
- AppStream metadata is generated from a template using the manifest version/release type
- AppImage/source archive naming is manifest-derived
- bundled Tor fetch/repair/runtime metadata is manifest-derived
- GitHub Actions accepts generic `v*` tags but rejects tags that do not match the manifest
- release preflight detects version/dependency drift

## 1.3.0 — Tabs & Private Browsing (planned)

- Stage 1 — Pinned Tabs
- Stage 2 — Temporary / Private Window
- Stage 3 — Private download handling
- Stage 4 — Private-session audit

