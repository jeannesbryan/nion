# NiOn 1.0.0 — Stable

NiOn reaches its first stable release.

**NiOn (Minimal Onion)** is a minimal Linux browser for clearnet and `.onion` sites. It uses GTK 4 + WebKitGTK 6 and routes browsing traffic through its bundled Tor runtime.

## Highlights

- clearnet and Tor v3 `.onion` browsing through Tor;
- multi-tab GTK/WebKitGTK interface;
- bundled, signature-verified Tor Expert Bundle runtime;
- persistent cookies/login sessions and restorable tabs;
- Onion-Location detection;
- downloads with persistent `Ctrl+J` history;
- Find in Page, zoom, hard reload, fullscreen and search-engine preference;
- fail-closed networking when Tor is unavailable;
- production-oriented x86_64 AppImage packaging with WebKit subprocess handling;
- stable version-independent profile path for AppImage upgrades.

## Download

For most users, download:

```text
NiOn-1.0.0-x86_64.AppImage
NiOn-1.0.0-x86_64.AppImage.sha256
```

Verify:

```bash
sha256sum -c NiOn-1.0.0-x86_64.AppImage.sha256
chmod +x NiOn-1.0.0-x86_64.AppImage
./NiOn-1.0.0-x86_64.AppImage
```

## Important privacy note

NiOn is **not Tor Browser** and does not claim Tor Browser-grade anti-fingerprinting or anonymity guarantees. Its goal is a small Linux browser with Tor-only routing and practical leak-reduction hardening.

## Upgrade

The browser profile lives outside the AppImage under the usual XDG directories, so replacing the AppImage is designed to keep the existing NiOn profile. See `UPGRADING.md`.

## License

NiOn is released under GPL-3.0-or-later.
