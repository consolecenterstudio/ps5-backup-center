// Le démon : seule source de vérité. Inventaire mis en cache sur disque (affichage
// immédiat au démarrage), scan en arrière-plan, file de tâches, réglages.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <chrono>
#include <condition_variable>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/json.hpp"
#include "model/model.hpp"
#include "scan/emulators.hpp"
#include "scan/recognizers/title_dir.hpp"
#include "scan/scanner.hpp"
#include "tasks/tasks.hpp"
#include "covers/fetch.hpp"
#include "plans/plans.hpp"
#include "saves/progress.hpp"
#include "accounts/identities.hpp"
#include "link/link.hpp"
#include "store/remotes.hpp"
#include "update/updater.hpp"

namespace pbc {

// Verrou principal du moteur, surveillé : une prise de plus de 250 ms est notée (fichier:ligne, durée) et visible dans
// /status (« slow_locks »). Diagnostic des lenteurs ; ne coûte qu'une lecture d'horloge.
void note_slow_lock(const char* file, int line, std::int64_t ms);
Json slow_locks_json();
class MuGuard {
public:
    MuGuard(std::mutex& m, const char* file, int line) : m_(m), file_(file), line_(line) { m_.lock(); t0_ = now(); }
    ~MuGuard() { const std::int64_t ms = now() - t0_; m_.unlock(); if (ms > 250) note_slow_lock(file_, line_, ms); }
    MuGuard(const MuGuard&) = delete;
    MuGuard& operator=(const MuGuard&) = delete;
private:
    static std::int64_t now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    std::mutex& m_;
    const char* file_;
    int line_;
    std::int64_t t0_ = 0;
};

#if defined(PBC_VERSION)
inline constexpr const char* engine_version = PBC_VERSION;
#else
inline constexpr const char* engine_version = "2.0.0";
#endif

struct Settings {
    std::string default_place = "auto";   // « auto » : premier USB, sinon interne
    std::string custom_folder;            // vide : « PBC Backup » à la racine du disque
    std::string update_source;            // adresse du manifeste de mises à jour (http://…)
    std::string update_channel = "stable";  // « stable » (recommandé) ou « beta » : les bêtas ne sont jamais des Releases GitHub
    bool update_auto_install = false;     // installer seul ce qui est trouvé, puis relancer le moteur — jamais pendant une partie
    bool update_auto_check = true;        // rechercher seul : démarrage du moteur, ouverture de l'appli (télé, web), Paramètres, toutes les heures
    bool covers_online = true;            // jaquettes manquantes des ROM depuis libretro-thumbnails
    std::string tv_seen_version;          // « Quoi de neuf » déjà montré sur la télé pour cette version
    std::string theme = "nuit";           // thème de l'interface (télé et web) : nuit, oled, playstation, aurore…
    bool sound_ui = true;                 // sons de l'interface sur la télé (déplacement, valider, retour, messages)
    bool music = false;                   // musique d'ambiance dans l'app télé
    int volume = 70;                      // volume des sons de la télé, 0 à 100
    std::string language = "auto";        // langue de l'interface (télé et web) : « auto » ou un code (en, de, pt-BR…)
    std::string system_language;          // langue de la console, signalée par l'app télé (le web la suit en « auto »)
    std::vector<std::string> ignored;     // jeux ignorés (numéros) : ni sauvegardés, ni listés dans Sauvegardes et Backups
    std::vector<std::string> my_accounts; // anciens identifiants de compte qui sont aussi « moi » (ex. avant un formatage)
    std::map<std::string, std::string> account_names;  // nom donné par l'utilisateur à un identifiant (ex. « ancien compte »)
    // Liaison par Internet (« PS5 à distance ») : désactivée par défaut ; port TCP ; adresse publique ou nom (invitations).
    bool wan_enabled = false;
    int wan_port = 52373;
    std::string wan_public_host;
    // Démarrage du moteur par l'appli télé : chargeurs à essayer, dans l'ordre (vide : aucun). Voir update/autostart.hpp.
    std::vector<std::string> launchers{"elfldr"};
    bool is_ignored(const std::string& title_id) const { return !title_id.empty() && std::find(ignored.begin(), ignored.end(), title_id) != ignored.end(); }
    Json to_json() const;
    static Settings from_json(const Json&);
};

class Engine {
public:
    // `state_dir` : dossier console de l'état du moteur, ex. /data/PPSA99950/engine2
    explicit Engine(std::string state_dir, std::string app_dir = "/data/homebrew/PPSA99950");
    ~Engine();

