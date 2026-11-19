#!/bin/sh
# Build all release artifacts of the modern firmware (docs/modern-192.md)
# into out/ of the SDK's directory.  RNSE_DISC must be a user-owned
# V222/0371 ISO.  The emulator's `rnse` packs the images: $RNSE, the
# rnse repository's own build or one on the PATH (tools/rnse.sh).
set -eu
sdk=$(cd "$(dirname "$0")" && pwd)
. "$sdk/tools/rnse.sh"
: "${RNSE_DISC:?Set RNSE_DISC to the stock V222/0371 update ISO}"
: "${RNSE_INDEX:=0390}"
disc=$(abs "$RNSE_DISC")
rnse=$(rnse_bin)
cd "$sdk"
./build.sh 192 modern
./build.sh 192 swap
apps/build.sh trip
mkdir -p out/modern/card
cp out/app-trip.RNA out/modern/card/TRIP.RNA
"$rnse" card out/modern/apps.img out/modern/card
"$rnse" firmware "$disc" V222 out/modern-192.bin out/modern/modern-192.iso --index "$RNSE_INDEX" --swap out/swap-192.bin
