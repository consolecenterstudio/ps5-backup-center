// File de tâches persistante. Chaque tâche est un fichier JSON (état, demande,
// progression) plus un journal de reprise. Au démarrage, une tâche « en cours »
// redevient « en attente » et reprend là où elle s'était arrêtée.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include "backup/backup.hpp"
#include "core/json.hpp"

namespace pbc::tasks {

enum class State : std::uint8_t { queued, running, paused, done, failed, cancelled };
// image : opérations sur les images de jeux (compress, uncompress, make_image, transfer…), le type exact
// exposé est celui de la demande (request.op, voir images::task_type).
enum class Type : std::uint8_t { backup, restore, restore_save, import_switch, export_switch, image, inspect_save, files };
const char* to_string(State);

struct Task {
    std::string id;
    Type type = Type::backup;
    State state = State::queued;
    std::string title;            // pour l'affichage (titre de l'élément)
    std::string error;            // raison lisible si échec
    std::int64_t created = 0, started = 0, finished = 0;
    backup::Progress progress;
    double bytes_per_second = 0;
    Json request;                 // demande complète (élément, composants, destination…)
    std::string bundle;           // résultat : dossier .pbck
    std::string code;             // code d'échec lisible par l'interface (ex. « account_mismatch »)
    Json report;                  // compte rendu (restauration de sauvegarde)
    Json to_json(bool with_request = false) const;
};

class Queue {
public:
    // `dir` : dossier hôte où vivent les fichiers de tâches.
    explicit Queue(std::string dir) : dir_(std::move(dir)) {}
    ~Queue() { stop(); }

    bool load();     // relit les tâches ; « en cours » → « en attente »
    void start();    // lance le travailleur
    void stop();     // interrompt proprement la tâche en cours (elle reprendra au prochain démarrage)

    std::string add_backup(const backup::Request& req);
    std::string add_restore(const backup::RestoreRequest& req, const std::string& title);
    // Restauration d'une sauvegarde PS5 sur un compte (via Garlic) : bundle, user_id, title_id, allow_resign, state_dir.
    std::string add_restore_save(const Json& request, const std::string& title);
    std::string add_import_switch(const Json& request, const std::string& title);
    std::string add_export_switch(const Json& request, const std::string& title) { return add_typed(Type::export_switch, request, title); }
    // Opération sur une image de jeu (demande : images::Options::to_json()).
    std::string add_image(const Json& request, const std::string& title) { return add_typed(Type::image, request, title); }
    // Comparaison des versions d'une sauvegarde PS5 par leur contenu (saves/inspect.hpp).
    std::string add_inspect_save(const Json& request, const std::string& title) { return add_typed(Type::inspect_save, request, title); }
    // « Fichiers » (Transferts) : {src_place, src_dir, names[], dst_place, dst_dir, move} — voir files/files.hpp.
    std::string add_files(const Json& request, const std::string& title) { return add_typed(Type::files, request, title); }
    bool pause(const std::string& id);
    bool resume(const std::string& id);
    bool cancel(const std::string& id);
    bool remove(const std::string& id);   // seulement une tâche terminée/échouée/annulée
    // « Vider la liste » : retire toutes les tâches de cet état (done, failed ou cancelled). Une copie « Fichiers »
    // dont les fichiers copiés peuvent encore être retirés reste (sinon on perdrait « Retirer les fichiers copiés »).
    bool clear(const std::string& state, int& removed, int& kept);
    // « Fichiers » annulé ou en échec : retirer ce que cette copie a créé (journal), en arrière-plan.
    bool cleanup_files(const std::string& id, std::string& error);

    std::vector<Task> list() const;
    // Sans copier toute la liste (146 tâches et leurs rapports : ~750 Ko) : la tâche en cours (id, titre), et s'il y a
    // du travail (en cours ou en attente). Pour /status, appelé chaque seconde par l'appli télé.
    bool running(std::string& id, std::string& title) const;
    bool has_work() const;
    bool get(const std::string& id, Task& out) const;
    std::uint64_t revision() const { return revision_.load(); }

    // Appelé après chaque sauvegarde réussie (mise à jour de l'index, notification…).
    std::function<void(const Task&)> on_finished;
    // Ouvre un appareil réseau enregistré (FTP…) pour une tâche qui le vise.
    std::function<std::unique_ptr<store::Store>(const std::string& remote_id, std::string& err)> open_remote;
    // Ouvre n'importe quel endroit pour « Fichiers » (disque d'ici, NAS / Switch, autre PS5) ; fourni par le moteur.
    std::function<std::unique_ptr<store::Store>(const std::string& place_id, std::string& err)> open_place;

private:
    std::string add_typed(Type type, const Json& request, const std::string& title);
    void worker();
    void run_one(Task& t);
    bool save(const Task& t);
    Task* find(const std::string& id);
    std::string task_path(const std::string& id) const;
    std::string journal_path(const std::string& id) const;

    std::string dir_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::vector<Task> tasks_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> interrupt_{false};   // arrête la tâche en cours (pause, annulation, arrêt)
    std::string running_id_;
    std::atomic<std::uint64_t> revision_{1};
};

}  // namespace pbc::tasks
