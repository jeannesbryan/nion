#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/manifest.sh"

# NiOn glibc floor check (release hardening, 2.2.0).
#
# NiOn bundles GTK/GLib/WebKitGTK but deliberately leaves the C library to the
# host. Those bundled libraries therefore carry the GLIBC_* symbol versions of
# the machine that built them, and an AppImage built on a newer distribution
# than the one it claims to support dies at the loader with
#   "version `GLIBC_2.x' not found (required by .../libgtk-4.so.1)"
# before a single line of NiOn code runs.
#
# This script makes that failure a build-time failure instead of a runtime one:
# it reads the required GLIBC_* versions out of every ELF shipped in the AppDir
# and rejects the artifact when the requirement exceeds the manifest's
# supported floor.
#
# Usage:
#   check-glibc-floor.sh <AppDir|AppImage|directory> [--write-required FILE]
#                        [--list] [--quiet]
#
# Exit codes: 0 = within floor, 1 = above floor / unreadable input, 2 = usage.

usage() {
  cat >&2 <<'USAGE'
Usage: check-glibc-floor.sh <AppDir|AppImage|directory> [--write-required FILE] [--list] [--quiet]

Reads the required GLIBC_* symbol versions out of every ELF shipped in the
given tree and fails when the requirement exceeds the supported floor recorded
in release/manifest/GLIBC_FLOOR.

  --write-required FILE   also write the highest required version to FILE
  --list                  list every entry that is above the floor
  --quiet                 only print the PASS/FAIL line
USAGE
  exit 2
}

TARGET=""
WRITE_REQUIRED=""
LIST=0
QUIET=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --write-required) WRITE_REQUIRED="${2:-}"; [[ -n "$WRITE_REQUIRED" ]] || usage; shift 2 ;;
    --list) LIST=1; shift ;;
    --quiet) QUIET=1; shift ;;
    -h|--help) usage ;;
    -*) echo "Unknown option: $1" >&2; usage ;;
    *) TARGET="$1"; shift ;;
  esac
done
[[ -n "$TARGET" ]] || usage

FLOOR="$NION_GLIBC_FLOOR"

for tool in readelf find sort awk; do
  command -v "$tool" >/dev/null 2>&1 || { echo "$tool is required for the glibc floor check" >&2; exit 1; }
done

WORK=""
# The EXIT trap must never be able to change this script's exit status: under
# `set -e` a trap handler whose last command fails *becomes* the script's exit
# status, and `[[ ... ]] && rm -rf` returns 1 whenever WORK is empty (the common
# case, since only the AppImage path needs a temp dir). Always return 0.
cleanup() {
  if [[ -n "$WORK" && -d "$WORK" ]]; then
    rm -rf "$WORK"
  fi
  return 0
}
trap cleanup EXIT

if [[ -d "$TARGET" ]]; then
  SCAN="$TARGET"
elif [[ -f "$TARGET" ]]; then
  # An AppImage is a SquashFS; extract it without FUSE.
  #
  # Resolve to an absolute path first: extraction runs from a temporary
  # directory, so a relative target such as dist/NiOn-2.2.0-x86_64.AppImage
  # would no longer resolve and the extraction would look like a corrupt file.
  TARGET_ABS="$(cd "$(dirname "$TARGET")" && pwd)/$(basename "$TARGET")"
  WORK="$(mktemp -d "${TMPDIR:-/tmp}/nion-glibc.XXXXXX")"
  ( cd "$WORK" && APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET_ABS" --appimage-extract >/dev/null 2>&1 ) || {
    echo "Could not extract $TARGET (expected an AppImage or a directory)" >&2
    exit 1
  }
  SCAN="$WORK/squashfs-root"
  [[ -d "$SCAN" ]] || { echo "Extraction produced no squashfs-root in $TARGET" >&2; exit 1; }
else
  echo "Not a file or directory: $TARGET" >&2
  exit 1
fi

version_gt() { # $1 > $2 ?
  awk -v a="$1" -v b="$2" 'BEGIN{
    n=split(a,x,"."); m=split(b,y,".");
    for (i=1;i<=3;i++) {
      xi=(i<=n?x[i]:0)+0; yi=(i<=m?y[i]:0)+0;
      if (xi>yi) exit 0;
      if (xi<yi) exit 1;
    }
    exit 1
  }'
}

# Required GLIBC_* versions of one ELF: only the "Version needs" section counts,
# never the "Version definition" one (a library may define versions it does not
# itself require).
elf_glibc_needs() {
  readelf --version-info "$1" 2>/dev/null | awk '
    /Version needs section/ { inblock = 1; next }
    /Version definition section/ { inblock = 0 }
    inblock' | grep -oE 'Name: GLIBC_[0-9]+(\.[0-9]+)*' | sed 's/^Name: //' | sort -uV
}

required=""
worst_file=""
scanned=0
offenders=""

while IFS= read -r -d '' file; do
  # Cheap ELF magic test so we never shell out to readelf for plain data.
  magic="$(head -c 4 "$file" 2>/dev/null | od -An -tx1 | tr -d ' \n')"
  [[ "$magic" == "7f454c46" ]] || continue
  scanned=$((scanned + 1))

  # Never count the bundled Tor daemon's own debug tree or the C library
  # itself (it is not shipped by design).
  for need in $(elf_glibc_needs "$file"); do
    [[ "$need" == GLIBC_* ]] || continue
    want="${need#GLIBC_}"
    if [[ -z "$required" ]] || version_gt "$want" "$required"; then
      required="$want"
      worst_file="$file"
    fi
    if version_gt "$want" "$FLOOR"; then
      offenders+="  $want  ${file#"$SCAN"/}"$'\n'
    fi
  done
done < <(find "$SCAN" -type f -print0 2>/dev/null)

if (( scanned == 0 )); then
  echo "No ELF files found under $SCAN — nothing to check" >&2
  exit 1
fi

if [[ -n "$WRITE_REQUIRED" ]]; then
  printf '%s\n' "${required:-0}"
  mkdir -p "$(dirname "$WRITE_REQUIRED")"
  printf '%s\n' "${required:-0}" > "$WRITE_REQUIRED"
  chmod 644 "$WRITE_REQUIRED"
fi

if (( QUIET == 0 )); then
  printf 'NiOn glibc floor check\n'
  printf '  scanned ELF files     %d\n' "$scanned"
  printf '  supported floor       glibc %s (release manifest)\n' "$FLOOR"
  printf '  required by this tree glibc %s\n' "${required:-unknown}"
  [[ -n "$worst_file" ]] && printf '  driven by             %s\n' "${worst_file#"$SCAN"/}"
  if (( LIST )) && [[ -n "$offenders" ]]; then
    printf '  above-floor entries:\n%s' "$offenders"
  fi
fi

if [[ -z "$required" ]]; then
  echo "PASS  no GLIBC symbol version requirement found" >&2
  exit 0
fi

if version_gt "$required" "$FLOOR"; then
  {
    echo
    echo "FAIL  this build requires glibc $required but NiOn $NION_VERSION supports glibc $FLOOR"
    echo "      AppImage/library above the floor:"
    printf '%s' "$offenders"
    echo
    echo "      A bundled library was taken from a build host newer than the supported"
    echo "      floor. Rebuild on the pinned release environment instead:"
    echo "        ./scripts/build-release-container.sh"
  } >&2
  exit 1
fi

if (( QUIET == 0 )); then
  printf 'PASS  required glibc %s <= supported floor %s\n' "$required" "$FLOOR"
fi
exit 0
