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
  site-javascript.ini
  content-blocking.ini

~/.cache/nion/
  WebKit cache...
  content-filters/ (compiled bundled filter cache)
```

This means normal browsing can leave local evidence such as cookies, site data, bookmarks, tab/session URLs, download history, per-site zoom keys, per-site JavaScript rules, and cached browser data.

Use **Clear Data for This Site…** or **Browsing Data…** when that persistence is not desired.

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

- WebRTC peer connections disabled;
- DNS prefetching disabled;
- hyperlink auditing disabled;
- camera and microphone blocked by default, with explicit per-origin temporary grants;
- geolocation blocked by default, with an explicit per-origin temporary grant and a warning that physical location can defeat network anonymity;
- notifications blocked by default, with an explicit per-origin temporary grant and a warning that the desktop environment may retain notification artifacts;
- screen/display capture and other unscoped permission classes remain denied;
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

## Lightweight content blocking

NiOn 1.5.0 uses WebKit's native declarative content-filter engine with a small ruleset bundled inside the application resources. The rules currently target common third-party advertising/tracking endpoints and intentionally avoid top-level-document blocking.

There is no runtime remote-list download or background filter updater. Updating the bundled ruleset is part of updating NiOn itself. Normal per-site disable exceptions are stored in bounded, atomic `~/.config/nion/content-blocking.ini`; this file can reveal hostnames where the user disabled filtering. Private Window therefore keeps its exceptions only in memory and never reads the normal exception file.

WebKit may cache the compiled representation under `~/.cache/nion/content-filters/`. That cache is derived from NiOn's bundled ruleset, not a record of which pages were visited.

Content blocking is defense-in-depth only. It does not guarantee that all trackers, advertisements, malware, fingerprinting, first-party tracking, or newly created domains are blocked, and a blocked third-party resource can break a website feature. Disabling blocking for a site should therefore be treated as a compatibility exception, not as a change to Tor routing.

## Intelligent Tracking Prevention and autoplay

NiOn 1.5.0 enables WebKit Intelligent Tracking Prevention (ITP) by default. ITP is an engine-level tracking defense and is separate from NiOn's bundled content-filter rules. The legacy strict third-party-cookie option is kept as an alternative: when it is enabled NiOn disables ITP and uses blanket `ACCEPT_NO_THIRD_PARTY`, because WebKit documents that ITP otherwise supersedes that cookie policy.

Audible autoplay is blocked by default using WebKit website policies while muted autoplay is allowed for compatibility. Normal per-site **Allow autoplay with sound** exceptions are stored in bounded atomic `~/.config/nion/autoplay.ini`, which can reveal hostnames where the user created an exception. Private Window keeps autoplay exceptions only in memory and never reads the normal file.

Neither ITP nor autoplay protection changes Tor routing. They are defense-in-depth and compatibility controls, not anonymity guarantees.

## Per-site JavaScript

Normal browsing stores only explicit **disabled** JavaScript site rules in `~/.config/nion/site-javascript.ini`. The store is bounded, written atomically, and can reveal hostnames for which JavaScript was disabled.

Private Window does not read the normal JavaScript-rule file. Private JavaScript changes are held only in memory for that Private Window. JavaScript changes reload the current page so the selected rule applies from the start of the next document load.

## Temporary site permissions

Camera, microphone, geolocation, and notification requests are blocked by default. NiOn can grant a supported request only after an explicit **Allow temporarily** decision. The grant is scoped to the requesting origin and current NiOn window and is not written to the profile.

WebRTC peer connections remain disabled even when camera or microphone capture is temporarily allowed. Resetting temporary permissions from Site Information removes the current origin's grants and stops camera/microphone capture in matching tabs. A site that receives geolocation or media access can learn sensitive information despite Tor routing, and desktop notifications may leave artifacts outside NiOn.

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


## Crash recovery boundary

Normal-window session snapshots contain URLs and bounded WebKit session state. After an unclean shutdown NiOn requires an explicit **Restore Tabs** or **Start Fresh** choice and skips opaque WebKit session-state blobs during the initial recovery setup. This is a reliability safeguard against automatic restore loops; it is not an anonymity feature. Private Window state is never included in normal crash recovery.

## Browsing Data Manager

The normal profile can selectively clear WebKit website data/cache, saved per-site zoom, persistent per-site JavaScript rules, content-blocking exceptions, autoplay exceptions, and temporary permission grants. Temporary permission clearing also stops active camera/microphone capture. In Private Window, site JavaScript rules and permission grants are memory-only; clearing them does not write them into the normal profile.

## 1.4.0 final audit note

The 1.4.0 finalization rechecks the interaction between temporary site permissions, per-site JavaScript rules, crash recovery, selective data clearing, normal/private persistence boundaries, and Tor fail-closed behavior. The audit is a regression safeguard for this implementation; it does not upgrade NiOn's threat-model claim to Tor Browser equivalence.
