#!/bin/sh
# Build an example for one unit:   sdk/build.sh 192|193 [example]
# The result, out/<example>-<unit>.bin, is the image from the module's
# entry points on; `rnse custom` puts it on a copy of an update disc.
# Needs sh4-linux-gnu-gcc (any SH-4 GCC will do, set CROSS).
set -e
cd "$(dirname "$0")"
unit=${1:?unit: 192 or 193}
example=${2:-hello}
: "${CROSS:=sh4-linux-gnu-}"
case $unit in
    # Load address of the stock loader, and how much memory from there
    # on the program may use for code, data and stack.  The stock
    # loaders are longer (0x88800 bytes and more on the 192, 0x2da368
    # and more on the 193); the rest of the image is where `rnse custom`
    # puts the stock loader, packed and twice over, for the way back.
    192) load=0x8df00000 size=0x20000 ;;
    193) load=0x09b60000 size=0x100000 ;;
    *) echo "unit must be 192 or 193" >&2; exit 1 ;;
esac
mkdir -p out
# EXTRA_CFLAGS: for instance -DWAIT=5 for the probe.
cflags="-g -mb -m4-nofpu -Os -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude $EXTRA_CFLAGS"
objs=""
for src in src/crt0.S src/common.c src/panel.c src/cd.c src/can.c src/sd.c src/radio.c src/wayback.c src/hw$unit.c examples/$example/*.c; do
    obj=out/$unit-$(basename "$src" | sed 's/\.[cS]$/.o/')
    ${CROSS}gcc $cflags -c -o "$obj" "$src"
    objs="$objs $obj"
done
${CROSS}ld -EB --no-warn-rwx-segments -T link.ld --defsym LOAD=$load --defsym SIZE=$size -o out/$example-$unit.elf $objs
${CROSS}objcopy -O binary out/$example-$unit.elf out/$example-$unit.bin
ls -l out/$example-$unit.bin