    bool start();
    void stop();

    // Inventaire
    void request_scan();
    Json status() const;
    void note_tv_app(const std::string& version);
    // Mise à jour d'une autre PS5 depuis celle-ci : elle télécharge NOTRE version installée (manifeste + fichiers,
    // empreintes vérifiées chez elle), l'installe, puis relance son moteur par la voie normale. Jeton à usage limité
    // (2 h, en mémoire seulement) dans le chemin des fichiers servis.
    bool update_peer(const std::string& instance, std::string& error);
    Json update_peer_status();
    bool update_token_ok(const std::string& token);
    // Côté qui reçoit : rechercher puis installer depuis l'adresse donnée (sans changer la source réglée).
    bool update_from(const std::string& source, std::string& error);
    void note_local_request(const std::string& what);  // diagnostic : les 40 dernières demandes de l'appli de la console  // version de l'appli télé qui nous parle (en-tête X-PBC-Client)
    Json places_json();
    Json items_json(const std::string& kind_filter, const std::string& place_filter) const;
    Json emulators_json() const;
    bool find_item(const std::string& key, Item& out) const;
    std::string cover_path(const std::string& key) const;
    // Demande d'une PS5 jumelée (« local=1 ») : la clé de l'élément d'ICI qui a la jaquette de ce jeu, sinon vide
    // (jamais de nouvel appel vers les pairs : pas d'aller-retour en boucle).
    std::string local_cover_key(const std::string& key) const;
    void official_cover(Item& it);  // chemin hôte, vide si aucun
    // Jaquette réduite (max×max, PNG), mise en cache sur disque ; vide si aucune.
    bool cover_thumbnail(const std::string& key, int max, std::string& png);

