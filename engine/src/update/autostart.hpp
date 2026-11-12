#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"

// Démarrage du moteur : quels chargeurs d'ELF sont présents sur la console (l'appli télé s'en sert, dans l'ordre choisi,
// quand le moteur ne tourne pas), et l'option « Démarrer avec la console » (Payload Manager lance notre moteur à
// l'allumage). Une fois lancé, le moteur se relance seul (update/self_launch.hpp) : rien d'externe.
namespace pbc::autostart {

// Chargeurs connus, dans l'ordre par défaut.
const std::vector<std::string>& known_launchers();       // « elfldr » (chargeur du débridage)
std::vector<std::string> clean_order(const Json& order);  // identifiants connus, sans doublon (vide : ordre par défaut)

// Fichier lu par l'appli télé même quand le moteur est arrêté : <état>/launchers.txt (un identifiant par ligne).
void write_launchers_file(const std::string& state_dir, const std::vector<std::string>& order);

// État : chargeurs (présent ou non, ordre, activé) et Payload Manager (présent, son démarrage auto, notre entrée).
Json status(const std::string& app_dir, const std::vector<std::string>& order);

// Ajouter / retirer notre moteur du démarrage de Payload Manager. Ajouter : copie du moteur installé dans SON dossier
// (il ne lance que ce qui est rangé chez lui), puis une ligne dans sa liste ; ses autres lignes ne bougent pas, et
// une copie de sa liste d'avant est gardée dans <état>. Retirer : notre ligne et notre dossier seulement.
bool set_pldmgr(bool on, const std::string& app_dir, const std::string& state_dir, std::string& error);

// Après une mise à jour (et au démarrage) : si l'option est active, la copie chez Payload Manager suit le moteur installé.
void refresh_pldmgr_copy(const std::string& app_dir);

}  // namespace pbc::autostart
