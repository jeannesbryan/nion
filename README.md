# NiOn — Minimal Onion

NiOn is a minimal Linux browser built with C, GTK 4, and WebKitGTK 6. It opens both clearnet and Tor v3 `.onion` sites through its own bundled Tor runtime and is designed to fail closed rather than silently fall back to a direct connection.

**Stable release: 1.7.0**  
**Project focus after 1.7.0: maintenance, compatibility, privacy/security fixes, and bug fixes.**  
**Platform: GNU/Linux x86_64 AppImage**

> NiOn is not Tor Browser. It does not claim Tor Browser-grade anonymity, anti-fingerprinting, or browser-hardening guarantees.

## What NiOn does

- Routes clearnet and `.onion` browsing through bundled Tor.
- Blocks new web navigation when Tor is unavailable or the Tor child process fails.
- Supports multiple tabs, session restore, crash-safe recovery choices, pinned tabs, closed-tab recovery, bookmarks, downloads, per-site zoom, page find, printing/PDF, site information, temporary site permissions, per-site JavaScript control, lightweight native content blocking, WebKit tracking prevention, autoplay protection, selectable Security Levels, external-protocol confirmation, and popup/window escape hardening.
- Keeps normal cookies and website data persistent so ordinary login sessions can survive restarts.
- Provides a separate Private Window backed by an ephemeral WebKit network session.
- Detects Onion-Location advertisements and can open the advertised onion service in a new tab.
- Uses WebKitGTK's download pipeline, so web downloads stay on the same Tor-proxied browser network path.

## Main features

### Tabs

- Multi-tab browsing.
- Reopen the last 10 closed tabs with `Ctrl+Shift+T`.
- Tab context menu: Reload, Duplicate, Pin/Unpin, Mute/Unmute, Close, Close Other Tabs, and Close Tabs to the Right.
- Pinned tabs stay in the left tab block, show a `📌` marker, and are restored with the normal session.
- Pinned tabs are protected from bulk-close actions, while explicit Close Tab / `Ctrl+W` still works.
- Links using `target="_blank"`, `window.open()`, and context-menu new-tab actions open inside NiOn.
- The internal Home/New Tab page preserves the page you came from, so Back returns to the previous website instead of losing navigation context.
- Web process crash recovery keeps the NiOn window alive when a tab's WebKit process terminates; the affected tab shows a local recovery page with **Reload Tab** instead of taking down the whole browser.

### Private Window

Open a Private Window with `Ctrl+Shift+P`.

Private Window behavior:

- uses a dedicated ephemeral WebKit network session;
- does not persist cookies, site storage, WebKit credentials, session restore, pinned state, per-site zoom, or download history;
- keeps private closed-tab recovery only while that Private Window is alive;
- confirms individual private-tab closes to reduce accidental loss;
- uses the same bundled Tor runtime and the same fail-closed policy as normal browsing;
- keeps bookmarks and the selected search engine global by design.

Private downloads are memory-only in NiOn. Active private downloads are cancelled when the Private Window closes and tracked partial files are cleaned. A file that already finished downloading remains on disk because it was explicitly exported by the user.

### Bookmarks

- Bookmark the current page with `Ctrl+D` or the `☆ / ★` toolbar button.
- Local flat-file storage; no account or cloud sync.
- Open, Rename, and Delete actions.
- Live search by title and URL.
- Search is local/in-memory and does not create a separate index.

### Downloads

Open Downloads with `Ctrl+J`.

Normal download history is persistent and supports:

- progress and cancellation;
- Open File;
- Open Containing Folder;
- Copy Download Link;
- Retry Failed Download.

Cancelled downloads are not treated as failed retries. Retry uses the active NiOn WebView and remains blocked while Tor is unavailable.

### Page actions

- Better web-page context menu for links/images while retaining useful WebKit actions.
- Print / Save as PDF with `Ctrl+P`.
- Find in Page with `Ctrl+F`.
- Hard Reload with `Ctrl+Shift+R`.
- Per-site zoom memory with `Ctrl++`, `Ctrl+-`, and `Ctrl+0`.
- HTTP and HTTPS on the same hostname share zoom; non-default ports remain distinct; `.onion` hosts are supported.
- `Ctrl+0` returns to 100% and removes the current site's stored zoom override.

