// Mises à jour (façon Orbit) : rechercher, télécharger, installer, redémarrer.
//
// Source : une adresse http:// vers un manifeste JSON (servi par le Mac pour l'instant) :
//   {"version":"2.0.1","build":"…","notes":["…"],"files":[{"path":"eboot.bin","size":n,"sha256":"…"}]}
// Chaque fichier est à côté du manifeste, dans files/<path>.
//
// Sûreté : seuls les fichiers différents sont téléchargés, dans un dossier d'attente, et vérifiés
// (taille + SHA-256) avant de toucher à l'app. L'installation remplace fichier par fichier en gardant
// l'ancien ; au moindre échec, tout est remis en place. version.json est écrit en dernier.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/json.hpp"

namespace pbc {

class Updater {
public:
    // state_dir : /data/pbc-backup-center ; app_dir : dossier de l'app installée (/data/homebrew/PPSA99950).
    Updater(std::string state_dir, std::string app_dir, std::string running_version);
    ~Updater();

    std::string auto_source() const;
    void set_source(const std::string& url);
    Json status() const;
    std::uint64_t revision() const { return revision_.load(); }

    bool check(std::string& error);    // lance la recherche en arrière-plan
    // Recherche automatique (démarrage du moteur, ouverture de l'appli télé ou web, Paramètres, toutes les heures) :
    // rien si une opération est en cours, si une mise à jour attend le redémarrage, ou si la dernière recherche date de
    // moins de `min_age_s` secondes. Sans bruit : une source injoignable ne passe pas l'état en erreur.
    bool check_auto(int min_age_s);
    // Appelé (fil de la recherche) quand une version est trouvée pour la première fois : notification système.
    std::function<void(const std::string& version)> on_available;
    // Recherche depuis une source donnée une seule fois (une autre PS5 qui sert sa version), sans toucher au réglage.
    bool check_from(const std::string& url, std::string& error);
    // Cette console comme source pour une autre : manifeste de l'app installée (empreintes en cache) et ses fichiers.
    Json self_manifest();
    bool self_file(const std::string& rel, std::string& out);
    void note_received_from(const std::string& name);  // « reçue de PS5-Salon » (mise à jour poussée par une autre PS5)
    bool install(std::string& error);  // télécharge puis installe en arrière-plan
    // Revenir à la version d'avant la dernière installation (gardée dans <état>/update-previous : ses fichiers remplacés
    // et sa liste complète). Même chemin qu'une mise à jour : vérifiée, réversible. La version quittée n'est plus
    // proposée par les recherches automatiques (ni notification, ni installation automatique) ; une recherche à la
    // main la montre toujours.
    bool rollback(std::string& error);
    std::string previous_version() const;   // version disponible pour « Revenir » (vide : aucune)
    // « Versions disponibles » : versions.json à côté de latest.json sur la source réglée (dépôt de versions).
    // {"versions":[{version,date,title,tested,manifest}], "recommended", "installed"} ; erreur si la source n'en a pas.
    Json versions(std::string& error);
    // Installer une version de la liste (plus récente ou plus ancienne) : même chemin vérifié et réversible ; si elle
    // est plus ancienne que celle installée, celle-ci est mise de côté (pas reproposée seule), comme « Revenir ».
    bool install_version(const std::string& version, std::string& error);
    bool skipped(const std::string& version) const;
    // Recharge le moteur installé : le nouveau prend la place de celui-ci (ses tâches reprennent).
    bool restart_engine(std::string& error);
    // Relance l'app télé après `delay_s` secondes (le temps qu'elle se ferme).
    bool relaunch_app(int delay_s, std::string& error);
    // Version installée sur la console (version.json de l'app).
    std::string installed_version() const;
    // Historique des versions installé avec l'app (changelog.json).
    Json changelog() const;

private:
    struct File { std::string path, sha256; std::uint64_t size = 0; };
    void run_check();
    void run_install();
    void set_state(const std::string& state, const std::string& message = "");
    bool busy() const;
    void join();

    std::string dir_, app_dir_, running_;
    mutable std::mutex mu_;
    std::string configured_;  // réglage de l'utilisateur (vide : recherche automatique)
    std::string source_;
    std::string state_ = "idle";  // idle, checking, up_to_date, available, downloading, installing, installed, error
    std::string message_;
    std::string restart_error_;  // relance du moteur pas prise (45 s sans nouveau moteur)
    Json manifest_;
    std::vector<File> changed_;
    std::uint64_t total_ = 0, done_ = 0;
    unsigned file_index_ = 0;          // fichier en cours de téléchargement (1…n)
    std::string current_file_;
    std::int64_t dl_started_ms_ = 0;   // début du téléchargement (vitesse, temps restant)
    std::int64_t checked_at_ = 0;
    std::int64_t attempted_at_ = 0;  // dernière recherche lancée (réussie ou non)
    bool quiet_ = false;             // recherche automatique en cours
    std::string quiet_prev_state_, quiet_prev_message_;
    bool engine_changed_ = false, app_changed_ = false;
    std::thread worker_;
    std::mutex self_mu_;
    Json self_cache_;              // manifeste servi (recalculé si la version installée change)
    std::string self_cache_key_;
    std::string received_from_;
    std::string announced_;
    bool rollback_pending_ = false;   // la recherche en cours est un retour en arrière : installer aussitôt
    std::string skipped_;
    std::string previous_;            // version gardée pour « Revenir » (lue au départ, mise à jour à chaque installation)             // version quittée par « Revenir » (<état>/update-skip)  // dernière version annoncée par notification (une seule fois par version)
    std::atomic<std::uint64_t> revision_{1};
};

}  // namespace pbc
