#!/usr/bin/env bash
# Build the macOS app icon (.icns) from tools/icon/opensheep-macos.png (tools/icon/make-icons.py).
# Used by tools/package-gasm.sh (Sheep (gasm).app). Needs macOS sips and iconutil.
#   tools/make-icns.sh <out.icns>
set -euo pipefail
OUT=${1:?usage: make-icns.sh <out.icns>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/tools/icon/opensheep-macos.png"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SET="$TMP/opensheep.iconset"; mkdir -p "$SET"
for s in 16 32 128 256 512; do
  sips -z "$s" "$s" "$SRC" --out "$SET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s * 2)) $((s * 2)) "$SRC" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT"
