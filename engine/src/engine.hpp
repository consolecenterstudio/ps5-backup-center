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
#include <functional>
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
#include "bt/bt.hpp"
#include "store/remotes.hpp"
#include "update/updater.hpp"

namespace pbc {

// Verrou principal du moteur, surveillé : une prise de plus de 250 ms est notée (fichier:ligne, durée) et visible dans
// /status (« slow_locks »). Diagnostic des lenteurs ; ne coûte qu'une lecture d'horloge.
void note_slow_lock(const char* file, int line, std::int64_t ms);
Json slow_locks_json();
Json qr_code(const std::string& text);  // une ligne par rangée, « 1 » = case noire ; vide au-delà de 400 caractères
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
    std::string send_place;               // « Envoyer vers… » (Bibliothèque) : endroit proposé en premier ; vide : demander
    bool ask_where = true;                // demander où ranger à chaque action (le défaut en premier) ; sinon toujours le défaut
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
    // Paramètres › Réseau (16/11) : BitTorrent (téléchargements, synchro — pas encore dans le moteur : réglages gardés
    // pour son arrivée) et l'endroit où ranger les téléchargements (n'importe quel endroit connu ; vide : interne).
    std::string bt_proto = "both";        // « both », « tcp » ou « udp » (uTP)
    int bt_port_from = 6891, bt_port_to = 6899;  // un port ou une plage : le premier libre est pris
    bool bt_incoming = false;             // ancien réglage (jamais appliqué) : remplacé par bt_accept_in en 3.1.40
    bool bt_accept_in = true;             // accepter les connexions entrantes (non : sortant seulement)
    bool bt_upnp = false;                 // ouvrir le port sur la box (UPnP / NAT-PMP)
    bool bt_dht = true;                   // trouver des sources sur Internet (liens magnet) ; jamais pour nos sauvegardes
    int bt_rate_max = 0;                  // Ko/s, 0 : illimité
    int bt_rate_game = 2048;              // Ko/s pendant une partie, 0 : illimité
    int bt_upload_max = 1024;             // Ko/s d'envoi, 0 : illimité
    int bt_active = 3;                    // téléchargements actifs en même temps (les autres attendent leur tour)
    int bt_connections = 200;             // connexions au plus
    // Options BitTorrent (3.1.35) : arrêter de partager un torrent terminé après un ratio (×100) ou une durée (heures) ;
    // 0 : jamais. Les liens créés ici ne s'arrêtent pas seuls. Réserver l'espace au départ ; refuser un torrent trop gros
    // pour le disque ; la nuit (de bt_night_from à bt_night_to heures), téléchargements sans limite.
    int bt_seed_ratio = 0;
    int bt_seed_hours = 0;
    bool bt_prealloc = false;
    bool bt_space_check = true;
    bool bt_night = false;
    int bt_night_from = 1, bt_night_to = 7;
    // À la fin d'un téléchargement (3.1.36) : le copier vers un endroit (NAS, nuage, USB… ; vide : non) dans son dossier
    // « Téléchargements », et / ou déplacer ses fichiers vers un autre disque de cette console (il reste partagé).
    std::string bt_done_copy, bt_done_move;
    std::string bt_watch;  // dossier surveillé : disque de la console (id) ; son dossier « Torrents à ajouter »
    // Réseau BitTorrent (3.1.39) : chiffrement « prefer » | « force » | « off » ; adresses refusées (200 au plus).
    std::string bt_encrypt = "prefer";
    std::vector<std::string> bt_blocklist;
    std::string dl_place;                 // identifiant de l'endroit des téléchargements (vide : interne)
    std::string pkg_mount = "auto";      // .pkg Kraken : « auto » (Kraken Mount s'il tourne, sinon le nôtre), « theirs », « ours »
    std::string img_mount = "auto";      // images et dossiers : « auto » (ShadowMountPlus s'il tourne, sinon le nôtre) ou « theirs »
    // Démarrage du moteur par l'appli télé : chargeurs à essayer, dans l'ordre (vide : aucun). Voir update/autostart.hpp.
    std::vector<std::string> launchers{"elfldr"};
    // Cartes de l'Accueil acquittées (« Pas maintenant ») : ce qu'elles montraient à ce moment-là (télé et web). Une carte
    // revient dès qu'elle montre quelque chose qui n'y est pas. Clé : la carte (« sync ») ; valeur : ses lignes vues.
    std::map<std::string, std::vector<std::string>> home_ack;
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
    // Où vit un élément : le dossier du jeu, le dossier de son image ou de sa ROM, sinon son inscription.
    static std::string item_location(const Item& it);
    // Le jeu lui-même, à partager : son dossier ou son image (partie « app »), sinon le fichier ou dossier de la ROM.
    static std::string item_game_path(const Item& it);
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
    struct OriginScope { explicit OriginScope(std::string o); ~OriginScope(); std::string prev; };  // « lancée par »
    static std::string task_origin();
    Json mkdir_remote(const std::string& id, const std::string& path, std::string& error);  // explorateur : « Nouveau dossier… »
    bool mkdir_in(pbc::store::Store& st, const std::string& path, std::string& error);
    bool read_only_place(const std::string& id) const;  // partage reçu : lecture seule
    Json probe_share(const Json& body, std::string& error);
    bool test_config(const Json& body, std::string& error);  // tester avant d'enregistrer
    // Gestion des backups : action « protect » {on}, « measure », « delete » {confirm:true} sur {place, bundle}.
    // Une tâche de sauvegarde terminée : où est la version qu'elle a faite ({place, bundle} au format de version_action).
    Json task_backup(const std::string& task_id, std::string& error);
    Json version_action(const std::string& action, const Json& body, std::string& error);
    Json backups_summary(const std::string& account, bool with_peers = true, bool exact = false);  // exact : attend le calcul en cours (partagé) plutôt que la dernière liste connue  // with_peers : copies rangées sur les PS5 jumelées                 // résumé par jeu + proposition
    void peer_changed() { peer_backups_at_ = 0; }  // l'autre PS5 a changé ses copies : relue au prochain tour
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
    // system_ok | backport_present | backport_insufficient (backport fait pour un système plus récent) | no_backport | insufficient (verified / incompatible_known : verdicts prouvés, PS5 Center),
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
    Json trash_delete(const Json& body, std::string& error);   // supprimer pour de bon (une version, ou toute la corbeille d'un endroit)
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
    // Verrou d'un plan à sa destination (docs/PLAN-taches-automatiques.md, règle 4) : un petit fichier « .pbc-en-cours.json »
    // (console, plan, fin prévue). SEULEMENT pour les plans lancés seuls : sauvegardes à la main, synchros, restaurations
    // et envois ne le regardent jamais. take : le poser (faux si une autre PS5 le tient encore, holder = son nom) ;
    // sinon : le retirer s'il est à nous. Un verrou expiré (console coupée) ne compte plus.
    bool plan_lock(const plans::Plan& p, bool take, std::string& holder);
    // « Garder N » d'un plan (docs/PLAN-taches-automatiques.md, règle 3), sur tout endroit : disque de la console, NAS,
    // nuage. Compté par jeu à cette destination, toutes consoles ensemble (le dossier est commun) : les N contenus les
    // plus récents restent (une copie de chacun) ; le reste part à la CORBEILLE de l'endroit (30 jours, récupérable),
    // jamais effacé d'un coup. Une copie protégée ou pas encore terminée n'est jamais touchée.
    void stamp_console(backup::Request& req);  // la PS5 qui fait la copie (manifeste)
    // Messages entre PS5 pour les plans (docs/PLAN-taches-automatiques.md, règle 4) — autant de PS5 qu'on veut :
    // dest_key : la même destination vue par n'importe quelle console (type, adresse, dossier ; un disque de la console :
    // vide, il n'est pas partagé). plan_note_peers : prévenir toutes les PS5 jumelées en ligne (« start » / « done »).
    // plan_note : reçu d'une autre PS5 ; « done » fait relire tout de suite les copies de cette destination.
    std::string dest_key(const std::string& place_id);
    void plan_note_peers(const std::string& event, const std::string& key, const std::string& plan);
    Json plan_note(const Json& body);
    bool dest_busy(const std::string& key, std::string& who);
    std::mutex dest_busy_mu_;
    std::map<std::string, std::map<std::string, std::pair<std::int64_t, std::string>>> dest_busy_;  // clé → console → (jusqu'à, nom)
    void apply_retention_at(const std::string& place_id, const std::string& item_key, int keep, const Json& parts);
    std::map<std::string, std::int64_t> plan_locks_;  // plans dont nous tenons le verrou → heure de pose
    // Vue « Sauvegardes par compte » : comptes (console et backups) et, par jeu, état console + versions sur les disques.
    Json saves_overview(bool include_ignored = false);
    // La même vue pour les écrans : une avancée (« progress_hint ») pour les jeux dont la partie ne la dit pas d'elle-même
    // (lue dans le contenu, comme dans Sauvegardes) ou qui ne sont pas sur cette console (la copie la plus avancée).
    Json saves_overview_view();
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
    // Synchro automatique des parties (maquette docs/maquettes/synchro-auto.html, validée le 19/11) : réglage par cible
    // (« off », « close », « close_night ») ; à la fermeture d'un jeu suivi, la partie qu'on vient de jouer part ; la nuit
    // à 4 h, tous les jeux suivis dans les deux sens. Jamais pendant une partie, jamais un conflit.
    void parties_auto_tick(const std::string& running, std::int64_t now);
    void parties_auto_run(const std::string& why, const std::string& only_tid);
    Json parties_auto_last(const std::string& peer);
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
    Json apps_downloads(bool fresh = false);
    // Orbit / BlackBox, étape 3 : les jeux qu'ils ont amenés (téléchargement terminé, numéro connu), l'élément de la
    // bibliothèque qui y correspond, et s'il en existe déjà une copie complète quelque part (disques, NAS, nuage).
    Json arrivals(bool fresh = false);
    Json items_view(const std::string& kind, const std::string& place);  // items_json + « via » (arrivé par Orbit / BlackBox)
    Json arrivals_backup(const Json& body, std::string& error);  // {place?, title_ids?} → sauvegarde de ceux pas encore sauvegardés
    bool arrivals_later();
    // « Installer… » (maquette docs/maquettes/installer.html) : un jeu qui se monte (image, dossier) rangé dans le dossier de
    // jeux de son propre disque, sans copie — ShadowMount le monte. Déjà dans un dossier de jeux ou déjà monté : rien à faire.
    Json install_here(const Json& body, std::string& error);
    // « Installer… » d'un jeu qui est ailleurs (NAS, nuage, autre PS5 — la source) vers une PS5 et ses disques (la cible) :
    // {src_place, path, dir, dst_place, how: "mount" | "console"} ; copie vérifiée (tâche), puis montage ou console.
    std::string install_from(const Json& body, std::string& error);
    // Installer par étapes (install_jobs.cpp) : façons d'installer (format, étapes, temps, place), tâches en cours.
    struct InstallJobSpec {
        std::string title, as, how, src_place, src_label, dst, dst_dir, name, target_label, copy_task;
        std::uint64_t bytes = 0;
        bool peer = false;
    };
    Json install_options(const Json& body);
    Json install_jobs();
    std::set<std::string> install_children();  // tâches de la file cachées derrière une installation
    bool install_cancel(const std::string& id, std::string& error);
    int install_jobs_clear(const std::string& state);
    // Envoi depuis le navigateur du PC (Fichiers › « Installer depuis ce PC… ») : le fichier arrive par morceaux sur le
    // disque choisi (ou, pour une autre PS5, dans un dossier d'attente d'ici), puis s'installe comme install_from.
    bool upload_path(const std::string& place, const std::string& name, std::string& path, std::string& error);
    Json upload_done(const Json& body, std::string& error);
    // Lancer depuis un .pkg Kraken (engine/src/kraken.cpp) : préparer + inscrire en fond ; l'état de nos jeux .pkg.
    Json kraken_mount(const std::string& pkg_path, const std::string& title_id, std::string& error);
    Json kraken_titles();
    void kraken_note(const std::string& tid, const Json& entry);
    void kraken_start();
    // Paramètres › Montage des jeux : ShadowMountPlus et Kraken Mount installés / en marche, nos versions intégrées, et
    // qui monte les .pkg (« theirs » : Kraken Mount installé, « ours » : notre moteur) d'après le réglage pkg_mount.
    Json mounting();
    std::string pkg_uses();
    // Images et dossiers (smp.cpp) : le ShadowMountPlus installé ou notre version intégrée, jamais les deux.
    bool smp_installed_running();
    std::string images_uses();
    std::string smp_state();
    void smp_tick(const std::string& running_title);
    void smp_scan_now();
    void smp_shutdown();
    // Paramètres › Outils de la console (tools.cpp) : payloads installés et en marche, les lancer, les arrêter.
    Json payload_processes();
    Json tools_json();
    Json tool_start(const std::string& key, std::string& error);
    Json tool_stop(std::int64_t pid, const std::string& name, std::string& error);
    Json tool_open_app(const std::string& title_id, std::string& error);
    // Un paquet .pkg installé par l'installeur de la console (cas 3) : jamais pendant une partie, un seul à la fois.
    Json package_install(const Json& body, std::string& error);  // « Pas maintenant » : la carte de l'Accueil revient seulement pour un nouveau jeu
    bool package_delete(const Json& body, std::string& error);
    std::string package_send(const Json& body, std::string& error);  // {path, place} : un .pkg vers un NAS, le nuage ou une autre PS5 (copie vérifiée)  // {path, confirm:"SUPPRIMER"} : un paquet .pkg, jamais son jeu ouvert
    std::string apps_dl_move(const Json& body, std::string& error);  // {app, id, place} → tâche de déplacement
    std::string apps_dl_free(const Json& body, std::string& error);  // {path, confirm:"SUPPRIMER"} → tâche de suppression  // Orbit / BlackBox : leurs téléchargements et fichiers de passage (apps_dl.cpp, lecture seule)
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
    // Partager ma copie (share_out.cpp) : {place, bundle, kind: local | bt | mega, hours, title} → lien (+ code QR) ou job.
    Json share_create(const Json& body, std::string& error);
    Json shares_out();                         // {shares: [{token, title, place, bundle, expires, downloads, url, qr}]}
    bool share_stop(const std::string& token);
    // « Mes liens » : tous les liens donnés depuis cette console, quelle que soit leur sorte — .zip de la console,
    // BitTorrent (en préparation ou partagé), MEGA, lien du service (OneDrive…). Chacun avec son adresse et son code QR.
    Json links();
    bool link_stop(const Json& b, std::string& error);   // {kind:"bt", id} (les fichiers restent) ; {kind:"mega"|"service", place, bundle}
    Json torrent_link(const std::string& id);            // la fiche d'un torrent + magnet (avec nos adresses x.pe) et son QR
    Json public_address(bool refresh);
    Json mega_keys(const std::string& remote, std::string& error);  // coffre ^!keys d'un compte MEGA : lecture seule (diagnostic)         // {host, set_by_hand} : adresse publique (redemandée si refresh)
    struct ShareZip {                          // GET /s/<jeton>/… : la copie en .zip, taille connue d'avance
        std::string name;
        std::uint64_t size = 0;
        std::function<bool(const std::function<bool(const char*, std::size_t)>&)> write;
    };
    bool share_zip(const std::string& token, ShareZip& out, std::string& error);
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
    bt::Session* torrents() { return bt_.get(); }
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
    Json uninstall_info(const std::string& title_id);  // par qui, et ce qui peut partir avec (menu « Désinstaller… »)
    bool kraken_owns(const std::string& title_id);
    Json uninstall_status() const;  // id de la tâche ; body.confirm == "SUPPRIMER"
    std::unique_ptr<store::Store> open_files_place(const std::string& id, std::string& error);
    // Bibliothèques de jeux non installés (gamelib.cpp) : dossiers déclarés, lus en fond, index gardé.
    Json gamelib_list();
    Json gamelib_games();
    bool gamelib_add(const std::string& place, const std::string& path, std::string& error);
    bool gamelib_remove(const std::string& place, const std::string& path, std::string& error);
    void gamelib_rescan(const std::string& place, const std::string& path);
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
    std::string install_job_start(const InstallJobSpec& spec);
    void install_job_run(const std::string& id, InstallJobSpec spec);
    void job_update(const std::string& id, const std::function<void(Json&)>& f, bool persist = false);
    void install_jobs_resume();
    bool pkg_ok_at(const std::string& dst, std::string& why);
    double install_speed(const std::string& step, const std::string& src);
    void learn_speed(const std::string& step, const std::string& src, double bps);
    std::string speeds_file() const;
    std::mutex jobs_mu_;
    Json jobs_ = Json::object();      // id → installation (étapes, état)
    std::mutex speeds_mu_;
    Json speeds_;                     // vitesses mesurées (octets/s) : « copy:<endroit> », « verify », « convert »
    std::atomic<int> jobs_seq_{0};
    void gamelib_loop();
    void gamelib_load();
    void gamelib_save_locked();
    void gamelib_scan(const std::string& place, const std::string& path);
    std::string gamelib_file() const;
    std::thread gamelib_thread_;
    std::mutex gamelib_mu_;
    Json gamelib_;                    // {libs:[{place, path, games, scanned_at, error, rescan}]}
    std::string gamelib_scanning_;    // « place|path » en cours de lecture
    std::atomic<std::uint64_t> gamelib_rev_{0};
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
                       std::int64_t expires = 0, next = 0; std::uint64_t free = 0, total = 0;
                       bool checking = false; };  // une seule question au service à la fois (un code ne sert qu'une fois)
    std::mutex links_mu_;
    std::map<std::string, CloudLink> links_;
    bool take_link(const Json& body, store::Remote& r, std::string& error);  // jeton d'une connexion par code
    std::unique_ptr<plans::Store> plans_;
    std::unique_ptr<progress::Tracker> playtime_;  // notre compteur de temps de jeu par jeu
    std::atomic<bool> night_{false};  // la plage « plein débit la nuit » est en cours (relevé toutes les 30 s)
    bool night_now(const Settings& s) const;
    int dl_rate_kbps(const Settings& s) const;
    void bt_after(const std::string& name, const std::string& dir);  // à la fin d'un téléchargement : copie  // débit de réception : maximum, la nuit, pendant une partie
    std::atomic<bool> game_running_{false};  // un jeu au premier plan (relevé toutes les 30 s) : téléchargements plus lents
    std::unique_ptr<link::Link> link_;  // liaison entre consoles (module réutilisable, docs/PROTOCOLE-link.md)
    std::unique_ptr<bt::Session> bt_;  // BitTorrent (Téléchargements) ; indisponible sur le banc de test du Mac
    bt::Config bt_config(const Settings& s) const;  // Paramètres › Réseau → réglages de la session
    std::string link_id_;
    std::uint16_t http_port_ = 52370;
    std::string shares_out_path() const;
    std::string share_public_host(bool refresh = false);
    Json shares_out_load() const;
    std::string links_path() const;                     // liens MEGA / du service donnés (pour les retrouver)
    void links_note(const Json& rec, bool off);
    void bt_name_note(const std::string& path, const std::string& title);
    // Backups des PS5 jumelées (leurs copies, sur LEURS disques) : relus en arrière-plan toutes les 2 min, jamais pendant
    // une demande. { instance: {name, summary, accounts, at} }.
    Json peer_backups_ = Json::object();
    std::mutex sync_mu_;
    Json sync_status_ = Json::object();   // état de la synchro des copies en cours / dernière
    std::atomic<bool> sync_running_{false}, sync_stop_{false};
    Json parties_status_ = Json::object();  // mise à niveau des parties actives en cours / dernière
    std::atomic<bool> parties_running_{false};
    std::string auto_last_game_;                       // jeu ouvert au dernier relevé (synchro automatique)
    std::map<std::string, std::int64_t> auto_closed_;  // jeux fermés → heure (la partie part 1 min après)
    std::int64_t auto_night_day_ = 0;                  // jour (heure de la console) de la dernière synchro de nuit
    std::atomic<bool> auto_running_{false};
    std::int64_t peer_backups_at_ = 0;
    void refresh_peer_backups();
    // Après une sauvegarde, une restauration ou une comparaison : l'autre PS5 relit nos copies tout de suite
    // (avant : jusqu'à 10 min). Un seul appel en attente à la fois.
    void notify_peers_later();
    std::atomic<bool> notify_pending_{false};
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
    Json remote_index_cached(const std::string& id);  // sans attendre : dernière liste connue, relue en arrière-plan
    // Résumé des sauvegardes : un seul calcul à la fois par vue, en arrière-plan ; les demandes simultanées (télé,
    // page web, autre PS5) partagent ce calcul. Tant que rien n'a changé (même révision), le dernier résultat est
    // redonné tout de suite ; sinon on attend le nouveau quelques secondes au plus, puis on rend le dernier connu
    // marqué « updating » (l'écran redemande peu après). Gardé sur disque : juste après un démarrage, pas d'écran vide.
    struct SummaryMemo { std::uint64_t rev = 0; Json value; bool busy = false; };
    void summary_memo_load();
    // Parties restaurées : « compte|jeu » → heure de la restauration. Tant que la partie de la console n'a pas changé
    // depuis, elle EST la copie restaurée : ni « à sauvegarder » ni « plus récente » (l'heure du fichier = la restauration).
    // Chaque restauration : quand, et depuis quelle copie (lieu + dossier ; vides si inconnus). Une copie faite d'une
    // partie restaurée sans la rejouer porte l'heure de la restauration : elle prend l'heure de la partie d'origine.
    struct Restored { std::int64_t at = 0; std::string place, bundle; };
    std::map<std::string, std::vector<Restored>> restored_;
    std::mutex restored_mu_;
    void restored_load();
    void restored_note(const std::string& key, const Restored& r);
    Json restores_json();  // pour l'autre PS5 : restaurations faites ici (compte PSN, jeu, heure)
    void summary_memo_save();
    std::map<std::string, SummaryMemo> summary_memo_;
    std::mutex summary_mu_;
    std::condition_variable summary_cv_;
    Json backups_summary_compute(const std::string& account, bool with_peers);
    std::thread scan_thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> scan_requested_{true};
    std::atomic<std::uint64_t> revision_{1};
    std::int64_t started_at_ = 0;
};

bool copy_store_tree(store::Store& src, const std::string& from, store::Store& dst, const std::string& to, std::atomic<std::int64_t>& done,
                     const std::atomic<bool>& stop, std::string& err);

}  // namespace pbc
