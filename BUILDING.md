# Building NiOn

NiOn 2.2.0 Stable targets GNU/Linux x86_64 for its production AppImage.

## Requirements

On Debian/Ubuntu-family systems:

```bash
./scripts/install-deps-debian.sh
```

This installs the compiler/build stack, GTK 4 and WebKitGTK 6 development files, libsoup 3, AppStream/desktop validators, WebKit sandbox helpers, and tools used to verify/download the bundled Tor runtime and AppImage tooling.

A system Tor package is not required.

### Stable dependency baseline for 2.2.0

The source keeps its API compatibility floors deliberately conservative. The "baseline" values are no longer an aspiration recorded next to the build: they describe the **pinned release environment** the AppImage is actually produced in, so documentation and artifact cannot drift apart.

```text
Minimum GTK          4.10
Minimum WebKitGTK    2.40

Stable GTK baseline       4.14.5
Stable WebKitGTK baseline 2.52.6
Stable GLib baseline      2.80.0

Supported C library floor glibc 2.39
```

## What NiOn bundles, and what your system must provide

NiOn ships GTK 4, GLib, WebKitGTK 6, libsoup and the Tor runtime. It deliberately does **not** ship, and takes from the host instead:

- the **C library** (glibc) — see the floor below;
- the **graphics driver stack**: Mesa (`libGL`, `libEGL`), `libgbm`, `libdrm` and Vulkan (`libvulkan`).

Those are exactly the components a browser should not second-guess: they are matched to the user's GPU, kernel driver and display server, not to the application. Every desktop Linux installation has them; minimal containers and server installs usually do not.

`scripts/test-appimage-containers.sh` installs this minimal runtime set in each test image before running the AppImage, so the check measures what NiOn ships rather than the absence of a desktop.

## Supported distributions and the C library floor

NiOn deliberately does **not** replace the system C library: shipping another libc is not something a browser should do. It does ship GTK, GLib and WebKitGTK, and those bundles carry the `GLIBC_*` symbol versions of whatever machine built them.

That makes the build host a compatibility decision:

- the supported floor is **glibc 2.39** — Ubuntu 24.04 LTS and derivatives such as Linux Mint 22.x;
- release AppImages must therefore be built on a host at or below that floor;
- `scripts/check-glibc-floor.sh` reads the required `GLIBC_*` versions out of every ELF in the AppDir (and again from the finished AppImage) and **fails the build** when anything exceeds the floor;
- `packaging/AppRun` records the required version in `usr/lib/nion/GLIBC-REQUIRED` and refuses to start on an older system with an actionable message instead of one raw loader error per bundled library.

Historically NiOn 2.1.0 was built by hand on a glibc 2.43 host and shipped with a documented floor of 2.39, so it died at the loader on supported systems. The three mechanisms above exist so that this failure mode is caught at build time.

### Building the release in the pinned container

On a host newer than the floor, build inside the pinned image instead:

```bash
./scripts/build-release-container.sh
```

It uses `podman` (preferred) or `docker`, verifies the image's glibc is at or below the floor *before* building, runs `install-deps-debian.sh` plus `build-appimage.sh` inside it, hands the artifacts back to the invoking user, and re-runs the glibc floor check on the result. Override the image with `--image` or `NION_RELEASE_CONTAINER_IMAGE` if you need a different (older or equal) base.

## Release manifest

Release-critical values live in `release/manifest/`.

```bash
source ./scripts/manifest.sh
printf 'NiOn: %s\nTor: %s\nExpert Bundle: %s\nAppImage: %s\n' \
  "$NION_VERSION" \
  "$NION_TOR_DAEMON_VERSION" \
  "$NION_TOR_BROWSER_VERSION" \
  "$NION_APPIMAGE_BASENAME"
```

For NiOn 2.2.0 Stable the manifest-derived AppImage name is:

```text
NiOn-2.2.0-x86_64.AppImage
```

Do not hard-code a release version into build/package scripts. Update the appropriate one-line manifest value instead.

## Bundled content-filter rules

`data/content-blocking.json` is embedded into the application GResource and compiled at runtime through WebKit's `WebKitUserContentFilterStore`. No additional ad-block extension engine or network updater is required. The compiled WebKit representation is kept under the NiOn cache directory and can be regenerated from the bundled JSON.

Keep this ruleset intentionally small and reviewable. Stage 1 regression tests reject an unexpectedly large ruleset, non-third-party rules, and rules that block top-level documents.

## Development build

```bash
rm -rf build
./scripts/run-dev.sh
```

`run-dev.sh` prepares/validates the pinned signed Tor Expert Bundle when necessary, builds NiOn in debug mode, and launches it.

