// Emplacements de stockage. Un disque est reconnu par son témoin `.pbck-disk`,
// pas par son numéro de montage, qui change selon le port et l'ordre de branchement.
#pragma once
#include <string>
#include <vector>

#include "model/model.hpp"

namespace pbc::places {

inline constexpr const char* marker_name = ".pbck-disk";
inline constexpr const char* default_folder = "PBC Backup";

// Interne (/data) + chaque /mnt/usbN monté et accessible. Lit le témoin s'il existe,
// sans en créer : un disque n'est marqué qu'au premier usage comme destination.
std::vector<Place> detect();

// Crée le témoin si absent et renvoie l'identifiant stable du disque.
bool ensure_marker(Place& place);

// Dossier de sauvegarde d'un emplacement : `custom` (chemin console) s'il est fourni,
// sinon « <montage>/PBC Backup ». Créé si besoin.
bool backup_folder(const Place& place, const std::string& custom, std::string& out_console_path);

const Place* find(const std::vector<Place>& all, const std::string& id);

}  // namespace pbc::places
