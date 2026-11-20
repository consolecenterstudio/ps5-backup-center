// Base des applications de la console (/system_data/priv/mms/appinfo.db), en lecture seule :
// vrai nom de chaque titre et son type (jeu ou application). Sert aux sauvegardes dont le jeu
// n'a plus de dossier lisible, et à distinguer YouTube & co des jeux.
#pragma once
#include <map>
#include <string>

namespace pbc::appdb {

struct Info {
    std::string title;
    long category = -1;   // CATEGORY_TYPE : 0 = jeu PS5, 61440 = jeu PS4, 65536 = application ; -1 = inconnu
    long app_type = -1;   // APP_TYPE : 1 = jeu (PS5 et PS4), 5 = application (homebrews, YouTube…), 0 = outil système ; -1 = inconnu
    // Un jeu si APP_TYPE = 1 (vu sur la PS5-02 : Detroit, jeu PS4 sur disque, CATEGORY_TYPE 61440, était pris pour une appli) ;
    // sans APP_TYPE, l'ancienne lecture (CATEGORY_TYPE au-dessus de 0, sauf celle des jeux PS4).
    bool is_app() const { return app_type >= 0 ? app_type != 1 : category > 0 && category != 61440; }
};

// Titre → informations. Vide si la base est absente ou illisible.
std::map<std::string, Info> read(const std::string& console_path = "/system_data/priv/mms/appinfo.db");

}  // namespace pbc::appdb