## Native release build

```bash
rm -rf build
meson setup build --buildtype=release
meson compile -C build
```

Result:

```text
build/nion
```

## Build the AppImage

The normal production flow is:

```bash
./scripts/install-deps-debian.sh
./scripts/build-appimage.sh
./scripts/release-preflight.sh
```

`build-appimage.sh` performs the following:

1. loads the centralized release manifest;
2. verifies/prepares the pinned Tor Expert Bundle;
3. builds NiOn in release mode;
4. creates `NiOn.AppDir`;
5. installs the bundled Tor runtime;
6. copies the WebKitGTK web/network/GPU subprocess executables available on the build host;
7. builds the NiOn WebKit subprocess path shim;
8. deploys practical recursive user-space ELF dependencies;
9. copies required WebKit/GIO runtime data where available;
10. installs desktop, icon, AppStream, license, README, manifest, and build-provenance metadata;
11. validates the AppDir;
12. checks the AppDir against the supported glibc floor and records the required C library version inside the AppImage;
13. obtains `appimagetool` when no explicit `APPIMAGETOOL` is supplied;
14. creates the AppImage;
15. generates SHA-256;
16. runs a FUSE-independent packaged diagnostic;
17. re-checks the finished AppImage against the glibc floor.

Expected artifacts:

```text
dist/NiOn-2.2.0-x86_64.AppImage
dist/NiOn-2.2.0-x86_64.AppImage.sha256
```

The AppImage intentionally does not replace host-core components such as the kernel, glibc base environment, or graphics-driver stack.

## Verify the artifact

```bash
cd dist
sha256sum -c NiOn-2.2.0-x86_64.AppImage.sha256
```

Expected:

```text
NiOn-2.2.0-x86_64.AppImage: OK
```

Run it:

```bash
chmod +x NiOn-2.2.0-x86_64.AppImage
./NiOn-2.2.0-x86_64.AppImage
```

Without FUSE:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-2.2.0-x86_64.AppImage
```

## Preflight and runtime validation

Run the static/release checks:

```bash
./scripts/release-preflight.sh
```

Preflight also verifies the C library floor of the built AppImage and runs the
cross-distribution container smoke test when `podman` or `docker` is available:

```bash
./scripts/test-appimage-containers.sh dist/NiOn-2.2.0-x86_64.AppImage
```

That test is the one that would have caught the 2.1.0 loader failure: it extracts
and starts the AppImage inside `ubuntu:24.04` (the pinned floor), `debian:stable`
and `fedora:latest`. In CI it is mandatory — the release workflow sets
`NION_REQUIRE_CONTAINER_TEST=1`, which turns a missing container engine into a
failure rather than a warning.

Then run the packaged diagnostic explicitly:

```bash
./scripts/test-appimage.sh dist/NiOn-2.2.0-x86_64.AppImage
```

Finally complete the live scenarios in `TESTING.md`, including Tor failure/recovery, normal/private persistence separation, downloads, context menus/new-window links, and the network audit.

```bash
./scripts/audit-network.sh 30
```

## Bundled Tor

The Tor runtime is prepared by:

```bash
./scripts/fetch-tor-runtime.sh
```

The fetcher verifies the pinned Expert Bundle using the release-manifest signing fingerprint and records runtime provenance in `runtime/tor/MANIFEST.ini`.

NiOn uses its own Tor data directory and chooses a local SOCKS endpoint for the runtime. If the Tor child fails, browser web navigation is blocked and WebKit is moved to a dead loopback SOCKS endpoint rather than intentionally falling back to a direct connection.

## AppImage profile behavior

The normal browser profile is outside the AppImage:

```text
~/.local/share/nion/
~/.config/nion/
~/.cache/nion/
```

Therefore replacing the AppImage normally keeps normal cookies, site data, bookmarks, session state, downloads history, preferences, zoom state, and Tor client state.

Private Window browsing/session/download history is intentionally not persisted into those normal stores.

## Manual GitHub release

NiOn's preferred release flow is manual after local validation.

1. Build and test the AppImage.
2. Verify its SHA-256 file.
3. Commit/push the final source.
4. Create the GitHub Release/tag `v2.2.0` only after the final runtime smoke test passes.
5. Upload:

```text
NiOn-2.2.0-x86_64.AppImage
NiOn-2.2.0-x86_64.AppImage.sha256
```

The repository's GitHub Actions workflow remains optional; the locally validated AppImage is the intended primary release artifact.

## Source archive

A manifest-derived source archive can be created with:

```bash
./scripts/make-source-archive.sh
```

The archive includes `release/manifest/` so release metadata remains reproducible from the source package.
