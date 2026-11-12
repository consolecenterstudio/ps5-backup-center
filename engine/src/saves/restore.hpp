// Restauration d'une sauvegarde PS5 depuis un backup, sur un compte de la console, avec le cœur de
// Garlic SaveMgr compilé dans le moteur (saves/garlic_core.h : aucun processus ni port externe),
// selon la procédure validée le 4 novembre (007 First Light) :
//   - refus si le compte cible n'a pas d'identifiant (le jeu verrait une sauvegarde corrompue) ;
//   - seulement les images principales sdimg_* (jamais sdimg_sce_bu_*) ;
//   - jeu fermé ; copie de sécurité des images déjà présentes avant tout import ;
//   - compte différent → portage (re-signature) seulement avec l'accord explicite (`allow_resign`) ;
//   - vérification finale en montant chaque image importée (identifiant non nul et cohérent, fichiers présents).
// Une réponse « ok » prouve l'import, pas la progression : la validation finale se fait dans le jeu.
#pragma once
#include <atomic>
#include <string>

#include "backup/backup.hpp"
#include "core/json.hpp"

namespace pbc::saves {

struct RestoreSaveRequest {
    std::string bundle;       // chemin console du .pbck
    std::string images_dir;   // ou : dossier d'images sdimg_* seules (ancienne sauvegarde, copie téléchargée d'un NAS)
    std::string user_id;      // compte cible (utilisateur local, hexadécimal)
    std::string title_id;     // jeu à restaurer (vide : tous ceux du backup)
    bool allow_resign = false;
    bool check_only = false;  // vérification seule : chaque image est lue puis démontée, rien n'est importé
    std::string state_dir;    // pour la copie de sécurité
};

struct RestoreSaveResult {
    bool ok = false;
    std::string error;
    std::string code;         // « account_mismatch », « game_running », « no_account_id », « no_user »…
    Json report = Json::object();
};

RestoreSaveResult restore_save(const RestoreSaveRequest& req, const backup::ProgressFn& progress, const std::atomic<bool>& stop);

// Suppression de la sauvegarde d'un jeu sur la console (compte donné), comme Réglages › Données sauvegardées :
//   - jeu fermé ;
//   - copie de sécurité d'abord (images, icônes, lignes de la base) dans <state_dir>/delete-safety/… — sinon rien ne bouge ;
//   - puis les lignes du jeu dans savedata.db, et ses dossiers d'images et d'icônes.
// L'appelant vérifie avant qu'une copie à jour existe dans Backups (on peut la restaurer).
RestoreSaveResult delete_console_save(const std::string& state_dir, const std::string& user_id, const std::string& title_id);

}  // namespace pbc::saves
