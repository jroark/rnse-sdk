#!/bin/sh
# The SD loader on a copy of a stock update disc, for every unit the disc
# has software for and whatever software such a unit has
# (docs/sd-loader.md, "One disc for any unit"):
#
#     PATH=tools/sh4-docker:$PATH sh tools/build-universal-loader.sh stock.iso [index]
#
# Each variant on the disc gets the loader of its unit in its update
# loader's place (examples/sdboot on the 192, examples/sdload193 on the
# 193), and the index 0990: the installed software compares the second
# and third digit with its own, so every unit whose index begins with 0
# takes the disc for an update and starts the loader.
#
# Writes out/universal-loader/ in the SDK's directory.  The emulator's
# `rnse` packs the disc: $RNSE, the rnse repository's own build or one on
# the PATH (tools/rnse.sh).
set -eu
sdk=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$sdk/tools/rnse.sh"
disc=$(abs "${1:?usage: build-universal-loader.sh stock.iso [index]}")
index=${2:-0990}
rnse=$(rnse_bin)
cd "$sdk"
out=out/universal-loader
mkdir -p "$out"
"$rnse" info "$disc" > "$out/stock-info.txt"
variants=$(sed -n 's/^variant \(V[0-9A-Z]*\) .*/\1/p' "$out/stock-info.txt")
[ -n "$variants" ] || { echo "no software for any unit on $disc" >&2; exit 1; }

# The 192's loader reads cards through the card processors, whose
# program it takes from the operating system on this disc when the
# unit's own has another (src/sd.c).  It knows one program: is it here?
for v in $variants; do
    case $v in
        V22*)
            rm -rf "$out/stock"
            "$rnse" extract "$disc" "$out/stock" > /dev/null
            if ! python3 - "$out/stock/$v/OSM.BIN" <<'EOF'
import struct, sys
b = open(sys.argv[1], 'rb').read()
def w(a):
    return struct.unpack_from('<H', b, a - 0xc100)[0] if 0xc100 <= a and a - 0xc100 + 2 <= len(b) else -1
sys.exit(0 if any(w(p) == 1 and w(p + 4) == 0x3800 and w(p + 0x380c) == 2 and w(p + 0x3810) == 0x71bc
                  for p in range(0x100000, 0x300000, 0x10000)) else 1)
EOF
            then
                echo "$v: this disc's operating system has another program for the card processors than" >&2
                echo "the 192's card driver knows (src/sd.c).  The loader will start, and read cards only" >&2
                echo "on a unit whose own software has the known one.  Use the newest update disc" >&2
                echo "(North America: indexes 0320/0350 or 0310/0340; Europe: 0310/0740 or 0300/0730)." >&2
                [ -n "${ANY_DISC:-}" ] || { echo "ANY_DISC=1 builds it all the same." >&2; exit 1; }
            fi
            rm -rf "$out/stock" ;;
    esac
done

cp "$disc" "$out/work.iso"
built192= built193=
for v in $variants; do
    case $v in
        V22*) unit=192 example=sdboot
            [ -n "$built192" ] || { ./build.sh 192 sdboot; built192=1; } ;;
        *) unit=193 example=sdload193
            [ -n "$built193" ] || { ./build.sh 193 sdload193; built193=1; } ;;
    esac
    "$rnse" custom "$out/work.iso" "$v" "out/$example-$unit.bin" "$out/next.iso" --index "$index"
    mv "$out/next.iso" "$out/work.iso"
    echo "$v: out/$example-$unit.bin, index $index"
done
mv "$out/work.iso" "$out/loader.iso"
"$rnse" info "$out/loader.iso" > "$out/disc-info.txt"
(cd "$out" && shasum -a 256 loader.iso > SHA256SUMS)
grep '^variant' "$out/disc-info.txt"
echo "wrote $sdk/$out/loader.iso"
