// Alimentation de la console : redémarrer, mode repos, éteindre (fonctions du système, libSceSystemService).
// Remplace Arsenal pour les redémarrages ; base de la commande à distance (PS5 Center).
#pragma once
#include <string>

namespace pbc::power {

// "reboot", "rest" ou "off". Exécuté après `delay_ms` (le temps de répondre à la requête), dans un fil à part.
// Faux (avec `error`) si l'action est inconnue ou indisponible ; sur le Mac : seulement écrit dans le journal.
bool request(const std::string& action, int delay_ms, std::string& error);

}  // namespace pbc::power
