# NiOn

**NiOn — Minimal Onion** is a minimal Linux web browser for clearnet and `.onion` sites. It uses GTK 4 and WebKitGTK 6 for the browser UI/engine and routes browsing traffic through its own bundled Tor runtime.

> NiOn is not Tor Browser. It does not claim Tor Browser-grade anti-fingerprinting, anonymity, or browser-hardening guarantees.

## Why NiOn?

NiOn is intentionally small. The goal is not to reproduce a full desktop browser, but to provide a practical multi-tab browser where Tor routing is the default and direct-network fallback is intentionally avoided.

**Open websites. Open onions. Everything through Tor.**

## Features

- Clearnet and Tor v3 `.onion` browsing through Tor.
- Bundled, signature-verified Tor Expert Bundle runtime.
- Multi-tab GTK 4 interface with WebKitGTK 6.
- Persistent cookies and website data so login sessions can survive restarts.
- Session restore, including tab URLs and WebKit back/forward state.
- Fail-closed behavior when Tor is unavailable or crashes.
- Onion-Location detection with an **Onion** badge that opens the advertised onion URL in a new tab.
- Downloads through the Tor-proxied WebKit network session, with `Ctrl+J` history.
- Find in Page, page zoom, hard reload, fullscreen, Home/New Tab, and configurable search engine.
- Simple local bookmarks with `Ctrl+D`, Open/Rename/Delete management, and a live bookmark button beside the address bar.
- Per-tab audio indicator and one-click mute/unmute for pages playing sound.
- Internal error pages and Tor-aware status information.
- Persistent browser profile outside the AppImage, so replacing the AppImage does not intentionally erase logins or tabs.
- Linux x86_64 AppImage packaging pipeline with bundled WebKit subprocesses and Tor runtime.

## Screens at a glance

```text
┌──────────────────────────────────────────────────────────┐
│ ←  →  ↻  ⌂ │ URL / Search                 │ ☆ │ ≡   │
├──────────────────────────────────────────────────────────┤
│ Tab 1 × │ Tab 2 × │ +                                  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                        WEB PAGE                          │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ ● TOR CONNECTED                                         │
└──────────────────────────────────────────────────────────┘
```

## AppImage

NiOn 1.1.0 targets **GNU/Linux x86_64** for the production AppImage pipeline.

After building:

```bash
chmod +x dist/NiOn-1.1.0-x86_64.AppImage
./dist/NiOn-1.1.0-x86_64.AppImage
```

The AppImage contains NiOn, the Tor runtime, WebKitGTK subprocess executables, and practical user-space runtime dependencies. Host graphics drivers, glibc, and other host-core components are intentionally not replaced.

See [APPIMAGE.md](APPIMAGE.md) for packaging details.

## Build from source

On Debian/Ubuntu-family systems:

```bash
./scripts/install-deps-debian.sh
rm -rf build
./scripts/run-dev.sh
```

`run-dev.sh` prepares the verified bundled Tor runtime when it is not already present.

For a release build without immediately launching NiOn:

```bash
meson setup build --buildtype=release
meson compile -C build
```

Full build documentation is in [BUILDING.md](BUILDING.md).

## Build the AppImage

```bash
./scripts/install-deps-debian.sh
./scripts/build-appimage.sh
./scripts/test-appimage.sh
```

Expected artifacts:

```text
dist/NiOn-1.1.0-x86_64.AppImage
dist/NiOn-1.1.0-x86_64.AppImage.sha256
```

Run the stable release checks with:

```bash
./scripts/release-preflight.sh
```

The runtime scenarios are documented in [TESTING.md](TESTING.md).

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+L` / `F6` | Focus address bar |
| `Ctrl+F` | Find in page |
| `Ctrl+R` / `F5` | Reload |
| `Ctrl+Shift+R` | Reload without cache |
| `Ctrl++` / `Ctrl+=` | Zoom in |
| `Ctrl+-` | Zoom out |
| `Ctrl+0` | Reset zoom |
| `F11` | Fullscreen |
| `Alt+Left` | Back |
| `Alt+Right` | Forward |
| `Ctrl+Tab` / `Ctrl+PageDown` | Next tab |
| `Ctrl+Shift+Tab` / `Ctrl+PageUp` | Previous tab |
| `Ctrl+J` | Downloads |
| `Ctrl+D` | Bookmark current page |

## Persistent profile

NiOn keeps its profile outside the executable/AppImage:

```text
~/.local/share/nion/
├── cookies.sqlite
├── session.ini
├── downloads.ini
├── bookmarks.ini
├── tor-runtime.ini
├── tor/
└── WebKit website data...

