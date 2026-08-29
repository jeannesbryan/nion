# NiOn — Minimal Onion

NiOn is a minimal Linux browser built with C, GTK 4, and WebKitGTK 6. It opens both clearnet and Tor v3 `.onion` sites through its own bundled Tor runtime and is designed to fail closed rather than silently fall back to a direct connection.

**Stable Release:** 1.8.0  
**Project Focus:** Maintenance, compatibility, memory safety, privacy/security fixes, and bug fixes.  
**Platform:** GNU/Linux x86_64 AppImage

> **Disclaimer:** NiOn is not Tor Browser. It does not claim Tor Browser-grade anonymity, anti-fingerprinting, or browser-hardening guarantees.

---

## 🛡️ Core Philosophy & Privacy Model

NiOn is intentionally Tor-only for web traffic. Important safeguards include:
* **Strict Routing:** Custom SOCKS proxy routing to the bundled Tor process with no direct-network fallback.
* **Fail-Closed Design:** Navigation is blocked and relevant WebKit downloads are canceled when Tor is offline or fails.
* **Feature Hardening:** WebRTC peer connections, DNS prefetching, WebGL, and WebAudio are entirely disabled.
* **Permission Gates:** Camera, microphone, geolocation, and notifications are blocked by default and require explicit temporary per-origin grants.
* **Data Separation:** The Private Window verifies at runtime that its WebKit network session is ephemeral and persistent storage is disabled.

See [PRIVACY.md](PRIVACY.md) for the complete threat model and limitations.

---

## ✨ Main Features

### Browsing & Tabs
* Routes clearnet and `.onion` browsing through bundled Tor.
* Multi-tab browsing with crash-safe recovery. If a tab's WebKit process terminates, a local recovery page appears instead of crashing the entire browser.
* Reopen the last 10 closed tabs (`Ctrl+Shift+T`).
* Pinned tabs (`📌`) are protected from bulk-close actions and restored with the normal session.

### Ephemeral Private Windows (`Ctrl+Shift+P`)
* Uses a dedicated ephemeral WebKit network session.
* Does not persist cookies, site storage, WebKit credentials, or download history.
* Private downloads are memory-only; active downloads are canceled when the window closes.

### Security Levels
NiOn 1.7.0 introduced a global Security Level in **Preferences**.
* **Standard:** The compatibility baseline. JavaScript enabled, audible autoplay blocked.
* **Safer:** Disables page-controlled fullscreen and blocks all autoplay.
* **Safest:** Disables JavaScript and MediaStream by default (can be explicitly enabled per site).

### Escape Guards
* Navigation to targets like `file:`, `javascript:`, or `data:` is never handed to external desktop applications.
* External protocols (`mailto:`, `magnet:`, etc.) require a **direct user gesture** and explicit confirmation, warning the user that the external app may bypass Tor.

### Built-in Protections
* **Tracking Prevention:** WebKit Intelligent Tracking Prevention (ITP) is enabled by default.
* **Content Blocking:** Uses a lightweight, bundled ad/tracker ruleset compiled by WebKit's native content-filter engine. It can be disabled per site for compatibility.
* **Onion-Location:** Detects Onion-Location advertisements on clearnet HTTPS pages and offers to route to the `.onion` service.

---

## 💾 Profile & Persistence

Normal NiOn data lives outside the AppImage:
* `~/.local/share/nion/` — Cookies, session, downloads, bookmarks, Tor state.
* `~/.config/nion/` — Preferences, per-site zoom, JavaScript rules, exceptions.
* `~/.cache/nion/` — WebKit cache and compiled content-filter cache.

---

## 🚀 Getting Started

### Run the AppImage
```bash
chmod +x NiOn-1.8.0-x86_64.AppImage
./NiOn-1.8.0-x86_64.AppImage
```
*If FUSE is unavailable on your system:*
```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.8.0-x86_64.AppImage
```

### Build from Source
On a Debian/Ubuntu-family x86_64 system:
```bash
./scripts/install-deps-debian.sh
rm -rf build
./scripts/run-dev.sh
```

**Build the production AppImage:**
```bash
./scripts/build-appimage.sh
./scripts/release-preflight.sh
```
Expected output:
```text
dist/NiOn-1.8.0-x86_64.AppImage
dist/NiOn-1.8.0-x86_64.AppImage.sha256
```
See [BUILDING.md](BUILDING.md) for the complete build/release procedure.

---

## 🛠️ 1.8.0 Release Highlights
* **Memory Safety:** Eliminated WebKit context memory leaks and potential segmentation faults during application teardown.
* **Tor Process Stability:** Refactored Tor subprocess lifecycle management to ensure clean `SIGTERM` signals and memory unreferencing, preventing GLib dangling pointers.
* **Code Refactoring:** Streamlined `nion_free_private_app_idle` routines to strictly follow GObject memory management best practices.

---

## 📚 Documentation
* [BUILDING.md](BUILDING.md) — Source build, AppImage build, and release flow.
* [TESTING.md](TESTING.md) — Runtime, Tor, private-session, and AppImage checks.
* [PRIVACY.md](PRIVACY.md) — Privacy model, persistence boundaries, and limitations.
* [SECURITY.md](SECURITY.md) — Security-reporting scope.
* [CHANGELOG.md](CHANGELOG.md) — Release history.
* [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) — Bundled/runtime component notices.

## 📜 License
NiOn source code is licensed under **GPL-3.0-or-later**. Bundled third-party components retain their own upstream licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).