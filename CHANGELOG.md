# Changelog

## 1.6.0 — 2026-07-27

### Reliability & Site Privacy

- Added per-tab WebKit web-process crash recovery using `WebKitWebView::web-process-terminated`. A terminated/crashed WebProcess no longer has to take the NiOn UI down with it: the affected tab shows a local recovery page, preserves the original URL, distinguishes memory-limit termination, and can recreate the page through the normal Tor-gated navigation path. Synthetic crash-page WebKit session state is never persisted; normal session recovery keeps the original URL instead.
- Added **Forget This Site…** as a deliberate superset of the existing **Clear Data for This Site…** operation. It removes targeted WebKit website data plus saved site zoom, JavaScript override, content-blocking exception, autoplay exception, temporary permission grants/capture, and matching temporary HTTP allowance. Bookmarks, download history, and downloaded files are intentionally preserved.
- Added **Forget This Site…** to both the main menu and Site Information.
- Kept the 1.5.0 Site Information transient-window width/scroll fixes and bounded long-URL wrapping.
- Added 1.6.0 regression guards for WebProcess recovery and per-site forgetting, re-ran historical feature regressions, and promoted 1.6.0 metadata to Stable.

## 1.5.0 — 2026-07-26

### Lightweight Content Blocking

- Added a deliberately small bundled JSON ruleset for common third-party advertising/tracking endpoints. Rules are compiled and enforced by WebKit's native `WebKitUserContentFilterStore` / `WebKitUserContentManager` pipeline; NiOn does not add an extension engine, daemon, or background list updater.
- Blocking is on by default for normal web pages and intentionally excludes top-level document blocking to reduce breakage.
- Added a **Content blocking** switch and status row to Site Information. Normal per-site disable exceptions persist in bounded atomic `~/.config/nion/content-blocking.ini`; Private Window exceptions remain memory-only.
- Added content-blocking exception clearing to Browsing Data for both normal and private contexts.
- Gave each WebView its own `WebKitUserContentManager` so per-tab/site filter state remains isolated while popup/new-window WebViews still use WebKitGTK 6 `related-view` and inherit the opener's Tor-backed network session.
- Embedded the ruleset in the NiOn GResource and added a dedicated Stage 1 regression test plus release-preflight coverage.

### Tracking & Media Protection

- Enabled WebKit Intelligent Tracking Prevention (ITP) by default on NiOn network sessions. The existing strict third-party-cookie option remains available as an explicit alternative and disables ITP to avoid WebKit's documented policy supersession.
- Added native WebKit autoplay protection using `WebKitWebsitePolicies`: audible autoplay is blocked by default while muted autoplay is allowed.
- Added a per-site **Allow autoplay with sound** control. Normal exceptions persist in bounded/atomic `~/.config/nion/autoplay.ini`; Private Window exceptions are memory-only.
- Applied autoplay policy at WebKit navigation-policy acceptance time with `webkit_policy_decision_use_with_policies()` and added autoplay-exception clearing to Browsing Data.

### Site Information reliability

- Replaced the Site Information `GtkMenuButton`/`GtkPopover` overlay entirely with a plain toolbar `GtkButton` that opens a transient Site Information window. This avoids popover-over-WebKit compositing, which continued to flicker on the tested lightweight X11 desktop even after earlier child/refresh fixes. The final 1.5.0 window is compact, resizable, and vertically scrollable so it remains usable on small displays.
- The transient window keeps the existing TLS/Tor/mixed-content/permission/JavaScript/content-blocking information and now also reports tracking-protection and autoplay state.

### Final hardening & release

- Added a bounded `GtkScrolledWindow` to Site Information so its growing security/privacy controls cannot force the transient window off-screen.
- Re-audited content filtering, ITP/strict-cookie mode, autoplay policy, JavaScript/permission controls, normal/private state separation, Tor fail-closed handling, popup/context-menu paths, recovery/data controls, and centralized release metadata.
- Promoted 1.5.0 release metadata to Stable. Feature development is paused after 1.5.0; subsequent work is maintenance and bug fixes unless the roadmap is explicitly reopened.

## 1.4.0 — 2026-07-26

### Site Controls

