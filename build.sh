#!/bin/sh
# Public macOS build for the frozen 3.2.0 sources. See BUILDING.md.
set -eu
cd "$(dirname "$0")"
: "${SDK:?set SDK}" "${LLVM:?set LLVM}" "${PS5_NATIVE_TOOL:?set PS5_NATIVE_TOOL}"
export SDK LLVM PS5_NATIVE_TOOL
python3 build-system/build.py engine "$@"
python3 build-system/build.py app "$@"
rm -rf out/PPSA99950
cp -R app/package/PPSA99950 out/PPSA99950
cp out/app/eboot.bin out/PPSA99950/eboot.bin
cp out/engine/pbc-engine2.elf out/PPSA99950/pbc-engine2.elf
(cd out && zip -qrX PPSA99950.zip PPSA99950)
