// Sauvegarde complète de la console : ce qui appartient à l'utilisateur, par catégorie
// (comptes, sauvegardes, inscriptions des jeux, images restées sur l'interne, homebrews et
// leurs données, réglages de démarrage, captures, système en lecture seule « expert »).
// Chaque catégorie devient un composant d'un élément « Cette PS5 » : la sauvegarde passe par
// la même mécanique que les autres (empreintes, journal, reprise, NAS).
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "model/model.hpp"

namespace pbc::full {

// Identifiants des catégories, dans l'ordre de restauration.
const std::vector<std::string>& categories();

// Tailles par catégorie : {"categories": {id: {bytes, files, detail}}}. Parcourt les dossiers
// (sans suivre les liens) : à appeler hors du fil HTTP.
Json plan(const std::vector<Item>& items);

// Élément synthétique à sauvegarder (une composante par catégorie cochée). Faux si rien à copier.
bool make_item(const std::vector<std::string>& cats, const std::vector<Item>& items, Item& out, std::string& error);

}  // namespace pbc::full