- Upgraded the existing deny-all permission handler into an explicit privacy-first gate for camera, microphone, geolocation, and notifications. Supported permissions remain blocked by default and can be allowed temporarily per origin for the lifetime of the current NiOn window; grants are never written to disk.
- Kept WebRTC peer connections disabled while allowing MediaStream permission requests to reach NiOn's camera/microphone gate. Screen/display capture and unscoped permission classes remain blocked.
- Added per-site JavaScript enable/disable controls to Site Information. Normal rules persist in bounded `~/.config/nion/site-javascript.ini`; Private Window rules are memory-only and never read the normal rule store.
- Added Site Information permission status rows and a reset action that revokes temporary grants and stops matching camera/microphone capture.
- Permission grants are revalidated against the requesting origin and Tor-ready state before approval; Tor failure revokes temporary grants and stops active capture.

### Recovery & Data Controls

- Added explicit unclean-shutdown recovery with **Restore Tabs** and **Start Fresh** choices. Unclean sessions are never auto-navigated before the user decides.
- Unclean recovery deliberately skips opaque WebKit session-state blobs and rebuilds from bounded URL/tab metadata first, reducing startup crash-loop risk while retaining malformed/oversized session quarantine.
- Upgraded the former all-or-nothing global clear into **Browsing Data…**, with selective clearing for cookies/site storage, Web cache, saved zoom, per-site JavaScript rules, and temporary permissions.
- Kept **Clear Data for This Site…** as the narrower per-site operation and provided equivalent early-clear controls for ephemeral Private Window state.
- Fixed the Site Information popover flashing closed by keeping a stable `GtkImage` child and updating only the image instead of resetting the `GtkMenuButton` child.

### Hardening & reliability

- Fixed Home navigation history: entering NiOn's synthetic Home/New Tab page now retains a strong reference to the active WebKit back/forward item, keeps Back enabled, and returns to that exact page when Back/`Alt+Left` is used.
- Re-audited Tor fail-closed navigation, private-session ephemerality, temporary permissions, per-site JavaScript isolation, crash recovery, data clearing, pinned/session state, downloads, `target=_blank`, and GTK4 context-menu invariants.
- Promoted 1.4.0 release metadata to Stable and added a final hardening/release regression guard.

## 1.3.0 — 2026-07-26

### Tabs & Private Browsing

- Added pinned tabs with a dedicated left-side block, session persistence, bulk-close protection, closed-tab recovery, and a visible `📌` marker.
- Added **New Private Window** (`Ctrl+Shift+P`) backed by a dedicated ephemeral WebKit network session while continuing to use NiOn's shared bundled Tor runtime and fail-closed routing.
- Private Window does not persist cookies/site data, WebKit credentials, tab/session restore, pinned state, per-site zoom, private download history, or private recently-closed tabs after the window closes.
- Bookmarks and the selected search engine remain intentionally global between normal and private windows.
- Added private download handling: history/source URLs stay memory-only, desktop completion notifications are suppressed, active downloads are cancelled on Private Window close/Tor failure, and tracked partial files are cleaned where possible. Completed files intentionally remain on disk.
- Closing an individual private tab requires confirmation; private bulk-close actions use a single confirmation.
- Added runtime verification that the private WebKit session is truly ephemeral and that persistent credential storage is disabled; Private Window creation fails closed if those invariants are not met.
- Expanded **Privacy & Leak Audit** to report private-session persistence boundaries and intentional user-export/global exceptions.

### Runtime fixes

- Fixed `target="_blank"`, `window.open()`, and related new-view handling for WebKitGTK 6 by constructing popup WebViews with the `related-view` property while preserving opener settings/content policies.
- Fixed page-context-menu crashes on WebKitGTK 6 / GTK4 by using the current `context-menu` signal signature without the removed `GdkEvent` parameter.
- Corrected context-menu ownership handling so NiOn does not free WebKit-owned menu lists and manages custom menu-item references safely.
- Added regression checks for related-view handling, context-menu ownership, and the GTK4/WebKitGTK 6 context-menu callback signature.

### Documentation and release cleanup

- Promoted 1.3.0 to Stable release metadata and AppStream `stable` status.
- Reworked `README.md` to focus on NiOn as a product: features, capabilities, privacy model, strengths, limitations, shortcuts, profile behavior, and concise run/build guidance.
- Consolidated AppImage, Tor-runtime, upgrade, repository, and release-process guidance into the remaining core documentation.
- Removed obsolete/duplicative Markdown files and made historical feature tests independent of `ROADMAP.md`.
- Kept the centralized `release/manifest/` as the single source of truth for NiOn/Tor/AppImage release-critical values.

