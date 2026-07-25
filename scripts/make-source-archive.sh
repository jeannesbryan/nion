#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="1.1.0"
OUTDIR="$ROOT/release"
OUT="$OUTDIR/NiOn-${VERSION}-source.tar.gz"
mkdir -p "$OUTDIR"
rm -f "$OUT" "$OUT.sha256"

if command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git archive --format=tar --prefix="NiOn-${VERSION}/" HEAD | gzip -9 > "$OUT"
else
  STAGE="$(mktemp -d)"
  trap 'rm -rf "$STAGE"' EXIT
  mkdir -p "$STAGE/NiOn-${VERSION}"
  tar \
    --exclude='./build' \
    --exclude='./build-*' \
    --exclude='./build-appimage' \
    --exclude='./NiOn.AppDir' \
    --exclude='./dist' \
    --exclude='./release' \
    --exclude='./runtime/tor' \
    --exclude='./.tools' \
    -cf - . | tar -xf - -C "$STAGE/NiOn-${VERSION}"
  tar -C "$STAGE" -czf "$OUT" "NiOn-${VERSION}"
fi
(cd "$OUTDIR" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256")
printf 'Created:\n  %s\n  %s.sha256\n' "$OUT" "$OUT"
