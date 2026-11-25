#!/usr/bin/env bash
# Builds build/mhxx_smithy_tree.nro: clang (ARMv7, Forge's soft-float calls), ld.lld, then tools/elf_to_nro.py.
#   ./build.sh                    the plugin
#   ./build.sh dist 0.13.0        the plugin, then the release archive dist/mhxx-smithy-tree-0.13.0.zip
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
CFLAGS=(--target=armv7a-none-eabi -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -fPIC -ffreestanding -fno-builtin
        -fno-stack-protector -fvisibility=default -O2 -Wall -Wextra -Wno-unused-parameter -ffile-prefix-map="$PWD"=.
        -Ithird_party/cimgui -Ithird_party/shim)
clang "${CFLAGS[@]}" -c src/crt0.S -o build/crt0.o
clang "${CFLAGS[@]}" -c src/plugin.c -o build/plugin.o
ld.lld -shared -nostdlib -z text --build-id=sha1 -T link/switch32.ld build/crt0.o build/plugin.o -o build/mhxx_smithy_tree.elf
python3 tools/elf_to_nro.py build/mhxx_smithy_tree.elf build/mhxx_smithy_tree.nro
ls -l build/mhxx_smithy_tree.nro
if [ "${1:-}" = dist ]; then python3 tools/dist.py "${2:-}"; fi
