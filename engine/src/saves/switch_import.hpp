// Sauvegarde d'une vraie Switch (JKSV) → Eden / ProsperoEden sur la PS5.
// Conversion : le zip JKSV est décompressé dans le dossier du jeu pour le profil choisi
// (user/nand/user/save/0000000000000000/<profil>/<jeu>), sans le fichier de métadonnées de JKSV.
// Sûreté : vérification d'abord (rien n'est écrit) ; une sauvegarde Eden plus récente que celle
// de la Switch n'est remplacée qu'avec `force_older` ; copie de sécurité ; remplacement du dossier
// seulement une fois la nouvelle version entièrement écrite et vérifiée.
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "backup/backup.hpp"
#include "core/json.hpp"
#include "store/store.hpp"

namespace pbc::saves {

struct EdenProfile { std::string root, folder, name; };  // root : /data/prosperoeden ; folder : 32 hexa
// Profils de chaque Eden installé (fichier profiles.dat), plus le profil « Appareil » (sauvegardes communes).
std::vector<EdenProfile> eden_profiles(const std::vector<std::string>& roots);

struct SwitchImportRequest {
    std::string game, file;          // /JKSV/<game>/<file>
    std::string title_id;            // jeu Switch (16 hexa)
    std::string eden_root, profile;  // dossier Eden, dossier du profil (32 hexa)
    std::string state_dir;
    bool check_only = false, force_older = false;
    std::vector<std::string> eden_apps;  // titres de l'émulateur : l'import attend qu'il soit fermé
};

struct SwitchImportResult { bool ok = false; std::string error, code; Json report = Json::object(); };

SwitchImportResult import_switch(store::Store& src, const SwitchImportRequest& req, const backup::ProgressFn& progress, const std::atomic<bool>& stop);

// Eden → Switch : la sauvegarde du profil est mise dans un zip JKSV, déposé dans /JKSV/<jeu>/ sur la Switch
// sous un nom neuf (« <profil> - AAAA-MM-JJ_HH-MM-SS PBC.zip ») : rien n'est écrasé ni effacé.
// On la restaure ensuite sur la Switch avec JKSV.
struct SwitchExportRequest {
    std::string eden_root, profile;  // dossier Eden, dossier du profil (32 hexa)
    std::string title_id;
    std::string game_folder;         // dossier JKSV du jeu (existant de préférence)
    std::string profile_name;        // nom du profil côté Switch
    bool check_only = false;
};
SwitchImportResult export_switch(store::Store& dst, const SwitchExportRequest& req, const std::atomic<bool>& stop);

// Date d'une sauvegarde JKSV d'après son nom (0 si illisible).
std::int64_t jksv_time(const std::string& file);

}  // namespace pbc::saves
