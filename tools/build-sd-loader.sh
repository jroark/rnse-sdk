#!/bin/sh
# The persistent SD test loader on a copy of a stock V222 disc
# (docs/sd-loader.md):
#
#     PATH=tools/sh4-docker:$PATH sh tools/build-sd-loader.sh stock.iso [index]
#
# Writes out/sd-loader/ in the SDK's directory.  The emulator's `rnse`
# packs the disc: $RNSE, the rnse repository's own build or one on the
# PATH (tools/rnse.sh).
set -eu
sdk=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$sdk/tools/rnse.sh"
disc=$(abs "${1:?usage: build-sd-loader.sh stock.iso [index]}")
index=${2:-0380}
rnse=$(rnse_bin)
cd "$sdk"
./build.sh 192 sdboot
./build.sh 192 returntest
mkdir -p out/sd-loader
"$rnse" custom "$disc" V222 out/sdboot-192.bin out/sd-loader/sdboot.iso --index "$index"
cp out/returntest-192.bin out/sd-loader/RNSE.BIN
"$rnse" info out/sd-loader/sdboot.iso > out/sd-loader/disc-info.txt
(cd out/sd-loader && shasum -a 256 sdboot.iso RNSE.BIN > SHA256SUMS)
