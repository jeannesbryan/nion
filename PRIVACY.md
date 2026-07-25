# NiOn Privacy Model

**NiOn — Minimal Onion** is a minimal Linux browser whose WebKit network session is configured to use NiOn's own Tor runtime. NiOn is not Tor Browser and does not claim Tor Browser's anti-fingerprinting, site-isolation, anonymity, or security guarantees.

## Network policy

NiOn creates one persistent WebKit network session and assigns a custom SOCKS proxy selected at runtime from:

```text
socks://127.0.0.1:19050
...
socks://127.0.0.1:19069
```

HTTP, HTTPS, WS, and WSS are explicitly mapped to the same selected proxy and the proxy configuration has no ignore/bypass host list.

Tor receives `SafeSocks 1`, `WarnUnsafeSocks 1`, `ClientRejectInternalAddresses 1`, and `ClientDNSRejectInternalAddresses 1` through NiOn's private configuration.

If Tor has not completed bootstrap, normal browsing controls remain blocked.

### 1.0.0 fail-closed reinforcement

When NiOn detects a Tor runtime failure it now applies two independent guards:

1. HTTP/HTTPS top-level navigation policy is rejected while `tor_ready` is false.
2. WebKit's custom proxy is changed away from the former Tor SOCKS port to a deliberately dead loopback SOCKS endpoint.

NiOn also stops active page loads and cancels active downloads. It never intentionally switches the WebKit network session to the system/default proxy.

The second guard matters for background/subresource requests that do not necessarily pass through the same top-level navigation UI path.

## Browser hardening

0.5.0+ applies these WebKit settings to each tab:

- WebRTC disabled.
- Media-stream capture disabled.
- DNS prefetching disabled.
- WebGL disabled.
- WebAudio disabled.
- Accelerated 2D canvas disabled where exposed by WebKitGTK.
- JavaScript clipboard access disabled.
- Automatic JavaScript popup/window opening disabled.
- File-URL access to local files disabled.
- Universal access from file URLs disabled.
- Hyperlink auditing disabled.
- Developer extras disabled.
- Encrypted Media Extensions disabled.
- Legacy plugin/Java/application-cache options disabled when exposed by the installed WebKitGTK.

NiOn denies WebKit permission requests by default, covering camera/microphone capture, geolocation, notifications, and other permission-gated capabilities exposed through WebKit.

## Local-network policy

Top-level navigation to local/private targets is rejected before loading. Tor's own internal-address rejection options are also enabled.

Examples rejected by NiOn:

```text
http://localhost:8080
http://127.0.0.1/
http://192.168.1.1/
http://[::1]/
```

## External URI handlers

NiOn accepts top-level HTTP/HTTPS web navigation only. `file:`, `mailto:`, `ftp:`, and custom schemes are rejected instead of being handed to external applications by NiOn.

The 1.0.0 desktop entry also stops advertising itself as an OS-level HTTP/HTTPS handler because command-line URL opening has not been implemented. This avoids claiming an integration path that NiOn does not actually support.

## WebSocket policy

WebSocket remains enabled for modern-site compatibility. WS and WSS are explicitly assigned to the same Tor SOCKS proxy as HTTP/HTTPS.

## Cookies and storage

Cookies, local/site data, cache, and credentials remain persistent so normal website logins can survive closing and reopening NiOn.

Users can explicitly clear stored website data with **Menu → Clear Browsing Data…**.

1.0.0 performs a minimal SQLite header check on an existing `cookies.sqlite` before WebKit opens it. An obviously invalid file is quarantined rather than repeatedly handed back to WebKit. This recovery can log the user out only when the cookie database itself is already unusable.

## Profile-file resilience

Small NiOn-owned state files now have conservative size/format bounds:

- preferences: 1 MiB;
- saved session file: 16 MiB;
- individual encoded tab session state: 8 MiB;
- download history file: 4 MiB;
- stored completed/failed/cancelled download history: newest 500 entries.

