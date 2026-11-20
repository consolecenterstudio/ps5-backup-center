#!/bin/zsh
# Bibliothèques glissées dans un jeu lancé depuis son .pkg (docs/PLAN-lancement-kraken.md), toutes compilées par nous :
#   ampr-emu-ng (build_ampr.sh, qui prépare aussi les relais du kit et de LLVM), pgo_stub (ici).
#   build-system/build_injected.sh  →  out/injected/*.sprx
set -e
ROOT=${0:a:h}/..
$ROOT/build-system/build_ampr.sh
W=$ROOT/out/ampr/work
O=$ROOT/out/injected
mkdir -p $O/obj
cp $ROOT/out/ampr/libSceAmpr.sprx $O/
export PS5_PAYLOAD_SDK=$W/sdk LLVM_CONFIG=$W/llvm-bin/llvm-config
F=(-fPIC -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
$W/sdk/bin/prospero-clang $F -c $ROOT/build-system/prx/crt.c -o $O/obj/crt.o
$W/sdk/bin/prospero-clang++ $F -c $ROOT/engine/third_party/pgo_stub/library.cpp -o $O/obj/pgo_library.o
python3 $ROOT/build-system/prx/build_prx.py --module libScePlayGo --export '^scePlayGo' --out $O/libScePlayGo.sprx $O/obj/crt.o $O/obj/pgo_library.o
# dlc_emu 0.3 (drakmor) : ses sources telles quelles, nos en-têtes à la place de ceux du kit officiel (prx/sce_compat),
# ses fichiers .pemd pour les bibliothèques exportées ; mêmes objets pour les trois modules.
DLC=$ROOT/engine/third_party/dlc_emu
mkdir -p $O/obj/dlc
FD=(-std=c++17 -fPIC -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -DNDEBUG -D_CRT_SECURE_NO_WARNINGS
    -I $DLC/src -I $DLC/src/dlc_modules -I $ROOT/build-system/prx/sce_compat)
for f in dlc_content dlc_log sceappcontent_exports scegameupdate_exports scenpentitlementaccess_exports; do
  $W/sdk/bin/prospero-clang++ $FD -c $DLC/src/dlc_modules/$f.cpp -o $O/obj/dlc/$f.o
done
D=$O/obj/dlc; P=$DLC/msbuild/pemd
python3 $ROOT/build-system/prx/build_prx.py --pemd $P/libSceAppContent.pemd --file libSceAppContent.prx --out $O/libSceAppContent.sprx $O/obj/crt.o $D/dlc_content.o $D/dlc_log.o $D/sceappcontent_exports.o
python3 $ROOT/build-system/prx/build_prx.py --pemd $P/libSceNpEntitlementAccess.pemd --out $O/libSceNpEntitlementAccess.sprx $O/obj/crt.o $D/dlc_content.o $D/dlc_log.o $D/scenpentitlementaccess_exports.o
python3 $ROOT/build-system/prx/build_prx.py --pemd $P/libSceGameUpdate.pemd --out $O/libSceGameUpdate.sprx $O/obj/crt.o $D/dlc_content.o $D/dlc_log.o $D/scegameupdate_exports.o
shasum -a 256 $O/*.sprx
