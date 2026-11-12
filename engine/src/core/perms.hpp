// Droits des fichiers d'un paquet d'appli ou de jeu (dossier avec sce_sys/param.json).
//
// Pourquoi : un transfert vérifié par SHA-256 peut arriver avec des fichiers identiques mais sans droit d'exécution
// (cas réel : Prospero Themes, eboot.bin et sce_module/libc.prx en 0666 sur la PS5 de destination →
// processSpawn() 0x80aa001a, errno 13). Le SHA-256 ne vérifie pas les droits : on les applique et on les RELIT.
//
// Politique (ciblée, jamais un chmod récursif aveugle) :
//   - dossiers du paquet : traversables (0777, comme la console les crée) ;
//   - exécutables et modules (eboot.bin, *.elf, *.self, *.prx, *.sprx, tout fichier sous sce_module/) : 0777 ;
//   - droits d'origine notés dans le manifeste d'une copie : remis tels quels (ils priment sur la règle par nom) ;
//   - tout le reste (données, sauvegardes, images) : pas touché.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pbc::perms {

// Ce fichier (chemin relatif au paquet) doit-il pouvoir s'exécuter ?
bool executable_name(std::string_view rel);

// Le dossier est-il un paquet d'appli ou de jeu (sce_sys/param.json) ?
bool is_app_folder(const std::string& host_root);

struct Report {
    int dirs = 0;                      // dossiers vus
    int exec_files = 0;                // exécutables et modules vus
    int fixed = 0;                     // droits corrigés
    std::vector<std::string> failed;   // « chemin : raison » — droits impossibles à appliquer ou à relire
    bool ok() const { return failed.empty(); }
    std::string summary() const;       // première erreur, lisible
};

// Applique la politique puis relit chaque droit touché. modes : chemin relatif → droits d'origine (manifeste).
Report apply_app(const std::string& host_root, const std::map<std::string, unsigned>& modes = {});

// Relecture seule (rien n'est modifié) : exécutables et modules exécutables, dossiers traversables.
Report check_app(const std::string& host_root);

}  // namespace pbc::perms
