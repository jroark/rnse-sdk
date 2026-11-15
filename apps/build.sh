#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
name=${1:-trip}
: "${CROSS:=sh4-linux-gnu-}"
mkdir -p out
"${CROSS}gcc" -mb -m4-nofpu -Os -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -Wall -Wextra -Iinclude -c "apps/$name/main.c" -o "out/app-$name.o"
"${CROSS}ld" -EB --no-warn-rwx-segments -T apps/app.ld -o "out/app-$name.elf" "out/app-$name.o"
"${CROSS}objcopy" -O binary "out/app-$name.elf" "out/app-$name.raw"
"${CROSS}nm" "out/app-$name.elf" > "out/app-$name.sym"
python3 apps/package.py "out/app-$name" "$name"
