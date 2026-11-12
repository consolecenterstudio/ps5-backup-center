// Noms de jeux par leur numéro, pour ce qui n'est pas installé sur cette PS5 (sauvegarde seule,
// sauvegarde d'Eden…). Tables livrées avec l'app : assets/ps-titles.tsv (PS4 / PS5) et
// assets/switch-titles.tsv (Switch), faites au build (build-system/make_*_titles.py).
#pragma once
#include <string>

namespace pbc::titles {

void set_app_dir(std::string app_dir);   // dossier de l'app installée (une fois, au démarrage)
std::string name(const std::string& tid);  // vide si inconnu ; PPSA…/CUSA… ou numéro Switch (16 chiffres hexa)
// Où trouver l'image officielle : numéro « concept » du PlayStation Store (PS4 / PS5), ou adresse
// https de l'icône sur le CDN Nintendo (Switch). Vide si inconnu.
std::string ps_concept(const std::string& tid);
std::string switch_icon_url(const std::string& tid);
// Numéro d'un jeu PS4 / PS5 par son nom exact (sans tenir compte des majuscules) ; prefix « PPSA » ou « CUSA ».
// Pour une ancienne sauvegarde rangée sous le seul nom du jeu, quand le jeu n'est pas installé ici. Vide si inconnu.
std::string ps_by_name(const std::string& title, const std::string& prefix);

}  // namespace pbc::titles
