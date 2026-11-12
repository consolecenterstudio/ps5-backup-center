#pragma once
// Une autre PS5 jumelée comme destination de backup (liaison, docs/PROTOCOLE-link.md).
// Les chemins vus par le moteur de sauvegarde sont relatifs au dossier « PBC Backup » d'un disque de l'autre console
// (« /<bundle>/… ») ; l'autre console refuse tout chemin qui sortirait de ce dossier.
#include <memory>
#include <string>

#include "store/store.hpp"

namespace pbc::link { class Link; }

namespace pbc::store {

// id = « peer:<instance>:<identifiant du disque chez l'autre> ».
bool parse_peer_id(const std::string& id, std::string& instance, std::string& place);
std::unique_ptr<Store> peer(link::Link& link, const std::string& instance, const std::string& place, const std::string& label);

}  // namespace pbc::store