    // Sauvegardes sur un disque
    Json backups_json(const std::string& place_id);
    // Appareils réseau (FTP…) : liste sans mot de passe, ajout (testé), test, oubli.
    Json remotes_json() const;
    bool add_remote(const Json& body, std::string& id, std::string& error);
    bool set_emulator_settings(const std::string& id, const Json& settings);
    Json browse(const std::string& path, std::string& error) const;
    Json browse_remote(const std::string& id, const std::string& path, std::string& error);
    // Parcourir avec des réglages pas encore enregistrés (choisir le dossier avant d'enregistrer).
    Json browse_config(const Json& body, std::string& error);
    bool test_config(const Json& body, std::string& error);  // tester avant d'enregistrer
    // Gestion des backups : action « protect » {on}, « measure », « delete » {confirm:true} sur {place, bundle}.
    // Une tâche de sauvegarde terminée : où est la version qu'elle a faite ({place, bundle} au format de version_action).
    Json task_backup(const std::string& task_id, std::string& error);
    Json version_action(const std::string& action, const Json& body, std::string& error);
    Json backups_summary(const std::string& account, bool with_peers = true);  // with_peers : copies rangées sur les PS5 jumelées                 // résumé par jeu + proposition
    Json compare_all(const std::string& account, std::string& error);  // une comparaison par jeu pas encore lu
    Json players();                                                     // joueurs : identifiants regroupés + suggestions
    Json player_action(const std::string& action, const Json& body, std::string& error);  // link / name
    // « Tout reprendre sur ce compte » : pour chaque jeu, la meilleure copie faite sous l'identifiant « from » comparée à
    // la partie de la console du compte user_id ; titles vide = aperçu, sinon une restauration (re-signée) par jeu choisi.
    // Envoyer une copie de sauvegarde (backup local ou NAS) à une console jumelée : ses images partent dans la boîte
    // de réception de l'autre, qui la restaure (re-signée) sur le compte choisi là-bas. Suivi : link_sends().
    bool send_save(const std::string& instance, const std::string& place, const std::string& bundle, const std::string& title_id,
                   const std::string& target_user, std::string& send_id, std::string& error);
    Json link_sends() const;
    // Côté qui reçoit : restaurer ce qu'une console jumelée a déposé dans la boîte de réception.
    // Côté qui reçoit (autre PS5 = destination de backup) : chemin réel d'un fichier relatif au dossier « PBC Backup »
    // d'un de NOS disques (refus de tout ce qui en sortirait), et opérations de fichiers sur ce dossier.
    bool peer_fs_path(const std::string& place, const std::string& rel, std::string& path, std::string& error);
    bool peer_fs_op(const Json& body, Json& out, std::string& error);
    // Installer un jeu depuis une copie (backup complet, local) : son image va dans le dossier de jeux du disque
    // « target » puis ShadowMountPlus l'inscrit ; sa sauvegarde est restaurée re-signée pour le compte de la console.
    // tasks : identifiants des tâches créées.
    // format : « auto » (selon le système), « keep » (tel quel), « ffpfsc » (compressé), « exfat » (image non compressée).
    bool install_backup(const std::string& place, const std::string& bundle, const std::string& target, const std::string& format, const std::string& user_id, Json& tasks, std::string& error, bool drop_copy = false, bool replace = false);
    // Envoyer un jeu à une PS5 jumelée et l'y installer : copie vers son disque (« peer:<inst>:<disque> »), puis
    // installation demandée là-bas. Suivi dans les tâches (« Gex → PS5-Salon »).
    // Compatibilité (Backup Center : constat et avertissement, jamais de correctif — la gestion avancée est
    // dans PS5 Center). Identité du jeu lue sans monter (images/identify.hpp), console visée, puis un état :
    // system_ok | backport_present | no_backport | insufficient (verified / incompatible_known : verdicts prouvés, PS5 Center),
    // avec ses preuves (codes, traduits par l'interface). compat_item ajoute « peers » : le constat pour chaque PS5 jumelée.
    // Application ouverte au premier plan : {open, title_id, title, game, ours} ; fermer celle qu'on a confirmée.
    Json foreground_app();
    bool close_foreground(const std::string& expected_title_id, std::string& error);
    Json compat_console();                                                    // cette console : système, montages observés, backports
    Json compat_item(const std::string& item_key);                            // identité + état pour cette console
    Json send_check(const std::string& peer_place, const std::string& item_key);  // pour la PS5 jumelée visée
    Json install_check(const std::string& place, const std::string& bundle);      // pour cette console, depuis une copie
    bool send_item(const std::string& peer_place, const std::string& item_key, bool save_only, const std::string& format,
                   const std::string& account, const std::string& remote_user, std::string& send_id, std::string& error, bool keep_copy = false, bool replace = false);
    bool send_game(const std::string& peer_place, const std::string& item_key, const std::string& format, const std::string& remote_user, std::string& send_id, std::string& error, bool keep_copy = false, bool replace = false);
    bool restore_inbox(const std::string& transfer, const std::string& title_id, const std::string& user_id, std::string& task_id, std::string& error);
    Json takeover(const std::string& from, const std::string& user_id, const Json& titles, std::string& error);
    Json trash_list(const std::string& place_id, std::string& error);   // corbeille d'un appareil
    Json trash_restore(const Json& body, std::string& error);           // {place, path} → remise en place
    static constexpr const char* trash_dir = "Corbeille";
    void pbc_root(const std::string& place_id, const std::string& path, std::string& root, std::string& rel);
    void purge_trash(store::Store& st, const std::string& trash, std::int64_t now);
    std::unique_ptr<store::Store> version_store(const std::string& place_id, const std::string& bundle, std::string& path, std::string& error);
    // Ranger sous « PBC Backup » les dossiers « PS5 backup »… de la racine d'un appareil (renommage).
    Json adopt_backups(const std::string& id, std::string& error);
    static Json list_store(store::Store& st, const std::string& path, std::string& error);
    bool update_remote(const std::string& id, const Json& body, std::string& error);
    bool test_remote(const std::string& id, std::string& error);
    bool forget_remote(const std::string& id);
    // Sauvegardes JKSV d'une Switch (lecture seule), associées aux jeux Switch connus d'Eden.
    Json switch_saves(const std::string& id, std::string& error);
    // Profils des Eden installés (et « Appareil »), pour choisir où importer.
    Json eden_profiles_json() const;
    // Import d'une sauvegarde JKSV dans Eden (check_only : vérification sans rien écrire).
    bool import_switch(const Json& body, std::string& task_id, std::string& error);
    bool export_switch(const Json& body, std::string& task_id, std::string& error);
    // Synchronisation : sauvegardes de deux « côtés » (« switch:<appareil> », « eden:/data/prosperoeden »…)
    // comparées par jeu et par profil. Conçu pour d'autres côtés ensuite (PS5/PS4 de la console, cartes mémoire PS1/PS2…).
    Json sync_compare(const std::string& a, const std::string& b, std::string& error);

