// Jaquettes manquantes des ROM, téléchargées depuis la base libre libretro-thumbnails
// (http://thumbnails.libretro.com/<système>/Named_Boxarts/<nom>.png), en arrière-plan.
//
// - Liste des noms de chaque système gardée en cache (30 jours) : correspondance approchée
//   (sans les parenthèses de région ni la ponctuation), région du fichier préférée.
// - Images enregistrées dans <état>/covers-dl/ ; un nom introuvable n'est réessayé qu'après 7 jours.
// - Jamais bloquant : le scan se termine sans attendre, les jaquettes arrivent ensuite.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pbc::covers {

// Système libretro pour une plateforme du catalogue (« Nintendo 64 » → « Nintendo - Nintendo 64 »), vide si inconnu.
std::string libretro_system(const std::string& platform);
// Nom simplifié pour comparer : minuscules, sans (…) ni […], ponctuation réduite, « , The » remis devant.
std::string normalize(const std::string& name);
// Meilleur nom de la liste pour `wanted` (vide si aucun) ; région du nom demandé préférée.
std::string best_match(const std::string& wanted, const std::vector<std::string>& names);

class Fetcher {
public:
    explicit Fetcher(std::string state_dir) : dir_(std::move(state_dir)) {}
    ~Fetcher() { stop(); }
    void start();
    void stop();
    void set_enabled(bool on) { enabled_ = on; }
    // Jaquette déjà téléchargée pour cette ROM (chemin console), sinon vide et la demande est mise en file.
    std::string lookup(const std::string& key, const std::string& platform, const std::string& title);
    std::function<void()> on_found;  // appelé (fil de téléchargement) quand de nouvelles jaquettes sont prêtes

private:
    struct Want { std::string key, system, title; };
    void loop();
    bool names_for(const std::string& system, std::vector<std::string>& out);
    void save_state();
    std::string official(const std::string& system, const std::string& tid);
    std::string dir_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Want> queue_;
    std::map<std::string, std::string> found_;        // clé ROM → chemin console
    std::map<std::string, std::int64_t> missed_;      // clé ROM → date du dernier échec
    std::map<std::string, std::vector<std::string>> lists_;
    std::thread thread_;
    std::atomic<bool> quit_{false}, enabled_{true};
    bool loaded_ = false;
};

}  // namespace pbc::covers
