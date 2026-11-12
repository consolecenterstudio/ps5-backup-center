// Comptes de la console : utilisateurs locaux, leur nom, leur identifiant de compte (PSN ou
// activation hors ligne) et leurs sauvegardes PS5/PS4. Lecture seule.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::accounts {

struct Account {
    std::string user_id;      // utilisateur local, hexadécimal (dossier /user/home/<id>)
    std::string name;         // username.dat
    std::string account_id;   // 0x… (vide si inconnu)
    std::string account_source;  // "np", "offline" ou ""
    unsigned ps5_saves = 0, ps4_saves = 0;
    std::uint64_t saves_bytes = 0;
};

std::vector<Account> list();

// Registre de la console, LECTURE SEULE : les 16 emplacements de comptes (nom, identifiant, type « np », flags
// d'activation), comme les lisent OffAct / Prospero Manager / Garlic. Sert à comparer avant toute réactivation.
// Clés : base + (emplacement - 1) × 0x10000 ; nom 0x07800200, identifiant 0x07800500, flags 0x07800800, type 0x0780B007.
Json registry_slots();
Json to_json(const std::vector<Account>&);

}  // namespace pbc::accounts