## 1.2.1 — Version & Dependency Manifest

- Added `release/manifest/` as the canonical source for NiOn release version, AppImage architecture, bundled Tor pins, release status, and minimum GTK/WebKitGTK versions.
- Meson now reads the canonical version directly and generates C/About version macros from manifest values.
- Converted AppStream metadata to a build-time template so the current release version/type is manifest-driven.
- AppImage/source archive naming, Tor fetch/repair validation, runtime metadata, and development runtime checks now consume the same manifest.
- GitHub Actions release tags/assets are manifest-driven and reject tag/version mismatches.
- Added `test-manifest-1.2.1.sh` and expanded release preflight to detect release-critical version drift.
- Kept NiOn 1.2.0 Stage 1–6 browser behavior and profile formats unchanged.

## 1.2.0 — Stage 6: Bookmark Search

- Added a native GTK search field to the existing Bookmarks window without changing `bookmarks.ini` or creating a separate index/database.
- Search filters saved bookmarks live by both title and URL.
- Matching is case-insensitive, Unicode-aware for valid UTF-8 text, and supports multiple whitespace-separated terms; every non-empty term must match either the title or URL.
- Added live result-count feedback and a dedicated no-results message while preserving the existing no-bookmarks placeholder.
- Typing while the Bookmarks window is active can feed the search field through GTK search key capture; the temporary query is cleared when the window is closed.
- Open, Rename, Delete, duplicate protection, `Ctrl+D`, and the toolbar bookmark toggle continue to operate on the same bounded local bookmark store.
- No network requests, Tor configuration, session format, cookie state, download history, or site-zoom state are added or changed by bookmark search.

## 1.2.0 — Stage 5: Download Improvements

- Added a compact per-download **⋮** actions menu without widening the existing Downloads row layout.
- Completed files can be opened with the desktop's registered local file handler.
- **Open Containing Folder** launches the local parent directory without sending it through WebKit or Tor.
- **Copy Download Link** copies the original WebKit download request URI to the system clipboard.
- Failed downloads can be retried with a fresh `webkit_web_view_download_uri()` request on the active NiOn WebView, preserving NiOn's Tor-proxied WebKit network session and fail-closed behavior.
- Retry is only offered for genuine **Failed** history entries; user-cancelled downloads are not treated as failures.
- Download history now optionally stores a bounded `source-uri` field so Copy Link and Retry survive a NiOn restart. Existing history without this field remains backward-compatible.
- Source-dependent retry accepts only NiOn-valid HTTP(S) addresses and is blocked while bundled Tor is unavailable.
- No changes to cookie/session persistence, bookmark storage, site zoom, TLS/mixed-content handling, or the saved-tab session format.

## 1.2.0 — Stage 4: Site & Connection Information

- Added a compact site-information button and popover beside the Onion-Location area for normal web pages.
- The popover reports the host, website connection type, bundled-Tor route state, mixed-content state, and complete current address.
- HTTPS pages read WebKit TLS information after navigation commits; certificate verification remains strict and the network session explicitly uses `WEBKIT_TLS_ERRORS_POLICY_FAIL`.
- Tor v3 `.onion` pages are identified separately so a plain-HTTP onion is not falsely labeled as HTTPS.
- Added WebKit `insecure-content-detected` handling for insecure active/run content and insecure displayed content.
- A mixed-content event changes the site-information icon to a warning icon and surfaces a `MIXED CONTENT DETECTED` status for the active tab.
- Mixed-content flags reset on each new navigation and remain per-tab; switching tabs therefore shows the selected page's own state.
- Home/New Tab and internal error pages keep the site-information control disabled.
- No proxy-routing, cookie/session persistence, bookmark, download, or site-zoom format changes in this stage.

## 1.2.0 — Stage 3: Per-Site Zoom Memory

- Page zoom now persists per HTTP(S) hostname and Tor v3 `.onion` hostname across tabs and NiOn restarts.
- HTTP and HTTPS on the same hostname intentionally share one zoom preference; explicit non-default ports remain separate site keys.
- Existing `Ctrl++` / `Ctrl+=`, `Ctrl+-`, and `Ctrl+0` controls remain the UI for zoom.
- `Ctrl+0` returns the active page to 100% and removes its stored site-specific override instead of persisting a redundant default value.
- Stored zoom is applied when navigation commits, so redirects use the destination site's remembered value.
- New Tab/Home and internal error pages always render at 100% and never create a site-zoom record.
- Added bounded, atomic `~/.config/nion/site-zoom.ini` persistence with mode `0600`, a 1 MiB file limit, at most 2048 remembered sites, and quarantine of malformed/oversized state.
- No Tor proxy/fail-closed, cookies, bookmarks, downloads, or session-format changes in this stage.

