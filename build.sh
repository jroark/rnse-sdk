#!/bin/sh
# Build an example for one unit:   sdk/build.sh 192|193 [example]
# The result, out/<example>-<unit>.bin, is the image from the module's
# entry points on; `rnse custom` puts it on a copy of an update disc.
# Needs sh4-linux-gnu-gcc (any SH-4 GCC will do, set CROSS).
set -e
cd "$(dirname "$0")"
unit=${1:?unit: 192 or 193}
example=${2:-hello}
if [ "$example" = modern ] && [ "$unit" != 192 ]; then
    echo "modern firmware supports the 192 only" >&2
    exit 1
fi
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
objdir=out/objects-$example-$unit
mkdir -p "$objdir"
# EXTRA_CFLAGS: for instance -DWAIT=5 for the probe.
cflags="-g -mb -m4-nofpu -Os -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude $EXTRA_CFLAGS"
# An example that uses code of third_party/ (its file `libs`, below)
# sees that code's headers.
libs=""
[ -f examples/$example/libs ] && libs=$(cat examples/$example/libs)
for lib in $libs; do
    cflags="$cflags -Ithird_party/$lib"
done
[ -n "$libs" ] && cflags="$cflags -Ithird_party/libc"
objs=""
# An example's file `flags` may give one of its sources other code
# generation, a line each: the file's name and the options that take
# the place of -m4-nofpu -Os (-m4 for code that uses the
# FPU, which the kit's own code leaves alone; -O2 for speed).
for src in src/crt0.S src/common.c src/heap.c src/libgcc.S src/cache.S src/panel.c src/display.c src/cd.c src/can.c src/sd.c src/pcm.c src/radio.c src/flash.c src/wayback.c src/hw$unit.c examples/$example/*.c; do
    obj=$objdir/$(basename "$src" | sed 's/\.[cS]$/.o/')
    own=""
    case $src in
        examples/*) [ -f examples/$example/flags ] && own=$(awk -v f="$(basename "$src")" '$1 == f { $1 = ""; print }' examples/$example/flags) ;;
    esac
    if [ -n "$own" ]; then
        ${CROSS}gcc $(echo "$cflags" | sed 's/-m4-nofpu -Os//') $own -c -o "$obj" "$src"
    else
        ${CROSS}gcc $cflags -c -o "$obj" "$src"
    fi
    objs="$objs $obj"
done
# Code from elsewhere that an example names in its file `libs`, one
# directory of third_party/ a line, built for speed and with the few C
# library headers it wants from third_party/libc.
for lib in $libs; do
    for src in third_party/$lib/*.c; do
        obj=$objdir/$lib-$(basename "$src" .c).o
        ${CROSS}gcc -g -mb -m4-nofpu -O2 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector \
            -Ithird_party/libc -Ithird_party/$lib $EXTRA_CFLAGS -c -o "$obj" "$src"
        objs="$objs $obj"
    done
done
${CROSS}ld -EB --no-warn-rwx-segments -T link.ld --defsym LOAD=$load --defsym SIZE=$size -o out/$example-$unit.elf $objs
${CROSS}objcopy -O binary out/$example-$unit.elf out/$example-$unit.bin
ls -l out/$example-$unit.bin
