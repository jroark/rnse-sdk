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
    # Load address and size of the stock loader's image.
    192) load=0x8df00000 size=0x88000 ;;
    193) load=0x09b60000 size=0x2d8000 ;;
    *) echo "unit must be 192 or 193" >&2; exit 1 ;;
esac
mkdir -p out
cflags="-mb -m4-nofpu -Os -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude"
objs=""
for src in src/crt0.S src/common.c src/panel.c src/hw$unit.c examples/$example/*.c; do
    obj=out/$unit-$(basename "$src" | sed 's/\.[cS]$/.o/')
    ${CROSS}gcc $cflags -c -o "$obj" "$src"
    objs="$objs $obj"
done
${CROSS}ld -EB --no-warn-rwx-segments -T link.ld --defsym LOAD=$load --defsym SIZE=$size -o out/$example-$unit.elf $objs
${CROSS}objcopy -O binary out/$example-$unit.elf out/$example-$unit.bin
ls -l out/$example-$unit.bin
