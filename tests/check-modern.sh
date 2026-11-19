#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p out/modern
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Iinclude tests/modern_phone.c examples/modern/phone.c -o out/modern/phone-test
out/modern/phone-test
