// Sauvegarde et restauration. Un seul passage par fichier : lecture, empreinte et
// écriture ensemble. Chaque fichier terminé est noté dans le journal de la tâche :
// une reprise saute ce qui est déjà fait, sans rien rehacher.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "model/model.hpp"
#include "store/store.hpp"

namespace pbc::backup {

struct Progress {
    std::uint64_t bytes_done = 0, bytes_total = 0;
    unsigned files_done = 0, files_total = 0;
    std::string current;  // fichier en cours (chemin relatif)
    // Étape à part, avec sa propre avancée (« Fichiers ») : "verify" = relecture de la copie, "compare" = un fichier
    // de même taille est déjà là, on compare les empreintes. Vide sinon.
    std::string phase;
    std::uint64_t phase_done = 0, phase_total = 0;
};
using ProgressFn = std::function<void(const Progress&)>;

// Journal d'une tâche : une ligne JSON par fichier terminé (append + fsync).
class Journal {
public:
    explicit Journal(std::string host_path) : path_(std::move(host_path)) {}
    bool load();                                   // relit les fichiers déjà faits
    bool done(const std::string& rel, std::uint64_t size, std::string* sha) const;
    bool record(const std::string& rel, std::uint64_t size, const std::string& sha);
    const std::string& path() const { return path_; }
private:
    struct Line { std::string rel, sha; std::uint64_t size = 0; };
    std::string path_;
    std::vector<Line> lines_;
};

struct Request {
    Item item;
    std::vector<Part> parts;      // vide = tous les composants
    std::string folder;           // dossier de sauvegarde (chemin console), ex. /mnt/usb0/PBC Backup
    std::string disk_id;          // témoin du disque de destination
    std::string bundle_name;      // fixé à la création ; réutilisé à la reprise
    store::Store* store = nullptr; // appareil de destination ; nul = disque de la console
    std::string remote_id;         // appareil réseau enregistré (ouvert par la tâche)
    int keep = 0;                  // rétention : versions gardées par élément (0 = toutes)
    // Rangement « par plateforme » : les images de sauvegarde vont directement dans le dossier
    // (« …/saves/<date>/sdimg_* »), le reste et le manifeste dans « _pbc/ ».
    bool flat = false;
};

// Rangement « par plateforme » (rangement classique d'un NAS).
// Dossier relatif d'une sauvegarde : « PS5 backup/[PS5] Nom (PPSA…)/saves/<AAAA-MM-JJ> ».
std::string platform_bundle(const Item& item, const std::string& date);
// Toutes les sauvegardes rangées ainsi sous `root` (y compris celles faites hors de l'app, sans
// manifeste : « legacy »), au même format que read_index().
Json scan_platform(store::Store& st, const std::string& root);

struct Result {
    bool ok = false;
    std::string bundle;           // chemin console du dossier .pbck
    std::uint64_t bytes = 0;
    unsigned files = 0, skipped = 0;
    std::string error;
};

// Nom de dossier lisible et unique : « <titre> - <composants> - <date> [<id>].pbck ».
std::string make_bundle_name(const Request& r);

Result run(Request& req, Journal& journal, const ProgressFn& progress, const std::atomic<bool>& stop);

// Index des sauvegardes d'un dossier « PBC Backup ». Reconstruit à partir des
// manifestes si absent ou abîmé.
Json read_index(const std::string& folder, bool rebuild_if_needed = true);
bool rebuild_index(const std::string& folder);
// Même chose sur un appareil réseau (FTP…).
Json read_index(store::Store& st, const std::string& folder, bool rebuild_if_needed = true);
bool rebuild_index(store::Store& st, const std::string& folder);

struct RestoreRequest {
    // (manifeste dans « _pbc/ » et fichiers « stored » pour une sauvegarde rangée par plateforme)
    std::string bundle;           // chemin du .pbck sur son appareil
    store::Store* store = nullptr; // appareil où se trouve le backup ; nul = disque de la console
    std::string remote_id;
    std::string item_key;          // élément d'origine (image dans les tâches)
    std::vector<Part> parts;      // vide = tout
    std::string target;           // vide = emplacements d'origine (restore_to) ; sinon un dossier neuf
    bool overwrite = false;       // false : refuse d'écraser un fichier existant
};
Result restore(const RestoreRequest& req, const ProgressFn& progress, const std::atomic<bool>& stop);

}  // namespace pbc::backup
