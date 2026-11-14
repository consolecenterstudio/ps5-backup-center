// Données de l'app télé : copie de l'état du moteur, synchronisée en arrière-plan.
// L'interface n'attend jamais le réseau : elle lit le dernier instantané et envoie ses
// commandes dans une file traitée par le fil de synchronisation.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/json.hpp"
#include "gfx.hpp"

namespace app {

struct Endpoint { std::string host = "127.0.0.1"; int port = 52370; std::string token; };

// Requête HTTP courte (une connexion par requête). Renvoie le code HTTP, 0 si injoignable.
int http(const Endpoint& ep, const std::string& method, const std::string& path, const std::string& body, std::string& out, int timeout_ms = 3000);

struct Component { std::string part, label; std::uint64_t bytes = 0; };
struct Item {
    std::string key, kind, title, title_id, platform, place, image_type;
    std::vector<std::string> apps;
    std::vector<Component> components;
    bool has_cover = false;
    std::string cover_ref;  // source de la jaquette côté moteur : quand elle change, l'image est relue
    std::string location;   // dossier du jeu sur la console (premier fichier sauvegardé), pour « Parcourir »
    bool ignored = false;   // ignoré : plus sauvegardé ni proposé (Paramètres › Éléments ignorés)
};
struct Place { std::string id, type, mount, label; std::uint64_t free = 0, total = 0; bool marked = false; std::string protocol, same_as, use; };  // same_as : synchro du même dossier du même NAS par un autre appareil ; use : « both », « backups » ou « sync »
struct Task {
    std::string id, type, state, title, error;
    std::uint64_t done = 0, total = 0;
    unsigned files_done = 0, files_total = 0;
    double speed = 0;
    pbc::Json report;  // compte rendu (vérification d'une synchronisation…)
    std::string item;  // élément concerné (pour son image)
    std::string step;  // étape en cours (compression, vérification…)
    std::string phase;  // « Fichiers » : "verify" (relecture de la copie) ou "compare", avec sa propre avancée
    std::uint64_t phase_done = 0, phase_total = 0;
    std::int64_t started = 0, finished = 0;  // heures (Unix) de début et de fin
    // Texte et fraction de l'étape à part (vide / -1 s'il n'y en a pas).
    std::string phase_text() const;
    double phase_fraction() const { return phase.empty() || !phase_total ? -1.0 : std::min(1.0, static_cast<double>(phase_done) / static_cast<double>(phase_total)); }
};
// État des mises à jour (copie de /status → update).
// Une version et ce qu'elle apporte (affichage à la façon d'Orbit).
struct ChangelogEntry { std::string version, date, title; std::vector<std::string> added, improved, fixed; };
struct Update {
    std::string state = "idle", message, source, installed, available, build, received_from;  // received_from : poussée par une autre PS5
    std::vector<std::string> notes;
    std::uint64_t bytes_total = 0, bytes_done = 0, bytes_per_second = 0, eta_seconds = 0;
    unsigned files = 0, file_index = 0;
    std::string current_file, previous, skipped;
    std::int64_t checked_at = 0;
    bool app_changed = false, engine_restart_needed = false;
    std::vector<ChangelogEntry> changelog;  // versions plus récentes que celle installée
};
struct EmulatorRoot { std::string path, platform, source; };
struct Emulator {
    std::string name;
    bool installed = false;
    std::vector<EmulatorRoot> roots;
    std::vector<std::string> title_ids;
    std::string id, platform;
    std::vector<std::string> config_files, manual_roms;  // réglages manuels (prioritaires)
    std::string manual_config, manual_saves;
    bool is_manual = false;
};

struct Snapshot {
    bool connected = false;        // dernière requête réussie
    bool ever_connected = false;
    std::int64_t revision = -1;
    std::string version, pair_code, default_place;
    bool update_auto_check = true;
    bool update_auto_install = false;
    std::string update_channel = "stable";  // « stable » ou « beta »
    std::string tv_seen_version;  // dernière version dont la télé a montré « Quoi de neuf »
    std::string theme;            // thème choisi (Paramètres › Général), vide : moteur sans thèmes
    bool sound_ui = true, music = false;  // son (Paramètres › Général)
    int volume = 70;
    std::string language = "auto";  // réglage de langue du moteur (« auto » = langue de la console)
    std::string system_language;    // langue de la console déjà signalée au moteur
    bool settings_loaded = false;
    std::vector<std::string> ignored;  // jeux ignorés (identifiants), Paramètres › Jeux ignorés
    Update update;
    std::int64_t started_at = 0;
    bool scanning = false;
    std::int64_t scanned_at = 0;
    std::vector<Item> items;
    std::vector<Place> places;
    std::vector<Task> tasks;
    std::vector<Emulator> emulators;
    std::string last_error;        // message de la dernière commande refusée
    std::uint64_t error_serial = 0;
};

class Model {
public:
    explicit Model(Endpoint ep) : ep_(std::move(ep)) {}
    ~Model() { stop(); }
    void start();
    void stop();
    std::shared_ptr<const Snapshot> snapshot() const { std::lock_guard<std::mutex> l(mu_); return snap_; }
    std::uint64_t serial() const { return serial_.load(); }   // change à chaque nouvel instantané

    // Commandes (asynchrones). `done` est appelé sur le fil de synchronisation avec l'erreur (vide si succès) et la réponse.
    void command(std::string method, std::string path, std::string body, std::function<void(const std::string& error, const std::string& body)> done = nullptr);

private:
    void loop();
    bool refresh(bool force);
    int misses_ = 0;  // échecs d'affilée de /status : « injoignable » seulement après 3
    Endpoint ep_;
    mutable std::mutex mu_;
    std::shared_ptr<const Snapshot> snap_ = std::make_shared<Snapshot>();
    std::atomic<std::uint64_t> serial_{1};
    struct Cmd { std::string method, path, body; std::function<void(const std::string&, const std::string&)> done; };
    std::deque<Cmd> cmds_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
};

// Jaquettes : téléchargées et réduites en vignettes fixes sur un fil dédié ; une image déjà
// affichée reste en mémoire (pas de clignotement ni de lettre qui alterne avec l'image).
class Covers {
public:
    static constexpr int size = 256;
    // `slots` vignettes de size×size, mémoire fournie par l'appelant (arène sur la console).
    Covers(Endpoint ep, std::uint32_t* memory, int slots) : ep_(std::move(ep)), mem_(memory), slots_(slots) {}
    ~Covers() { stop(); }
    void start();
    void stop();
    // Image prête, sinon demande son chargement et renvoie une image vide.
    Image get(const std::string& key);
    // Grande image (large×large) pour l'Accueil, le bandeau et la fiche : deux emplacements.
    static constexpr int large = 512;
    void set_large_memory(std::uint32_t* mem) { large_mem_ = mem; }  // 2 × large × large pixels
    Image get_large(const std::string& key);
    std::uint64_t serial() const { return serial_.load(); }
private:
    void loop();
    Endpoint ep_;
    std::uint32_t* mem_;
    int slots_;
    std::mutex mu_;
    std::condition_variable cv_;
    struct Slot { std::string key; bool ready = false; bool failed = false; std::uint64_t used = 0; std::uint64_t at = 0; };
    std::vector<Slot> table_;
    struct Big { std::string key; bool ready = false; std::uint64_t used = 0; };
    Big big_[2];
    std::uint32_t* large_mem_ = nullptr;
    std::deque<std::string> wanted_;
    std::uint64_t tick_ = 0;
    std::atomic<std::uint64_t> serial_{1};
    std::thread thread_;
    std::atomic<bool> quit_{false};
};

}  // namespace app
