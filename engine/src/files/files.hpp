// « Fichiers » (onglet Transferts) : copier ou déplacer n'importe quels fichiers ou dossiers d'un endroit à un autre
// (disques de cette PS5, de l'autre PS5, NAS, Switch), avec les garanties du reste de l'appli :
// - rien n'est jamais écrasé : un fichier identique déjà présent est sauté, un fichier différent est gardé à côté
//   (« nom (2).ext ») ;
// - chaque fichier est écrit en .part puis validé, et vérifié par relecture (empreinte SHA-256) ;
// - « Déplacer » ne supprime les originaux qu'une fois TOUT copié et vérifié ;
// - reprise : relancer la même demande saute ce qui est déjà là et identique.
#pragma once
#include <atomic>
#include <string>
#include <vector>

#include "backup/backup.hpp"
#include "core/json.hpp"
#include "store/store.hpp"

namespace pbc::files {

struct Request {
    store::Store* src = nullptr;
    std::string src_dir;              // dossier de départ (chemin de l'appareil)
    std::vector<std::string> names;   // éléments cochés dans ce dossier (fichiers ou dossiers)
    store::Store* dst = nullptr;
    std::string dst_dir;              // dossier d'arrivée (créé s'il manque)
    bool move = false;
    // Journal de la copie (fichier hôte du moteur) : une ligne par fichier fait (« C » copié, « S » déjà là et
    // identique) et par dossier créé (« D »). Sert à reprendre sans tout relire, et à « Retirer les fichiers copiés ».
    std::string journal;
};

struct Result {
    bool ok = false;
    std::string error;
    Json report;  // {copied, skipped, renamed:[…], removed, errors:[…]}
};

Result run(const Request& req, const backup::ProgressFn& progress, std::atomic<bool>& interrupt);

// Retire ce qu'une copie a CRÉÉ (lignes « C » du journal, si le fichier a toujours la taille notée), puis les dossiers
// qu'elle a créés s'ils sont vides. Jamais un fichier qui était déjà là (« S »), jamais autre chose.
// report : {removed, kept, dirs}.
bool cleanup(store::Store& dst, const std::string& journal, Json& report, std::string& error);

// « Supprimer » : retire définitivement les éléments cochés d'un dossier (fichiers, puis dossiers du plus profond au
// moins profond). Les gardes (endroits interdits, confirmations) sont faites avant, par le moteur et l'appli.
// report : {removed_files, removed_dirs, bytes, errors:[{file, error}]}.
Result remove_items(store::Store& st, const std::string& dir, const std::vector<std::string>& names,
                    const backup::ProgressFn& progress, std::atomic<bool>& interrupt);

// Où en est la copie en cours (diagnostic : /status « files_stage ») : étape, fichier, depuis quand.
Json stage_json();

// Nom sûr pour un élément d'un dossier : ni vide, ni « . » / « .. », ni « / ».
bool safe_name(const std::string& name);

}  // namespace pbc::files
