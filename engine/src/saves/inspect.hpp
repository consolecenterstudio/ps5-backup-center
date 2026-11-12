// Comparaison des versions d'une sauvegarde PS5 par leur CONTENU (et non par la date du backup).
//
// Chaque version (sauvegarde en place sur la console, backup .pbck ou dossier par plateforme d'un disque,
// dossier d'un NAS) est d'abord copiée dans un dossier de travail <état>/inspect/<tâche>/ ; chaque image
// principale sdimg_* (jamais sdimg_sce_bu_*) y est montée par le cœur Garlic (saves/garlic_core.h), on lit son
// param.sfo (MAINTITLE, SUBTITLE, DETAIL, horodatage PARAMS) et l'empreinte SHA-256 de chaque fichier intérieur,
// puis on démonte et on efface la copie. La sauvegarde en place n'est jamais montée directement.
//
// Résultat par version : fichier le plus récent à l'intérieur des images (date réelle d'écriture par le jeu),
// empreinte de contenu (identique ⇔ même contenu, quel que soit le compte ou la date de copie), texte de
// progression écrit par le jeu. Les versions d'un backup ne changent jamais : elles sont gardées dans
// <état>/inspect-cache.json.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backup/backup.hpp"
#include "core/json.hpp"
#include "store/store.hpp"

namespace pbc::saves {

// Champs utiles d'un param.sfo (PSF) de sauvegarde.
struct ParamInfo {
    std::string title_id, dir_name, main_title, subtitle, detail;
    std::uint64_t account_id = 0;
    std::int64_t save_time = 0;   // PARAMS + 0x68 (secondes Unix, écrit par la console à chaque enregistrement)
    std::int64_t user_param = -1; // SAVEDATA_LIST_PARAM (entier libre du jeu, -1 si absent)
};
bool parse_param(const std::string& sfo, ParamInfo& out);

// Temps de jeu (secondes) lu au mieux dans le texte du jeu (SUBTITLE / DETAIL) : « 12:34:56 », « 34h »,
// « 34 h 12 min », « 34 hrs 5 mins », « 12 min », « Temps de jeu 34:12 » (h:mm seulement après un mot-clé
// « temps de jeu / play time / durée… »). -1 si rien de reconnu.
std::int64_t parse_play_time(const std::string& text);

// Fichier à l'intérieur d'une image montée (chemin relatif à la racine du montage).
struct InnerFile {
    std::string path;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    std::string sha256;
};
// Empreinte d'une image : SHA-256 de « chemin \t taille \t sha256 \n » triés par chemin.
std::string image_fingerprint(std::vector<InnerFile> files);

// Une version à partir des comptes rendus de ses images ({name, fingerprint, newest_inner_mtime, save_time,
// subtitle, detail, progress_text, play_time_seconds, bytes…}) : progress_text (textes du jeu, images les plus
// récentes d'abord), play_time_seconds (le plus grand des images, null si inconnu), save_time,
// newest_inner_mtime, bytes, fingerprint (SHA-256 des « nom:empreinte » triés par nom).
Json aggregate_version(const Json& images);
// Comparaison : identical_groups (tableaux d'indices des versions au contenu identique, groupes de 2 ou plus),
// distinct (contenus différents), newest (indice du fichier intérieur le plus récent, puis PARAMS ; -1),
// most_advanced {index, by:"play_time"|"inconnu", note} : le plus grand temps de jeu connu ; « inconnu »
// (index -1) si aucune version n'en donne — la date seule ne prouve rien (après un plantage, une sauvegarde
// plus récente peut être moins avancée).
Json compare_versions(const Json& versions);

// Monte (copies déjà faites, sous /data) chaque image principale, lit son param.sfo et ses fichiers, démonte.
// Renvoie le tableau des comptes rendus d'image ; erreur (et tableau vide) si une image ne peut être lue.
// Version du lecteur d'avancée (catalogue + lecteur générique). Une analyse notée avec une version plus ancienne
// est relue à la prochaine comparaison ; le résumé des backups compte ce jeu comme « pas encore comparé ».
constexpr int kReaderVersion = 3;
bool reader_outdated(const Json& images_or_version);

Json inspect_images(const std::vector<std::string>& image_paths, std::string& error,
                    const std::atomic<bool>* stop = nullptr, const backup::ProgressFn& progress = nullptr);

// Copie les images principales sdimg_* trouvées sous `dir` (jusqu'à 6 niveaux : .pbck, dossier daté…) dans
// `staging` (chemin console), avec la clé PS4 voisine « <dossier>.bin » si elle existe. `title_id` / `user_id`
// (facultatifs) : dans un .pbck, seulement data/save/ps5/<compte>/<jeu>/ (le compte demandé d'abord, si
// plusieurs) ; `allow_ps4` : à défaut, data/save/ps4/<compte>/<jeu>/. Renvoie les chemins des images copiées.
std::vector<std::string> fetch_images(store::Store& st, const std::string& dir, const std::string& staging,
                                      const std::string& title_id, const std::string& user_id,
                                      const backup::ProgressFn& progress, const std::atomic<bool>& stop, std::string& error,
                                      bool allow_ps4 = false);

struct InspectResult {
    bool ok = false;
    std::string error, code;  // code : « game_running », « no_images », « mount », « stopped »…
    Json report = Json::object();
};
using OpenRemote = std::function<std::unique_ptr<store::Store>(const std::string& remote_id, std::string& err)>;

// Tâche « inspect_save ». Demande : {title_id, user_id, state_dir, force?, sources:[{kind:"console"} |
// {kind:"local", path, place, bundle} | {kind:"remote", remote, path, place, bundle}]}.
InspectResult inspect_save(const std::string& task_id, const Json& request, const OpenRemote& open_remote,
                           const backup::ProgressFn& progress, const std::atomic<bool>& stop);

// Versions déjà analysées d'un jeu (cache), comparées entre elles (mêmes champs que le compte rendu).
// La version console est marquée « stale » si ses images ont changé depuis l'analyse.
Json cached_inspections(const std::string& state_dir, const std::string& title_id, const std::string& user_id);

// Identifiants de compte vus dans les sauvegardes déjà lues (cache) : pour chacun, nombre d'images, jeux, première et
// dernière écriture par le jeu, et empreintes de contenu par jeu (pour reconnaître deux identifiants d'un même joueur :
// même contenu sous deux identifiants = sauvegarde restaurée). {"<ID>": {images, first, last, titles: {tid: [fp…]}}}
Json account_stats(const std::string& state_dir);

}  // namespace pbc::saves
