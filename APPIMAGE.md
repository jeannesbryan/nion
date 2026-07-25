# NiOn 1.1.0 AppImage Engineering

NiOn 1.0.0 finalizes the AppDir/AppImage packaging pipeline introduced in 0.9.0 and hardened through the 0.11.0 release candidate.

## Build

```bash
./scripts/install-deps-debian.sh
./scripts/build-appimage.sh
```

Output:

```text
dist/NiOn-1.1.0-x86_64.AppImage
dist/NiOn-1.1.0-x86_64.AppImage.sha256
```

The builder prepares the pinned, signature-verified Tor Expert Bundle when needed and fetches `appimagetool` when `APPIMAGETOOL` is not supplied.

## AppDir layout

```text
NiOn.AppDir/
├── AppRun
├── io.github.jeannesbryan.Nion.desktop -> usr/share/applications/...
├── io.github.jeannesbryan.Nion.svg -> usr/share/icons/...
└── usr/
    ├── bin/
    │   └── nion
    ├── lib/
    │   ├── *.so*
    │   ├── gio/modules/
    │   └── nion/
    │       ├── BUILD-INFO
    │       ├── libnion-execshim.so
    │       ├── webkit-exec/
    │       │   ├── WebKitWebProcess
    │       │   ├── WebKitNetworkProcess
    │       │   └── WebKitGPUProcess      when supplied by the host build
    │       └── tor/                       verified Tor runtime, without debug-symbol trees
    └── share/
        ├── applications/
        ├── icons/hicolor/scalable/apps/
        └── metainfo/
```

## Tor runtime packaging

The development Tor Expert Bundle can contain `debug/` trees with symbol/debug objects whose names resemble real shared libraries. They are not needed to run NiOn.

1.0.0 removes those debug directories from the production AppDir after copying the verified runtime. This:

- reduces AppImage size;
- prevents a future loader-path regression from accidentally discovering debug objects;
- does not alter the Tor wrapper, GeoIP files, runtime libraries, or manifest needed by NiOn.

## WebKit subprocess handling

WebKitGTK is multi-process. `WebKitWebProcess`, `WebKitNetworkProcess`, and on builds that use it `WebKitGPUProcess` are not normal ELF dependencies of the NiOn UI executable.

The builder explicitly discovers the helpers belonging to `webkitgtk-6.0`, bundles them, then deploys their shared-library dependencies.

Distribution WebKitGTK builds may contain compile-time absolute helper paths. NiOn ships a small `LD_PRELOAD` spawn-path shim that rewrites only WebKit helper executable paths to the AppImage copies. It does not alter network destinations.

WebKitGTK 6's WebProcess sandbox remains enabled. Before a WebView is created, NiOn exposes the read-only AppImage mount path to the WebKit sandbox so rewritten helpers remain visible.

## Library policy

`deploy-elf-deps.sh` recursively bundles user-space dependencies for NiOn and WebKit helpers. It deliberately leaves these to the host:

- glibc and the ELF dynamic loader;
- NSS/resolver components coupled to host libc;
- OpenGL/EGL/Vulkan/DRM and vendor GPU-driver libraries.

This reduces ABI and graphics-driver conflicts.

## Desktop integration

The AppDir contains NiOn's desktop file, icon, and AppStream metadata.

1.0.0 intentionally does **not** advertise `x-scheme-handler/http` or `x-scheme-handler/https`, and the desktop `Exec` line has no `%u`. NiOn does not yet implement OS-level command-line URL handling, so the stable release does not claim to be a system URL handler.

The repository link in NiOn's own About dialog remains handled internally and opens as a NiOn tab.

## WebKit sandbox host helpers

Current distro WebKitGTK builds normally use `bubblewrap` and `xdg-dbus-proxy` for sandboxing. NiOn does not disable the mandatory WebKitGTK 6 sandbox to make packaging easier.

The Debian/FunOS dependency script installs these helpers on the host. Release builds should still be validated on a real desktop because container loader tests do not exercise a complete GUI/Tor session.

## Persistent profile

The AppImage is read-only. NiOn state lives outside it:

```text
~/.local/share/nion/
~/.cache/nion/
~/.config/nion/
```

Therefore replacing `NiOn-1.0.0-x86_64.AppImage` with another AppImage filename/version does not intentionally replace cookies, website data, downloads history, Tor state, or saved tabs.

Run:

```bash
./scripts/test-appimage.sh
```

for packaged dependency diagnostics and the version-independent profile-path replacement test.

## Diagnostics

Direct diagnostic:

```bash
APPIMAGE_EXTRACT_AND_RUN=1 \
./dist/NiOn-1.1.0-x86_64.AppImage \
--appimage-diagnose
```

1.0.0 uses `mktemp` for diagnostic `ldd` output rather than a predictable `/tmp/...$$` path.

## Additional distro smoke test

With Docker or Podman:

```bash
./scripts/test-appimage-containers.sh
```

This checks extraction and loader/runtime diagnostics in Debian, Ubuntu, and Fedora containers. It is not a GUI/Tor browsing test.

## Tor routing validation

Run the AppImage normally. While actively loading clearnet and onion pages:

```bash
./scripts/audit-network.sh 30
```

Then perform the destructive fail-closed test once:

```bash
./scripts/test-fail-closed.sh 10
```

Expected model:

```text
NiOn/WebKit -> local Tor SOCKS -> Tor network
```

There is no intentional direct-network fallback.
