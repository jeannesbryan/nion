#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

VERSION="1.1.0"
ARCH_RAW="$(uname -m)"
case "$ARCH_RAW" in
  x86_64|amd64) ARCH=x86_64 ;;
  *) echo "NiOn $VERSION AppImage currently targets x86_64; got $ARCH_RAW" >&2; exit 2 ;;
esac

for tool in meson pkg-config cc find ldd readelf cp; do
  command -v "$tool" >/dev/null || { echo "$tool is required" >&2; exit 1; }
done
pkg-config --exists 'gtk4 >= 4.10' || { echo 'gtk4 >= 4.10 not found' >&2; exit 1; }
pkg-config --exists 'webkitgtk-6.0 >= 2.40' || { echo 'webkitgtk-6.0 >= 2.40 not found' >&2; exit 1; }

if [[ ! -x runtime/tor/tor ]]; then
  ./scripts/fetch-tor-runtime.sh
fi
# Normalize/validate the wrapper even when a runtime from an earlier NiOn
# checkout already exists (notably the 0.6.0 pre-hotfix wrapper).
./scripts/repair-tor-runtime.sh

BUILD="$ROOT/build-appimage"
APPDIR="$ROOT/NiOn.AppDir"
DIST="$ROOT/dist"
rm -rf "$BUILD" "$APPDIR"
mkdir -p "$DIST"

meson setup "$BUILD" "$ROOT" --buildtype=release --prefix=/usr
meson compile -C "$BUILD"
DESTDIR="$APPDIR" meson install -C "$BUILD"

mkdir -p "$APPDIR/usr/lib/nion" "$APPDIR/usr/lib/nion/webkit-exec" \
         "$APPDIR/usr/share/metainfo" "$APPDIR/usr/lib"

# Tor Expert Bundle + wrapper.
cp -a runtime/tor "$APPDIR/usr/lib/nion/tor"
# Debug-symbol trees are useful when developing Tor itself but are not runtime
# dependencies. Keeping them inside the AppImage increases size and previously
# caused a loader hazard when an overly broad LD_LIBRARY_PATH discovered them.
find "$APPDIR/usr/lib/nion/tor" -type d -name debug -prune -exec rm -rf {} + 2>/dev/null || true

# WebKit subprocesses are not ELF dependencies of the UI binary, so deploy
# them explicitly. Restrict discovery to webkitgtk-6.0 paths to avoid picking
# helpers from webkit2gtk-4.x on systems that have both installed.
WEBKIT_LIBDIR="$(pkg-config --variable=libdir webkitgtk-6.0)"
mapfile -t helper_candidates < <(
  find "$WEBKIT_LIBDIR" /usr/lib /usr/libexec -type f \
    \( -name WebKitWebProcess -o -name WebKitNetworkProcess -o -name WebKitGPUProcess \) \
    -path '*webkitgtk-6.0*' -perm -u+x 2>/dev/null | sort -u
)

web=""; net=""; gpu=""
for h in "${helper_candidates[@]}"; do
  case "$(basename "$h")" in
    WebKitWebProcess) [[ -z "$web" ]] && web="$h" ;;
    WebKitNetworkProcess) [[ -z "$net" ]] && net="$h" ;;
    WebKitGPUProcess) [[ -z "$gpu" ]] && gpu="$h" ;;
  esac
done
[[ -n "$web" && -n "$net" ]] || {
  echo "Could not locate WebKitGTK 6 WebKitWebProcess/WebKitNetworkProcess." >&2
  echo "Searched libdir: $WEBKIT_LIBDIR" >&2
  exit 1
}
helpers=("$web" "$net")
[[ -n "$gpu" ]] && helpers+=("$gpu")
for h in "${helpers[@]}"; do
  install -m755 "$h" "$APPDIR/usr/lib/nion/webkit-exec/$(basename "$h")"
done

# Build the portable subprocess path shim.
cc -shared -fPIC -O2 -Wall -Wextra -Werror \
  -o "$APPDIR/usr/lib/nion/libnion-execshim.so" packaging/nion-execshim.c -ldl

# Deploy linked user-space libraries for NiOn and WebKit helpers. Core glibc
# and graphics-driver libraries are intentionally left to the host.
elfs=("$APPDIR/usr/bin/nion" "$APPDIR/usr/lib/nion/libnion-execshim.so")
for h in "$APPDIR/usr/lib/nion/webkit-exec"/WebKit*Process; do [[ -f "$h" ]] && elfs+=("$h"); done
./scripts/deploy-elf-deps.sh "$APPDIR" "${elfs[@]}"

# WebKit data/resources are not necessarily ELF dependencies. Preserve the
# distro's webkitgtk-6.0 data directory when present (for example injected
# resources used by the subprocesses).
WEBKIT_DATADIR="$(pkg-config --variable=datadir webkitgtk-6.0 2>/dev/null || true)"
if [[ -n "$WEBKIT_DATADIR" && -d "$WEBKIT_DATADIR/webkitgtk-6.0" ]]; then
  mkdir -p "$APPDIR/usr/share"
  cp -a "$WEBKIT_DATADIR/webkitgtk-6.0" "$APPDIR/usr/share/"
