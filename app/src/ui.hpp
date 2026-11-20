// Interface télé : mêmes écrans et mêmes options que le web (Bibliothèque, Destinations,
// Sauvegardes, Tâches, Paramètres). Navigation identique partout : croix et stick, L1/R1 onglets,
// Croix valide, Rond revient, Options = menu de l'élément.
#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "gfx.hpp"
#include "i18n.hpp"
#include "model.hpp"

namespace app {
bool copy_suspect(const pbc::Json& c);
void draw_checkbox(Surface& s, int x, int cy, bool on, bool dim);  // case à cocher commune (ui_manage.cpp)
std::string install_job_line(const Task& t);  // installation par étapes, en une ligne (ui_transfers.cpp)
const char* task_kind(const std::string& type);  // « Compression », « Transfert »… (ui_transfers.cpp)  // copie « à vérifier » : incomplète ou vide (ui_manage.cpp)

// Services du nuage (même rôle qu'un NAS) : S3, Google Drive, OneDrive, Dropbox, MEGA.
// « share » : lien de partage reçu (MEGA…), rangé avec le nuage, logo de son service.
inline bool cloud_proto(const std::string& t) { return t == "s3" || t == "gdrive" || t == "onedrive" || t == "dropbox" || t == "mega" || t == "share"; }

namespace key {
inline constexpr std::uint32_t up = 0x10, right = 0x20, down = 0x40, left = 0x80, l2 = 0x100, r2 = 0x200, l1 = 0x400, r1 = 0x800,
                               triangle = 0x1000, circle = 0x2000, cross = 0x4000, square = 0x8000, options = 0x8,
                               touchpad = 0x100000,  // clic du pavé tactile (bouton du milieu) : retour à l'Accueil
                               l3 = 0x2, r3 = 0x4;   // clics des sticks : L3 = tous les menus, R3 = Paramètres
}

struct InputState {
    std::uint32_t pressed = 0;       // touches pressées depuis la dernière image (répétition incluse)
    bool connected = true;
    unsigned quit_progress = 0;      // 0..1000 (Rond maintenu 2 s)
    std::uint32_t down = 0;          // touches tenues en ce moment (confirmations « maintenir Carré »)
};

// Paragraphe coupé aux mots (ui_synchro.cpp) : renvoie la hauteur occupée.
int para(Surface& s, const Font& f, int x, int y, const std::string& text, FontSize sz, std::uint32_t c, int w, int max, int lh);

// Lignes d'un paragraphe coupé aux mots (ui_manage.cpp).
std::vector<std::string> wrap(const std::string& text, FontSize size, int max_w);

class Ui {
public:
    Ui(Model& model, Covers& covers, Font font, std::string address) : model_(model), covers_(covers), font_(font), address_(std::move(address)) {}
    // Renvoie vrai si l'écran doit être redessiné.
    bool update(const InputState& in, std::uint64_t now_ms);
    void draw(Surface& s);
    void set_logo(Image logo) { logo_ = logo; }
    // Images d'appareils (Destinations) : interne, USB, Switch, NAS, réseau, nuage.
    enum Device { dev_internal, dev_usb, dev_switch, dev_nas, dev_network, dev_cloud, dev_synology, dev_qnap, dev_consoles, dev_x, dev_youtube, dev_reddit, dev_discord, dev_gdrive, dev_dropbox, dev_onedrive, dev_b2, dev_aws, dev_mega, device_count };
    static constexpr const char* device_files[device_count] = {"internal", "usb", "switch", "nas", "network", "cloud", "synology", "qnap", "consoles", "x", "youtube", "reddit", "discord", "svc-gdrive", "svc-dropbox", "svc-onedrive", "svc-b2", "svc-aws", "svc-mega"};
    int cloud_dev(const std::string& type, const std::string& service = {}) const;  // logo du service (Google Drive…), sinon le nuage
    void set_device(int i, Image img) { if (i >= 0 && i < device_count) devices_[i] = img; }
    void skip_whats_new() { whats_new_checked_ = true; }  // aperçu Mac : captures sans fenêtre
    // Aperçu Mac : montrer une fenêtre sans attendre l'événement (« Quoi de neuf », « … est prête » avec son minuteur).
    void preview_news(std::vector<ChangelogEntry> list) { whats_new_ = std::move(list); }
    void preview_reopen(const std::string& v);
    bool start_engine_requested() { bool r = start_engine_; start_engine_ = false; return r; }
    // Vrai quand l'app doit se fermer (redémarrage après une mise à jour : le moteur la relance).
    bool quit_requested() const { return quit_requested_.load(); }

private:
    enum Tab { home, library, parties, backups, synchro, transfers, downloads, destinations, tasks, settings, tab_count };
    void draw_header(Surface& s);
    void draw_footer(Surface& s, std::initializer_list<std::pair<int, const char*>> hints);
    // Fenêtre (formulaire) : l'aide suivante est dessinée dans la fenêtre, en bas, sous un trait (charte §4.6), pas en bas de l'écran.
    struct Box { int x = 0, y = 0, w = 0, h = 0; } footer_box_;
    void help_in(int x, int y, int w, int h) { footer_box_ = {x, y, w, h}; }
    void draw_quit_hint(Surface& s);
    int quit_hint_width() const;
    int home_hint_width() const;
    void draw_library(Surface& s);
    void draw_sheet(Surface& s);
    void draw_destinations(Surface& s);
    void draw_parties(Surface& s);
    void draw_backups_tab(Surface& s);
    void draw_tasks(Surface& s);
    void draw_settings(Surface& s);
    void draw_engine_overlay(Surface& s);
    void draw_changelog(Surface& s, const std::vector<ChangelogEntry>& list, int x, int& y, int w, int bottom);
    void draw_whats_new(Surface& s);
    // Liste déroulante (émulateur, emplacement, destination) : Haut/Bas, Croix choisit, Rond ferme.
    int popup_ = 0, popup_sel_ = 0;  // 1 émulateur, 2 emplacement, 3 destination
    std::string popup_title_;
    std::string popup_note_;
    // Fenêtres en chaîne (un menu qui en ouvre un autre) : ○ ramène à la précédente, telle qu'elle était (charte §2).
    struct PopupState { int kind = 0, sel = 0; std::string title, note; std::vector<std::string> items, subs; std::vector<int> kinds;
                        std::vector<std::function<void()>> fns; };
    std::vector<PopupState> popup_back_;
    std::vector<int> popup_kind_;  // menu d'actions : 0 normale, 1 efface (rouge, en bas), 2 Annuler / Fermer (tout en bas)
    std::vector<std::string> popup_subs_;  // explication courte de chaque choix (même ordre), vide d'habitude  // paragraphe sous le titre (coupé aux mots), vide d'habitude
    std::vector<std::string> popup_items_;
    std::vector<int> dest_places_;  // index des emplacements proposés comme destination
    void open_popup(int kind, std::string title, std::vector<std::string> options, int selected);
    void input_popup(std::uint32_t k);
    void draw_popup(Surface& s);
    bool guide_ = false;
    std::string guide_file_;
    void draw_guide(Surface& s);
    // Plans programmés
    std::vector<pbc::Json> plans_;
    pbc::Json pending_plans_;
    bool has_pending_plans_ = false;
    std::uint64_t plans_loaded_ = 0;
    void load_plans();
    void input_plans(std::uint32_t k);
    void draw_plans(Surface& s);
    void input_library(std::uint32_t k);
    void input_sheet(std::uint32_t k);
    void input_parties(std::uint32_t k);
    void input_backups_tab(std::uint32_t k);
    // Parties : cartes des joueurs (comptes, Switch, Eden), jeux du joueur choisi en grandes jaquettes.
    int pt_player_ = 0, pt_zone_ = 0, pt_view_ = 0, pt_row_ = 0;
    std::set<std::string> pt_sel_;            // jeux cochés (compte|élément)
    int pt_players() const;
    std::vector<std::size_t> pt_rows() const;
    int sync_dy_ = 0;                         // décalage vertical de la vue Synchro (sous les cartes des joueurs)
    std::size_t rs_index_ = 0;                // jeu du menu « Sauvegarder / restaurer » (ligne de l'aperçu)
    // Menu d'actions (Options) : entrées et ce qu'elles lancent.
    std::vector<std::function<void()>> act_fns_;
    void open_actions(std::string title, std::vector<std::pair<std::string, std::function<void()>>> items);
    // Format d'un jeu installé (envoi à une PS5, installation depuis une copie) : auto / ffpfsc / exfat / keep.
    void card_title(Surface& s, int x, int y, const std::string& t, int max_w);
    // Sauvegarde actuelle d'un jeu → PS5 jumelée : console, puis compte qui la reçoit (re-signée si besoin).
    // Choisir le compte qui reçoit une sauvegarde : comptes de cette console (instance vide) ou d'une PS5 jumelée.
    // Fermer l'application ouverte sur une PS5 jumelée, après confirmation nominative (jeu : partie non sauvegardée perdue).
    void confirm_close_remote(const std::string& instance, const std::string& console_name, const pbc::Json& fg);
    // Supprimer la sauvegarde d'un jeu sur la console (choix du compte s'il y en a plusieurs, puis confirmation).
    void delete_console_save(const std::string& title_id, const std::string& title);
    // Dernière étape d'une action sans retour : maintenir Carré 5 s (Rond annule). La barre se remplit.
    void open_hold_confirm(std::string title, std::string note, std::function<void()> fn, std::string how = {});  // how : la consigne (« … pour confirmer »)
    std::string hold_how_;
    void draw_hold_confirm(Surface& s);
    bool hold_open_ = false;
    std::string hold_title_, hold_note_;
    std::function<void()> hold_fn_;
    std::uint64_t hold_since_ = 0;
    bool has_console_save(const std::string& title_id) const;
    // L'élément de la bibliothèque (par clé, sinon par numéro de jeu), sinon un remplaçant : icône officielle par le numéro,
    // plateforme PS4 / PS5 d'après le numéro (CUSA = PS4) ou la clé.
    const Item* find_or_stand_in(const std::string& key, const std::string& title, const std::string& title_id, Item& stand_in, const std::string& kind = "") const;
    void pick_account(const std::string& instance, const std::string& console_name, std::function<void(const std::string& uid, const std::string& name)> then);
    void send_save_to_peer(const std::string& item_key, const std::string& title, const std::string& local_uid);
    void choose_format(const std::string& title, std::function<void(const std::string&)> then);
    void draw_checked(Surface& s, int n);     // « 3 cochés » au-dessus de l'aide
    // Toutes les versions d'un élément (détail de Backups), depuis la fiche, Parties ou Backups.
    void open_versions(const std::string& account, const std::string& title_id, const std::string& title, const std::string& item);
    int mg_back_tab_ = -1;                    // onglet d'où l'on vient (Rond y revient)
    int mg_zone_ = 0;                         // Backups : 0 cartes des endroits, 1 liste
    int mg_place_ = 0;                        // 0 Partout, 1..n un disque / NAS, n+1 Corbeille
    void input_tasks(std::uint32_t k);
    void input_settings(std::uint32_t k);
    void toast(std::string msg);
    // Mises à jour : actions proposées selon l'état (0 rechercher, 1 installer, 2 redémarrer le moteur,
    // 3 redémarrer l'app, 4 rechercher au démarrage).
    std::vector<int> update_actions() const;
    void run_update_action(int action);
    void open_launch_menu();
    void open_mounting_menu();
    void open_tools_menu();
    void open_install_from(const pbc::Json& src_place, const std::string& path, const std::string& name, std::uint64_t bytes, bool dir,
                           const pbc::Json& alt = pbc::Json(), const std::string& title = "");
    void open_install_formats(const pbc::Json& options, const std::string& sid, const std::string& pid, const std::string& label, const std::string& img_path,
                              std::uint64_t img_size, const std::string& pkg_path, std::uint64_t pkg_size, bool dir, const std::string& shown);
    void open_power_menu();
    void open_versions_menu();  // démarrage du moteur : chargeurs (ordre) et « Démarrer avec la console »
    void post_toast(std::string msg) { std::lock_guard<std::mutex> l(async_mu_); pending_toast_ = std::move(msg); }  // depuis le fil de synchronisation
    void post_ui(std::function<void()> fn) { std::lock_guard<std::mutex> l(async_mu_); pending_ui_.push_back(std::move(fn)); }  // idem, exécuté sur l'interface
    // Vérifie le système demandé par le jeu (send-check / install-check) ; s'il est trop récent pour la console, avertit avant.
    void system_check_then(const std::string& path, const pbc::Json& body, const std::string& title, std::function<void()> then);
    std::vector<const Item*> visible_items() const;
    std::vector<std::string> app_filters() const;
    void icon(Surface& s, int which, int x, int y, std::uint32_t c);
    void cover(Surface& s, const Item& it, int x, int y, int size, bool badges);

