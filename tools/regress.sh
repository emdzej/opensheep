#!/bin/sh
# Video hashes of levels run headless for N ticks (default 1500): tools/regress.sh [N] > tests/hashes.txt,
# then diff later runs against it. Needs build-gasm and the disc (OPENSHEEP_DATA, default ~/Downloads/Sheep.iso).
n=${1:-1500}
iso=${OPENSHEEP_DATA:-$HOME/Downloads/Sheep.iso}
run=.deps/gasm-runner-macos-universal/gasm-run
for l in 1.1 1.4 1.5 2.1 2.4 2.5 3.1 3.5 4.1 4.5 5.3 5.4 5.5 6.1 6.2 6.3 6.4 6.5 7.1 9.1 9.2 9.3 9.4; do
    h=$($run build-gasm/opensheep.wasm --asset cd="$iso" --param level=$l --headless "$n" 2>&1 |
        sed -n 's/.*video_fnv32=\([0-9a-f]*\).*/\1/p')
    echo "$l $h"
done