    // Plans de sauvegarde (calendrier, contenu par règle, rétention).
    Json plans_json();
    std::string save_plan(const Json& body, std::string& error);
    bool delete_plan(const std::string& id);
    // Crée les tâches du plan maintenant ; renvoie le nombre de sauvegardes ajoutées.
    int run_plan(const std::string& id, std::string& error);
    // Vue « Sauvegardes par compte » : comptes (console et backups) et, par jeu, état console + versions sur les disques.
    Json saves_overview(bool include_ignored = false);
    Json ignore_title(const Json& body, std::string& error);
    // {title_id, user_id} : supprime la sauvegarde de ce jeu sur la console — seulement si une copie à jour est dans Backups.
    Json delete_console_save(const Json& body, std::string& error);
    // {peer, place, bundle, title_id, user_id} : demande à l'autre PS5 d'envoyer ici une sauvegarde rangée chez elle, restaurée
    // sur le compte choisi de cette console.
    Json pull_peer_save(const Json& body, std::string& error);
    Json peer_games();                                        // jeux des PS5 jumelées absents d'ici
    // Bibliothèque d'une PS5 jumelée (vue « autre console ») : ses éléments depuis le cache de fond, chacun comparé à ici
    // (« here » : both / missing ; « save » : here / there / same / conflict, depuis le plan des parties déjà calculé).
    // fresh : relit ses éléments maintenant (un seul appel), seulement si elle est en ligne. Hors ligne : aucun appel.
    Json peer_library(const std::string& instance, bool fresh);
    // Actions faites PAR une PS5 jumelée sur ses propres éléments (liste fermée) : backup, backup_saves. Elle refait ses
    // propres vérifications ; on ne fait que transmettre.
    Json peer_action(const Json& body, std::string& error);
    // Synchro des copies avec une PS5 jumelée : ce qui manque de chaque côté (sans réseau, depuis le cache de fond), puis
    // copie en arrière-plan, fichier par fichier, sans jamais rien écraser ni supprimer.
    Json sync_copies_plan(const std::string& peer, bool include_full);
    Json sync_copies_start(const Json& body, std::string& error);  // {peer, include_full, here_place?, peer_place?}
    Json sync_copies_status();
    // Jeux suivis par la synchro des copies, par PS5 : {mode: "chosen" (défaut, seulement les jeux choisis) | "all"
    // (tous sauf les exclus), games: [numéros], excluded: [numéros]} — gardé dans <état>/sync-prefs.json.
    Json sync_prefs(const std::string& peer);
    Json set_sync_prefs(const Json& body, std::string& error);  // {peer, mode?, follow?: {numéro: bool}}
    bool sync_followed(const Json& prefs, const std::string& title_id) const;
    bool sync_configured(const std::string& peer);  // au moins un jeu / une partie / une appli suivis avec cet appareil
    void sync_copies_cancel();
    // Synchro des PARTIES ACTIVES (sync_parties.cpp) : la plus récente gagne, version gardée avant d'écraser.
    Json parties_plan(const std::string& peer);
    Json parties_start(const Json& body, std::string& error);   // {peer, title_ids?, here_place?, peer_place?}
    Json parties_status();
    Json set_parties_prefs(const Json& body, std::string& error);  // {peer, follow?: {numéro: bool}, follow_all?}
    // Synchro des APPLIS (sync_parties.cpp) : même version → rien ; plus récente → remplace l'ancienne de l'autre côté.
    Json apps_versions();                           // applis installées ici et leur version (param.json), empreinte, réglages
    Json app_config(const std::string& title_id, const std::string& title, bool with_data);  // réglages de l'appli ici (sync_config.cpp)
    bool app_config_receive(const Json& body, std::string& error);                            // réglages reçus : profil d'ici, copie de sécurité
    Json dlc_list();                                // DLC installés ici (Switch dans Eden, PS5 / PS4 dans /user/addcont)
    Json dlc_plan(const std::string& peer);         // comparaison avec l'autre PS5
    // Étape B : DLC / mises à jour Switch qui manquent. direction « here » : cette console va chercher chez le pair ;
    // « there » : on demande au pair de venir chercher chez nous. Chaque console n'écrit que dans SES dossiers de jeux.
    Json dlc_start(const Json& body, std::string& error);
    // Bibliothèque de paquets (.pkg) rangés sur les points de stockage de cette console (clés USB, dossiers de
    // téléchargement) : identifiant de contenu, jeu, DLC ou jeu / mise à jour. Lecture seule, relue toutes les 5 min.
    Json pkg_library(bool fresh = false);
    // Lecture d'un morceau d'un fichier DLC / mise à jour déclaré par dlc_list (rien d'autre n'est servi).
    bool dlc_read(const std::string& title_id, const std::string& id, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error);
    // Même progression des émulateurs (ProsperoEden, Églantine) entre deux PS5 — sync_emu.cpp
    // Google Drive, OneDrive : connexion par code. start → {id, user_code, url, qr, expires_in} ; status → {state, account,
    // free, total, error} (state : pending, ok, denied, expired, error). Le jeton reste ici, repris par add_remote (« link »).
    Json cloud_link_start(const std::string& provider, std::string& error);
    Json cloud_link_status(const std::string& id);
    // Dropbox (pas de code court) : le téléphone ouvre la page de la console (lien à usage unique, 30 min), y trouve
    // l'adresse d'autorisation et y colle le code que Dropbox affiche. Sans association : l'identifiant du lien suffit.
    Json cloud_link_info(const std::string& id);
    bool cloud_link_code(const std::string& id, const std::string& code, std::string& error);
    Json cloud_services();  // {gdrive: bool, onedrive: bool} : déclarés dans cette version
    Json web_link(const std::string& place);  // adresse de la page web (+ code QR), ex. « #cloud »
    Json support(bool fresh);  // « Soutenir le projet » : moyens de soutien + codes QR (support.cpp)
    Json emu_saves();                                                        // sauvegardes d'émulateurs d'ici (aussi lue par l'autre PS5)
    Json emu_files(const std::string& key, std::string& error);              // fichiers d'une sauvegarde (chemin, taille, heure)
    bool emu_read(const std::string& key, const std::string& path, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error);
    Json emu_plan(const std::string& peer);                                  // la plus récente gagne, journal, conflits
    Json emu_start(const Json& body, std::string& error);                    // {peer, keys?} ou {peer, direction:"here", keys} (demandé par l'autre PS5)
    std::string emu_pull_one(const std::string& peer, const std::string& key, const Json& here, std::int64_t& here_at, std::int64_t& there_at);
    Json apps_plan(const std::string& peer);
    Json apps_start(const Json& body, std::string& error);
    // NAS = dépôt (mêmes écrans, mêmes règles qu'entre deux PS5) : parties actives ↔ dernière copie du NAS, applis ↔
    // dépôt d'applis du NAS (« PBC Backup/pbc-apps.json »). Appelées par parties_* / apps_* quand l'autre côté est « remote:… ».
    Json nas_parties_plan(const std::string& nas);
    Json nas_parties_start(const Json& body, std::string& error);
    Json nas_apps_plan(const std::string& nas);
    Json nas_apps_start(const Json& body, std::string& error);
    Json nas_apps_depot(const std::string& nas, std::string& error);                 // { tid: {version, hash, bundle, title, at} }
    bool nas_apps_depot_set(const std::string& nas, const std::string& tid, const Json& entry, std::string& error);
    Json parties_journal(const std::string& peer);   // parties déjà synchronisées : pas d'aller-retour en boucle
    void config_remember(const std::string& peer, const std::string& tid, const std::string& here_hash, const std::string& there_hash);
    void parties_remember(const std::string& peer, const std::string& key, std::int64_t here_at, std::int64_t there_at);
    Json pull_peer_game(const Json& body, std::string& error);  // {peer, item, place, format, user_id}  // {title_id, on, trash} : ignorer un jeu (+ ses copies à la corbeille)
    Json playtime();