### Site and connection information

The Site Information window reports:

- current host;
- connection type;
- Tor route state;
- mixed-content state;
- full address;
- tracking-protection mode;
- autoplay policy for the current site.

HTTPS certificate errors remain strict-fail. For `.onion` services NiOn distinguishes onion-service transport from ordinary HTTPS rather than labeling every onion URL as HTTPS.

Site controls are also available here:

- JavaScript can be enabled/disabled per site. Standard/Safer default to enabled; Safest defaults to disabled and supports explicit per-site enable exceptions. Normal-window rules persist locally; Private Window rules are memory-only.
- Camera, microphone, geolocation, and notifications are blocked by default and can be **Allowed temporarily** for the current origin.
- Temporary permission grants last only for the current NiOn window and can be reset from Site Information.
- Screen/display capture and permission classes outside this scoped UI remain blocked.

### Security Levels

NiOn 1.7.0 introduces a global Security Level in **Preferences**. The levels only tighten NiOn's existing hardening; selecting Standard never re-enables WebRTC, WebGL, WebAudio, JavaScript clipboard access, or automatic JavaScript popups.

- **Standard** — the NiOn compatibility baseline: JavaScript enabled by default, camera/microphone MediaStream remains behind the temporary permission gate, page fullscreen is available, audible autoplay is blocked while muted autoplay is allowed.
- **Safer** — keeps JavaScript and permission-gated MediaStream available, disables page-controlled fullscreen, and blocks all autoplay unless the current site has an explicit autoplay exception.
- **Safest** — additionally disables JavaScript and MediaStream by default. JavaScript can still be explicitly enabled for an individual site from Site Information.

Changing Security Level reloads open website tabs and revokes temporary permission grants. Private Windows inherit the global level but keep their per-site JavaScript/autoplay exceptions memory-only. Site Information displays the effective level for the current window.

### Escape guards

NiOn keeps ordinary `http://` and `https://` navigation inside the Tor-routed browser. Stage 2 adds an explicit boundary for actions that would leave NiOn:

- `file:`, `javascript:`, `data:`, `blob:`, `about:`, and NiOn internal `nion:` targets are never handed to desktop applications.
- Other external schemes such as `mailto:`, `tel:`, `magnet:`, `steam:`, or custom application schemes require a **direct user gesture** and a confirmation dialog.
- The confirmation warns that the external application is outside NiOn and may access the network without Tor.
- External-protocol requests without a user gesture are blocked silently at the policy layer.
- New-window actions also require a WebKit user gesture. User-clicked `target="_blank"` links continue to open as NiOn tabs, while script-driven popup/window escape attempts are rejected.
- WebKit's `javascript-can-open-windows-automatically` setting remains disabled at every Security Level; the Stage 2 policy gate is defense in depth rather than a relaxation of that baseline.

External applications are intentionally outside NiOn's Tor-only guarantee. Confirming an external protocol is an explicit choice to cross that boundary.

### Lightweight content blocking

NiOn uses a small bundled third-party ad/tracker ruleset compiled by WebKit's native content-filter engine. It is intentionally lightweight: there is no extension engine, background list updater, remote filter download, or attempt to reproduce uBlock Origin.

- Blocking is enabled by default for normal web pages.
- The rules target a bounded set of common third-party advertising/tracking domains and do not block top-level documents.
- **Site Information → Content blocking** can disable blocking for the current site when compatibility requires it.
- Normal-window exceptions persist locally in `~/.config/nion/content-blocking.ini`.
- Private Window exceptions are memory-only and disappear when that Private Window closes.
- **Browsing Data…** can clear saved normal exceptions or current private exceptions.

Content blocking reduces some unwanted third-party requests; it is not an anonymity, malware-blocking, or complete anti-tracking guarantee.

### Tracking and media protection

NiOn enables **WebKit Intelligent Tracking Prevention (ITP)** by default on normal and Private network sessions. The older Preferences option for blanket third-party-cookie blocking is retained as a stricter alternative; enabling it disables ITP because upstream WebKit supersedes `ACCEPT_NO_THIRD_PARTY` while ITP is active.

Autoplay protection uses WebKit website policies:

- Standard blocks audible autoplay while allowing muted autoplay;
- Safer/Safest block all autoplay by default;
- **Site Information → Allow autoplay for this site** adds an exception for the current site;
- normal exceptions persist in `~/.config/nion/autoplay.ini`;
- Private Window exceptions are memory-only;
- **Browsing Data…** can clear normal/private autoplay exceptions.

The toolbar Site Information control opens a compact, scrollable transient window instead of a `GtkPopover`. This avoids the WebKit/GTK overlay-compositing path that caused flicker on the tested lightweight X11 desktop, while keeping all controls reachable on smaller displays.

### Onion-Location

When an HTTPS clearnet page advertises a valid Tor v3 Onion-Location, NiOn shows an **Onion** badge. Selecting it opens the onion address inside NiOn.

### Audio

- Per-tab audio activity indicator.
- Mute/unmute from the tab controls/context menu.
- Mute state participates in normal session restore.

### Recovery and local data controls

If NiOn detects that the previous normal session did not shut down cleanly, it does **not** restore that session automatically. A recovery dialog offers **Restore Tabs** or **Start Fresh**. Unclean-session recovery uses saved URLs/tab metadata and intentionally skips opaque WebKit session-state blobs before the user chooses to restore, reducing the chance of a startup crash loop. Invalid or oversized session files continue to be quarantined and NiOn starts fresh.

Data controls:

- **Clear Data for This Site…** removes matching WebKit website data for the active site.
- **Forget This Site…** is the stronger per-site reset: it removes matching WebKit data plus saved zoom, JavaScript override, content-blocking exception, autoplay exception, temporary permissions, and the tab's temporary HTTP allowance. Bookmarks, download history, and downloaded files are intentionally kept.
- **Browsing Data…** opens a selective manager for cookies/site storage, Web cache, saved zoom levels, per-site JavaScript rules, content-blocking exceptions, autoplay exceptions, and temporary site permissions.
- Website data + cache are selected by default to preserve the old global-clear behavior; zoom and site-control rules require explicit selection.
- Private website data is ephemeral and is not mixed into the normal persistent profile; the same manager can clear the current Private Window's in-memory state early.

## Privacy model

NiOn is intentionally Tor-only for web traffic. Important safeguards include:

- custom SOCKS proxy routing to the bundled Tor process;
- no intentional direct-network fallback;
- lightweight third-party content filtering through WebKit's native content-filter engine;
- WebKit Intelligent Tracking Prevention by default;
- global Standard / Safer / Safest Security Levels that only tighten the existing hardening baseline;
- Security-Level-aware autoplay policy, with explicit per-site exceptions;
- dead loopback SOCKS replacement after Tor failure;
- navigation blocking while Tor is offline;
- WebRTC peer connections disabled;
- DNS prefetching disabled;
- camera, microphone, geolocation, and notifications blocked by default with explicit temporary per-origin grants;
- WebGL and WebAudio disabled;
- Tor internal-address rejection;
- cancellation of relevant WebKit activity/downloads when Tor fails.

The Private Window additionally verifies at runtime that its WebKit network session is ephemeral and that persistent credential storage is disabled.

See [PRIVACY.md](PRIVACY.md) for the threat model and limitations.

## Release dependency baseline

NiOn keeps compatibility floors separate from the stable toolchain baseline used for 1.7.0 release validation:

- minimum GTK: 4.10; stable baseline: GTK 4.22.4;
- minimum WebKitGTK: 2.40; stable baseline: WebKitGTK 2.52.5;
- stable GLib baseline: 2.88.2.

The AppImage records the actual GTK/WebKitGTK/GLib versions present on the build host in its bundled `BUILD-INFO`; these baseline values do not artificially raise NiOn's minimum API requirements.

## Strengths

- Small native GTK application rather than a full general-purpose browser suite.
- Bundled and signature-verified Tor runtime; no system Tor is required for normal use.
- Fail-closed behavior is treated as a core invariant.
- Normal persistent browsing and ephemeral private browsing are intentionally separated.
- Version, Tor pin, AppImage name, About metadata, and release metadata are controlled by the centralized `release/manifest/` files.
- AppImage packaging keeps the user profile outside the executable so replacing the AppImage does not intentionally wipe browser data.

## Limitations