    Model& model_;
    Covers& covers_;
    Font font_;
    Image devices_[device_count];
    Image logo_;
    std::string address_;
    std::shared_ptr<const Snapshot> snap_ = std::make_shared<Snapshot>();
    std::uint64_t seen_serial_ = 0, seen_covers_ = 0, seen_error_ = 0;
    std::uint64_t now_ = 0, started_ = 0, todo_tick_ = 0;
    Tab tab_ = home;
    // Accueil : carrousel (jeux à sauvegarder d'abord) et rangée « Votre collection ».
    int home_zone_ = 0, hero_ = 0;
    std::uint64_t hero_at_ = 0;
    std::vector<const Item*> hero_items() const;
    // bk_first / peer_first : index de la 1re case dans home_bk_games_ / home_peer_refs_ ; away : pastille « pas sur cette
    // console » ; later : rangée affichée après le bloc « ailleurs » (rétro, applications).
    struct Shelf { std::string title; std::vector<const Item*> items; bool todo = false; std::size_t total = 0; int bk_first = -1; int peer_first = -1; std::string away = {}; bool later = false; int gl_first = -1; };  // peer_first : index dans home_peer_refs_ ; gl_first : dans home_gl_refs_ / lib_gl_refs_
    // Accueil › « Backups disponibles » : jeux dont des copies existent (ici ou sur une autre PS5) mais absents de la console.
    mutable std::vector<Item> home_bk_items_;
    mutable std::vector<pbc::Json> home_bk_games_;  // total : taille de la catégorie (grille)
    static constexpr std::size_t lib_per_row = 8;
    std::vector<Shelf> category_shelves() const;
    // Accueil › « À faire » : cartes qui mènent où il faut agir.
    struct HomeCard { std::string eyebrow, title, sub; std::uint32_t tone; int action; std::vector<std::pair<std::string, std::string>> games; std::vector<int> progress = {}; std::vector<std::string> eta = {}; };  // games (clé, titre) : défilent dans la bulle ; progress : ‰ de chaque tâche (-1 : en attente)  // 0..3 vues de Backups, 4 Tâches, 5 Parties, -1 rien
    std::vector<HomeCard> home_cards() const;
    void run_home_card(int action);
    // Cadre commun de toutes les fenêtres (charte §4.6) : ombre douce, panneau, bandeau du titre, contour (rouge si danger).
    void window_frame(Surface& s, int x, int y, int w, int h, int band = 96, bool danger = false);
    // Carte d'appareil commune (charte : un seul dessin) — celle de l'onglet Appareils, à toutes les tailles :
    // ombre, panneau et reflet à gauche, l'image, le nom, une étiquette en haut à droite (type, « 2 › »), une ligne,
    // et en grande taille une pastille (« expire dans 7 j »). « Ajouter… » : fond léger ; « N de plus » : mini-images.
    struct DevCard {
        int dev = -1;                // image (devices_), -1 : aucune
        bool reserve = false;        // garder la place de l'image (icône dessinée par l'écran)
        std::string name, tag, line, pill;
        std::uint32_t tag_col = 0, line_col = 0, pill_col = 0;
        bool light = false;          // « + Ajouter… », « + N de plus » : carte légère
        std::vector<int> minis;      // petites images sous la ligne (« + 3 de plus »)
        int img = 0;                 // taille de l'image (0 : selon la hauteur de la carte)
    };
    // Retourne l'abscisse du texte (après l'image).
    int device_card(Surface& s, const DevCard& c, int x, int y, int w, int h, bool focus, bool chosen = false);
    // Le cadre seul de cette carte (ombre douce, panneau, reflet arrondi, contour) : pour toutes les bulles de l'appli.
    void card_frame(Surface& s, int x, int y, int w, int h, bool focus, bool chosen = false, bool light = false);
    // Haut de page commun (charte §4.2) : le nom de l'onglet en grand, une phrase courte à sa droite, toujours au même endroit.
    void draw_page_head(Surface& s, const std::string& name, const std::string& sub);
    // Carte Synchro de l'Accueil : ce qu'elle montre (« appareil|jeu|sens|date », « appareil|idle », « appareil|same ») ;
    // acquittée (« Pas maintenant »), elle reste cachée tant qu'elle ne montre rien de plus. Une synchro en cours s'affiche toujours.
    std::vector<std::string> sync_card_keys() const;
    static std::string sync_title_key(const std::string& peer, const pbc::Json& t);
    std::set<std::string> sync_acked() const;
    void sync_ack(bool on);
    std::set<std::string> sync_ack_local_;   // juste après un changement, avant que les réglages relus ne le disent
    std::uint32_t sync_ack_at_ = 0;
    std::vector<Shelf> home_shelves() const;
    std::vector<Shelf> library_shelves() const;
    int lib_row_ = 0, lib_scroll_ = 0;
    std::vector<int> lib_cols_;
    std::string lib_sig_;
    std::vector<int> home_cols_;
    int home_scroll_ = 0;
    bool animating_ = false;
    // Clavier à l'écran (recherche, et plus tard réglages des appareils) : le texte est appliqué en direct.
    bool kb_open_ = false;
    bool heart_ = false;   // focus sur le cœur du haut (L1 / R1), Croix = Soutenir le projet
    bool loupe_ = false;   // focus sur la loupe du haut (L1 / R1), Croix = recherche
    std::string kb_title_, kb_text_, search_;
    bool kb_shift_ = false;  // majuscules (L1)
    bool kb_sym_ = false;    // caractères spéciaux (R1)
    bool kb_secret_ = false; // mot de passe : points à l'écran
    // Destinations : sélection, et fiche de réglages d'un appareil réseau (Switch, FTP…).
    int dest_zone_ = 0, dest_sel_ = 0, dest_add_ = 0;
    pbc::Json remotes_all_, pending_remotes_all_;
    bool ed_open_ = false, ed_busy_ = false, ed_forget_armed_ = false;
    int ed_count0_ = -1;  // nombre d'appareils à l'ouverture : un ajout depuis le téléphone ferme le formulaire
    std::string ed_id_, ed_type_;
    std::string ed_service_;              // stockage S3 : « aws », « b2 » ou « other »
    pbc::Json ed_qr_, pending_ed_qr_;          // code QR de la page web (coller les clés depuis un téléphone)
    bool has_pending_ed_qr_ = false;
    pbc::Json ed_s3_body() const;              // réglages S3 saisis, pour le moteur
    pbc::Json ed_mega_body() const;
    bool ro_place(const std::string& id) const;  // partage reçu : lecture seule (ui_manage.cpp)
    pbc::Json ed_share_body() const;           // lien de partage : le lien, un nom, combien de jours            // MEGA : e-mail et mot de passe saisis, pour le moteur
    pbc::Json ed_drive_body() const;           // Google Drive, OneDrive : réglages saisis + connexion par code
    std::string ed_link_;                      // connexion par code réussie, reprise par Enregistrer
    // Connexion par code (Google Drive, OneDrive) : code QR + code court, la télé attend l'accord donné sur le téléphone.
    void open_cloud_link(const std::string& provider);
    void input_cloud_link(std::uint32_t k);
    void draw_cloud_link(Surface& s);
    bool cl_open_ = false, cl_polling_ = false;
    std::string cl_provider_, cl_id_, cl_code_, cl_url_, cl_state_, cl_error_;
    std::string cl_reconnect_;   // compte déjà là à reconnecter (mêmes réglages, nouveau jeton) ; vide : nouveau compte
    pbc::Json cl_qr_, cl_status_, pending_cl_;
    bool has_pending_cl_ = false;
    std::uint64_t cl_next_ = 0;
    std::vector<std::pair<std::string, std::string>> ed_fields_;  // (libellé, valeur)
    int ed_row_ = 0;
    void input_destinations(std::uint32_t k);
    // Plans : choisir les plans d'un jeu depuis sa fiche, et éditeur complet d'un plan.
    bool pp_open_ = false;              // fenêtre « Plans de ce jeu »
    int pp_row_ = 0;
    std::string pp_item_;
    void input_item_plans(std::uint32_t k);
    void draw_item_plans(Surface& s);
    bool pe_open_ = false;              // éditeur de plan
    pbc::Json pe_;                      // le plan en cours de modification
    int pe_row_ = 0, pe_day_ = 0, pe_action_ = 0;
    void open_plan_editor(pbc::Json plan);
    void spinner(Surface& s, int cx, int cy, int r = 22);  // roue de chargement (tourne tant qu'on attend)
    void input_plan_editor(std::uint32_t k);
    void draw_plan_editor(Surface& s);
    // Réglages d'un émulateur (Paramètres › Sources) : dossiers de ROM, fichier de réglages, sauvegardes.
    bool em_open_ = false;
    std::string em_id_, em_name_, em_config_, em_saves_, em_config_default_;
    std::vector<std::string> em_roms_;
    int em_row_ = 0;
    void open_emulator(const Emulator& e);
    void input_emulator(std::uint32_t k);
    void draw_emulator(Surface& s);
    // Explorateur de dossiers de la console (pour choisir sans taper de chemin).
    bool br_open_ = false, br_files_ = false, br_loading_ = false;
    bool br_any_ = false;  // « Créer un lien » : Croix choisit un fichier, Triangle le dossier ouvert
    std::string br_path_, br_title_, br_remote_;  // br_remote_ : appareil réseau parcouru (vide = console)
    pbc::Json br_config_;                          // appareil pas encore enregistré : réglages saisis (parcourir avant d'enregistrer)
    bool br_view_ = false;                         // simple consultation (Destinations) : rien à choisir
    pbc::Json br_entries_, pending_br_;
    bool has_pending_br_ = false;
    bool br_fallback_ = false;     // dossier de départ absent (ex. « PBC Backup » pas encore créé) : on ouvre son parent
    std::string pending_br_goto_;  // dossier créé par « Nouveau dossier… » : on y entre
    bool br_can_mkdir() const;
    int br_sel_ = 0, br_top_ = 0;
    std::function<void(const std::string&)> br_pick_;
    void open_browser(std::string title, std::string start, bool files, std::function<void(const std::string&)> pick);
    void browse_to(const std::string& path);
    void input_browser(std::uint32_t k);
    void draw_browser(Surface& s);
    void open_editor(const std::string& id, const std::string& type);
    void input_editor(std::uint32_t k);
    std::string ed_field(const char* name) const;  // valeur d'un champ de l'éditeur d'appareil (vide si absent)
    void draw_editor(Surface& s);
    int kb_row_ = 0, kb_col_ = 0;
    std::function<void(const std::string&)> kb_apply_;  // appelé à chaque frappe
    std::function<void()> kb_done_;  // appelé à la fermeture (renommer un identifiant…)
    void open_keyboard(std::string title, std::string text, std::function<void(const std::string&)> apply);
    void input_keyboard(std::uint32_t k);
    void draw_keyboard(Surface& s);  // un défilement est en cours : redessiner à la prochaine image
    void draw_home(Surface& s);
    void input_home(std::uint32_t k);
    void load_overview();
    static std::string cover_key(const Item& it);
    void load_remotes();
    bool start_engine_ = false;
    bool offline_ok_ = false;        // « Continuer sans le moteur »
    std::string seen_language_;      // dernier réglage de langue lu dans le moteur
    bool system_language_sent_ = false;
    int engine_choice_ = 0;
    unsigned quit_ = 0;
    bool connected_pad_ = true;
    std::string toast_;
    std::uint64_t toast_until_ = 0;