## 1.2.0 — Stage 2: Page Actions

- Improved WebKit's page context menu without replacing its context-sensitive editing/media actions.
- Relabeled WebKit's new-window link/image actions as **Open Link in New Tab** and **Open Image in New Tab**; they continue through NiOn's existing `WebView::create` handler, so new content stays inside NiOn.
- Relabeled the WebKit image download action as **Save Image**; downloads continue through NiOn's Tor-proxied WebKit download pipeline and Downloads history.
- Kept WebKit's native Copy Link Address, Copy Selection, editing, spelling, and media context actions intact.
- Added **Print / Save as PDF…** to the page context menu and hamburger menu.
- Added `Ctrl+P` for printing the active page with `WebKitPrintOperation`. The system print dialog can use the platform's Print to File/PDF option where available.
- New Tab remains intentionally non-printable.
- No Tor, cookie, session, bookmark, or profile-format changes in this stage.

## 1.2.0 — Stage 1: Tab Recovery & Tab Context Menu

- Added `Ctrl+Shift+T` to reopen recently closed web tabs.
- Kept a bounded in-memory stack of the 10 most recently closed reopenable HTTP(S) tabs; blank/New Tab pages are not recorded.
- Reopened tabs preserve their URL and muted state without pretending to restore the full back/forward history.
- Added a right-click tab context menu with Reload, Duplicate Tab, Mute/Unmute Tab, Close Tab, Close Other Tabs, and Close Tabs to the Right.
- Closing the final tab still keeps NiOn alive by creating a fresh New Tab.
- Bulk tab-close actions feed the same bounded reopen stack, so accidental closes can be recovered in reverse order.
- No Tor/network/profile format changes in this stage.

## 1.1.0 - 2026-07-25

### Stable

- Added HTTPS-First handling for clearnet with an explicit warning before plain-HTTP clearnet navigation.
- Added simple persistent local bookmarks plus an address-bar bookmark toggle and `Ctrl+D`.
- Added per-site browsing-data clearing without intentionally clearing unrelated sites.
- Added per-tab audio playback indication and mute/unmute.
- Kept the 1.0.0 profile layout and AppImage upgrade path compatible.
- Updated AppImage/release packaging metadata and GitHub Actions for the 1.1.0 release.

## 1.1.0 — Stage 4: Tab Audio Indicator & Mute

- Added a compact per-tab audio button driven by WebKit's `is-playing-audio` property.
- Clicking the audio button toggles only that tab's `is-muted` state.
- The button icon changes between speaker and muted states and exposes matching tooltips.
- Saved per-tab mute state in `session.ini` so restored tabs keep their intended mute state without affecting other tabs.
- Removed the redundant **Bookmark This Page** item from the hamburger menu; the toolbar bookmark button and `Ctrl+D` remain available.
- Completed the four planned NiOn 1.1.0 stages.

## 1.1.0 — Stage 3: Clear Data for This Site + Bookmark Toolbar

- Added a bookmark icon button immediately beside the address bar and before the hamburger menu.
- The bookmark icon automatically follows the active tab/URL and switches between unbookmarked and bookmarked states.
- Clicking the bookmark icon toggles the current URL in `bookmarks.ini`.
- Blank/New Tab and internal error pages remain non-bookmarkable and keep the toolbar bookmark button disabled.
- Bookmark state is refreshed on tab switches, URI changes, bookmark additions, and bookmark deletion.
- Added **Clear Data for This Site…** with a confirmation dialog for the active HTTP(S) host.
- Site-specific clearing uses WebKit website-data fetch/remove APIs rather than clearing the whole profile.
- Matching records include the current host and a parent-domain record when WebKit groups a subdomain under that domain; unrelated/third-party records are not deliberately selected.
- Successful site-data clearing reloads the active page without cache so cookie/storage changes become visible immediately.
- Kept the existing global **Clear Browsing Data…** action unchanged.

## 1.1.0 — Stage 2: Simple Bookmarks

