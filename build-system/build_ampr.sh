#!/bin/zsh
# ampr-emu-ng (libSceAmpr.sprx), compilé par nous depuis engine/third_party/ampr-emu-ng (v0.4.1, sources telles quelles) :
# la même recette que leur Makefile (`make verify`), sans Docker — notre kit PS5, notre LLVM 18, notre Rust 1.100.
# Le kit et LLVM sont appelés par de petits relais (dossier de travail) : le kit passe par bash, et macOS y efface
# DYLD_LIBRARY_PATH, dont libLLVM a besoin pour trouver zstd. Rien n'est modifié dans toolchain/ ni dans les sources.
#   build-system/build_ampr.sh  →  out/ampr/libSceAmpr.sprx (vérifié par leur tools/verify_payload_prx.py)
set -e
ROOT=${0:a:h}/..
TC=$ROOT/toolchain/.local/toolchain
L=$LLVM
Z=${DYLD_LIBRARY_PATH:-}
W=$ROOT/out/ampr/work
SRC=$ROOT/engine/third_party/ampr-emu-ng
mkdir -p $W/llvm-bin
rm -rf $W/sdk && cp -R $SDK $W/sdk && chmod +x $W/sdk/bin/*
for t in $(ls $L); do
  printf '#!/bin/bash\nexport DYLD_LIBRARY_PATH="%s"\nexec "%s/%s" "$@"\n' "$Z" "$L" "$t" > $W/llvm-bin/$t
  chmod +x $W/llvm-bin/$t
done
printf '#!/bin/bash\nexport DYLD_LIBRARY_PATH="%s"\nif [ "$1" = "--bindir" ]; then echo "%s"; exit 0; fi\nexec "%s/llvm-config" "$@"\n' "$Z" "$W/llvm-bin" "$L" > $W/llvm-bin/llvm-config
chmod +x $W/llvm-bin/llvm-config
# Copie de travail des sources (leur build écrit dans out/ à côté du Makefile).
rm -rf $W/src && cp -R $SRC $W/src
cd $W/src
PATH="$HOME/.cargo/bin:$W/llvm-bin:$PATH" PS5_PAYLOAD_SDK=$W/sdk LLVM_CONFIG=$W/llvm-bin/llvm-config RUSTUP_TOOLCHAIN=1.100.0 \
  make OUT=out/release AMPR_MEM_REPORT=0 verify
cp out/release/libSceAmpr.sprx $ROOT/out/ampr/libSceAmpr.sprx
shasum -a 256 $ROOT/out/ampr/libSceAmpr.sprx