    // Tâches
    tasks::Queue& queue() { return *queue_; }
    Updater& updater() { return *updater_; }
    // Crée une tâche de sauvegarde ; `place_id` vide = disque par défaut. Renvoie l'id ou une erreur.
    // `account` (utilisateur local) : ne garde que les sauvegardes PS5/PS4 de ce compte.
    bool backup(const std::string& item_key, const std::vector<Part>& parts, const std::string& place_id,
                std::string& task_id, std::string& error, const std::string& account = "", int keep = 0);
    bool backup_item(Item item, const std::vector<Part>& parts, const std::string& place_id,
                     std::string& task_id, std::string& error, const std::string& account = "", int keep = 0);
    // Sauvegarde complète (onglet Transferts) : catégories cochées → un disque USB ou le NAS.
    bool full_backup(const std::vector<std::string>& categories, const std::string& place_id, std::string& task_id, std::string& error);
    // Tailles par catégorie, calculées en arrière-plan (« computing » tant que ce n'est pas fini).
    Json full_plan();
    // Sauvegarde PS5 d'un backup remise sur un compte de la console (portage si allow_resign).
    bool restore_save(const std::string& place_id, const std::string& bundle_name, const std::string& user_id, const std::string& title_id,
                      bool allow_resign, std::string& task_id, std::string& error, bool check_only = false);
    bool restore(const std::string& place_id, const std::string& bundle_name, const std::vector<Part>& parts,
                 const std::string& target, std::string& task_id, std::string& error, bool overwrite = false);
    // Comparaison de versions d'une sauvegarde PS5 par leur contenu (tâche « inspect_save », saves/inspect.hpp) :
    // body = {title_id, user_id, force?, sources:[{kind:"console"} | {place, bundle}]}.
    bool inspect_save(const Json& body, std::string& task_id, std::string& error);
    // Versions déjà analysées d'un jeu (cache), comparées : GET /api/v2/saves/inspect.
    Json save_inspections(const std::string& title_id, const std::string& user_id);

