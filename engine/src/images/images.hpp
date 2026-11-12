// Images de jeux PS5 : compression .ffpfsc, décompression, création / extraction
// d'images .exfat / .ffpfs, validation (et réparation), test de vitesse de lecture,
// index AMPR (APR-EMU), réglages ShadowMountPlus.
//
// Adapté de engine/third_party/game-compressor — PS5 Game Compressor v1.0.4,
// juma-sayeh, GPL-3.0 (crédité dans l'app). Le code de calcul d'origine (pfs_*.c,
// miniz, index AMPR) est compilé tel quel dans le moteur ; ce module en est
// l'orchestration (garde-fous, place libre, mode sûr, vérification, rapport),
// réécrite pour le moteur : pas de serveur ni de port à part, tout passe par
// les tâches et l'API HTTP du moteur.
//
// Tous les chemins reçus et rendus sont des chemins console (« /data/… », « /mnt/usb0/… »).
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::images {

// Nature d'un chemin : dossier d'application (dump décompressé), image exFAT, image PFS, conteneur
// compressé, paquet .ffpkg (monté par ShadowMountPlus), installation native de la PS5 (/user/app/<ID>/app.pkg).
enum class Kind : std::uint8_t { unknown, folder, exfat, ffpfs, ffpfsc, ffpkg, native };
// Comment le jeu vit sur la console : « native » (installé par le système), « image » (.ffpfsc / .exfat /
// .ffpfs / .ffpkg montée par ShadowMountPlus), « folder » (dump décompressé, monté par ShadowMountPlus).
const char* storage_of(Kind);
const char* container_of(Kind);  // « ffpfsc », « exfat », « ffpfs », « ffpkg » ou ""
const char* to_string(Kind);
Kind kind_of(const std::string& console_path);  // d'après le disque et l'extension

struct Info {
    std::string path;            // chemin console
    Kind kind = Kind::unknown;
    std::string title_id;
    std::string title;           // nom (param.json) si connu
    std::uint64_t size = 0;      // place occupée (dossier : somme des fichiers ; fichier : taille)
    std::uint64_t data_size = 0; // contenu : dossier = size ; image = taille de l'image ; .ffpfsc = image imbriquée
    std::string inner;           // .ffpfsc : « exfat » | « pfs »
    std::string inner_name;      // .ffpfsc : nom de l'image imbriquée (PPSA01234.exfat, pfs_image.dat)
    bool has_hashes = false;     // .ffpfsc : empreintes de validation (<image>.vhash) présentes
    bool apr_indexed = false;    // dossier : ampr_emu.index présent
    bool mounted = false;        // contenu monté en ce moment (point de montage actif)
    bool registered = false;     // inscrit sur la console : /user/app/<ID>/mount.lnk ou mount_img.lnk vers ce chemin (natif : toujours)
    std::string mount_path;      // contenu monté (si connu)
    bool running = false;        // le jeu tourne en ce moment
    Json validation;             // dernier contrôle connu (null si jamais)
    Json to_json() const;
};
// `measure` : additionne la taille d'un dossier (peut être long sur un gros jeu).
bool inspect(const std::string& console_path, Info& out, std::string& error, bool measure = true);

// Liste des jeux en dossier et des images trouvés aux emplacements habituels
// (mêmes racines que Game Compressor : /data/homebrew, /data/etaHEN/games, USB…)
// et inscrits par ShadowMountPlus (/user/app/*/mount_img.lnk).
std::vector<Info> discover(bool measure_folders = true);

enum class Op : std::uint8_t { compress, uncompress, make_image, extract_image, validate, read_speed, ampr_index, set_read_only, transfer };
const char* to_string(Op);
bool parse_op(std::string_view, Op&);  // « compress », « uncompress »… (= type de tâche, voir task_type)
const char* task_type(Op);              // « compress », « uncompress », « make_image », « extract_image »,
                                        // « validate_image », « read_speed_test », « build_ampr_index », « set_read_only », « transfer »
bool parse_task_type(std::string_view, Op&);

struct Options {
    Op op = Op::compress;
    std::string source;          // chemin console de la source
    std::string target_dir;      // dossier de destination (vide : à côté de la source)
    std::string output;          // chemin de sortie exact (facultatif, prioritaire)
    int level = 6;               // compress : niveau zlib 0-9
    std::string inner = "exfat"; // compress / make_image : « exfat » (recommandé) | « pfs » (expérimental)
    std::string to = "folder";   // uncompress : « folder » | « exfat » (image imbriquée telle quelle)
    bool delete_source = false;  // mode sûr : supprime la source après vérification complète
    bool destructive = false;    // compress d'un dossier seulement : supprime pendant l'écriture (≥ 1 Gio libre)
    bool repair = false;         // validate : répare les blocs fautifs (image montée)
    bool shadowmount_hints = true;  // écrit les réglages ShadowMountPlus (secteur 64 Kio, lecture seule)
    bool mount = false;          // demande à ShadowMountPlus de monter le résultat (manual.lst)
    int seconds = 20;            // read_speed : durée
    std::string mode = "copy";   // transfer : « copy » | « move » (l'original n'est supprimé qu'après vérification)
    bool verify = true;          // transfer : relecture SHA-256 de chaque fichier copié (toujours faite pour « move »)
    std::string previous;        // version installée mise de côté (mise à jour d'une appli) : supprimée si tout va bien, remise sinon
    bool compress = false;       // transfer : compresse en .ffpfsc vers la destination au lieu de copier
    static bool from_json(const Json& body, Options& out, std::string& error);
    Json to_json() const;
};

struct Plan {
    bool ok = false;
    std::string error, code;     // code : not_found, unsupported, no_space, running, mounted, output_exists…
    std::string title_id;
    std::string output;          // chemin console prévu
    std::uint64_t source_bytes = 0;
    std::uint64_t required_bytes = 0;  // place libre nécessaire sur la destination
    std::uint64_t free_bytes = 0;
    bool estimate = false;       // besoin estimé (calcul exact pendant la tâche)
    Json to_json() const;
};
Plan plan(const Options&);

struct Progress {
    std::uint64_t bytes_done = 0, bytes_total = 0;
    unsigned files_done = 0, files_total = 0;
    std::string step;            // étape lisible : « Compression », « Vérification »…
    std::string current;         // détail
};
struct Result {
    bool ok = false;
    std::string error, code;
    std::string output;
    Json report;                 // compte rendu (tailles, blocs, vérification, suppression…)
};
// Exécute l'opération (une seule à la fois dans le moteur). `interrupt` : pause / annulation —
// ignorée une fois la suppression destructive commencée (comme Game Compressor).
Result run(const Options&, const std::function<void(const Progress&)>& progress, std::atomic<bool>& interrupt);

// --- garde-fous et réglages -----------------------------------------------------------
// Dossier d'état (chemin console) : mémoire des validations.
void set_state_dir(const std::string& console_dir);
// Title ID du jeu en cours d'exécution ("" si aucun) ; remplaçable pour les tests.
std::string running_title();
// Application ou jeu au premier plan (app_id système et Title ID) ; faux si rien n'est ouvert.
bool foreground_app(int& app_id, std::string& title_id);
// Fermer une application (par son app_id) ; 0 si accepté.
int kill_app(int app_id);
void set_running_title_probe(std::function<std::string()> probe);
// Image / dossier monté par ShadowMountPlus (et chemin du contenu monté).
bool mounted(const std::string& console_path, Kind kind, const std::string& title_id, std::string* mount_path = nullptr);
// Dossier de jeux d'un stockage (règle de Game Compressor) : /data → /data/homebrew, /mnt/usbN → /mnt/usbN/homebrew.
std::string games_root(const std::string& storage_mount);
// Inscrire une image déjà en place (copie identique déjà faite) : réglages ShadowMountPlus puis demande de montage.
bool register_image(const std::string& title_id, const std::string& console_path, std::string& err);
// Après une demande de montage : raison du report par ShadowMountPlus (« game_active »…), vide si immédiat ou inconnu.
std::string last_mount_deferred();
// Inscrire un jeu en dossier déjà en place (manual.lst + scan demandé à ShadowMountPlus).
bool register_folder(const std::string& title_id, const std::string& console_dir, std::string& err);
// Désinstallation (API de ShadowMountPlus 1.7, docs/api.md) :
// sources de ce jeu dans manual.lst ; retrait d'une source de la liste (sinon un scan le réinstalle) ;
// demande de désinstallation au système (asynchrone : le jeu disparaît ensuite de /user/app).
std::vector<std::string> manual_sources(const std::string& title_id);
std::vector<std::string> smp_game_sources(const std::string& title_id);  // chemins connus de ShadowMountPlus pour ce jeu
bool smp_manual_add(const std::string& source);                         // remettre une source dans la liste
bool smp_manual_remove(const std::string& source, std::string& err);
bool smp_uninstall(const std::string& title_id, std::string& err);

}  // namespace pbc::images
