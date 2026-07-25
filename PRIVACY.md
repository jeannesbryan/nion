# Privacy model

NiOn is a small Tor-routed browser for Linux. It is designed so normal web browsing uses NiOn's bundled Tor runtime and does not intentionally fall back to a direct connection.

NiOn is **not Tor Browser** and does not claim Tor Browser-grade anonymity, anti-fingerprinting, or browser-hardening guarantees.

## Network model

For HTTP(S) browsing NiOn configures WebKitGTK with a custom SOCKS proxy that points to the bundled Tor child process.

Important fail-closed behavior includes:

- navigation policy rejects new HTTP(S) loads while Tor is offline;
- WebKit is moved to a dead loopback SOCKS endpoint after Tor failure;
- active WebKit activity/downloads are stopped or cancelled where NiOn tracks them;
- Tor is configured to reject internal-address targets and internal DNS targets;
- NiOn does not intentionally switch to the host's direct network when Tor fails.

The Tor process uses its own data directory under the NiOn profile and an automatically selected local SOCKS endpoint.

## Normal browsing persistence

Normal NiOn intentionally persists browser state so it can behave like a practical daily browser.

Typical persistent locations:

```text
~/.local/share/nion/
  cookies.sqlite
  session.ini
  downloads.ini
  bookmarks.ini
  tor-runtime.ini
  tor/
  WebKit website data...

~/.config/nion/
  preferences.ini
  site-zoom.ini

~/.cache/nion/
  WebKit cache...
```

This means normal browsing can leave local evidence such as cookies, site data, bookmarks, tab/session URLs, download history, per-site zoom keys, and cached browser data.

Use **Clear Data for This Site…** or **Clear Browsing Data…** when that persistence is not desired.

## Private Window

Private Window uses a separate ephemeral `WebKitNetworkSession` and NiOn verifies at runtime that the session reports itself as ephemeral before allowing the window to open.

NiOn also verifies that persistent WebKit credential storage is disabled for the private session.

Private Window does not persist:

- cookies or normal WebKit website data;
- WebKit credentials;
- tab/session restore state;
- pinned-tab state;
- per-site zoom state;
- private download history;
- private recently-closed tabs after the Private Window closes.

Private contexts also have explicit guards preventing writes to the normal NiOn session, zoom, download-history, and preferences stores.

### Data intentionally shared with normal NiOn

The following remain global by design:

- bookmarks;
- selected search engine preference.

Adding a bookmark from a Private Window therefore creates persistent global data.

### Private downloads

Private downloads use the Private Window's Tor-proxied ephemeral WebKit session.

Within NiOn, their source URLs/status/history stay in memory only. Desktop completion notifications are suppressed. Closing a Private Window cancels active private downloads and removes tracked partial destinations where possible.

A download that already completed remains on disk because the user explicitly exported it outside the private session.

The same principle applies to:

- Save/Print to PDF;
- other files explicitly saved by the user;
- bookmarks explicitly created by the user;
- text copied to the operating-system clipboard.

NiOn cannot truthfully erase those external artifacts merely by closing Private Window.

## Browser hardening

NiOn currently applies practical restrictions including:

- WebRTC disabled;
- DNS prefetching disabled;
- hyperlink auditing disabled;
- camera permission denied;
- microphone permission denied;
- geolocation permission denied;
- notification permission denied;
- WebGL disabled;
- WebAudio disabled;
- local/private network navigation restrictions;
- strict TLS certificate-error policy;
- mixed-content detection on HTTPS pages.

These choices can reduce website compatibility.

## HTTPS and onion services

Clearnet input without an explicit scheme is treated HTTPS-first. Explicit plain-HTTP clearnet navigation requires a warning/confirmation because Tor protects the path to the exit relay but plain HTTP does not add TLS protection between the Tor exit and the destination website.

A Tor v3 `.onion` service is different from ordinary clearnet HTTP: onion-service transport is routed end-to-end inside Tor. NiOn therefore identifies onion-service connections separately in Site Information instead of pretending every onion page is HTTPS.

## Onion-Location

When a clearnet HTTPS page advertises a valid Tor v3 Onion-Location, NiOn can expose it through the toolbar. Opening the advertised onion address uses the normal NiOn/Tor navigation path.

## Bookmarks and bookmark search

Bookmarks are stored locally in `~/.local/share/nion/bookmarks.ini`.

Bookmark search is performed locally/in memory against stored titles and URLs. NiOn does not send the search query to a website or search engine merely to filter the bookmark list.

## Per-site zoom

Normal browsing stores bounded per-site zoom preferences in `~/.config/nion/site-zoom.ini`.

That file can reveal hostnames for which the user changed zoom. Private Window therefore does not read or write the per-site zoom store.

## Download source history

Normal persistent download history may retain the original bounded HTTP(S) source URI so **Copy Download Link** and **Retry Failed Download** can work after restart.

This is local browsing history. Private downloads deliberately do not write this information to the persistent normal history.

## Site-data clearing

**Clear Data for This Site…** asks WebKit for website-data records and removes records matching the active host/domain scope selected by NiOn. It is narrower than the global browsing-data clear operation but should not be treated as a forensic deletion guarantee for every operating-system or filesystem artifact.

## Profile-file resilience

NiOn bounds and validates its small INI-style profile files. Malformed or oversized NiOn-owned metadata may be quarantined with a `.corrupt-...` suffix rather than silently reused.

An obviously invalid cookie SQLite database may also be quarantined before WebKit opens it.

These safeguards improve recovery but do not make profile storage encrypted.

## Local storage is not encrypted by NiOn

NiOn does not provide its own encryption layer for the normal browser profile. Anyone who can read the user's filesystem may be able to inspect browser metadata or WebKit storage.

Use operating-system account security, disk encryption, and appropriate filesystem permissions when local confidentiality matters.

## Network audit

A live socket audit can be run while browsing clearnet and `.onion` pages:

```bash
./scripts/audit-network.sh 30
```

The output should be reviewed together with the fail-closed tests in `TESTING.md`.

A successful test is evidence for the tested build/environment; it is not a proof that every possible network, browser-engine, kernel, or side-channel leak has been eliminated.

## Threat-model boundary

NiOn aims to provide a minimal Tor-only browsing path with practical fail-closed safeguards. It does not attempt to reproduce Tor Browser's full fingerprinting defenses, security patches, browser configuration, circuit/isolation model, or anonymity research.

For high-risk anonymity use cases, NiOn should not be assumed equivalent to Tor Browser.