    // Images de jeux (d'après PS5 Game Compressor ; src/images/engine_images.cpp) :
    // liste des dossiers / images, place nécessaire, création des tâches compress, uncompress, make_image,
    // extract_image, validate_image, read_speed_test, build_ampr_index, set_read_only, transfer.
    Json images_json(bool measure);
    Json image_plan(const Json& body, std::string& error);
    bool image_task(const Json& body, std::string& task_id, std::string& error);

    // Identité et arrêt (fiche /status) : identifiant d'instance aléatoire, arrêt en cours.
    const std::string& instance() const { return instance_; }
    link::Link* link() { return link_.get(); }
    const std::string& state_dir() const { return dir_; }
    const std::string& app_dir() const { return app_dir_; }
    void set_http_port(std::uint16_t p) { http_port_ = p; }
    const std::string& link_id() const { return link_id_; }
    void set_stopping() { stopping_since_ = now_unix_engine(); }
    static std::int64_t now_unix_engine();
    Settings settings() const;
    void auto_install_step();
    bool power(const std::string& action, bool force, std::string& error);  // reboot, rest, off
    // « Fichiers » (Transferts) : endroits navigables, contenu d'un dossier, nouveau dossier, copier / déplacer.
    Json files_places();
    bool files_list(const std::string& place, const std::string& path, Json& out, std::string& error);
    bool files_mkdir(const std::string& place, const std::string& path, std::string& error);
    std::string files_copy(const Json& body, std::string& error);  // id de la tâche
    std::string files_delete(const Json& body, std::string& error);
    // Désinstaller un jeu de CETTE PS5 (moteur : utilisable à distance par l'autre PS5, puis PS5 Center).
    // body : {title_id, delete_source, confirm:"DESINSTALLER"}. Jeu fermé ; sauvegardes du jeu sauvegardées d'abord ;
    // retiré de la liste de ShadowMountPlus puis désinstallé par lui ; image / dossier supprimés seulement si demandé.
    bool uninstall_game(const Json& body, std::string& error);
    Json uninstall_status() const;  // id de la tâche ; body.confirm == "SUPPRIMER"
    std::unique_ptr<store::Store> open_files_place(const std::string& id, std::string& error);
    std::string ui_text(const std::string& fr, const std::string& arg = "") const;  // texte traduit (notifications)
    bool set_settings(const Settings&);