    // Bibliothèque
    int lib_zone_ = 1;              // 0 : filtres, 1 : grille
    int lib_filter_ = 0;            // pastille sélectionnée dans la rangée de filtres
    int lib_kind_before_ = -1;      // type choisi avant de traverser les types vers les réglages (rendu à la sortie)
    int kind_ = 0, app_ = 0, place_ = 0;
    int grid_ = 0, grid_top_ = 0;
    // Fiche
    bool sheet_ = false;
    std::string sheet_key_;
    int sheet_focus_ = 0;
    // Boutons du bas de la fiche tels que dessinés (focus, centre x, ligne) : Haut / Bas vont au plus proche.
    static constexpr int sheet_go = 100000;  // focus « Sauvegarder » à l'ouverture de la fiche (le rang dépend du nombre de cases)
    struct SheetBtn { int focus, cx, line; };
    std::vector<SheetBtn> sheet_btns_;
    std::set<int> unchecked_;
    int dest_ = 0;
    std::string sheet_place_;  // fiche : endroit choisi par Carré (vide : le défaut des sauvegardes, proposé en premier)
    // Sauvegardes
    int backups_disk_ = 0, backups_row_ = 0;
    // Sauvegardes › Par disque : la liste de chaque disque est gardée (on passe de l'un à l'autre sans
    // relire) ; Triangle relit ; un disque USB est relu discrètement au bout d'une minute, le NAS seulement à la demande.
    std::map<std::string, pbc::Json> disk_cache_;
    std::map<std::string, std::uint64_t> disk_at_;
    std::set<std::string> disk_loading_;
    std::string pending_backups_place_;
    void load_disk(const std::string& place);
    const pbc::Json& disk_list(const std::string& place) const;
    // Résultats arrivés sur le fil de synchronisation, repris par update().
    std::mutex async_mu_;
    std::string pending_toast_;
    std::vector<std::function<void()>> pending_ui_;
    pbc::Json pending_backups_;
    bool has_pending_backups_ = false;
    pbc::Json pending_credits_, credits_;
    pbc::Json pending_support_, support_;  // Paramètres › Soutenir le projet
    bool support_big_ = false;            // code QR agrandi
    pbc::Json pending_status_, status_info_;  // Signaler un problème : version du système
    bool has_pending_status_ = false;
    std::atomic<bool> status_requested_{false};
    // Paramètres → Nouveautés (historique installé avec l'app)
    std::vector<ChangelogEntry> news_, pending_news_;
    bool has_pending_news_ = false;
    std::atomic<bool> news_requested_{false};
    // « Quoi de neuf » après une mise à jour
    std::vector<ChangelogEntry> whats_new_, pending_whats_new_;
    bool has_pending_whats_new_ = false, whats_new_checked_ = false;
    bool update_opened_checked_ = false, update_in_settings_ = false;
    std::uint64_t lost_at_ = 0, lost_tick_ = 0;  // moteur perdu à cet instant (0 : connecté)
    bool engine_choice_needed() const;
    std::string update_progress_text(const Update& u) const;  // recherches de mise à jour automatiques
    std::string reopen_offered_;
    bool reopen_popup_ = false;            // la fenêtre « rouvrir l'appli » est ouverte
    std::uint64_t reopen_deadline_ = 0;    // réouverture automatique (0 : pas de minuteur, un jeu est au premier plan)
    int reopen_kind_ = 2;                  // au bout du minuteur : 2 = rouvrir l'appli, 1 = relancer le moteur
    std::string engine_offered_;           // « Mise à jour X installée : relancer le moteur ? » déjà proposé pour X
    void offer_engine_restart(const std::string& v);
    std::string seen_engine_v_;            // version du moteur vue en dernier
    std::uint64_t engine_changed_at_ = 0;  // quand elle a changé (pastille verte une minute)  // version du moteur pour laquelle « rouvrir l'appli » a déjà été proposé
    // Sauvegardes par compte
    pbc::Json overview_, pending_overview_;
    bool has_pending_overview_ = false;
    std::uint64_t overview_loaded_ = 0;
    std::atomic<bool> overview_inflight_{false};
    int saves_zone_ = 0, saves_acc_ = 0, saves_row_ = 0, saves_filter_ = 0;
    // Restaurer une sauvegarde PS5 depuis n'importe quelle version (disque ou NAS) : choix de la version,
    // vérification (rien n'est modifié), confirmation (portage si autre compte), restauration.
    int rs_stage_ = 0;            // 1 vérification, 2 confirmation, 3 restauration
    std::string rs_task_, rs_title_;
    pbc::Json rs_entry_, rs_versions_, rs_body_;
    bool rs_resign_ = false;
    void rs_start(const pbc::Json& version);
    // Gestion des backups (ui_manage.cpp) : vue d'ensemble par jeu (résumé du moteur, gardé en mémoire et relu
    // à la demande), un jeu et ses copies, aperçu avant la corbeille, corbeille de chaque disque / NAS.
    int mg_screen_ = 0;                          // 0 vue d'ensemble, 1 un jeu
    int mg_view_ = 0;                            // 0 tous, 1 avec doublons, 2 pas encore comparés, 3 corbeille
    int mg_row_ = 0, mg_crow_ = 0, mg_trow_ = 0; // ligne choisie : jeux, copies du jeu, corbeille
    std::string mg_game_;                        // jeu ouvert (compte|identifiant)
    pbc::Json mg_sum_, pending_mg_sum_;          // GET /api/v2/backups/summary
    bool has_pending_mg_sum_ = false;
    // Jeux installés sur les PS5 jumelées et absents d'ici (Accueil › « Sur … ») : GET /api/v2/link/peer-games.
    pbc::Json peer_games_, pending_peer_games_;
    bool has_pending_peer_games_ = false;
    std::uint64_t peer_games_at_ = 0;
    mutable std::vector<Item> home_peer_items_;
    mutable std::vector<std::pair<std::string, pbc::Json>> home_peer_refs_;  // (instance, jeu) de chaque case
    // Bibliothèques de jeux non installés (dossiers déclarés : NAS, nuages, USB, l'autre PS5) : l'index du moteur,
    // lu toutes les 60 s sur l'Accueil et la Bibliothèque ; cases de remplacement et jeu de chaque case.
    pbc::Json gamelib_;
    std::uint64_t gamelib_at_ = 0;
    void load_gamelib();
    std::vector<std::size_t> gamelib_missing() const;  // index dans gamelib_["games"] : un par jeu, absents de cette PS5
    mutable std::vector<Item> home_gl_items_, lib_gl_items_;
    mutable std::vector<std::size_t> home_gl_refs_, lib_gl_refs_;
    void open_gamelib_game(const pbc::Json& g);
    void open_gamelibs();  // Paramètres › Bibliothèques de jeux
    void receive_peer_game(const std::string& peer, const std::string& peer_name, const pbc::Json& g);
    // Synchro des copies avec une autre PS5 (Backups › carte de cette PS5 › Options) : aperçu puis validation.
    void sync_copies_dialog(const std::string& peer, const std::string& name, bool full);
    void sync_followed_dialog(const std::string& peer, const std::string& name);    // « Synchroniser les jeux suivis »
    void sy_set_follow(const std::vector<std::string>& title_ids, bool on);         // suivre / ne plus suivre (mémorisé)
    // Onglet Synchro (ui_synchro.cpp) : une carte par appareil (autres PS5, Switch), ses sections et ses boutons.
    struct SyDev { std::string kind, id, name; int count = 0; };  // kind "group" : famille fermée (id = famille)
    std::vector<SyDev> sy_devices_all() const;  // tous les appareils, sans regroupement
    std::string sy_fam(const SyDev& d) const;
    std::string sn_open_;                       // famille ouverte sur le côté de Synchro
    void sy_select(const std::string& id);      // choisir un appareil (ouvre sa famille si besoin)
    std::vector<SyDev> sy_devices() const;
    std::string sy_nas_name(const Place& pl) const;  // nom d'un NAS de synchro, + protocole / dossier s'il y en a deux du même nom
    const pbc::Json* sy_plan_for(const std::string& peer) const;
    static bool sy_needs(const pbc::Json& t);                // ce jeu choisi a-t-il quelque chose à échanger ?
    std::vector<const pbc::Json*> sy_title_rows() const;     // « Choisir les jeux » : filtre et tri
    void sy_prefs(const pbc::Json& body, const std::string& ok);
    void sy_open_pick(const std::string& peer, const std::string& name);
    void sy_start(const std::string& peer, const std::string& name);
    void draw_synchro_versions(Surface& s, int x, int w);
    int sn_target_ = 0;
    bool sn_games_ = false;  // carte « Jeux entiers » choisie (mène à Transferts)
    std::set<std::string> sy_pick_versions_;  // anciennes versions cochées (noms de copies)  // page Synchro : 0 = « Mettre à niveau », 1 = Sauvegardes, 2-5 = sections à venir
    void sy_run(const SyDev& d, int action);
    void input_synchro(std::uint32_t k);
    void draw_synchro(Surface& s);
    void draw_synchro_pick(Surface& s, int x, int w);
    int sn_card_ = 0, sn_zone_ = 0, sn_btn_ = 0, sn_row_ = 0;
    int hdr_px0_ = 0, hdr_px1_ = 0;  // la pastille des onglets (gauche, droite), pour l'allonger sur place (L3)
    int hdr_first_ = 0;  // premier onglet visible quand la barre du haut défile  // « sn » : onglet Synchro (« sy » : Transferts › Synchroniser)
    bool sy_pick_ = false;
    pbc::Json sy_pick_plan_;
    std::string sy_pick_peer_, sy_pick_name_;
    std::set<std::string> sy_sel_;
    // Choisir les jeux : filtre (0 tout, 1 vers ici, 2 vers là-bas, 3 sauvegardes, 4 jeux complets), tri (0 nom, 1 date,
    // 2 taille), toutes les versions ou seulement la plus récente de chaque jeu.
    int sn_filter_ = 0, sn_sort_ = 1;
    bool sn_all_versions_ = false;
    std::vector<std::pair<const pbc::Json*, bool>> sy_pick_rows() const;  // (copie, rapatriée ici ?)
    pbc::Json sync_copies_status_;           // GET /api/v2/sync/copies/status (bulle d'avancement)
    std::uint64_t sync_copies_at_ = 0, sync_plans_at_ = 0;
    void load_sync_plans();
    // Plans de synchro demandés UN À LA FOIS (avant : ~18 demandes ensemble toutes les 15 s, un par appareil et par sorte :
    // le moteur saturait, « serveur occupé »). Une demande déjà en attente n'est pas reposée.
    void plan_cmd(const std::string& path, std::function<void(const std::string&, const std::string&)> cb);
    void plan_next();
    std::mutex plan_mu_;
    std::deque<std::pair<std::string, std::function<void(const std::string&, const std::string&)>>> plan_q_;
    bool plan_busy_ = false;
    // Parties actives (ui_synchro.cpp) : plan par PS5 jumelée, état de la mise à niveau, écran de choix
    std::vector<std::pair<std::string, pbc::Json>> parties_plans_, apps_plans_, dlc_plans_, emu_plans_;
    int sy_section_ = 0;                          // écran ouvert : 0 = parties actives, 1 = applis
    pbc::Json parties_status_;
    std::uint64_t parties_status_at_ = 0;
    bool sy_parties_ = false;
    int sp_filter_ = 0;                          // filtre de l'écran des parties actives
    std::vector<const pbc::Json*> sy_parties_rows() const;                   // l'écran de choix ouvert est celui des parties actives
    const pbc::Json* parties_plan_for(const std::string& peer) const;
    std::string parties_auto_label(const std::string& a) const;
    std::string parties_auto_line(const pbc::Json& last, const std::string& name) const;
    void sy_open_parties(const std::string& peer, const std::string& name, int section = -1);
    void sy_parties_start(const std::string& peer, const std::string& name);
    void input_synchro_parties(std::uint32_t k);
    void draw_synchro_parties(Surface& s, int x, int w);
    void draw_synchro_dlc(Surface& s, int x, int w);
    void sy_dlc_details(const pbc::Json& t);
    void sy_dlc_start();
    std::vector<std::pair<std::string, pbc::Json>> sync_plans_;  // (nom, plan) par PS5 jumelée : bulle « SYNCHRO · … »
    std::string mg_sum_account_, mg_sum_error_, pending_mg_sum_error_;
    std::uint64_t mg_sum_at_ = 0;
    std::atomic<bool> mg_sum_inflight_{false};
    int mg_comparing_ = 0;                       // tâches de comparaison en cours à l'image précédente
    std::set<std::string> mg_gsel_;              // jeux cochés
    std::map<std::string, std::set<std::string>> mg_csel_;  // jeu → copies cochées (« emplacement|nom »), choix fait à la main
    std::set<std::string> mg_protected_, pending_mg_protected_, pending_mg_unprotected_;
    std::string mg_task_, mg_task_game_;         // comparaison du contenu d'un jeu (tâche inspect_save)
    std::map<std::string, pbc::Json> mg_trash_;  // emplacement → contenu de sa corbeille
    std::map<std::string, pbc::Json> pending_mg_trash_;
    std::set<std::string> mg_trash_loading_;
    // Aperçu avant la corbeille, puis mise à la corbeille une copie après l'autre.
    struct MgPvGame { std::string title; int keep = 0, remove = 0; std::uint64_t bytes = 0; };
    struct MgPvPlace { std::string label; int count = 0; std::uint64_t bytes = 0; };
    bool mg_preview_ = false, mg_deleting_ = false;
    int mg_pv_btn_ = 0;
    std::vector<MgPvGame> mg_pv_games_;
    std::vector<MgPvPlace> mg_pv_places_;
    std::vector<std::pair<std::string, std::string>> mg_pv_copies_;  // (emplacement, nom)
    std::atomic<int> mg_del_done_{0}, mg_del_skipped_{0}, mg_del_failed_{0};
    std::atomic<bool> mg_del_finished_{false};
    std::atomic<bool> mg_sum_dirty_{false};      // résumé à relire dès que la lecture en cours est revenue
    std::string mg_del_error_;                   // dernier refus (sous async_mu_)
    void mg_request_trash(const std::string& place);
    // Ignorer un jeu (POST /api/v2/ignore) : fenêtre de choix, puis attente de la réponse (NAS : peut être long).
    bool mg_ign_open_ = false, mg_ign_busy_ = false;
    int mg_ign_btn_ = 2, mg_ign_count_ = 0;     // 0 ignorer + corbeille, 1 ignorer seulement, 2 annuler ; copies (-1 : inconnu)
    // Éléments à ignorer : jeu ou application par son identifiant (« title_id »), ROM / homebrew par sa clé (« key »).
    struct IgnTarget { std::string field, value, title; };
    std::vector<IgnTarget> mg_ign_targets_;
    std::atomic<int> mg_ign_done_{0}, mg_ign_trashed_{0}, mg_ign_refused_{0}, mg_ign_failed_{0};
    std::string mg_ign_error_;                  // dernier refus (sous async_mu_)
    void open_ignore(std::vector<IgnTarget> targets, int copies);
    void ignore_next(std::size_t i, bool trash);
    void set_ignored(const Item& it, bool on);  // « Sauvegarder de nouveau » (fiche)
    std::set<std::string> lib_marks_;
    std::string disk_del_place_, disk_del_bundle_;  // Par disque : copie à mettre à la corbeille (après confirmation)           // Bibliothèque : éléments marqués (L2) pour « Ignorer la sélection »
    bool has_pending_mg_ign_ = false;           // réponse arrivée (sous async_mu_)
    std::string pending_mg_ign_msg_;
    void mg_open_ignore(const pbc::Json& g);
    void draw_mg_ignore(Surface& s);
    std::uint64_t peer_ping_at_ = 0;  // dernière demande d'état aux PS5 jumelées (temps de réponse)
    pbc::Json link_peers_, pending_link_peers_;  // autres consoles (liaison) : vues et jumelées
    pbc::Json upd_push_ = pbc::Json::object();     // mise à jour d'une autre PS5 depuis celle-ci (état)
    void push_update(const std::string& inst, const std::string& name, const std::string& theirs);
    void open_peer_menu(const pbc::Json& p, const pbc::Json& st);
    void offer_reopen(const std::string& v);  // « Backup Center X est prête » : rouvrir maintenant / plus tard (minuteur)
    void pull_update(const std::string& inst, const std::string& name, const std::string& theirs);  // récupérer sa version ici   // st : son état (/api/v2/status), vide si illisible
    bool has_pending_link_peers_ = false;
    std::uint64_t link_peers_at_ = 0;
    std::string link_host_, link_code_;           // saisie : adresse IP, puis code de l'autre console
    void load_link_peers();
    void open_other_ps5();
    void open_remote_ps5();                                   // PS5 à distance (liaison par Internet)
    void open_remote_menu(const pbc::Json& wan);
    void wan_test(const std::string& inst, const std::string& name);
    void wan_session(const pbc::Json& p);  // qui ouvre la session avec une PS5 à distance, et son adresse
    std::string wan_code_;                                    // saisie (code d'invitation, port, adresse)
    std::vector<pbc::Json> paired_peers() const;
    struct PeerCard { pbc::Json peer; const Place* disk; };  // disk nul : la console elle-même (hors ligne)
    // Appareils groupés : une bulle par famille (cette PS5, NAS, nuage, Switch) ; Croix l'ouvre sur place, Rond la referme.
    struct DestEntry { int old = -1; std::string fam; bool group = false; int count = 0; };  // old : rang dans l'ancienne liste
    std::vector<DestEntry> dest_entries() const;
    std::string dest_fam(int old) const;
    std::string dest_open_;
    // Sauvegardes : choix du haut groupés (Partout · Cette PS5 · NAS · Nuage · chaque autre PS5 · Corbeille).
    struct MgChip { int old = 0; std::string fam; bool group = false; int count = 0; };
    std::vector<MgChip> mg_chips() const;
    std::string peer_name(const std::string& fam) const;
    void mg_compare_all();  // « Tout comparer » (Sauvegardes et Comparer · trier)  // « peer:<instance> » → nom de l'autre PS5
    // Paramètres › Réseau : changer un réglage du moteur, tester les ports.
    void net_set(const char* key, pbc::Json value);
    void input_network(std::uint32_t k);
    pbc::Json net_test_;
    bool net_testing_ = false;
    std::string mg_fam(int old) const;
    std::string mg_open_;
    int mg_chip_ = 0;  // bulle sous le curseur (rang visible)
    std::vector<PeerCard> peer_cards() const;
    void open_peer(const pbc::Json& p);
    void link_pair(const std::string& host, const std::string& name);
    pbc::Json players_, pending_players_;
    bool has_pending_players_ = false;                          // Paramètres › Comptes : /api/v2/players
    std::uint64_t players_at_ = 0;
    std::string acc_name_;                       // nom en cours de saisie
    // « Tout reprendre sur ce compte » : aperçu jeu par jeu (/players/takeover), cases cochées, ligne choisie
    bool tk_open_ = false, tk_loading_ = false, has_pending_tk_ = false;
    pbc::Json tk_, pending_tk_;
    std::set<std::string> tk_sel_;
    int tk_row_ = 0;
    std::string tk_from_, tk_user_, tk_target_name_;
    void open_takeover(const std::string& from, const std::string& user_id, const std::string& target_name);
    std::set<std::string> unignored_;           // Paramètres › Jeux ignorés : retirés ici, avant la relecture des réglages
    std::string mg_account() const;
    void mg_load_summary();
    void mg_load_trash(const std::string& place);
    void mg_open_preview(const std::set<std::string>& games);
    std::vector<std::size_t> mg_rows() const;        // jeux de la vue et de l'endroit choisis, dans l'ordre affiché
    std::vector<const Place*> mg_places() const;     // disques USB et NAS, puis une carte par autre PS5 (cartes des endroits)
    mutable std::vector<Place> mg_peer_chips_;       // « peer:<instance>: » : toutes les copies rangées sur cette PS5
    std::string mg_place_id() const;                 // endroit choisi (vide : Partout ou Corbeille)
    bool mg_in_trash() const;
    void mg_select_all(const pbc::Json& g);          // toutes ses copies (de l'endroit choisi), jamais un autre compte ni une protégée
    void mg_clean_duplicates();
    bool mg_pv_delete_ = false;
    bool mg_rs_target_chosen_ = false;  // restauration : compte choisi parmi plusieurs comptes PS5
    int mg_rs_target_ = -1;  // confirmation ouverte par « Supprimer » (et non par le nettoyage des doublons)
    void mg_actions(const pbc::Json* focus);         // menu d'actions (Options) de la liste
    void mg_version_actions(const pbc::Json& g, const pbc::Json& c);
    void mg_restore(const pbc::Json& g, const pbc::Json& c);
    void mg_protect(const pbc::Json& c);
    void mg_delete_next(std::size_t i);
    void mg_finish_delete();
    void mg_compare_game(const pbc::Json& g);
    void draw_mg_overview(Surface& s, int x, int w);
    // Sauvegardes › Comparer et trier : toutes les copies d'un jeu côte à côte, jugées par leur contenu.
    std::string hero_btn_;  // texte du bouton du bandeau de l'Accueil (repris par l'aide du bas)
    bool mg_compare_ = false;
    int mg_cfilter_ = 0, mg_cmp_row_ = 0;
    std::vector<std::size_t> mg_cmp_rows() const;
    void draw_mg_compare(Surface& s, int x, int w);
    void input_mg_compare(std::uint32_t k);
    // Même partie à plusieurs endroits : tri à la demande (un jeu, ou « Garder seulement sur… » pour tous).
    struct MgTidyRow { std::string gk, key, label, sub, letter; bool keep = true, lock = false; int n = 0; };
    bool mg_tidy_open_ = false, mg_tidy_all_ = false;
    int mg_tidy_row_ = 0;
    std::string mg_tidy_title_;
    std::vector<MgTidyRow> mg_tidy_rows_;
    std::set<std::string> mg_tidy_games_;  // jeux dont le tri a coché des copies (décochées si l'aperçu est annulé)
    void mg_tidy_start(const pbc::Json* g);
    void mg_tidy_locks();
    void mg_tidy_apply();
    void draw_mg_tidy(Surface& s);
    void input_mg_tidy(std::uint32_t k);
    void open_compare(const std::string& title_id, const std::string& item);  // fiche du jeu › Comparer
    void draw_mg_game(Surface& s, int x, int w);
    void draw_mg_trash(Surface& s, int x, int w, int y0);
    void draw_mg_preview(Surface& s);
    void draw_manage(Surface& s, int x, int w);
    void input_manage(std::uint32_t k);
    // Rangée de filtres (Sauvegardes, Par disque, Synchronisation, Tâches) : Haut depuis la première
    // ligne pour y aller, Gauche / Droite pour changer, Bas pour revenir — comme la Bibliothèque.
    bool bar_ = false;
    // L3 : tous les menus sur deux rangées (on y descend au lieu de défiler L1 / R1) ; R3 : Paramètres, Rond ramène où on était.
    bool menus_open_ = false;
    int menus_sel_ = 0;
    int set_back_tab_ = -1;
    void draw_menus(Surface& s);
    void input_menus(std::uint32_t k);
    void close_overlays();
    bool filter_bar(std::uint32_t k, int& value, int count, int& row);
    std::vector<std::size_t> saves_rows() const;
    // Synchronisation Switch ↔ ProsperoEden
    std::vector<std::pair<std::string, std::string>> switches_, pending_switches_;  // (id, nom)
    bool has_pending_switches_ = false;
    std::uint64_t switches_loaded_ = 0;
    pbc::Json eden_profiles_, sync_, pending_sync_, sync_check_;
    bool has_pending_sync_ = false, sync_loading_ = false, sync_menu_ = false;
    std::string sync_key_, sync_remote_, sync_task_;
    // Comparaisons déjà faites, par paire de côtés : changer de filtre, de Switch ou d'onglet ne les perd pas.
    std::map<std::string, pbc::Json> sync_cache_;
    std::string sync_loading_key_, pending_sync_key_;
    std::uint64_t sync_load_started_ = 0;
    std::size_t sync_entry_ = 0;
    int sync_filter_ = 0, sync_dir_ = 0, sync_stage_ = 0;  // stage : 1 vérification, 2 confirmation, 3 transfert
    std::vector<std::size_t> sync_rows() const;
    void sync_start(bool check);
    void input_sync(std::uint32_t k);
    void draw_sync(Surface& s, const std::pair<std::string, std::string>& sw, int x, int w);
    void save_entries(const std::vector<std::size_t>& rows);
    bool has_pending_credits_ = false;
    std::atomic<bool> credits_requested_{false};
    std::atomic<bool> support_requested_{false};
    bool has_pending_support_ = false;
    int credits_rows_ = 1;
    // Transferts (ui_transfers.cpp) : source → destination, quoi faire, choix des éléments, suivi,
    // sauvegarde complète. Écran 0 principal, 1 choix, 2 suivi, 3 sauvegarde complète.
    struct TrDevice { std::string id, name, sub, kind; int image = 0; std::uint64_t free = 0; bool has_free = false; };
    std::vector<TrDevice> tr_devices() const;
    int tr_screen_ = 0, tr_zone_ = 0, tr_from_ = 0, tr_to_ = 1, tr_action_ = 0;
    int tr_first_[2] = {0, 0};  // première carte visible des rangées DEPUIS / VERS (elles défilent)
    // Bulles groupées : une par appareil ou famille (cette PS5, autre PS5, NAS, nuage, Switch) ; Croix ouvre ses éléments.
    struct TrGroup { std::string key, name; int image = 0; std::vector<int> members; };
    std::vector<TrGroup> tr_groups(const std::vector<TrDevice>& devs) const;
    int tr_gsel_[2] = {0, 0}, tr_open_[2] = {-1, -1}, tr_msel_[2] = {0, 0};
    // Transferts › Fichiers (écran 5) : DEPUIS (étape 0) puis VERS (étape 1) ; [0] départ, [1] destination.
    int fl_step_ = 0, fl_zone_ = 0, fl_row_ = 0, fl_top_ = 0, fl_pcur_ = 0;
    int fl_place_[2] = {-1, -1};
    std::string fl_path_[2], fl_error_[2];
    pbc::Json fl_places_, fl_entries_[2];
    bool fl_loading_[2] = {false, false}, fl_places_loading_ = false;
    std::uint64_t fl_free_[2] = {0, 0};
    std::set<std::string> fl_sel_;
    void draw_tr_files(Surface& s);
    void input_tr_files(std::uint32_t k);
    void fl_open(int side, int place, const std::string& path);
    std::string fl_pending_place_;  // endroit à ouvrir dès que la liste des endroits est relue (lien reçu tout juste ajouté)
    // Téléchargements (ui_downloads.cpp) : liens reçus, en cours, terminés ; notifications à lui.
    int dl_zone_ = 0, dl_card_ = 0, dl_row_ = 0, dl_drow_ = 0, dl_bt_ = 0;  // zones : 0 partage, 5 BitTorrent, 6 pour les deux, 7 Orbit et BlackBox, 1 en cours, 2 terminés
    std::set<std::string> dl_seen_done_, dl_warned_;
    bool dl_seen_init_ = false;
    std::vector<const pbc::Json*> dl_shares() const;
    std::vector<const Task*> dl_tasks(bool active) const;
    // Torrents (moteur, /api/v2/torrents) : relus toutes les 2 s sur Téléchargements ; ajout d'un lien magnet / .torrent
    // avec le choix de l'endroit ; menu d'un torrent (pause, reprise, retirer).
    pbc::Json tor_;
    std::uint64_t tor_loaded_ = 0;
    bool tor_loading_ = false;
    void load_torrents();
    void add_torrent(const std::string& source);
    void open_bt_options();  // Paramètres › Réseau › Options BitTorrent : arrêter de partager, place, plein débit la nuit
    static std::string bt_options_summary(const pbc::Json& n);
    // « Créer un lien » : un fichier ou un dossier de la console devient un torrent partagé depuis là où il est.
    void create_link(const std::string& path);
    std::vector<const pbc::Json*> dl_tors() const;  // les liens en préparation (« job »), puis les torrents
    std::set<std::string> tor_jobs_seen_, tor_jobs_cancelled_;
    void open_torrent_menu(const pbc::Json& t, bool from_detail = false);
    // Fiche d'un torrent (Croix sur un torrent) : Général (avec le code QR du lien magnet), Fichiers (priorité de chacun),
    // Pairs, Trackers ; relue toutes les 1,5 s ; ≡ : toutes ses actions ; △ : pause / reprise.
    bool td_open_ = false, td_loading_ = false;
    std::string td_id_;
    pbc::Json td_;
    std::uint64_t td_loaded_ = 0;
    int td_tab_ = 0, td_row_ = 0;
    void open_torrent_detail(const std::string& id);
    // « + Lien magnet » : le clavier, ou plus simple le téléphone (code QR de la page web, ouverte sur l'ajout d'un lien
    // magnet) ; la fenêtre se ferme seule dès qu'un torrent de plus arrive.
    bool mag_open_ = false;
    std::string mag_torrent_;  // non vide : la fenêtre montre le code QR du .torrent de ce torrent (son nom)
    pbc::Json mag_qr_;
    int mag_count0_ = -1;
    // Partager ma copie (ui_manage.cpp)
    bool sh_open_ = false;
    pbc::Json sh_;  // lien affiché : {kind, url, qr, url_public, qr_public, title, where, expires, token}
    bool sh_pub_ = false;
    std::uint64_t sh_poll_ = 0;          // lien BitTorrent en préparation : relu toutes les 2 s tant que la fenêtre est ouverte
    pbc::Json links_ = pbc::Json::array();  // « Mes liens » (GET /api/v2/links), pour la carte de Téléchargements
    std::uint64_t links_at_ = 0;
    void load_links();
    void open_link(const pbc::Json& l);   // un lien dans la fenêtre « Partager » (QR, adresse, préparation)
    void show_new_bt_link(const std::string& job, const std::string& title);
    std::string link_line(const pbc::Json& l) const;
    bool pub_asked_ = false;  // adresse publique redemandée à l'ouverture de l'appli
    std::string pub_addr_;    // la dernière connue (Paramètres › Réseau)
    void refresh_public_address(bool say);  // Carré : le lien Internet (adresse publique) au lieu du lien de la maison
    void open_share_copy(const pbc::Json& g, const pbc::Json& c);
    void share_from_sheet(const Item& it);
    std::string item_disk_label(const Item& it) const;  // le vrai disque d'un élément (comme la fiche)
    void open_share_hub();                              // Téléchargements › Partager… : quoi ?
    void share_hub_where(std::vector<Item> list, const std::string& what);  // où ?
    void share_hub_list(std::vector<Item> list, const std::string& what);   // la liste, de A à Z
    std::string copy_kind_text(const pbc::Json& g, const pbc::Json& c) const;  // « Sauvegarde de partie · Alex » ou « Copie du jeu »
    void open_my_shares();
    // Orbit / BlackBox (passerelle, étape 1 : voir) : GET /api/v2/apps-downloads, lu toutes les 5 s sur Téléchargements.
    pbc::Json apps_dl_ = pbc::Json::object();
    std::uint64_t apps_dl_at_ = 0;
    void load_apps_dl();
    bool apps_dl_present() const;
    std::string apps_dl_summary() const;
    std::string apps_dl_state(const pbc::Json& x) const;
    void open_apps_dl();
    // Orbit / BlackBox, étape 3 : les jeux qu'ils ont amenés (carte « À faire » de l'Accueil, choix).
    pbc::Json arrivals_ = pbc::Json::object();
    std::uint64_t arrivals_at_ = 0;
    void load_arrivals();
    void open_arrivals();
    // Transferts › Paquets .pkg : les paquets des disques (GET /api/v2/images, format « pkg »), leurs outils.
    pbc::Json pk_list_ = pbc::Json::array();
    std::uint64_t pk_at_ = 0;
    void load_packages();
    std::string package_line(const pbc::Json& x) const;
    void open_packages();
    void open_package(const pbc::Json& x);
    // « Installer… » (maquette docs/maquettes/installer.html) : le monter là où il est, ou l'installer sur un autre disque ou
    // une autre PS5. path : image, dossier ou .pkg ; disk_id : son disque (vide : trouvé par le chemin).
    void open_install(const std::string& path, const std::string& name, std::uint64_t bytes, const std::string& disk_id, bool pkg);
    void open_apps_dl_item(const pbc::Json& x);
    void draw_share(Surface& s);
    void input_share(std::uint32_t k);
    void open_magnet();
    void draw_magnet(Surface& s);
    void input_magnet(std::uint32_t k);
    void load_torrent_detail();
    void draw_torrent_detail(Surface& s);
    void input_torrent_detail(std::uint32_t k);
    void torrent_post(const std::string& id, const std::string& what, const std::string& body, const std::string& done);
    void torrent_file_priority(const std::string& id, int index, int current, const std::string& name);
    void open_network_settings(int row);
    // « Où ranger ? » (propriétaire, 17/11) : un réglage par action dans Paramètres › Où ranger, puis la même fenêtre à
    // chaque action — toutes les destinations par famille (cette PS5, autres PS5, Switch, NAS, nuage), le choix par
    // défaut en premier. wh_pick_ reçoit l'id de l'endroit (« auto » : premier USB, sinon interne).
    bool wh_open_ = false, wh_auto_ = false;
    std::string wh_title_, wh_note_, wh_default_;
    std::function<void(const std::string&)> wh_pick_;
    int wh_sel_ = 0;
    struct WhCard { std::string id; int x, y; };
    std::vector<WhCard> wh_cards_;  // positions dessinées (Haut / Bas : la carte la plus proche de la rangée voisine)
    // Réglages « demander où ranger » et « Envoyer vers… par défaut » : ceux du moteur, ou la valeur que l'on vient
    // d'enregistrer tant que l'instantané ne l'a pas encore relue.
    int wh_ask_set_ = -1;
    std::string send_place_set_;
    bool send_place_pending_ = false;
    bool ask_where() const { return wh_ask_set_ >= 0 ? wh_ask_set_ == 1 : snap_->ask_where; }
    std::string send_place() const { return send_place_pending_ ? send_place_set_ : snap_->send_place; }
    void save_where_setting(const std::string& key, const pbc::Json& value);
    // Choisir où ranger puis agir : sans la question si « Demander où ranger » vaut Non (toujours le défaut).
    void ask_where_then(const std::string& title, const std::string& note, const std::string& def, int filter, std::function<void(const std::string&)> pick);
    int wh_filter_ = 0;              // 0 sauvegardes (ni Switch, ni « synchro seulement »), 1 téléchargements (disques de la console), 2 envoi
    void send_marked_to(const std::vector<std::string>& keys, const std::string& place);
    void open_where(const std::string& title, const std::string& note, const std::string& def, int filter, std::function<void(const std::string&)> pick);
    void draw_where(Surface& s);
    void input_where(std::uint32_t k);
    void input_storage(std::uint32_t k);
    std::string where_label(const std::string& id) const;  // Paramètres › Réseau, sur une rangée (Rond ramène à l'onglet d'avant)
    bool fl_from_downloads_ = false;  // Fichiers ouvert depuis Téléchargements : ○ y ramène
    std::string dl_place_name(const std::string& id) const;
    void open_share_files(const std::string& id);
    void dl_notify();
    void draw_downloads(Surface& s);
    void input_downloads(std::uint32_t k);
    void verify_menu(const Task& t);
    void open_add_service();  // « Ajouter un service » : PS5, USB, Switch, NAS, nuage
    void add_device(int which);
    void files_task_menu(const Task& t);
    void files_delete_flow();
    // Désinstaller un jeu : sur cette PS5 (inst vide) ou sur une PS5 jumelée (inst = son identifiant de liaison).
    void uninstall_flow(const std::string& inst, const std::string& console_name, const std::string& title_id, const std::string& title, const std::string& detail);
    void uninstall_menu(const std::string& inst, const std::string& console_name, const std::string& title_id, const std::string& title, const std::string& detail, const pbc::Json& info);
    void peer_uninstall_list(const std::string& inst, const std::string& name);
    // Bibliothèque d'une autre PS5 (« Console : … » dans les filtres) : vide = cette PS5. Ses éléments viennent du cache du
    // moteur (aucun appel tant qu'on ne relit pas) ; chacun dit s'il est aussi ici et où est la partie la plus récente.
    std::string lib_peer_, lib_peer_name_;
    bool lib_peer_online_ = false, remote_loading_ = false;
    std::int64_t lib_peer_read_at_ = 0;
    std::vector<Item> remote_items_;
    std::vector<pbc::Json> remote_meta_;  // même ordre : l'élément tel que l'autre PS5 l'a (here, save, key, components…)
    int lib_presence_ = 0;                // vue autre console : 0 tout, 1 manque ici, 2 aussi ici
    void load_peer_library(bool fresh);
    void cycle_lib_console();
    const std::vector<Item>& lib_items() const { return lib_peer_.empty() ? snap_->items : remote_items_; }
    const pbc::Json* remote_meta_of(const Item* it) const;
    // Fiche d'un élément d'une autre PS5 (Bibliothèque de l'autre console, Accueil « Sur … ») : ses actions, faites là-bas
    // par son moteur (ou ici quand on récupère).
    bool rsheet_ = false;
    int rsheet_focus_ = 0;
    std::string rs_inst_, rs_name_;
    pbc::Json rs_item_;
    bool rs_online_ = true;
    std::vector<std::pair<std::string, std::function<void()>>> rs_acts_;
    void open_remote_sheet(const std::string& inst, const std::string& name, const pbc::Json& item, bool online);
    void draw_remote_sheet(Surface& s);
    void input_remote_sheet(std::uint32_t k);
    void task_details(const Task& t);
    void clear_tasks(int group);
    int tr_cat_ = 0, tr_pill_ = 0, tr_row_ = 0, tr_top_ = 0, tr_opt_ = 0, tr_task_ = 0, tr_full_ = 0, tr_full_mode_ = 0;
    int tr_format_ = 0, tr_pkg_level_ = 1;  // compression : 0 image .ffpfsc, 1 paquet .pkg (et sa compression 0 / 1 / 2)
    void open_tr_format();
    bool tr_compress_ = false, tr_saves_ = true, tr_verify_ = true;
    std::set<std::string> tr_sel_;
    std::set<int> tr_full_off_{3, 7};            // catégories décochées dans la sauvegarde complète
    pbc::Json tr_full_info_, pending_full_info_;
    bool has_pending_full_info_ = false, tr_full_loading_ = false;
    std::uint64_t tr_full_loaded_ = 0;
    // Comment et où vit chaque jeu (GET /api/v2/images) : installation native, image ou dossier, et le
    // disque qui porte vraiment les données. Clé : élément du scanner.
    std::map<std::string, pbc::Json> tr_images_;
    std::map<std::string, pbc::Json> compat_;   // compatibilité d'un élément avec cette console (fiche), par clé
    std::set<std::string> compat_asked_;
    // Phrase courte d'un état de compatibilité et de ses preuves (réponse de /api/v2/compat, send-check, install-check).
    std::string compat_state_text(const pbc::Json& c) const;
    std::string compat_reasons_text(const pbc::Json& c, std::size_t max = 4) const;
    std::string compat_backport_text(const pbc::Json& c) const;  // « fakelib du jeu, 3 bibliothèque(s), SDK 4.00 »
    pbc::Json pending_images_;
    bool has_pending_images_ = false, tr_images_loading_ = false;
    std::uint64_t tr_images_loaded_ = 0;
    void load_images();
    // Depuis un NAS (ou tout appareil réseau) : ses sauvegardes, à remettre sur la PS5.
    pbc::Json tr_backups_, pending_tr_backups_;
    bool has_pending_tr_backups_ = false, tr_backups_loading_ = false;
    std::string tr_backups_place_;
    std::uint64_t tr_backups_loaded_ = 0;
    void load_tr_backups(const std::string& place);
    std::vector<std::size_t> tr_backup_rows() const;
    void draw_tr_restore(Surface& s, const TrDevice& from, const TrDevice& to);
    void input_tr_restore(std::uint32_t k);
    void tr_restore_launch();
    // Outils de l'image depuis la fiche (Triangle) : vérifier, réparer, compresser, décompresser…
    std::vector<std::string> img_actions_;
    std::string img_item_;
    void open_image_tools(const Item& it);
    void run_image_tool(const std::string& action);
    std::vector<const Item*> tr_items() const;
    void tr_launch();
    void draw_transfers(Surface& s);
    void input_transfers(std::uint32_t k);
    void draw_tr_main(Surface& s);
    void draw_tr_choice(Surface& s);
    void draw_tr_progress(Surface& s);
    void draw_tr_full(Surface& s);
    // Transferts › Synchroniser : synchros habituelles (Switch ⇄ Eden), puis « Nouvelle synchro » (côté A, côté B).
    int sy_zone_ = 0, sy_card_ = 0, sy_a_ = 0, sy_b_ = 1;
    struct SySide { std::string name, kind, id; int image; };
    std::vector<SySide> sy_sides() const;
    void draw_tr_sync(Surface& s);
    void input_tr_sync(std::uint32_t k);
    void open_switch_sync(const std::string& remote_id);  // Parties, sur cette Switch
    void tr_device_card(Surface& s, const TrDevice& d, int x, int y, int w, int h, bool focus, bool chosen, const std::string& tag = {});
    // Tâches
    int tasks_group_ = 0, tasks_row_ = 0, tasks_action_ = 0;
    std::set<std::string> tasks_sel_;  // tâches cochées (L2) : retirer de la liste, mettre leurs sauvegardes à la corbeille
    void tasks_selection_menu();
    void tasks_trash_next(std::vector<std::string> ids, std::size_t i, int done, int failed);
    // Paramètres
    int set_section_ = 0, set_zone_ = 0, set_row_ = 0;
    void save_theme_copy();
    std::string theme_seen_;  // dernier thème reçu du moteur (appliqué quand il change, ex. choisi depuis le web)
    std::uint64_t theme_local_at_ = 0;
    void cycle_theme(int step);
    // Son (Paramètres › Général, lignes 1 à 3) : réglage local, appliqué tout de suite puis enregistré dans le moteur.
    bool sound_ui_ = true, music_ = false;
    int volume_ = 70, tasks_done_ = -1;
    std::uint64_t sound_local_at_ = 0;
    void change_sound(int row, int step);
    static void key_sound(std::uint32_t k);
    // Compte à rebours avant redémarrage (1 moteur, 2 app) ; Croix ou Rond annule.
    std::uint64_t countdown_until_ = 0;
    int countdown_kind_ = 0;
    std::atomic<bool> quit_requested_{false};
};

}  // namespace app
