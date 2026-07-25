# Building NiOn

NiOn 1.3.0 targets GNU/Linux x86_64 for its production AppImage.

## Requirements

On Debian/Ubuntu-family systems:

```bash
./scripts/install-deps-debian.sh
```

This installs the compiler/build stack, GTK 4 and WebKitGTK 6 development files, libsoup 3, AppStream/desktop validators, WebKit sandbox helpers, and tools used to verify/download the bundled Tor runtime and AppImage tooling.

A system Tor package is not required.

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

For 1.3.0 the expected AppImage name is:

```text
NiOn-1.3.0-x86_64.AppImage
```

Do not hard-code a release version into build/package scripts. Update the appropriate one-line manifest value instead.

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
12. obtains `appimagetool` when no explicit `APPIMAGETOOL` is supplied;
13. creates the AppImage;
14. generates SHA-256;
15. runs a FUSE-independent packaged diagnostic.

Expected artifacts:

```text
dist/NiOn-1.3.0-x86_64.AppImage
dist/NiOn-1.3.0-x86_64.AppImage.sha256
```

The AppImage intentionally does not replace host-core components such as the kernel, glibc base environment, or graphics-driver stack.

## Verify the artifact

```bash
cd dist
sha256sum -c NiOn-1.3.0-x86_64.AppImage.sha256
```

Expected:

```text
NiOn-1.3.0-x86_64.AppImage: OK
```

Run it:

```bash
chmod +x NiOn-1.3.0-x86_64.AppImage
./NiOn-1.3.0-x86_64.AppImage
```

Without FUSE:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./NiOn-1.3.0-x86_64.AppImage
```

## Preflight and runtime validation

Run the static/release checks:

```bash
./scripts/release-preflight.sh
```

Then run the packaged diagnostic explicitly:

```bash
./scripts/test-appimage.sh dist/NiOn-1.3.0-x86_64.AppImage
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
4. Create tag `v1.3.0` in GitHub.
5. Create a GitHub Release for `v1.3.0`.
6. Upload:

```text
NiOn-1.3.0-x86_64.AppImage
NiOn-1.3.0-x86_64.AppImage.sha256
```

The repository's GitHub Actions workflow remains optional; the locally validated AppImage is the intended primary release artifact.

## Source archive

A manifest-derived source archive can be created with:

```bash
./scripts/make-source-archive.sh
```

The archive includes `release/manifest/` so release metadata remains reproducible from the source package.