- Added `Ctrl+D` to bookmark the current clearnet or `.onion` page.
- Added **Bookmark This Page** and **Bookmarks** entries to the NiOn menu.
- Added a local Bookmarks window with Open, Rename, and Delete actions.
- Bookmarks persist in `~/.local/share/nion/bookmarks.ini`.
- Duplicate URLs are ignored instead of creating repeated entries.
- Added bounded bookmark profile loading and quarantine for oversized/malformed bookmark files.
- Kept bookmarks deliberately local and flat: no folders, tags, accounts, sync, or cloud services.

## 1.1.0 - Unreleased

### Stage 1 — HTTPS-First for clearnet

- Clearnet addresses without an explicit scheme continue to resolve to HTTPS first.
- Added an interstitial warning before top-level plain-HTTP clearnet navigation is allowed.
- The warning explains that Tor still carries traffic to an exit relay, while plain HTTP does not provide HTTPS protection between the exit relay and the clearnet website.
- `http://` Tor v3 `.onion` addresses remain allowed without the clearnet HTTP warning.
- Choosing **Continue** allows plain HTTP for that clearnet origin in the current tab until the tab commits to a non-HTTP page; this avoids repeated prompts for same-site links, reloads, and form submissions.
- Choosing **Cancel** leaves the current page in place and does not issue the blocked HTTP navigation.
- No profile-format change. Cookies, saved sessions, downloads history, and 1.0.0 profiles remain compatible.

## 1.0.0 - 2026-07-25

### Stable

- Promoted the feature-frozen 0.11.0 release-candidate code line to NiOn 1.0.0 Stable without introducing a new browser feature family.
- Finalized release metadata, desktop integration, AppStream information, icon packaging, and version strings for 1.0.0.
- Added GPL-3.0-or-later licensing for NiOn source code plus third-party runtime notices.
- Added a release-quality `.gitignore` so build output, AppImage artifacts, downloaded Tor runtime files, tools, crash files, and editor state do not pollute the repository.
- Reworked the README for the public repository and added `BUILDING.md`, `UPGRADING.md`, `SECURITY.md`, `GITHUB_RELEASE.md`, and repository metadata suggestions.
- Renamed the RC preflight/test documentation to stable release validation and retained the fail-closed/profile recovery test suite.
- AppImage builds now include NiOn license/notices in the AppDir and run metadata validation when the relevant host tools are available.
- Added a GitHub Actions release workflow for tag-driven x86_64 AppImage builds and GitHub Release asset upload.
- Documented the stable profile path and 0.11.0 → 1.0.0 upgrade behavior. No profile-format bump is introduced by 1.0.0.

## 0.11.0 - 2026-07-25

### Release Candidate

- Feature freeze: no new browser feature family was introduced.
- Strengthened fail-closed behavior after Tor failure by moving WebKit to a dead loopback SOCKS endpoint and rejecting new HTTP/HTTPS navigation in policy code while Tor is offline.
- Added bounded/corruption-aware loading for preferences, saved sessions, and download history. Malformed or oversized NiOn-owned profile files are quarantined with `.corrupt-...` suffixes instead of being retried forever.
- Added a SQLite-header sanity check for the persistent cookie database before WebKit opens it; obviously invalid cookie DB/WAL/SHM files are quarantined.
- Bounded opaque WebKit tab-session snapshots and total session-state budget while preserving URL fallback recovery.
- Capped persistent download history to the newest 500 finished/failed/cancelled records.
- Increased session-save debounce from 250 ms to 750 ms to reduce repeated session serialization during busy tab activity.
- Normalized NiOn data/config/cache/Tor directories to mode `0700` where possible.
- Refined Tor corruption detection so ordinary mentions of cached consensus/microdescriptor files no longer count as corruption without a damage/parse/read failure indicator.
- Tor state recovery now reports a hard failure when its quarantine directory cannot be created.
- AppImage packaging removes Tor Expert Bundle debug-symbol directories from the production AppDir and uses safer temporary files in diagnostics.
- Removed the desktop file's unimplemented `%u`/HTTP(S) handler advertisement; internal About links continue opening inside NiOn.
- Added `rc-preflight.sh`, `test-profile-recovery.sh`, `test-fail-closed.sh`, and `RC_TESTING.md`.

## 0.10.0 Hotfix 1 - 2026-07-25