    const std::string& token() const { return token_; }
    // Appairage d'un navigateur / téléphone : code à 6 chiffres affiché sur la télé (et dans
    // /data/pbc-backup-center/pair-code), échangé contre un jeton d'appareil.
    const std::string& pair_code() const { return pair_code_; }
    bool pair(const std::string& code, const std::string& name, std::string& device_token, std::string& error);
    bool authorized(const std::string& token) const;
    Json devices_json() const;
    void refresh_peer_places();  // disques des PS5 jumelées (destinations), relus aussi juste après un jumelage
    std::string issue_device_token(const std::string& name);  // appareil ajouté sans code (jumelage mutuel des consoles)
    bool forget_device(const std::string& id);
    std::uint64_t revision() const { return revision_.load() + queue_->revision() + updater_->revision(); }

private:
    void scan_loop();
    // Synchro « quasi instantanée » : les plans (sauvegardes, parties, applis, DLC) de chaque appareil sont préparés
    // en continu dans un fil à part ; une demande reçoit tout de suite le dernier plan s'il a moins de 20 s, que rien
    // n'a changé depuis (choix, mise à niveau, nouveau scan), sinon il est recalculé.
    void plans_loop();
    std::thread plans_thread_;
    std::mutex plans_mu_;
    struct CachedPlan { Json plan; std::int64_t at = 0; std::uint64_t gen = 0, rev = 0; std::int64_t took_s = 0; };
    std::map<std::string, CachedPlan> plans_cache_;
    std::set<std::string> plans_running_;      // calculs en cours : un seul à la fois par plan
    std::condition_variable plans_cv_;
    std::atomic<std::uint64_t> plans_gen_{0};
public:
    Json plan_cached(const std::string& kind, const std::string& peer, bool full, bool fresh = false);
    Json plan_peek(const std::string& kind, const std::string& peer);  // plan déjà en cache (quel que soit son âge), sinon null
    void plans_invalidate() { ++plans_gen_; }
private:
    void save_items();
    void load_items();
    bool resolve_place(const std::string& place_id, Place& out, std::string& error);

