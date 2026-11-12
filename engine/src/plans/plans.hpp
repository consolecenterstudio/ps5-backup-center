// Plans de sauvegarde (comme l'ancienne app) : un nom, un calendrier, ce qu'on sauvegarde et où,
// et combien de versions garder. Le moteur crée les tâches à l'heure dite ; « Lancer maintenant »
// les crée tout de suite. Contenu par règle (« toutes les sauvegardes PS5 d'un compte », « les ROM
// Nintendo 64 »…) : les nouveaux jeux sont pris automatiquement.
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::plans {

enum class Timing { manual, once, daily, weekly };

struct Plan {
    std::string id, name;
    bool enabled = true;
    Timing timing = Timing::manual;
    int minute_of_day = 8 * 60;      // heure locale de la console
    unsigned weekdays = 0;           // bit 0 = lundi … bit 6 = dimanche
    std::int64_t once_at = 0;        // date unique (heure Unix)
    // Contenu : éléments précis et/ou règles.
    std::vector<std::string> items;  // clés d'éléments
    std::string rule;                // "", "ps5_saves", "all_games", "roms", "apps"
    std::string rule_arg;            // ex. compte (utilisateur) ou plateforme
    std::vector<std::string> parts;  // composants (vide = tout)
    std::string place;               // destination ("" = par défaut)
    int keep = 5;                    // versions gardées par élément (0 = toutes)
    std::int64_t last_run = 0, next_run = 0;
    std::string last_result;
    Json to_json() const;
    static Plan from_json(const Json&);
};

// Prochaine exécution après `after` (0 si manuel ou terminé). `utc_offset` : décalage local en secondes.
std::int64_t next_run(const Plan& p, std::int64_t after, std::int64_t utc_offset);

class Store {
public:
    explicit Store(std::string host_path) : path_(std::move(host_path)) {}
    void load();
    std::vector<Plan> list() const;
    bool get(const std::string& id, Plan& out) const;
    std::string put(Plan p);              // ajoute ou remplace ; renvoie l'id
    bool remove(const std::string& id);
    void mark_run(const std::string& id, std::int64_t when, const std::string& result, std::int64_t utc_offset);
    // Plans dus à `now` (activés, prochaine exécution passée).
    std::vector<Plan> due(std::int64_t now) const;
    void refresh_next(std::int64_t now, std::int64_t utc_offset);
private:
    bool save() const;
    std::string path_;
    mutable std::mutex mu_;
    std::vector<Plan> list_;
};

}  // namespace pbc::plans
