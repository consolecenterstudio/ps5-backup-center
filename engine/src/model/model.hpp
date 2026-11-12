// Vocabulaire commun à tout le moteur : la vue est la même pour tous les types,
// seules les briques de reconnaissance savent où se trouvent les fichiers.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc {

// Ce qu'est l'élément, pour l'affichage et les filtres.
enum class Kind : std::uint8_t { game_ps5, game_ps4, app, rom };
// Ce qu'on peut sauvegarder séparément.
enum class Part : std::uint8_t { game, update, dlc, save, state, config, app, backport };

const char* to_string(Kind);
const char* to_string(Part);
bool parse_kind(std::string_view, Kind&);
bool parse_part(std::string_view, Part&);

// Un fichier ou un dossier à copier. `source` est un chemin console absolu ;
// `relative` est sa place dans la sauvegarde, sous data/<composant>/.
struct Source {
    std::string source;
    std::string relative;
    bool dir = false;
};

struct Component {
    Part part = Part::game;
    std::string label;          // « Jeu de base », « DLC : Pack 1 »… (affichage)
    std::vector<Source> sources;
    std::uint64_t bytes = 0;    // estimé au scan, exact après sauvegarde
    std::string restore_to;     // chemin console de restauration par défaut
    Json meta;                  // informations propres au composant (compte d'une sauvegarde PS5/PS4…)
};

// Un emplacement de stockage : interne, un disque USB… Reconnu par son témoin, pas par usbN.
struct Place {
    enum class Type : std::uint8_t { internal, usb };
    std::string id;       // identifiant stable : « internal » ou disk_id du témoin
    Type type = Type::internal;
    std::string mount;    // chemin console du moment : /data, /mnt/usb0…
    std::string label;    // « Interne », « USB 0 »…
    std::uint64_t free = 0, total = 0;
};

struct Item {
    std::string key;            // clé de dédoublonnage : « app:PPSA99950 », « rom:switch:0100…»
    Kind kind = Kind::app;
    std::string title;
    std::string title_id;       // PPSA…, ou identifiant de la ROM
    std::string platform;       // « PS5 », « Switch », « PSP »…
    std::string place_id;       // où se trouve le contenu principal
    std::string image_type;     // « dossier », « NSP », « XCI », « ISO », « CUE/BIN »…
    std::vector<std::string> apps;  // applications qui l'utilisent (émulateurs)
    std::string cover;          // chemin console d'une jaquette, si connue
    std::vector<Component> components;

    Json to_json() const;
    static bool from_json(const Json&, Item&);
};

}  // namespace pbc