Malformed files are renamed with `.corrupt-...` suffixes instead of silently deleted. URLs remain the fallback when opaque WebKit back/forward state cannot be restored.

NiOn attempts to keep its data, config, cache, and Tor directories mode `0700`.

## Fingerprinting limitation

NiOn reduces several fingerprinting surfaces but still exposes standard Canvas 2D and many normal browser/device characteristics. NiOn does not ship Tor Browser's extensive anti-fingerprinting patches.

Do not describe NiOn as fingerprint-resistant or equivalent to Tor Browser.

## Tor runtime ownership

NiOn passes its PID to Tor with `__OwningControllerProcess`. Tor is therefore expected to exit when its NiOn owner disappears. NiOn also keeps runtime PID metadata for conservative stale-process recovery after abnormal termination.

The Tor data directory is isolated at:

```text
~/.local/share/nion/tor/
```

Corruption recovery quarantines suspected files rather than silently deleting them. 1.0.0 requires both a state/cache reference and an actual corruption/parse/read-failure indicator before classifying a Tor log line as corruption.

## Runtime validation

With NiOn connected and actively loading both a clearnet page and a `.onion` page:

```bash
./scripts/audit-network.sh 30
```

The script samples user-owned NiOn/WebKit TCP and UDP sockets and flags observed paths outside the selected local Tor SOCKS endpoint. The Tor process itself is excluded because it must connect to remote Tor relays.

To test the failure side explicitly:

```bash
./scripts/test-fail-closed.sh 10
```

That test intentionally terminates NiOn's Tor child, then checks for remaining established NiOn/WebKit network sockets. Restart NiOn afterward.

A PASS from either script is evidence for the observed runtime window, not a mathematical proof that every possible WebKit path is leak-free.

## Tor controller surface

The experimental Tor Circuit viewer was removed in 0.10.0. NiOn does not expose a Tor ControlPort for browser UI diagnostics. Web browsing uses the loopback SOCKS endpoint only.

## Onion-Location

NiOn only accepts an advertised Onion-Location when the defining page is HTTPS clearnet and the target is a valid HTTP/HTTPS Tor v3 `.onion` URL. Clicking the badge is an explicit user action and opens the onion target in a separate tab.
## HTTPS-First clearnet warning (1.1.0 Stage 1)

NiOn resolves schemeless clearnet addresses to HTTPS. If a top-level clearnet navigation explicitly targets plain HTTP, NiOn blocks that navigation first and asks for confirmation. Continuing allows that plain-HTTP origin only in the current tab while the tab remains on it. The allowance is cleared when a non-HTTP page commits and is never written to the persistent profile.

The warning is intentionally not applied to `http://` Tor v3 `.onion` addresses. Onion services remain inside Tor and do not depend on an exit relay. This UI distinction does not claim that HTTP and HTTPS are equivalent; it keeps the warning focused on the clearnet exit path.


### Local bookmarks

NiOn 1.1.0 Stage 2 stores bookmarks locally in `~/.local/share/nion/bookmarks.ini`. Bookmark titles and URLs are not synchronized or uploaded by NiOn. Opening a bookmark performs the same Tor-routed navigation as entering that URL normally.


## Per-site data clearing (1.1.0 Stage 3)

**Clear Data for This Site…** does not run the global website-data clear. NiOn first asks WebKit for stored website-data records, selects records attributed to the active HTTP(S) host, and requests removal only for those matching records.

WebKit normally groups stored website data by domain/host name. For a subdomain, WebKit can report a parent-domain record; NiOn treats that parent-domain record as belonging to the current site. This means related subdomains can share a WebKit data bucket. NiOn does not deliberately select unrelated third-party website-data records.

The operation can remove cookies and other persistent website state for the selected record, so it can sign the user out of that site. A confirmation dialog is always shown first. After a successful clear, the active page is reloaded without cache when it is still the current tab.

The global **Clear Browsing Data…** command remains separate and clears website data across the whole NiOn profile.
