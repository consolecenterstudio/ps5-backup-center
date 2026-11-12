// Identité d'un jeu pour la compatibilité, lue sans monter : dossier de jeu, image .exfat ou .ffpfsc
// (exFAT imbriqué). Lecture seule.
//   - sce_sys/param.json : Title ID, nom, version (contentVersion / masterVersion), minimum déclaré
//     (requiredSystemSoftwareVersion), SDK déclaré (sdkVersion) ;
//   - exécutables (eboot.bin, *.prx / *.sprx à la racine et dans sce_module/) : SDK lu dans l'ELF
//     (segment SCE_PROCPARAM / SCE_MODULE_PARAM) ; un SELF signé est signalé comme non lisible ;
//   - bibliothèques propres au jeu (dossier fakelib/).
#pragma once

#include <string>

#include "core/json.hpp"

namespace pbc::images {

// « 0x1120000000000000 » → « 11.20 » ; « 0x04000031 » → « 4.00 » ; vide si illisible.
std::string sdk_text(std::uint64_t v);
std::string sdk_text_hex(const std::string& hex);

// {readable, error, title_id, name, version, master_version, declared_min, declared_sdk,
//  executables:[{path, sdk, format}], exe_sdk_max, own_libs:[{name, size, sdk, format}], own_libs_sdk_max}
Json identify(const std::string& console_path);

// Fichiers d'un dossier de backport (ShadowMountPlus backports/<ID>) : [{name, size, sdk, format}].
Json libraries(const std::string& console_dir);

// Formats d'image observés sur cette console (journal ShadowMountPlus) :
// {"ffpfsc":{"ok":n,"failed":n,"last_error":"…"}, "exfat":{…}, …}
Json observed_mounts();

}  // namespace pbc::images
