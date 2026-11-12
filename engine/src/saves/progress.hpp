#pragma once
// Avancée dans une sauvegarde : ce que le jeu écrit lui-même (titre, sous-titre, détail) et ce qu'on en tire
// (pourcentage, niveau, temps de jeu), plus notre propre compteur de temps de jeu par jeu.
//
// Sauvegardes de la console : la PS5 tient la base /system_data/savedata_prospero/<utilisateur>/db/user/savedata.db
// (une ligne par sauvegarde : textes + date). On la lit telle quelle, sans monter les images (immutable=1 :
// aucun verrou, aucune écriture). Copies sur disques / NAS : mêmes textes, lus par l'inspection (saves/inspect).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::progress {

struct Parsed {
    double percent = -1;           // « (7.4%) »
    int level = -1;                // « Level: 12 », « Niveau 12 », « Nv. 12 », « Lv 12 »
    std::int64_t play_seconds = -1;  // « Temps de jeu 34:12 », « 12 h 05 », « … - 00:31 » (Cyberpunk)
};
Parsed parse(const std::string& text);

// Texte lisible d'une sauvegarde : titre, sous-titre, détail, sans les libellés génériques ni les répétitions.
std::string text_of(const std::string& main_title, const std::string& sub_title, const std::string& detail);

struct SaveText {
    std::string dir, text;
    std::int64_t mtime = 0;
    Parsed p;
};
// Sauvegardes de la console d'un utilisateur (identifiant hexadécimal, ex. « 1a2b3c4d »), par jeu.
std::map<std::string, std::vector<SaveText>> console(const std::string& user_id, bool ps4 = false);

// Résumé d'un jeu : la sauvegarde qui va le plus loin (temps de jeu, sinon pourcentage, sinon niveau, sinon la
// plus récente avec un texte), en JSON : text, percent, level, play_time_seconds, mtime, saves (nombre), by.
Json summary(const std::vector<SaveText>& saves);
Json to_json(const Parsed& p);

// Notre compteur de temps de jeu (à partir de son installation) : sessions du jeu en cours, par jeu.
class Tracker {
public:
    explicit Tracker(std::string path);
    void tick(const std::string& running_title, std::int64_t now);  // à appeler régulièrement (≈ 30 s)
    void save();
    std::int64_t seconds(const std::string& title_id) const;
    Json all() const;
private:
    std::string path_, current_;
    std::int64_t since_ = 0, last_save_ = 0;
    std::map<std::string, std::int64_t> total_;
    bool dirty_ = false;
};

}  // namespace pbc::progress
