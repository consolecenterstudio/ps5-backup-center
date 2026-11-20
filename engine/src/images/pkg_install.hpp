// Installer un paquet .pkg par l'installeur de la console (lot 3, cas 3 de docs/maquettes/installer.html) : appel direct de
// sceAppInstUtilInstallByPackage, sans serveur ni port — la console recopie le jeu dans son stockage, comme un jeu du Store.
#pragma once
#include <string>

namespace pbc::pkginst {

// L'installeur est-il là ? (note : comment il a été trouvé, ou pourquoi pas). Sur le Mac : jamais.
bool available(std::string& note);
// Demande l'installation ; la console la fait ensuite en arrière-plan (sa propre notification). Chemin console.
bool install(const std::string& path, const std::string& content_id, const std::string& title, std::string& error);
// Désinstaller un jeu (sceAppInstUtilAppUnInstall) : pour les jeux .pkg inscrits par Kraken (notre version intégrée).
bool uninstall(const std::string& title_id, std::string& error);

}  // namespace pbc::pkginst
