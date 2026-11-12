// Notification système de la PS5 (le bandeau en haut à droite), envoyée par le moteur : utile quand l'appli est
// fermée ou qu'un jeu tourne. Sur le Mac (tests), seulement écrite dans le journal.
#pragma once
#include <string>
#include <vector>

namespace pbc::notify {

// `title` sur la première ligne, `detail` (facultatif) sur la seconde.
void send(const std::string& title, const std::string& detail = "");
// Même chose avec une icône donnée par son adresse (vide : icône du système).
void send_with_icon(const std::string& title, const std::string& detail, const std::string& icon_uri);


}  // namespace pbc::notify
