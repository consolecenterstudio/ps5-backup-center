// Base des applications de la console (/system_data/priv/mms/appinfo.db), en lecture seule :
// vrai nom de chaque titre et son type (jeu ou application). Sert aux sauvegardes dont le jeu
// n'a plus de dossier lisible, et à distinguer YouTube & co des jeux.
#pragma once
#include <map>
#include <string>

namespace pbc::appdb {

struct Info {
    std::string title;
    long category = -1;   // CATEGORY_TYPE : 0 = jeu, 65536 = application ; -1 = inconnu
    bool is_app() const { return category > 0; }
};

// Titre → informations. Vide si la base est absente ou illisible.
std::map<std::string, Info> read(const std::string& console_path = "/system_data/priv/mms/appinfo.db");

}  // namespace pbc::appdb
