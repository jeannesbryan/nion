# NiOn Tor Runtime

NiOn bundles and supervises its own Tor client runtime instead of assuming a system Tor service.

## Pinned upstream runtime

The runtime preparation script reads its canonical pins from `release/manifest/`. Inspect them with:

```bash
source ./scripts/manifest.sh
printf 'Tor Browser Expert Bundle: %s\nContained Tor daemon:       %s\nPlatform:                   GNU/Linux %s\n' \
  "$NION_TOR_BROWSER_VERSION" "$NION_TOR_DAEMON_VERSION" "$NION_APPIMAGE_ARCH"
```

`fetch-tor-runtime.sh` downloads the archive and detached signature, imports the Tor Browser Developers key into a temporary isolated GnuPG home, verifies the full expected fingerprint, then verifies the bundle signature before extraction.

The expected signing fingerprint is canonicalized in `release/manifest/TOR_SIGNING_FINGERPRINT` and consumed by the verified download script.

## Runtime selection

Lookup order:

1. `NION_TOR_BINARY` explicit override.
2. AppImage/AppDir bundled runtime.
3. Development-tree `runtime/tor/tor`.
4. System `tor` only when `NION_ALLOW_SYSTEM_TOR=1` is explicitly set.

There is no silent system-Tor fallback.

## Shared-library loader policy

The Expert Bundle includes runtime files and debug-symbol material. NiOn exposes only the directory containing the real Tor executable, plus its `libstdc++/` directory when present, to the Tor process through `LD_LIBRARY_PATH`.

The development `debug/` tree is never placed on Tor's runtime library path. 1.0.0 additionally removes debug-symbol directories from the production AppImage AppDir entirely.

## Isolated data

Tor receives a NiOn-specific data directory:

```text
~/.local/share/nion/tor/
```

It does not use `/var/lib/tor`, `/etc/tor/torrc`, or another system Tor instance's state.

## Process ownership

NiOn passes its own PID through Tor's `__OwningControllerProcess` option. NiOn also records child runtime metadata at:

```text
~/.local/share/nion/tor-runtime.ini
```

On a later start, a leftover PID is only terminated when `/proc/<pid>/cmdline` confirms that it looks like Tor using NiOn's private Tor directory. An unrelated reused PID is left untouched.

## SOCKS port selection

NiOn tries:

```text
127.0.0.1:19050
...
127.0.0.1:19069
```

and configures WebKit with the selected SOCKS endpoint before browsing is enabled.

If another process wins the small bind race after preflight, NiOn recognizes Tor's bind failure and retries another port up to three times.

## Bootstrap

Tor stdout/stderr is captured. `Bootstrapped N%` messages drive the UI state. Browsing remains disabled until 100%.

A 120-second startup watchdog turns an indefinitely stuck launch into a visible Tor error rather than leaving NiOn on “Connecting…” forever.

## Corrupted-state recovery

NiOn does not wipe Tor data on generic startup errors.

1.0.0 requires both:

- a reference to Tor state/cache data; and
- an actual damage indicator such as corruption, invalid/unparseable data, or parse/read failure

before classifying a log message as state corruption.

This avoids the earlier overly broad behavior where merely mentioning a cached consensus/microdescriptor filename could mark the run as corrupted.

When recovery is justified, affected disposable cache files are moved into:

```text
~/.local/share/nion/tor/recovery-YYYYMMDD-HHMMSS/
```

The persistent `state` file is only quarantined when Tor's own error points to state parsing/corruption.

If the recovery quarantine directory cannot be created, 1.0.0 stops with a Tor error instead of retrying as though recovery had succeeded.

## Fail-closed runtime failure

If Tor stops unexpectedly:

1. `tor_ready` becomes false;
2. WebKit is moved away from the former Tor SOCKS port to a dead loopback proxy;
3. top-level HTTP/HTTPS navigation is rejected while Tor is offline;
4. page loads are stopped;
5. active downloads are cancelled.

NiOn does not switch to the OS proxy configuration.

A destructive runtime test is provided:

```bash
./scripts/test-fail-closed.sh 10
```

Restart NiOn after running it.

## Shutdown

On normal shutdown:

1. session/download state is saved;
2. active downloads are cancelled;
3. Tor receives SIGTERM;
4. NiOn waits up to three seconds;
5. a still-running Tor child is force-exited;
6. runtime PID metadata is removed.

On abrupt NiOn death, `__OwningControllerProcess` is the first defense and stale-runtime cleanup is the second defense on the next launch.

## SOCKS-only model

The experimental Tor Circuit viewer was removed in 0.10.0. NiOn does not reserve a Tor ControlPort for browser UI diagnostics.

The runtime surface is intentionally focused on one job:

```text
WebKit -> loopback Tor SOCKS -> Tor network
```
