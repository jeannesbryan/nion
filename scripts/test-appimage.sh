#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPIMAGE="${1:-$ROOT/dist/NiOn-1.0.0-x86_64.AppImage}"
[[ -x "$APPIMAGE" ]] || { echo "AppImage not found/executable: $APPIMAGE" >&2; exit 1; }

echo "== Packaged diagnostics =="
APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGE" --appimage-diagnose

echo
echo "== Persistent-profile path test =="
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp "$APPIMAGE" "$TMP/NiOn-old.AppImage"
cp "$APPIMAGE" "$TMP/NiOn-new.AppImage"
chmod +x "$TMP"/*.AppImage
mkdir -p "$TMP/home/.local/share/nion"
printf 'NiOn profile replacement sentinel\n' > "$TMP/home/.local/share/nion/.replacement-test"
p1="$(HOME="$TMP/home" APPIMAGE_EXTRACT_AND_RUN=1 "$TMP/NiOn-old.AppImage" --appimage-diagnose | awk -F= '/^Profile=/{print $2}')"
p2="$(HOME="$TMP/home" APPIMAGE_EXTRACT_AND_RUN=1 "$TMP/NiOn-new.AppImage" --appimage-diagnose | awk -F= '/^Profile=/{print $2}')"
[[ "$p1" == "$p2" && "$p1" == "$TMP/home/.local/share/nion" && -f "$TMP/home/.local/share/nion/.replacement-test" ]] || {
  echo "FAIL profile path changed across AppImage replacement: '$p1' vs '$p2'" >&2
  exit 1
}
echo "PASS profile path is version-independent and existing profile data survived replacement: $p1"

echo
echo "For GUI/Tor validation now run the AppImage normally, then in another terminal:"
echo "  ./scripts/audit-network.sh 30"
