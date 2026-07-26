# NiOn — Minimal Onion

NiOn is a minimal Linux browser built with C, GTK 4, and WebKitGTK 6. It opens both clearnet and Tor v3 `.onion` sites through its own bundled Tor runtime and is designed to fail closed rather than silently fall back to a direct connection.

**Stable release: 1.5.0**  
**Platform: GNU/Linux x86_64 AppImage**
**Project focus after 1.5.0: maintenance and bug fixes.**

> NiOn is not Tor Browser. It does not claim Tor Browser-grade anonymity, anti-fingerprinting, or browser-hardening guarantees.

## What NiOn does

- Routes clearnet and `.onion` browsing through bundled Tor.
- Blocks new web navigation when Tor is unavailable or the Tor child process fails.
- Supports multiple tabs, session restore, crash-safe recovery choices, pinned tabs, closed-tab recovery, bookmarks, downloads, per-site zoom, page find, printing/PDF, site information, temporary site permissions, per-site JavaScript control, lightweight native content blocking, WebKit tracking prevention, and autoplay protection.
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

- JavaScript can be enabled/disabled per site. Normal-window rules persist locally; Private Window rules are memory-only.
- Camera, microphone, geolocation, and notifications are blocked by default and can be **Allowed temporarily** for the current origin.
- Temporary permission grants last only for the current NiOn window and can be reset from Site Information.
- Screen/display capture and permission classes outside this scoped UI remain blocked.

### Lightweight content blocking

NiOn 1.5.0 Stage 1 adds a small bundled third-party ad/tracker ruleset compiled by WebKit's native content-filter engine. It is intentionally lightweight: there is no extension engine, background list updater, remote filter download, or attempt to reproduce uBlock Origin.

- Blocking is enabled by default for normal web pages.
- The rules target a bounded set of common third-party advertising/tracking domains and do not block top-level documents.
- **Site Information → Content blocking** can disable blocking for the current site when compatibility requires it.
- Normal-window exceptions persist locally in `~/.config/nion/content-blocking.ini`.
- Private Window exceptions are memory-only and disappear when that Private Window closes.
- **Browsing Data…** can clear saved normal exceptions or current private exceptions.

Content blocking reduces some unwanted third-party requests; it is not an anonymity, malware-blocking, or complete anti-tracking guarantee.

### Tracking and media protection

NiOn 1.5.0 Stage 2 enables **WebKit Intelligent Tracking Prevention (ITP)** by default on normal and Private network sessions. The older Preferences option for blanket third-party-cookie blocking is retained as a stricter alternative; enabling it disables ITP because upstream WebKit supersedes `ACCEPT_NO_THIRD_PARTY` while ITP is active.

Autoplay protection uses WebKit website policies:

- audible autoplay is blocked by default;
- muted autoplay remains allowed for compatibility;
- **Site Information → Allow autoplay with sound** adds an exception for the current site;
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
- **Browsing Data…** opens a selective manager for cookies/site storage, Web cache, saved zoom levels, per-site JavaScript rules, content-blocking exceptions, autoplay exceptions, and temporary site permissions.
- Website data + cache are selected by default to preserve the old global-clear behavior; zoom and site-control rules require explicit selection.
- Private website data is ephemeral and is not mixed into the normal persistent profile; the same manager can clear the current Private Window's in-memory state early.

## Privacy model

NiOn is intentionally Tor-only for web traffic. Important safeguards include:

- custom SOCKS proxy routing to the bundled Tor process;
- no intentional direct-network fallback;
- lightweight third-party content filtering through WebKit's native content-filter engine;
- WebKit Intelligent Tracking Prevention by default;
- audible autoplay blocked by default while muted autoplay remains allowed;
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
- Website compatibility can be reduced by privacy hardening such as disabled WebRTC peer connections, WebGL, WebAudio, or content blocking; temporarily allowing camera, microphone, geolocation, or notifications can expose sensitive information to that site.
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

## Run the AppImage

```bash
chmod +x NiOn-1.5.0-x86_64.AppImage
./NiOn-1.5.0-x86_64.AppImage
```

If FUSE is unavailable:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.5.0-x86_64.AppImage
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
dist/NiOn-1.5.0-x86_64.AppImage
dist/NiOn-1.5.0-x86_64.AppImage.sha256
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
