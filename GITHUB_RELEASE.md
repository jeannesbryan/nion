# NiOn 1.1.0 — Stable

NiOn 1.1.0 keeps the browser intentionally small while improving everyday privacy and browsing controls.

## Highlights

- HTTPS-First behavior for clearnet navigation, with a warning before explicit plain-HTTP clearnet pages.
- Simple local bookmarks with `Ctrl+D`, a toolbar bookmark toggle, and Open/Rename/Delete management.
- **Clear Data for This Site…** to remove WebKit data for the current site without clearing unrelated sites.
- Per-tab audio indicator and one-click mute/unmute.
- Persistent profile remains compatible with NiOn 1.0.0.
- Bundled Tor, fail-closed routing, Onion-Location, downloads, session restore, AppImage packaging, and existing privacy hardening remain intact.

## Assets

```text
NiOn-1.1.0-x86_64.AppImage
NiOn-1.1.0-x86_64.AppImage.sha256
```

## Verify

```bash
sha256sum -c NiOn-1.1.0-x86_64.AppImage.sha256
chmod +x NiOn-1.1.0-x86_64.AppImage
./NiOn-1.1.0-x86_64.AppImage
```

NiOn is not Tor Browser and does not claim Tor Browser-grade anti-fingerprinting or anonymity guarantees.
