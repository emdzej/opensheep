#!/usr/bin/env bash
# Put opensheep.wasm into the site's play page (docs/public/play/opensheep.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/opensheep.wasm.
#   docs/scripts/copy-wasm.sh [path/to/opensheep.wasm]
# The module is the engine only: the game data always comes from the player's own CD.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/opensheep.wasm}"
[ -f "$src" ] || { echo "no $src: build opensheep.wasm first (docs/guide/build.md)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/opensheep.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/opensheep.wasm"
