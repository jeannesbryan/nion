#!/usr/bin/env bash
set -euo pipefail

# Usage: deploy-elf-deps.sh APPDIR ELF [ELF ...]
# Copies non-core shared-library dependencies recursively into APPDIR/usr/lib.

[[ $# -ge 2 ]] || { echo "usage: $0 APPDIR ELF [ELF ...]" >&2; exit 2; }
APPDIR="$1"; shift
DEST="$APPDIR/usr/lib"
mkdir -p "$DEST"

should_skip() {
  local base="${1##*/}"
  case "$base" in
    linux-vdso.so.*|ld-linux-*.so.*|ld-musl-*.so.*|libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|libutil.so.*|libanl.so.*|libnss_*.so.*)
      return 0 ;;
    libGL.so.*|libGLX.so.*|libEGL.so.*|libOpenGL.so.*|libGLdispatch.so.*|libgbm.so.*|libdrm*.so.*|libvulkan.so.*|libcuda.so.*|libnvidia-*.so.*)
      return 0 ;;
  esac
  return 1
}

list_deps() {
  local elf="$1"
  LC_ALL=C ldd "$elf" 2>/dev/null | awk '
    /=> \/[^ ]+/ { print $3 }
    /^\/[^ ]+ \(/ { print $1 }
  ' | grep '^/' || true
}

declare -A seen=()
queue=("$@")
index=0
while (( index < ${#queue[@]} )); do
  elf="${queue[$index]}"; ((index+=1))
  [[ -f "$elf" ]] || continue
  while IFS= read -r dep; do
    [[ -n "$dep" && -f "$dep" ]] || continue
    real="$(readlink -f "$dep")"
    base="$(basename "$dep")"
    should_skip "$base" && continue
    [[ -n "${seen[$real]:-}" ]] && continue
    seen[$real]=1

    # Preserve the SONAME-facing filename while copying the real target.
    cp -L "$dep" "$DEST/$base"
    chmod 0755 "$DEST/$base" || true
    queue+=("$DEST/$base")
  done < <(list_deps "$elf")
done

# Report unresolved dependencies with the AppDir library path active.
status=0
for elf in "$@" "$DEST"/*.so*; do
  [[ -f "$elf" ]] || continue
  if LD_LIBRARY_PATH="$DEST${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>/dev/null | grep -q 'not found'; then
    echo "Unresolved dependency in: $elf" >&2
    LD_LIBRARY_PATH="$DEST${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>/dev/null | grep 'not found' >&2 || true
    status=1
  fi
done
exit "$status"