- Fixed a C17 compile failure in Find in Page by forward-declaring the `found-text` and `failed-to-find-text` callbacks before `nion_new_tab()` connects them.
- No behavior or profile-format changes.

## 0.10.0 - 2026-07-25

### Browsing Essentials

- Removed the unreliable Tor Circuit viewer and removed its private Tor ControlPort configuration. NiOn now reserves only its SOCKS listener.
- Added Find in Page with `Ctrl+F`, live searching, match count, next/previous controls, Enter/Shift+Enter navigation and Escape to close.
- Added per-tab page zoom with `Ctrl++`, `Ctrl+-`, `Ctrl+0` and Zoom controls in the hamburger menu.
- Added hard reload / cache-bypass with `Ctrl+Shift+R`.
- Added `F11` fullscreen mode; browser chrome is hidden while the window is actually fullscreen and restored on exit.
- Added a persistent default search-engine preference with DuckDuckGo, Brave Search and Startpage choices.
- Kept the 0.9.0 AppImage packaging baseline and internal About repository-link handling.

## 0.9.0 - 2026-07-25

### AppImage Engineering

- Replaced the thin/developer AppImage builder with a final AppDir-oriented packaging pipeline.
- Bundles the NiOn executable and the verified Tor Expert Bundle runtime.
- Explicitly discovers and bundles WebKitGTK 6 WebKitWebProcess/WebKitNetworkProcess and WebKitGPUProcess when present.
- Added recursive ELF dependency deployment while deliberately leaving glibc and host graphics-driver libraries to the target system.
- Added GIO runtime module deployment for TLS/proxy backends where available.
- Added a small WebKit subprocess spawn-path shim for distro builds with compile-time absolute helper paths.
- Keeps WebKitGTK 6's mandatory WebProcess sandbox enabled and exposes the read-only AppImage mount to that sandbox before any WebProcess is created.
- Added final AppRun environment setup, root desktop/icon links, AppStream metadata and package build provenance.
- Added AppImage diagnostics, profile-path replacement checks and optional container loader smoke tests.
- Repository links in About NiOn now open inside NiOn rather than the operating system's default browser.

## 0.8.0 - 2026-07-25

### Browser UX & Tor Integration

- Added Onion-Location discovery from the standardized HTTP response header and equivalent HTML `meta http-equiv="onion-location"` declaration.
- Onion-Location is accepted only from HTTPS clearnet pages and must point to a valid HTTP/HTTPS Tor v3 `.onion` URL.
- Added an address-bar Onion badge; clicking it opens the advertised onion site in a new tab.
- Added a Tor Circuit viewer backed by NiOn's private loopback ControlPort with cookie authentication.
- Circuit snapshots show Guard / Middle / Exit roles for clearnet, and Guard / Middle / Rendezvous for onion circuits when available, plus relay nickname, IP and Tor GeoIP country code.
- Added paired dynamic ControlPort selection (`127.0.0.1:19150-19169`) alongside the SOCKS port range.
- Closing the final tab now creates a clean blank/New Tab page instead of terminating NiOn.
- Added close confirmation when one or more nonblank website tabs remain open. All-blank sessions close directly.
- Expanded About NiOn with author, repository, runtime status and GTK/WebKitGTK/libsoup/GLib versions.
- Added Exit to the hamburger menu.
- Added a dedicated persistent Downloads window with `Ctrl+J`.
- Download history records completed, failed and cancelled downloads across NiOn restarts and supports Clear Downloads.
- Added a Home button next to Reload to return the current tab to NiOn's blank/New Tab page.

## 0.7.0 - 2026-07-24

- UI polish, NiOn icon integration, cleaner toolbar/tab layout, keyboard navigation, improved new-tab page and About dialog.

## 0.6.0 - 2026-07-24

- Bundled Tor Expert Bundle runtime lifecycle.
- Added runtime repair to avoid loading debug-symbol objects as shared libraries.

### 1.3.0 Stage 1 Fix 4
- Fixed the remaining immediate crash when opening a web-page context menu on WebKitGTK 6 / GTK4.
- Corrected the `WebKitWebView::context-menu` callback signature by removing the GTK3-era `GdkEvent` parameter. On WebKitGTK 6 that extra argument shifted `hit_test_result` and `user_data`, allowing an invalid `NionTab` pointer to be dereferenced.
- Added a regression guard for the WebKitGTK 6 signal signature.