- NiOn is **not Tor Browser** and should not be treated as providing the same anti-fingerprinting/anonymity protections.
- Current production AppImage target is x86_64 Linux.
- Website compatibility can be reduced by privacy hardening such as disabled WebRTC peer connections, WebGL, WebAudio, content blocking, or the Safer/Safest Security Levels; temporarily allowing camera, microphone, geolocation, or notifications can expose sensitive information to that site.
- The bundled content-blocking list is deliberately small and can miss trackers or occasionally break third-party site features; use the per-site switch when needed.
- A completed download, saved/printed PDF, bookmark, or text copied to the system clipboard can outlive a Private Window because it was explicitly exported outside the ephemeral session.
- Normal browsing intentionally persists cookies/site data and therefore leaves local state unless the user clears it.
- AppImage portability still depends on host-core components such as the kernel, glibc compatibility, graphics stack, and desktop integration.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+T` | New tab |
| `Ctrl+Shift+P` | New Private Window |
| `Ctrl+W` | Close current tab |
| `Ctrl+Shift+T` | Reopen closed tab |
| `Ctrl+L` / `F6` | Focus address bar |
| `Ctrl+R` / `F5` | Reload |
| `Ctrl+Shift+R` | Reload without cache |
| `Ctrl+F` | Find in page |
| `Ctrl++` / `Ctrl+=` | Zoom in |
| `Ctrl+-` | Zoom out |
| `Ctrl+0` | Reset zoom and forget site override |
| `Ctrl+P` | Print / Save as PDF |
| `Ctrl+Tab` / `Ctrl+PageDown` | Next tab |
| `Ctrl+Shift+Tab` / `Ctrl+PageUp` | Previous tab |
| `Ctrl+J` | Downloads / Private Downloads |
| `Ctrl+D` | Bookmark current page |
| `Alt+Left` | Back |
| `Alt+Right` | Forward |
| `F11` | Fullscreen |

## Persistent profile

Normal NiOn data lives outside the AppImage:

```text
~/.local/share/nion/   cookies, session, downloads, bookmarks, Tor state, WebKit data
~/.config/nion/        preferences, per-site zoom, JavaScript rules, content-blocking exceptions
~/.cache/nion/         WebKit cache and compiled content-filter cache
```

Replacing the AppImage is designed to reuse these paths. Malformed NiOn-owned metadata files are bounded and may be quarantined rather than silently overwritten.

Private Window data does not use these normal persistence paths for private website/session/download state.

## 1.7.0 development plan

- **Stage 1 — Security Levels ✅**: Standard / Safer / Safest, persistent global preference, Site Information visibility, per-site JavaScript integration, Private Window inheritance.
- **Stage 2 — Escape Guards ✅**: direct-user-gesture external-protocol confirmation plus popup/new-window policy hardening.
- **Stage 3 — Hardening & Stable**: compatibility/privacy regression audit and final 1.7.0 release.

## Run the AppImage

```bash
chmod +x NiOn-1.7.0-x86_64.AppImage
./NiOn-1.7.0-x86_64.AppImage
```

If FUSE is unavailable:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.7.0-x86_64.AppImage
```

## Build from source

On a Debian/Ubuntu-family x86_64 system:

```bash
./scripts/install-deps-debian.sh
rm -rf build
./scripts/run-dev.sh
```

Build the production AppImage with:

```bash
./scripts/build-appimage.sh
./scripts/release-preflight.sh
```

Expected output:

```text
dist/NiOn-1.7.0-x86_64.AppImage
dist/NiOn-1.7.0-x86_64.AppImage.sha256
```

See [BUILDING.md](BUILDING.md) for the complete build/release procedure and [TESTING.md](TESTING.md) for runtime validation.

## Documentation

- [BUILDING.md](BUILDING.md) — source build, AppImage build, manifest, and manual release flow.
- [TESTING.md](TESTING.md) — regression, runtime, Tor, private-session, and AppImage checks.
- [PRIVACY.md](PRIVACY.md) — privacy model, persistence boundaries, and known limitations.
- [SECURITY.md](SECURITY.md) — security-reporting scope.
- [CHANGELOG.md](CHANGELOG.md) — release history.
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) — bundled/runtime component notices.

## License

NiOn source code is licensed under **GPL-3.0-or-later**. Bundled third-party components retain their own upstream licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
