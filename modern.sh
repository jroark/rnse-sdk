#!/bin/sh
# Build all release artifacts. RNSE_DISC must be a user-owned V222/0371 ISO.
set -eu
cd "$(dirname "$0")/.."
: "${RNSE_DISC:?Set RNSE_DISC to the stock V222/0371 update ISO}"
: "${RNSE_INDEX:=0390}"
cargo build --release
sdk/build.sh 192 modern
sdk/build.sh 192 swap
sdk/apps/build.sh trip
mkdir -p out/modern/card
cp sdk/out/app-trip.RNA out/modern/card/TRIP.RNA
target/release/rnse card out/modern/apps.img out/modern/card
target/release/rnse firmware "$RNSE_DISC" V222 sdk/out/modern-192.bin out/modern/modern-192.iso --index "$RNSE_INDEX" --swap sdk/out/swap-192.bin