~/.cache/nion/
└── WebKit cache...

~/.config/nion/
└── preferences.ini
```

This layout is version-independent. Replacing `NiOn-1.1.0-x86_64.AppImage` with a later NiOn AppImage is designed to reuse the same profile. See [UPGRADING.md](UPGRADING.md).

Malformed NiOn-owned profile files are quarantined rather than silently overwritten when possible. An obviously invalid cookie database is also quarantined before WebKit opens it.

## Tor runtime

NiOn pins a Tor Expert Bundle release and verifies its detached signature before preparing the development/package runtime. A system-installed Tor daemon is not required for normal NiOn operation.

The Tor data directory is isolated from the system Tor configuration and NiOn dynamically selects an available local SOCKS port in its reserved range.

See [TOR_RUNTIME.md](TOR_RUNTIME.md).

## Privacy and security

NiOn includes practical hardening such as:

- WebRTC disabled;
- DNS prefetching disabled;
- camera, microphone, geolocation, and notification permission requests denied;
- WebGL and WebAudio disabled;
- local/private network navigation restrictions;
- custom SOCKS proxy routing with no intentional direct fallback;
- dead-loopback proxy replacement after Tor failure;
- Tor-side internal-address rejection;
- WebKit activity/download cancellation when the Tor child fails.

For the threat model, limitations, and network test procedure, read [PRIVACY.md](PRIVACY.md).

Run a live socket audit while loading clearnet and `.onion` pages:

```bash
./scripts/audit-network.sh 30
```

A passing sample is useful evidence, not a mathematical proof that every possible browser/network leak has been eliminated.

## Onion-Location

When an HTTPS clearnet page advertises a valid Tor v3 onion URL using Onion-Location, NiOn shows an **Onion** badge. Clicking it opens the advertised `.onion` address inside NiOn in a new tab.


## Bookmarks

NiOn keeps bookmarks intentionally simple and local. Press `Ctrl+D` or click the bookmark button beside the address bar.

The toolbar button reflects the current page automatically:

- `☆` means the current page is not bookmarked;
- `★` means its exact URL already exists in `bookmarks.ini`;
- clicking the button toggles the bookmark;
- New Tab, blank pages, and internal error pages keep the button disabled.

Open **Bookmarks** from the hamburger menu to open, rename, or delete saved entries.

Bookmarks are stored locally in `~/.local/share/nion/bookmarks.ini`. Duplicate URLs are not added twice, and bookmarks do not sync to any cloud service.

## Tab audio & mute

When a tab is playing audio, NiOn shows a compact speaker button in that tab. Click it to mute/unmute only that WebView. The icon follows WebKit's `is-muted` state, while visibility follows `is-playing-audio`. A muted state is saved with NiOn session restore so it does not leak into unrelated tabs.

## Clear Data for This Site

For the current HTTP(S) site, NiOn asks WebKit for stored website-data records attributed to that host/domain and removes only the matching records. This can include cookies, cache, local/session storage, IndexedDB, and other WebKit website data. The active page is reloaded without cache after a successful clear so sign-out/storage changes are immediately visible.

WebKit may group related subdomains under a parent domain, so the confirmation dialog identifies the current site before anything is removed. The existing **Clear Browsing Data…** command remains available for a full-profile website-data clear.

## Downloads

Downloads use the same Tor-proxied `WebKitNetworkSession` as browsing. NiOn provides:

- progress and cancellation;
- filename collision handling;
- failed/interrupted status;
- persistent download history;
- `Ctrl+J` Downloads window;
- Clear Downloads without deleting completed files.

## Search engines

The Preferences dialog supports:

- DuckDuckGo;
- Brave Search;
- Startpage.

The preference is stored in the NiOn configuration directory.

## Repository layout

```text
.
├── src/                 NiOn C source
├── data/                icon, desktop file, AppStream, GResource
├── packaging/           AppImage launcher and WebKit exec shim
├── runtime/             runtime documentation; downloaded Tor is ignored
├── scripts/             build, audit, test and packaging scripts
├── .github/workflows/   CI/release automation
├── BUILDING.md
├── PRIVACY.md
├── TOR_RUNTIME.md
├── APPIMAGE.md
├── UPGRADING.md
└── LICENSE
```

## License

NiOn source code is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE).

NiOn bundles or interfaces with third-party software under their respective licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
