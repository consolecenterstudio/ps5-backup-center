// Identifiants de PS5 Backup Center chez Google et Microsoft (connexion par code). Ce ne sont pas des secrets :
// une appli installée sur un appareil ne peut pas en garder (Google le dit lui-même pour le type « TV et appareils à
// saisie limitée »). Vides tant que l'appli n'est pas déclarée : l'écran dit alors « pas encore disponible ».
#pragma once

namespace pbc::store::cloud {

inline constexpr const char* kGoogleClientId = "";
inline constexpr const char* kGoogleClientSecret = "";
inline constexpr const char* kMicrosoftClientId = "";
inline constexpr const char* kDropboxAppKey = "";

}  // namespace pbc::store::cloud
