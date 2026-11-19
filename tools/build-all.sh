#!/bin/sh
# Build every example for every unit tools/examples.txt lists it for,
# and the resident app; stops at the first failure.
set -eu
sdk=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$sdk"
grep -v '^#' tools/examples.txt | while read -r example units; do
    [ -n "$example" ] || continue
    for unit in $units; do
        ./build.sh "$unit" "$example" >/dev/null
        echo "built $example for the $unit"
    done
done
sh apps/build.sh trip >/dev/null
echo "built apps/trip"