    std::string dir_;             // chemin console
    std::string app_dir_;         // dossier de l'app installée (table des noms Switch…)
    std::string token_;
    std::string pair_code_;
    Json devices_ = Json::array();
    std::int64_t pair_window_start_ = 0;
    int pair_attempts_ = 0;
    mutable std::mutex mu_;
    std::mutex full_mu_;
    mutable std::mutex uninstall_mu_;
    Json uninstall_status_;
    bool uninstall_running_ = false;
    Json full_cache_;
    std::int64_t full_at_ = 0;
    bool full_busy_ = false;
    std::vector<Item> items_;
    std::vector<Place> places_;
    scan::Scanner scanner_;
    scan::Catalog catalog_;
    mutable std::mutex catalog_mu_;
    scan::TitleDirRecognizer* titles_ = nullptr;
    scan::RomRecognizer* roms_ = nullptr;
    scan::Stats last_scan_;
    std::int64_t scanned_at_ = 0;
    bool scanning_ = false;
    Settings settings_;
    std::unique_ptr<tasks::Queue> queue_;
    std::unique_ptr<Updater> updater_;
    std::unique_ptr<covers::Fetcher> fetcher_;
    std::unique_ptr<store::Remotes> remotes_;
    struct CloudLink { std::string provider, device_code, user_code, url, state = "pending", refresh, account, error, verifier, authorize; int interval = 5;
                       std::int64_t expires = 0, next = 0; std::uint64_t free = 0, total = 0; };
    std::mutex links_mu_;
    std::map<std::string, CloudLink> links_;
    bool take_link(const Json& body, store::Remote& r, std::string& error);  // jeton d'une connexion par code
    std::unique_ptr<plans::Store> plans_;
    std::unique_ptr<progress::Tracker> playtime_;  // notre compteur de temps de jeu par jeu
    std::unique_ptr<link::Link> link_;  // liaison entre consoles (module réutilisable, docs/PROTOCOLE-link.md)
    std::string link_id_;
    std::uint16_t http_port_ = 52370;
    // Backups des PS5 jumelées (leurs copies, sur LEURS disques) : relus en arrière-plan toutes les 2 min, jamais pendant
    // une demande. { instance: {name, summary, accounts, at} }.
    Json peer_backups_ = Json::object();
    std::mutex sync_mu_;
    Json sync_status_ = Json::object();   // état de la synchro des copies en cours / dernière
    std::atomic<bool> sync_running_{false}, sync_stop_{false};
    Json parties_status_ = Json::object();  // mise à niveau des parties actives en cours / dernière
    std::atomic<bool> parties_running_{false};
    std::int64_t peer_backups_at_ = 0;
    void refresh_peer_backups();
    Json peer_places_ = Json::array();  // disques des PS5 jumelées (destinations « peer:… »), relus toutes les 30 s
    std::int64_t peer_places_at_ = 0;
    std::string peer_online_sig_;  // PS5 jumelées en ligne au dernier rafraîchissement de leurs disques
    mutable std::mutex sends_mu_;
    Json sends_ = Json::object();       // envois vers une autre console : état par envoi
    mutable std::mutex console_name_mu_;
    std::string console_name_;
    bool console_name_system_ = false;  // nom lu dans les réglages de la console (prioritaire)          // « PS5 · <premier compte> », noté par la boucle de relevé               // identifiant stable de cette installation (état « link-id »)
    std::unique_ptr<accounts::Identities> identities_;  // registre des identifiants de compte et de leurs noms
    std::mutex playtime_mu_;
    std::int64_t utc_offset() const;                       // décalage local de la console (s)
    std::vector<std::string> plan_items(const plans::Plan& p);  // éléments visés (règle + liste)
    void apply_retention(const std::string& folder_place, const std::string& item_key, int keep);
    // Index des appareils réseau gardé 2 min (la vue par compte se relit souvent).
    std::map<std::string, std::pair<std::int64_t, Json>> remote_index_;
    std::mutex remote_index_mu_;
    std::string instance_;
    std::atomic<std::int64_t> stopping_since_{0};
    std::set<std::string> remote_fetching_;      // appareils en cours de lecture
    std::condition_variable remote_index_cv_;
    Json remote_index(const std::string& id, bool fresh = false);
    std::thread scan_thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> scan_requested_{true};
    std::atomic<std::uint64_t> revision_{1};
    std::int64_t started_at_ = 0;
};

bool copy_store_tree(store::Store& src, const std::string& from, store::Store& dst, const std::string& to, std::atomic<std::int64_t>& done,
                     const std::atomic<bool>& stop, std::string& err);

}  // namespace pbc
