#!/bin/sh
# Build an example for one unit:   ./build.sh 192|193 [example]
# The result, out/<example>-<unit>.bin, is the image from the module's
# entry points on; `rnse custom` puts it on a copy of an update disc.
# Needs sh4-linux-gnu-gcc (any SH-4 GCC will do, set CROSS).
#
# The example is the name of a directory of examples/ or, given with a
# slash in it, the path of a directory anywhere else (a program kept in
# a repository of its own: ./build.sh 192 ../doom/boot).  Its output
# is named after the directory's last component either way.
set -e
unit=${1:?unit: 192 or 193}
example=${2:-hello}
case $example in
    */*) dir=$(cd "$example" && pwd); example=$(basename "$dir") ;;
    *) dir="" ;;
esac
cd "$(dirname "$0")"
[ -n "$dir" ] || dir=examples/$example
[ -d "$dir" ] || { echo "no example $dir" >&2; exit 1; }
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
# An example too big for that place, started by a first stage of its own
# that loads it elsewhere (examples/aaview for aaviewer, as quakeboot
# does for Quake): its file `place`, "LOAD SIZE" for the 192.
if [ -f "$dir/place" ]; then
    [ "$unit" = 192 ] || { echo "$example: the 192 only" >&2; exit 1; }
    read -r load size < "$dir/place"
fi
objdir=out/objects-$example-$unit
mkdir -p "$objdir"
# EXTRA_CFLAGS: for instance -DWAIT=5 for the probe.
cflags="-g -mb -m4-nofpu -Os -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude $EXTRA_CFLAGS"
# An example that uses code of third_party/ (its file `libs`, below)
# sees that code's headers.  A line with a slash in it is a directory
# relative to the example's own (code kept beside a program in a
# repository of its own: ../third_party/h264bsd); the kit's C library
# headers come from its third_party/libc either way.
libs=""
if [ -f "$dir/libs" ]; then
    for lib in $(cat "$dir/libs"); do
        case $lib in
            */*) libs="$libs $(cd "$dir/$lib" && pwd)" ;;
            *) libs="$libs third_party/$lib" ;;
        esac
    done
fi
for lib in $libs; do
    cflags="$cflags -I$lib"
done
[ -n "$libs" ] && cflags="$cflags -Ithird_party/libc"
objs=""
# An example's file `flags` may give one of its sources other code
# generation, a line each: the file's name and the options that take
# the place of -m4-nofpu -Os (-m4 for code that uses the
# FPU, which the kit's own code leaves alone; -O2 for speed).
for src in src/crt0.S src/common.c src/heap.c src/libgcc.S src/cache.S src/panel.c src/display.c src/cd.c src/can.c src/car.c src/sd.c src/pcm.c src/radio.c src/flash.c src/wayback.c src/enter.c src/hw$unit.c "$dir"/*.c; do
    obj=$objdir/$(basename "$src" | sed 's/\.[cS]$/.o/')
    own=""
    case $src in
        "$dir"/*) [ -f "$dir/flags" ] && own=$(awk -v f="$(basename "$src")" '$1 == f { $1 = ""; print }' "$dir/flags") ;;
    esac
    if [ -n "$own" ]; then
        ${CROSS}gcc $(echo "$cflags" | sed 's/-m4-nofpu -Os//') $own -c -o "$obj" "$src"
    else
        ${CROSS}gcc $cflags -c -o "$obj" "$src"
    fi
    objs="$objs $obj"
done
# Code from elsewhere that an example names in its file `libs`, one
# directory a line (of third_party/, or relative to the example), built
# for speed and with the few C library headers it wants from
# third_party/libc.
for lib in $libs; do
    for src in "$lib"/*.c; do
        obj=$objdir/$(basename "$lib")-$(basename "$src" .c).o
        ${CROSS}gcc -g -mb -m4-nofpu -O2 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector \
            -Ithird_party/libc -I"$lib" $EXTRA_CFLAGS -c -o "$obj" "$src"
        objs="$objs $obj"
    done
done
${CROSS}ld -EB --no-warn-rwx-segments -T link.ld --defsym LOAD=$load --defsym SIZE=$size -o out/$example-$unit.elf $objs
${CROSS}objcopy -O binary out/$example-$unit.elf out/$example-$unit.bin
ls -l out/$example-$unit.bin