fi

# GIO modules (not always direct ELF dependencies) include TLS/proxy backends.
GIO_MODULE_DIR="$(pkg-config --variable=giomoduledir gio-2.0 2>/dev/null || true)"
if [[ -n "$GIO_MODULE_DIR" && -d "$GIO_MODULE_DIR" ]]; then
  mkdir -p "$APPDIR/usr/lib/gio/modules"
  gio_elfs=()
  for mod in "$GIO_MODULE_DIR"/*.so; do
    [[ -f "$mod" ]] || continue
    install -m755 "$mod" "$APPDIR/usr/lib/gio/modules/$(basename "$mod")"
    gio_elfs+=("$APPDIR/usr/lib/gio/modules/$(basename "$mod")")
  done
  if (( ${#gio_elfs[@]} )); then
    ./scripts/deploy-elf-deps.sh "$APPDIR" "${gio_elfs[@]}"
    if command -v gio-querymodules >/dev/null; then
      gio-querymodules "$APPDIR/usr/lib/gio/modules" || true
    fi
  fi
fi

# AppStream metadata and root AppDir integration files.
install -m644 data/io.github.jeannesbryan.Nion.metainfo.xml \
  "$APPDIR/usr/share/metainfo/io.github.jeannesbryan.Nion.metainfo.xml"
mkdir -p "$APPDIR/usr/share/doc/nion" "$APPDIR/usr/share/licenses/nion"
install -m644 LICENSE "$APPDIR/usr/share/licenses/nion/LICENSE"
install -m644 THIRD_PARTY_NOTICES.md "$APPDIR/usr/share/doc/nion/THIRD_PARTY_NOTICES.md"
install -m644 README.md "$APPDIR/usr/share/doc/nion/README.md"
cp packaging/AppRun "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
ln -sfn usr/share/applications/io.github.jeannesbryan.Nion.desktop \
  "$APPDIR/io.github.jeannesbryan.Nion.desktop"
ln -sfn usr/share/icons/hicolor/scalable/apps/io.github.jeannesbryan.Nion.svg \
  "$APPDIR/io.github.jeannesbryan.Nion.svg"
ln -sfn usr/share/icons/hicolor/scalable/apps/io.github.jeannesbryan.Nion.svg \
  "$APPDIR/.DirIcon"

# Save build provenance for troubleshooting without touching the user profile.
{
  echo "NiOn=$VERSION"
  echo "Architecture=$ARCH"
  echo "BuildDate=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "GTK=$(pkg-config --modversion gtk4)"
  echo "WebKitGTK=$(pkg-config --modversion webkitgtk-6.0)"
  echo "libsoup=$(pkg-config --modversion libsoup-3.0)"
  echo "GLib=$(pkg-config --modversion glib-2.0)"
  echo "WebKitWebProcessSource=$web"
  echo "WebKitNetworkProcessSource=$net"
  [[ -n "$gpu" ]] && echo "WebKitGPUProcessSource=$gpu"
} > "$APPDIR/usr/lib/nion/BUILD-INFO"

# Validate desktop/AppStream metadata when the build host provides validators.
if command -v desktop-file-validate >/dev/null 2>&1; then
  desktop-file-validate "$APPDIR/usr/share/applications/io.github.jeannesbryan.Nion.desktop"
fi
if command -v appstreamcli >/dev/null 2>&1; then
  appstreamcli validate "$APPDIR/usr/share/metainfo/io.github.jeannesbryan.Nion.metainfo.xml"
fi

# AppDir-level smoke test before creating SquashFS.
"$APPDIR/AppRun" --appimage-diagnose

APPIMAGETOOL="${APPIMAGETOOL:-}"
if [[ -z "$APPIMAGETOOL" ]]; then
  APPIMAGETOOL="$(./scripts/fetch-appimage-tools.sh | tail -n1)"
fi
[[ -x "$APPIMAGETOOL" ]] || { echo "appimagetool unavailable: $APPIMAGETOOL" >&2; exit 1; }

OUT="$DIST/NiOn-${VERSION}-${ARCH}.AppImage"
rm -f "$OUT"
# appimagetool itself can run without FUSE via APPIMAGE_EXTRACT_AND_RUN.
ARCH="$ARCH" VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1 \
  "$APPIMAGETOOL" "$APPDIR" "$OUT"
chmod +x "$OUT"
(cd "$DIST" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256")

# FUSE-independent packaged diagnostic. This proves the generated SquashFS can
# extract and its bundled loader paths resolve on the build host.
APPIMAGE_EXTRACT_AND_RUN=1 "$OUT" --appimage-diagnose

printf '\nCreated:\n  %s\n  %s.sha256\n' "$OUT" "$OUT"
printf 'Profile remains outside AppImage at: %s/nion\n' "${XDG_DATA_HOME:-$HOME/.local/share}"
