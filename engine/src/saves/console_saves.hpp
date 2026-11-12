// Sauvegardes PS5/PS4 de chaque compte de la console, rattachées à la bibliothèque :
// composant « Sauvegarde · <compte> » sur le jeu installé, ou élément « sauvegarde seule »
// quand le jeu n'est plus installé (nom lu dans les métadonnées, icône de la sauvegarde).
//
// Copie brute des images chiffrées (sdimg_*) et de leurs métadonnées : rien n'est déchiffré
// ni modifié ici. Le compte (utilisateur local, identifiant, nom) est enregistré dans le
// composant pour la restauration et le portage vers un autre compte.
#pragma once
#include <vector>

#include <map>

#include "accounts/accounts.hpp"
#include "core/appdb.hpp"
#include "model/model.hpp"

namespace pbc::saves {

// `apps` : base des applications (nom, jeu ou application) pour les titres sans dossier lisible.
void attach(std::vector<Item>& items, const std::vector<accounts::Account>& accounts, const std::map<std::string, appdb::Info>& apps = {});

// Titre lu dans un fichier .sfo (PSF) : MAINTITLE, sinon TITLE. Vide si illisible.
std::string sfo_title(const std::string& data);

}  // namespace pbc::saves
