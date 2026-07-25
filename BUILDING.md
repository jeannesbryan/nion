# Building NiOn 1.1.0

## Supported production target

The 1.1.0 production AppImage pipeline targets GNU/Linux x86_64.

## Debian / Ubuntu dependencies

```bash
./scripts/install-deps-debian.sh
```

The script installs the compiler/build stack, GTK 4 development files, WebKitGTK 6 development files, libsoup 3, AppStream/desktop validation tools, Tor-runtime verification tools, and WebKit sandbox helpers.

## Native development build

```bash
rm -rf build
./scripts/run-dev.sh
```

`run-dev.sh` will prepare the pinned signed Tor Expert Bundle if `runtime/tor/` has not been prepared yet, compile NiOn, and launch it.

## Native release build

```bash
rm -rf build
meson setup build --buildtype=release
meson compile -C build
```

Binary:

```text
build/nion
```

## Production AppImage

```bash
./scripts/build-appimage.sh
```

The builder:

1. verifies/prepares the pinned Tor Expert Bundle;
2. compiles NiOn in release mode;
3. creates the final AppDir;
4. bundles Tor;
5. finds and bundles WebKitGTK subprocess executables;
6. deploys practical recursive user-space ELF dependencies;
7. copies GIO modules and WebKit data where needed;
8. installs desktop/AppStream/icon metadata;
9. installs project license/notices;
10. runs AppDir diagnostics;
11. builds the AppImage with `appimagetool`;
12. generates SHA-256;
13. runs packaged diagnostics.

Expected output:

```text
dist/NiOn-1.1.0-x86_64.AppImage
dist/NiOn-1.1.0-x86_64.AppImage.sha256
```

## Validate

```bash
./scripts/release-preflight.sh
./scripts/test-appimage.sh
```

Then perform the live scenarios in `TESTING.md` and the runtime network audit:

```bash
./scripts/audit-network.sh 30
```

## GitHub release build

The repository includes `.github/workflows/release.yml`. For the 1.1.0 release:

```bash
git tag -a v1.1.0 -m "NiOn 1.1.0"
git push origin v1.1.0
```

The workflow builds and validates the x86_64 AppImage and then creates/updates the corresponding GitHub release with the AppImage and SHA-256 asset.
