#include "ui.hpp"
#include "ui_util.hpp"
#include "audio.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace app {

using pbc::Json;
using namespace ui_util;

namespace {

const char* tab_names[] = {N_("Accueil"), N_("Bibliothèque"), N_("Parties"), N_("Sauvegardes"), N_("Synchro"), N_("Transferts"), N_("Appareils"), N_("Tâches"), N_("Paramètres")};

const char* kind_keys[] = {"", "game_ps5", "game_ps4", "app", "rom", ""};
constexpr int kind_count = 6;  // Tout, PS5, PS4, Applications, ROM, Ignorés
constexpr int kind_ignored = 5;
const char* group_names[] = {N_("Actives"), N_("Terminées"), N_("Échecs"), N_("Annulées"), N_("Sauvegardes automatiques")};
struct PlanPreset { const char* label; const char* body; };
const PlanPreset plan_presets[] = {
    {N_("Mes sauvegardes PS5, chaque soir à 21 h"), "{\"name\":\"Sauvegardes PS5 du soir\",\"timing\":\"daily\",\"minute_of_day\":1260,\"rule\":\"ps5_saves\",\"parts\":[\"save\"],\"keep\":5"},
    {N_("Toutes mes ROM, chaque dimanche à 3 h"), "{\"name\":\"ROM du dimanche\",\"timing\":\"weekly\",\"weekdays\":64,\"minute_of_day\":180,\"rule\":\"roms\",\"keep\":2"},
    {N_("Mes applications, chaque lundi à 3 h"), "{\"name\":\"Applications du lundi\",\"timing\":\"weekly\",\"weekdays\":1,\"minute_of_day\":180,\"rule\":\"apps\",\"keep\":2"},
    {N_("Tous mes jeux, à la demande"), "{\"name\":\"Tous mes jeux\",\"timing\":\"manual\",\"rule\":\"all_games\",\"keep\":1"},
};
// Sections des Paramètres, dans l'ordre affiché (« Nouveautés » en premier, comme BlackBox).
// Textes de support.json (liste en ligne) traduits à l'affichage
[[maybe_unused]] const char* support_names[] = {N_("Offrir un cadeau"), N_("cartes cadeaux, abonnements, matériel"), N_("carte, PayPal")};
enum Section { sec_news, sec_general, sec_sources, sec_ignored, sec_accounts, sec_devices, sec_updates, sec_diag, sec_report, sec_goals, sec_support, sec_thanks, section_count };
const char* section_names[] = {N_("Nouveautés"), N_("Général"), N_("Sources et émulateurs"), N_("Éléments ignorés"), N_("Utilisateurs"), N_("Appareils"), N_("Mises à jour et démarrage"), N_("Diagnostic et à propos"), N_("Signaler un problème"), N_("Projets à financer"), N_("Soutenir le projet"), N_("Remerciements")};
// Texte de recherche : minuscules, sans accents (é → e, ç → c…), pour « pokemon » = « Pokémon ».
std::string fold(const std::string& in) {
    static const char* from[] = {"à", "â", "ä", "á", "ã", "é", "è", "ê", "ë", "î", "ï", "í", "ô", "ö", "ó", "õ", "ù", "û", "ü", "ú", "ç", "ñ",
                                 "À", "Â", "Ä", "É", "È", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Ù", "Û", "Ü", "Ç"};
    static const char to[] = "aaaaaeeeeiiioooouuuucnaaaeeeeiioouuuc";
    std::string s = in, out;
    for (std::size_t i = 0; i < sizeof from / sizeof *from; ++i)
        for (std::size_t p; (p = s.find(from[i])) != std::string::npos;) s.replace(p, std::string(from[i]).size(), 1, to[i]);
    for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Éléments ignorés (Paramètres) : identifiants (ou clés) des réglages, plus les éléments signalés « ignored »
// par le moteur (ROM, homebrews ignorés par leur clé). field = « title_id » ou « key » pour « Ne plus ignorer ».
struct IgnRow { std::string field, value; const app::Item* item; };
// Paramètres › Comptes : une ligne par identifiant (ceux du joueur de la console, puis les autres).
struct AccRow { const pbc::Json* a; bool mine; std::string player; };
std::vector<AccRow> account_rows(const pbc::Json& pl) {
    std::vector<AccRow> out;
    for (const auto& p : pl["players"].items()) for (const auto& a : p["accounts"].items()) out.push_back({&a, true, p["name"].as_string()});
    for (const auto& a : pl["others"].items()) out.push_back({&a, false, ""});
    return out;
}
std::vector<IgnRow> ignored_rows(const app::Snapshot& snap, const std::set<std::string>& gone) {
    std::vector<IgnRow> out;
    auto have = [&](const std::string& v) { for (const auto& r : out) if (r.value == v) return true; return false; };
    for (const auto& t : snap.ignored) {
        if (gone.count(t) || have(t)) continue;
        const app::Item* found = nullptr;
        bool by_key = false;
        for (const auto& it : snap.items) {
            if (it.title_id == t) { found = &it; break; }
            if (it.key == t) { found = &it; by_key = true; break; }
        }
        out.push_back({by_key ? "key" : "title_id", t, found});
    }
    for (const auto& it : snap.items) {
        if (!it.ignored) continue;
        const std::string v = it.title_id.empty() ? it.key : it.title_id;
        if (gone.count(v) || have(v)) continue;
        out.push_back({it.title_id.empty() ? "key" : "title_id", v, &it});
    }
    return out;
}

#if defined(PBC_VERSION)
constexpr const char* app_version = PBC_VERSION;
#else
constexpr const char* app_version = "2.0.0";
#endif
int version_cmp(const std::string& a, const std::string& b);  // plus bas

}  // namespace

// ---- Données dérivées -------------------------------------------------------------------

std::vector<std::string> Ui::app_filters() const {
    std::vector<std::string> v;
    for (const auto& it : snap_->items) for (const auto& a : it.apps) if (std::find(v.begin(), v.end(), a) == v.end()) v.push_back(a);
    std::sort(v.begin(), v.end());
    return v;
}

std::vector<const Item*> Ui::visible_items() const {
    const auto apps = app_filters();
    const std::string app = app_ > 0 && app_ <= static_cast<int>(apps.size()) ? apps[static_cast<std::size_t>(app_ - 1)] : "";
    const std::string place = place_ > 0 && place_ <= static_cast<int>(snap_->places.size()) ? snap_->places[static_cast<std::size_t>(place_ - 1)].id : "";
    std::vector<const Item*> v;
    const bool remote = !lib_peer_.empty();
    for (const auto& it : lib_items()) {
        // Autre console : « Ignorés » ne s'applique pas (ses éléments ignorés ne sont pas envoyés) → comme « Tout ».
        if (remote ? (kind_ && kind_ != kind_ignored && it.kind != kind_keys[kind_]) : kind_ == kind_ignored ? !it.ignored : kind_ && it.kind != kind_keys[kind_]) continue;
        if (!app.empty() && std::find(it.apps.begin(), it.apps.end(), app) == it.apps.end()) continue;
        if (!remote && !place.empty() && it.place != place) continue;
        if (remote && lib_presence_) {  // autre console : manque ici / aussi ici
            const Json* m = remote_meta_of(&it);
            const std::string h = m ? (*m)["here"].as_string() : std::string();
            if ((lib_presence_ == 1 && h != "missing") || (lib_presence_ == 2 && h != "both")) continue;
        }
        if (!search_.empty()) {  // recherche : titre ou identifiant, sans majuscules ni accents
            if (fold(it.title).find(search_) == std::string::npos && fold(it.title_id).find(search_) == std::string::npos) continue;
        }
        v.push_back(&it);
    }
    return v;
}

void Ui::toast(std::string msg) {
    // Son du message : grave s'il annonce un échec (français ou anglais), sinon une cloche douce.
    static constexpr const char* bad[] = {"mpossible", "chec", "choué", "rreur", "rror", "ailed", "nable", "annot", "ntrouvable", "njoignable", "nreachable", "efus", "not found"};
    bool err = false;
    for (const char* b : bad) err = err || msg.find(b) != std::string::npos;
    audio::play(err ? audio::Sound::error : audio::Sound::notify);
    toast_ = std::move(msg);
    toast_until_ = now_ + 3500;
}

// Un effet par touche, choisi ici une fois pour toutes plutôt que dans chaque écran.
void Ui::key_sound(std::uint32_t k) {
    if (k & key::circle) audio::play(audio::Sound::back);
    else if (k & (key::up | key::down | key::left | key::right | key::l1 | key::r1)) audio::play(audio::Sound::move);
    else audio::play(audio::Sound::select);
}

// ---- Entrées ----------------------------------------------------------------------------

bool Ui::update(const InputState& in, std::uint64_t now) {
    now_ = now;
    if (!started_) started_ = now;
    bool dirty = animating_ || (sync_loading_ && tab_ == parties) || (tab_ == home && home_zone_ == 0 && hero_at_ && now - hero_at_ > 8000);  // roue de lecture, carrousel
    if (tab_ == home && now / 3000 != todo_tick_) { todo_tick_ = now / 3000; dirty = true; }  // « À faire » : les jaquettes défilent toutes les 3 s
    if (model_.serial() != seen_serial_) {
        seen_serial_ = model_.serial();
        snap_ = model_.snapshot();
        if (!snap_->theme.empty() && snap_->theme != theme_seen_ && now - theme_local_at_ > 3000) { theme_seen_ = snap_->theme; theme::set(theme_seen_); save_theme_copy(); }
        if (snap_->settings_loaded && now - sound_local_at_ > 3000) { sound_ui_ = snap_->sound_ui; music_ = snap_->music; volume_ = snap_->volume; audio::configure(sound_ui_, music_, volume_); }
        int done = 0;  // une tâche vient de se terminer (sauvegarde, restauration, copie…) : petit arpège
        for (const auto& t : snap_->tasks) done += t.state == "done";
        if (snap_->connected && tasks_done_ >= 0 && done > tasks_done_) audio::play(audio::Sound::success);
        tasks_done_ = snap_->connected ? done : -1;
        if (snap_->settings_loaded) {
            // Langue changée depuis le web (ou au démarrage) : seulement quand la valeur du moteur change, pour ne pas
            // revenir en arrière juste après un choix fait sur la télé, avant que le moteur ne l'ait relu.
            if (snap_->language != seen_language_) { seen_language_ = snap_->language; if (seen_language_ != i18n::setting()) i18n::apply(seen_language_); }
            // Le web suit la langue de la console en « auto » : l'app la signale au moteur. Seulement la vraie
            // console : un aperçu sur le Mac (langue forcée pour tester) ne doit pas changer la langue du web.
#if defined(PBCK_PS5)
            if (snap_->system_language != i18n::system_code() && !system_language_sent_) {
                system_language_sent_ = true;
                model_.command("POST", "/api/v2/settings", "{\"system_language\":" + json_str(i18n::system_code()) + "}", nullptr);
            }
#endif
        }
        if (snap_->error_serial != seen_error_) { seen_error_ = snap_->error_serial; if (seen_error_ > 0) toast(snap_->last_error); }
        dirty = true;
    }
    if (covers_.serial() != seen_covers_) { seen_covers_ = covers_.serial(); dirty = true; }
    {
        std::vector<std::function<void()>> ui;
        { std::lock_guard<std::mutex> l(async_mu_); ui.swap(pending_ui_); }
        for (auto& f : ui) { f(); dirty = true; }
    }
    {
        std::lock_guard<std::mutex> l(async_mu_);
        if (!pending_toast_.empty()) { toast(std::move(pending_toast_)); pending_toast_.clear(); dirty = true; }
        if (has_pending_backups_) {
            disk_cache_[pending_backups_place_] = std::move(pending_backups_);
            disk_at_[pending_backups_place_] = now_;
            disk_loading_.erase(pending_backups_place_);
            has_pending_backups_ = false;
            dirty = true;
        }
        if (has_pending_br_) { br_entries_ = std::move(pending_br_); has_pending_br_ = false; br_loading_ = false; dirty = true; }
        if (has_pending_switches_) { switches_ = std::move(pending_switches_); remotes_all_ = std::move(pending_remotes_all_); has_pending_switches_ = false; dirty = true; }
        if (has_pending_sync_) {
            if (pending_sync_["error"].as_string().empty()) sync_cache_[pending_sync_key_] = pending_sync_;
            if (pending_sync_key_ == sync_key_) sync_ = std::move(pending_sync_);
            has_pending_sync_ = false; sync_loading_ = false; dirty = true;
        }
        if (has_pending_plans_) { plans_.clear(); for (const auto& p : pending_plans_.items()) plans_.push_back(p); has_pending_plans_ = false; dirty = true; }
        if (has_pending_news_) { news_ = std::move(pending_news_); has_pending_news_ = false; dirty = true; }
        // « Quoi de neuf » attend qu'une fenêtre de relance (moteur, appli) soit fermée : sinon il la cachait et son minuteur
        // de 15 s tournait sans qu'on le voie.
        if (has_pending_whats_new_ && !reopen_popup_) { whats_new_ = std::move(pending_whats_new_); has_pending_whats_new_ = false; dirty = true; }
        if (has_pending_overview_) { overview_ = std::move(pending_overview_); has_pending_overview_ = false; dirty = true; }
        if (has_pending_players_) { players_ = std::move(pending_players_); has_pending_players_ = false; dirty = true; }
        if (has_pending_link_peers_) { link_peers_ = std::move(pending_link_peers_); has_pending_link_peers_ = false; dirty = true; }
        if (has_pending_tk_) {
            tk_ = std::move(pending_tk_); has_pending_tk_ = false; tk_loading_ = false; dirty = true;
            tk_sel_.clear();
            for (const auto& g : tk_["games"].items()) if (g["suggest"].as_bool() && !g["running"].as_bool()) tk_sel_.insert(g["title_id"].as_string());
        }
        if (has_pending_credits_) { credits_ = std::move(pending_credits_); has_pending_credits_ = false; dirty = true; }
        if (has_pending_support_) { support_ = std::move(pending_support_); has_pending_support_ = false; dirty = true; }
        if (has_pending_status_) { status_info_ = std::move(pending_status_); has_pending_status_ = false; dirty = true; }
        if (has_pending_peer_games_) { peer_games_ = std::move(pending_peer_games_); has_pending_peer_games_ = false; dirty = true; }
        if (has_pending_mg_sum_) {
            if (pending_mg_sum_error_.empty()) mg_sum_ = std::move(pending_mg_sum_);  // en cas d'échec, l'ancien résumé reste affiché
            // Copies protégées connues du moteur (témoin « protected » sur les disques de la console).
            for (const auto& g : mg_sum_["games"].items())
                for (const auto& c : g["copies"].items())
                    if (c["protected"].as_bool()) mg_protected_.insert(c["place"].as_string() + "|" + c["bundle"].as_string());
            mg_sum_error_ = std::move(pending_mg_sum_error_);
            pending_mg_sum_ = Json();
            pending_mg_sum_error_.clear();
            has_pending_mg_sum_ = false;
            dirty = true;
        }
        for (auto& [pl, j] : pending_mg_trash_) { mg_trash_[pl] = std::move(j); mg_trash_loading_.erase(pl); dirty = true; }
        pending_mg_trash_.clear();
        if (has_pending_mg_ign_) {
            has_pending_mg_ign_ = false;
            toast(std::move(pending_mg_ign_msg_));
            pending_mg_ign_msg_.clear();
            mg_ign_open_ = mg_ign_busy_ = false;
            lib_marks_.clear();
            mg_screen_ = 0;
            mg_trash_.clear();
            overview_loaded_ = 0;
            mg_sum_dirty_ = true;
            dirty = true;
        }
        for (const auto& k2 : pending_mg_protected_) mg_protected_.insert(k2);
        for (const auto& k2 : pending_mg_unprotected_) mg_protected_.erase(k2);
        if (!pending_mg_protected_.empty() || !pending_mg_unprotected_.empty()) { pending_mg_protected_.clear(); pending_mg_unprotected_.clear(); dirty = true; }
        if (has_pending_tr_backups_) { tr_backups_ = std::move(pending_tr_backups_); has_pending_tr_backups_ = false; tr_backups_loading_ = false; dirty = true; }
        if (has_pending_images_) {
            tr_images_.clear();
            for (const auto& x : pending_images_["images"].items()) if (!x["item"].as_string().empty()) tr_images_[x["item"].as_string()] = x;
            has_pending_images_ = false; tr_images_loading_ = false; dirty = true;
        }
        if (has_pending_full_info_) { tr_full_info_ = std::move(pending_full_info_); has_pending_full_info_ = false; tr_full_loading_ = false; dirty = true; }
    }
    if (in.quit_progress != quit_ || in.connected != connected_pad_) { quit_ = in.quit_progress; connected_pad_ = in.connected; dirty = true; }
    if (!toast_.empty() && now > toast_until_) { toast_.clear(); dirty = true; }
    // Toutes les secondes : progression des tâches, temps écoulé de la connexion.
    static std::uint64_t last_tick = 0;
    if (now - last_tick > 1000) { last_tick = now; dirty = true; }
    if (countdown_until_ && now >= countdown_until_) {
        const int kind = countdown_kind_;
        countdown_until_ = 0;
        dirty = true;
        if (kind == 1) {
            started_ = now;  // le moteur revient en quelques secondes : pas d'écran « injoignable » entre-temps
            toast(tr("Redémarrage du moteur…"));
            model_.command("POST", "/api/v2/update/restart-engine", "{}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
        } else if (kind == 2) {
            toast(tr("Redémarrage de l'application…"));
            model_.command("POST", "/api/v2/update/relaunch-app", "{\"delay\":1}", [this](const std::string&, const std::string&) { quit_requested_ = true; });
        }
    }
    // Moteur perdu depuis quand (la fenêtre de choix n'arrive qu'après 30 s : relance, mise à jour…)
    if (snap_->connected) lost_at_ = 0;
    else if (!lost_at_) { lost_at_ = now; dirty = true; }
    if (lost_at_ && now / 1000 != lost_tick_) { lost_tick_ = now / 1000; dirty = true; }  // la fenêtre arrive à 30 s
    // Mises à jour : recherche à l'ouverture de l'appli et à chaque entrée dans Paramètres (le moteur ignore si la
    // dernière date de moins d'une minute, ou si « Rechercher automatiquement » est à Non).
    if (snap_->connected && (!update_opened_checked_ || (tab_ == settings && !update_in_settings_))) {
        update_opened_checked_ = true;
        model_.command("POST", "/api/v2/update/check", "{\"auto\":true}", [](const std::string&, const std::string&) {});
    }
    update_in_settings_ = tab_ == settings;
    // Le moteur vient de changer de version (mise à jour récupérée d'une autre PS5…) : la pastille passe au vert une minute.
    if (snap_->connected && !snap_->version.empty()) {
        if (seen_engine_v_.empty()) seen_engine_v_ = snap_->version;
        else if (snap_->version != seen_engine_v_) { seen_engine_v_ = snap_->version; engine_changed_at_ = now; dirty = true; }
    }
    // Le moteur tourne une version plus récente que cette appli (mise à jour installée et moteur relancé) : l'appli
    // ouverte reste l'ancienne tant qu'on ne la rouvre pas. On le propose une fois par version — jamais d'office.
    // Avec un minuteur de 15 s (sans « Plus tard » ni Rond, elle se rouvre) — seulement si Backup Center est l'appli au
    // premier plan : jamais par-dessus un jeu (sinon la fenêtre attend, sans minuteur).
    if (snap_->connected && !snap_->version.empty() && popup_ == 0 && whats_new_.empty() && reopen_offered_ != snap_->version && version_cmp(snap_->version, app_version) > 0) {  // seulement un moteur PLUS RÉCENT que l'appli (pas l'inverse)
        reopen_offered_ = snap_->version;
        const std::string v = snap_->version;
        offer_reopen(snap_->version);
    }
    // Mise à jour installée mais moteur pas encore relancé (installée à la main, reçue, ou console redémarrée entre-temps) :
    // on le propose une fois par version, avec un minuteur de 15 s si Backup Center est au premier plan ; jamais pendant
    // une copie (on attend qu'elle finisse). Avec « Installer automatiquement », le moteur se relance déjà seul.
    {
        const auto& u = snap_->update;
        bool busy = false;
        for (const auto& t : snap_->tasks) if (t.state == "running") busy = true;
        if (snap_->connected && popup_ == 0 && whats_new_.empty() && !snap_->update_auto_install && u.engine_restart_needed && !u.installed.empty() &&
            u.installed != snap_->version && engine_offered_ != u.installed && !busy && !countdown_until_) {
            engine_offered_ = u.installed;
            offer_engine_restart(u.installed);
        }
    }
    if (reopen_popup_) {
        if (popup_ == 0) { reopen_popup_ = false; reopen_deadline_ = 0; }  // fermée (Rond)
        else if (reopen_deadline_ && now >= reopen_deadline_) {           // rien choisi : on rouvre (ou on relance le moteur)
            reopen_popup_ = false; reopen_deadline_ = 0; popup_ = 0;
            countdown_kind_ = reopen_kind_; countdown_until_ = now;
        } else if (reopen_deadline_) dirty = true;                       // la barre avance
    }
    // « Quoi de neuf » : une fois par version de l'app, demandé au moteur (liste installée avec l'app).
    if (!whats_new_checked_ && snap_->connected && snap_->settings_loaded) {
        whats_new_checked_ = true;
        if (snap_->tv_seen_version != app_version)
            model_.command("GET", "/api/v2/changelog", "", [this, seen = snap_->tv_seen_version](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j)) return;
                std::vector<ChangelogEntry> list;
                for (const auto& v : j.items()) {
                    if (v["version"].as_string() == seen || list.size() >= 2) break;
                    ChangelogEntry c;
                    c.version = v["version"].as_string();
                    c.date = v["date"].as_string();
                    c.title = v["title"].as_string();
                    for (const auto& n : v["new"].items()) c.added.push_back(n.as_string());
                    for (const auto& n : v["improved"].items()) c.improved.push_back(n.as_string());
                    for (const auto& n : v["fixed"].items()) c.fixed.push_back(n.as_string());
                    list.push_back(std::move(c));
                }
                std::lock_guard<std::mutex> l(async_mu_);
                pending_whats_new_ = std::move(list);
                has_pending_whats_new_ = !seen.empty();  // tout premier lancement : on ne montre rien
            });
#if defined(PBCK_PS5)  // un aperçu sur le Mac ne marque pas les nouveautés comme vues à la place de la télé
        model_.command("POST", "/api/v2/settings", std::string("{\"tv_seen_version\":") + json_str(app_version) + "}", nullptr);
#endif
    }
    // Gestion des backups : comparaison du contenu d'un jeu terminée → résumé relu ; corbeille : fin de la série.
    {
        std::string id;
        { std::lock_guard<std::mutex> l(async_mu_); id = mg_task_; }
        if (!id.empty())
            for (const auto& t : snap_->tasks)
                if (t.id == id && (t.state == "done" || t.state == "failed" || t.state == "cancelled")) {
                    { std::lock_guard<std::mutex> l(async_mu_); mg_task_.clear(); }
                    if (t.state != "done") toast(t.error.empty() ? std::string(tr("Comparaison impossible")) : t.error);
                    else toast(tr("Contenu comparé : les versions identiques ont la même lettre"));
                    mg_task_game_.clear();
                    mg_load_summary();
                    dirty = true;
                    break;
                }
        if (mg_deleting_) { dirty = true; if (mg_del_finished_) mg_finish_delete(); }
    }
    // Restauration d'une sauvegarde PS5 : vérification terminée → confirmation ; restauration terminée → résultat.
    if (rs_stage_ == 1 || rs_stage_ == 3) {
        std::string id;
        { std::lock_guard<std::mutex> l(async_mu_); id = rs_task_; }
        if (!id.empty())
            for (const auto& t : snap_->tasks)
                if (t.id == id && (t.state == "done" || t.state == "failed" || t.state == "cancelled")) {
                    { std::lock_guard<std::mutex> l(async_mu_); rs_task_.clear(); }
                    dirty = true;
                    if (t.state != "done") { toast(t.error.empty() ? tr("Échec") : t.error); rs_stage_ = 0; break; }
                    if (rs_stage_ == 1) {
                        rs_resign_ = false;
                        for (const auto& i : t.report["checked"].items()) if (!i["match"].as_bool(true)) rs_resign_ = true;
                        rs_stage_ = 2;
                        open_popup(8, rs_resign_ ? tr("Vérifiée · elle vient d'un autre compte") : tr("Vérifiée · prête à être remise"),
                                   {rs_resign_ ? tr("La porter sur ce compte et la remettre") : tr("Remettre cette sauvegarde (l'actuelle est copiée avant)"), tr("Annuler")}, 0);
                    } else {
                        rs_stage_ = 0;
                        toast(trf("Restaurée : %lld image(s) vérifiée(s). Lancez le jeu pour confirmer.", static_cast<long long>(t.report["verified"].as_int())));
                        overview_loaded_ = 0;
                    }
                    break;
                }
    }
    // Tâche de synchronisation en cours : vérification terminée → confirmation ; transfert terminé → liste relue.
    if (sync_stage_ == 1 || sync_stage_ == 3) {
        std::string id;
        { std::lock_guard<std::mutex> l(async_mu_); id = sync_task_; }
        if (!id.empty())
            for (const auto& t : snap_->tasks)
                if (t.id == id && (t.state == "done" || t.state == "failed" || t.state == "cancelled")) {
                    { std::lock_guard<std::mutex> l(async_mu_); sync_task_.clear(); }
                    if (t.state != "done") { toast(t.error.empty() ? tr("Échec") : t.error); sync_stage_ = 0; }
                    else if (sync_stage_ == 1) { sync_check_ = t.report; sync_stage_ = 2; }
                    else {
                        sync_stage_ = 0;
                        sync_cache_.erase(sync_key_);  // un côté a changé : on relit
                        sync_key_.clear();
                        // Envoi vers la Switch : il reste à restaurer avec JKSV → guide pas à pas.
                        if (sync_dir_ == 1) { guide_file_ = sync_check_["target"].as_string(); guide_ = true; }
                        else toast(tr("Synchronisé"));
                    }
                    dirty = true;
                    break;
                }
    }
    const std::uint32_t k = in.pressed;
    if (hold_open_) {  // « maintenir Carré 5 s » : suivi à chaque image, même sans nouvel appui
        if (in.down & key::square) {
            if (!hold_since_) hold_since_ = now;
            if (now - hold_since_ >= 5000) { hold_open_ = false; hold_since_ = 0; auto f = hold_fn_; hold_fn_ = nullptr; if (f) f(); }
        } else hold_since_ = 0;
        if (k & key::circle) { hold_open_ = false; hold_since_ = 0; hold_fn_ = nullptr; toast(tr("Suppression annulée")); }
        return true;
    }
    if (!k) return dirty;
    key_sound(k);
    if (k & key::touchpad) {  // pavé tactile : retour à l'Accueil, d'où qu'on soit (rien n'est annulé en cours de route)
        if (!kb_open_ && !hold_open_ && !mg_deleting_) {
            whats_new_.clear(); popup_ = false; sheet_ = false; rsheet_ = false; br_open_ = false; pe_open_ = false; pp_open_ = false; em_open_ = false; ed_open_ = false; cl_open_ = false;
            mg_preview_ = false; mg_ign_open_ = false; sy_pick_ = false; loupe_ = false;
            tab_ = home; home_zone_ = 0;
            return true;
        }
    }
    if (!whats_new_.empty()) { if (k & (key::cross | key::circle)) whats_new_.clear(); return true; }
    if (kb_open_) { input_keyboard(k); return true; }
    if (br_open_) { input_browser(k); return true; }
    if (pe_open_) { input_plan_editor(k); return true; }
    if (pp_open_) { input_item_plans(k); return true; }
    if (em_open_) { input_emulator(k); return true; }
    if (cl_open_) { input_cloud_link(k); return true; }
    if (ed_open_) { input_editor(k); return true; }
    if (popup_) { input_popup(k); return true; }
    if (guide_) { if (k & (key::cross | key::circle)) guide_ = false; return true; }
    if (countdown_until_ && (k & (key::cross | key::circle))) { countdown_until_ = 0; toast(tr("Redémarrage annulé")); return true; }

    // Moteur injoignable : choix explicite (démarrer / réessayer / continuer). Si on l'avait déjà (mise à jour, relance
    // du moteur, moment chargé), on attend d'abord 30 s sans rien demander : « Reconnexion… » en haut.
    if (engine_choice_needed()) {
        if (k & (key::left | key::up)) engine_choice_ = std::max(0, engine_choice_ - 1);
        if (k & (key::right | key::down)) engine_choice_ = std::min(2, engine_choice_ + 1);
        if (k & key::cross) {
            if (engine_choice_ == 0) { start_engine_ = true; started_ = now; toast(tr("Démarrage du moteur…")); }
            else if (engine_choice_ == 1) started_ = now;
            else offline_ok_ = true;
        }
        return true;
    }
    if (mg_preview_ || mg_ign_open_) { input_manage(k); return true; }  // aperçu avant la corbeille, ignorer (Gérer les backups, fiche, Bibliothèque)
    if (sheet_) { input_sheet(k); return true; }
    if (rsheet_) { input_remote_sheet(k); return true; }
    // Le cœur et la loupe font partie du parcours L1 / R1, entre Tâches et la roue des Paramètres.
    if (heart_) {  // Croix : Paramètres › Soutenir le projet
        if (k & key::r1) { heart_ = false; loupe_ = true; }
        else if (k & key::l1) heart_ = false;  // retour sur Tâches
        else if (k & key::cross) { heart_ = false; tab_ = settings; set_section_ = sec_support; set_zone_ = 1; set_row_ = 0; support_big_ = false; }
        else if (k & (key::circle | key::down)) heart_ = false;
        return true;
    }
    if (loupe_) {
        if (k & key::r1) { loupe_ = false; tab_ = settings; }
        else if (k & key::l1) { loupe_ = false; heart_ = true; }  // retour sur le cœur
        else if (k & key::cross) { loupe_ = false; tab_ = library; lib_zone_ = 1; input_library(key::triangle); }
        else if (k & (key::circle | key::down)) loupe_ = false;
        return true;
    }
    if ((k & key::r1) && tab_ == tasks) { heart_ = true; bar_ = false; return true; }
    if ((k & key::l1) && tab_ == settings) { loupe_ = true; tab_ = tasks; bar_ = false; return true; }
    if (k & key::l1) { tab_ = static_cast<Tab>((tab_ + tab_count - 1) % tab_count); bar_ = false; return true; }
    if (k & key::r1) { tab_ = static_cast<Tab>((tab_ + 1) % tab_count); bar_ = false; return true; }
    switch (tab_) {
        case home: input_home(k); break;
        case library: input_library(k); break;
        case destinations: input_destinations(k); break;
        case parties: input_parties(k); break;
        case backups: input_backups_tab(k); break;
        case synchro: input_synchro(k); break;
        case transfers: input_transfers(k); break;
        case tasks: input_tasks(k); break;
        case settings: input_settings(k); break;
        default: break;
    }
    return true;
}

void Ui::input_library(std::uint32_t k) {
    // Carré : catégorie suivante (Tout, PS5, PS4, Applications, ROM), depuis la liste.
    if (k & key::square) { kind_ = (kind_ + 1) % kind_count; lib_filter_ = kind_; grid_ = grid_top_ = 0; lib_row_ = 0; return; }
    const auto items = visible_items();
    if (lib_zone_ == 0) {  // rangée de filtres
        constexpr int chips = kind_count + 4;  // Tout, PS5, PS4, Applications, ROM, Émulateur, Emplacement, Actualiser, Console
        const int was = lib_filter_;
        if (k & key::left) lib_filter_ = std::max(0, lib_filter_ - 1);
        if (k & key::right) lib_filter_ = std::min(chips - 1, lib_filter_ + 1);
        // Les types (Tout, PS5, PS4, Applications, ROM, Ignorés) s'appliquent dès qu'on passe dessus, comme des onglets.
        // Mais si on ne fait que les traverser pour aller aux réglages (Émulateur… Console), on garde le type d'avant :
        // sinon on arrivait toujours sur « Ignorés ».
        if (lib_kind_before_ < 0) lib_kind_before_ = kind_;
        if (was >= kind_count && lib_filter_ < kind_count) lib_kind_before_ = kind_;  // on revient dans les types
        if ((k & (key::left | key::right)) && lib_filter_ < kind_count && kind_ != lib_filter_) { kind_ = lib_filter_; grid_ = grid_top_ = 0; }
        if (was < kind_count && lib_filter_ >= kind_count && kind_ != lib_kind_before_) { kind_ = lib_kind_before_; grid_ = grid_top_ = 0; }
        if (k & (key::down | key::circle)) { lib_zone_ = 1; lib_kind_before_ = kind_; }
        if (k & key::cross) {
            if (lib_filter_ < kind_count) { kind_ = lib_filter_; lib_kind_before_ = kind_; lib_zone_ = 1; }
            // Émulateur / emplacement : chaque appui passe au suivant, directement dans la rangée (sans fenêtre).
            else if (lib_filter_ == kind_count) app_ = (app_ + 1) % (static_cast<int>(app_filters().size()) + 1);
            else if (lib_filter_ == kind_count + 1 && !lib_peer_.empty()) lib_presence_ = (lib_presence_ + 1) % 3;
            else if (lib_filter_ == kind_count + 1) {
                int n = 0;
                for (const auto& p : snap_->places) if (p.type == "internal" || p.type == "usb") ++n;
                place_ = (place_ + 1) % (n + 1);
            } else if (lib_filter_ == kind_count + 3) cycle_lib_console();
            else if (!lib_peer_.empty()) load_peer_library(true);
            else { model_.command("POST", "/api/v2/scan", "{}"); toast(tr("Actualisation demandée")); }
            grid_ = grid_top_ = 0;
        }
        return;
    }
    // Rangées (comme l'Accueil) : Gauche / Droite dans la rangée, Haut / Bas d'une rangée à l'autre.
    const auto shelves = library_shelves();
    (void)items;
    if (shelves.empty()) { if (k & key::up) lib_zone_ = 0; }
    else {
        lib_cols_.resize(shelves.size(), 0);
        lib_row_ = std::min(lib_row_, static_cast<int>(shelves.size()) - 1);
        int& col = lib_cols_[static_cast<std::size_t>(lib_row_)];
        const int n = static_cast<int>(shelves[static_cast<std::size_t>(lib_row_)].items.size());
        if (k & key::left) col = std::max(0, col - 1);
        if (k & key::right) col = std::min(n - 1, col + 1);
        if (k & key::r2) col = std::min(n - 1, col + 7);
        if ((k & key::l2) && col < n) {  // marquer / démarquer (ici : « Ignorer » ; autre PS5 : actions groupées)
            const std::string& key2 = shelves[static_cast<std::size_t>(lib_row_)].items[static_cast<std::size_t>(col)]->key;
            if (!lib_marks_.erase(key2)) lib_marks_.insert(key2);
        }
        const int keep = col;  // grille : on garde la colonne en changeant de rangée
        auto go = [&](int r) { lib_row_ = r; int& c2 = lib_cols_[static_cast<std::size_t>(r)]; c2 = std::min(keep, static_cast<int>(shelves[static_cast<std::size_t>(r)].items.size()) - 1); };
        if ((k & key::down) && lib_row_ + 1 < static_cast<int>(shelves.size())) go(lib_row_ + 1);
        else if (k & key::up) { if (lib_row_ > 0) go(lib_row_ - 1); else lib_zone_ = 0; }
        if ((k & key::cross) && col < n && !lib_peer_.empty()) {
            const Json* m = remote_meta_of(shelves[static_cast<std::size_t>(lib_row_)].items[static_cast<std::size_t>(col)]);
            if (m) open_remote_sheet(lib_peer_, lib_peer_name_, *m, lib_peer_online_);
        } else if ((k & key::cross) && col < n) {
            sheet_ = true; sheet_key_ = shelves[static_cast<std::size_t>(lib_row_)].items[static_cast<std::size_t>(col)]->key; sheet_focus_ = 0; unchecked_.clear(); dest_ = 0;
        }
    }
    if ((k & key::options) && !lib_peer_.empty() && !lib_marks_.empty()) {  // autre console, jeux cochés : une action pour tous
        std::vector<Json> picked;
        for (std::size_t i = 0; i < remote_items_.size() && i < remote_meta_.size(); ++i)
            if (lib_marks_.count(remote_items_[i].key)) picked.push_back(remote_meta_[i]);
        const std::string inst = lib_peer_, name = lib_peer_name_;
        if (!lib_peer_online_) { toast(trf("%s est hors ligne : les actions reviendront quand elle sera allumée.", name.c_str())); return; }
        int with_save = 0, full = 0, newer_there = 0;
        Json tids = Json::array();
        for (const auto& m : picked) {
            const bool so = m["key"].as_string().rfind("save:", 0) == 0;  // sauvegarde sans le jeu
            bool has_save = so;
            for (const auto& c : m["components"].items()) if (c["part"].as_string() == "save") has_save = true;
            with_save += has_save;
            full += !so && !m["components"].items().empty();
            if (m["save"].as_string() == "there" && !m["title_id"].as_string().empty()) { ++newer_there; tids.push(m["title_id"]); }
        }
        auto each = [this, inst, name, picked](const char* action, bool need_save) {
            int sent = 0;
            for (const auto& m : picked) {
                const bool so = m["key"].as_string().rfind("save:", 0) == 0;
                bool has_save = so;
                for (const auto& c : m["components"].items()) if (c["part"].as_string() == "save") has_save = true;
                if (need_save ? !has_save : (so || m["components"].items().empty())) continue;
                Json b = Json::object();
                b.set("instance", inst); b.set("action", action); b.set("item", m["key"]);
                model_.command("POST", "/api/v2/link/peer-action", b.dump(), [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
                ++sent;
            }
            toast(trf("%s : %d sauvegarde(s) lancée(s) là-bas (onglet Tâches)", name.c_str(), sent));
            lib_marks_.clear();
        };
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        if (with_save) acts.push_back({trf("Sauvegarder leurs parties là-bas (%d)", with_save), [each] { each("backup_saves", true); }});
        if (full) acts.push_back({trf("Faire un backup complet là-bas (%d)", full), [each] { each("backup", false); }});
        if (newer_there) acts.push_back({trf("Prendre ici les parties plus récentes là-bas (%d)", newer_there), [this, inst, tids] {
            Json b = Json::object();
            b.set("peer", inst); b.set("title_ids", tids); b.set("one", true);
            model_.command("POST", "/api/v2/sync/parties/start", b.dump(), [this](const std::string& e, const std::string&) {
                post_toast(!e.empty() ? e : std::string(tr("Parties : mise à niveau lancée (l'ancienne est sauvegardée d'abord)")));
            });
            lib_marks_.clear();
        }});
        acts.push_back({tr("Tout décocher"), [this] { lib_marks_.clear(); }});
        acts.push_back({tr("Annuler"), [] {}});
        open_actions(trf("%d élément(s) coché(s) sur %s", static_cast<int>(picked.size()), name.c_str()), std::move(acts));
    } else if ((k & key::options) && !lib_peer_.empty()) {  // autre console : la fiche distante (ses actions)
        if (!shelves.empty()) {
            const auto& sh = shelves[static_cast<std::size_t>(lib_row_)];
            const int c = lib_cols_[static_cast<std::size_t>(lib_row_)];
            if (c < static_cast<int>(sh.items.size())) { const Json* m = remote_meta_of(sh.items[static_cast<std::size_t>(c)]); if (m) open_remote_sheet(lib_peer_, lib_peer_name_, *m, lib_peer_online_); }
        }
    } else if (k & key::options) {  // menu d'actions : sur les éléments cochés, sinon sur celui sous le curseur
        const Item* focus = nullptr;
        if (!shelves.empty()) {
            const auto& sh = shelves[static_cast<std::size_t>(lib_row_)];
            const int c = lib_cols_[static_cast<std::size_t>(lib_row_)];
            if (c < static_cast<int>(sh.items.size())) focus = sh.items[static_cast<std::size_t>(c)];
        }
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        if (!lib_marks_.empty()) {
            acts.push_back({trf("Ignorer… (%d)", static_cast<int>(lib_marks_.size())), [this] {
                std::vector<IgnTarget> t;
                for (const auto& it : snap_->items)
                    if (lib_marks_.count(it.key) && !it.ignored) t.push_back({it.title_id.empty() ? "key" : "title_id", it.title_id.empty() ? it.key : it.title_id, it.title});
                if (t.empty()) { toast(tr("Les éléments marqués sont déjà ignorés")); return; }
                open_ignore(std::move(t), -1);
            }});
            acts.push_back({tr("Tout décocher"), [this] { lib_marks_.clear(); }});
        } else if (focus) {
            const std::string key2 = focus->key, tid = focus->title_id, title2 = focus->title;
            const bool ign = focus->ignored;
            acts.push_back({tr("Toutes les versions"), [this, key2, tid, title2] { open_versions("", tid, title2, key2); }});
            if (ign) acts.push_back({tr("Sauvegarder de nouveau"), [this, key2] { for (const auto& it : snap_->items) if (it.key == key2) { set_ignored(it, false); break; } }});
            else acts.push_back({tr("Ignorer…"), [this, key2, tid, title2] { open_ignore({{tid.empty() ? "key" : "title_id", tid.empty() ? key2 : tid, title2}}, -1); }});
            if (has_console_save(tid)) acts.push_back({tr("Supprimer la partie de la console…"), [this, tid, title2] { delete_console_save(tid, title2); }});
            acts.push_back({tr("Tout cocher"), [this] { for (const auto& sh : library_shelves()) for (const Item* it : sh.items) lib_marks_.insert(it->key); }});
            // Désinstaller (jeu ou appli installés ici ; jamais Backup Center ; pas une sauvegarde seule).
            if ((focus->kind == "game_ps5" || focus->kind == "game_ps4" || focus->kind == "app") && focus->key.rfind("save:", 0) != 0 &&
                tid.size() == 9 && tid != "PPSA99950" && (tid.rfind("PPSA", 0) == 0 || tid.rfind("CUSA", 0) == 0)) {
                const std::string detail = focus->image_type + (focus->location.empty() ? std::string() : "  ·  " + focus->location);
                acts.push_back({tr("Désinstaller de cette PS5…"), [this, tid, title2, detail] { uninstall_flow("", "", tid, title2, detail); }});
            }
        }
        open_actions(lib_marks_.empty() ? (focus ? focus->title : std::string(tr("Bibliothèque"))) : trf("%d coché(s)", static_cast<int>(lib_marks_.size())), std::move(acts));
    }
    if (k & key::triangle) open_keyboard(tr("Rechercher dans la bibliothèque"), search_, [this](const std::string& t) {
        search_ = fold(t);
        grid_ = grid_top_ = 0;
    });
}

// ---- Clavier à l'écran ----
namespace {
// Trois pages : lettres (avec les accents utiles), majuscules (L1), caractères spéciaux (R1).
const char* const kb_keys[4][10] = {{"a", "z", "e", "r", "t", "y", "u", "i", "o", "p"},
                                    {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m"},
                                    {"w", "x", "c", "v", "b", "n", "é", "è", "à", "ç"},
                                    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}};
const char* const kb_shift_keys[4][10] = {{"A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P"},
                                          {"Q", "S", "D", "F", "G", "H", "J", "K", "L", "M"},
                                          {"W", "X", "C", "V", "B", "N", "É", "È", "À", "Ç"},
                                          {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}};
const char* const kb_sym_keys[4][10] = {{"!", "@", "#", "$", "%", "&", "*", "(", ")", "?"},
                                        {"-", "_", "=", "+", "/", "\\", "|", "~", "^", "\""},
                                        {".", ",", ":", ";", "'", "`", "[", "]", "{", "}"},
                                        {"<", ">", "€", "£", "°", "§", "µ", "²", "«", "»"}};
// Services S3 proposés par leur nom : identifiant pour le moteur, nom affiché, région par défaut.
struct S3Service { const char* id; const char* name; const char* region; };
const S3Service kS3Services[] = {{"aws", "Amazon S3", "eu-west-3"}, {"b2", "Backblaze B2", "us-west-004"}, {"wasabi", "Wasabi", "eu-central-1"},
                                 {"r2", "Cloudflare R2", "auto"}, {"scaleway", "Scaleway", "fr-par"}, {"ovh", "OVHcloud", "gra"},
                                 {"c2", "Synology C2", "eu-002"}, {"other", N_("Autre S3"), ""}};
const S3Service& s3_service(const std::string& id) {
    for (const auto& x : kS3Services) if (id == x.id) return x;
    return kS3Services[0];
}
// L'adresse se saisit pour « Autre S3 » ; pour Cloudflare R2, c'est le n° de compte. Ailleurs : d'après la région.
bool s3_host_typed(const std::string& id) { return id == "other" || id == "r2"; }
// Rangée du bas : chaque bouton porte la touche de la manette qui fait la même chose.
const char* const kb_actions[5] = {N_("Maj"), "#+=", N_("Espace"), N_("Effacer"), N_("Terminé")};
const char* const kb_action_keys[5] = {"L1", "R1", nullptr, nullptr, "R2"};  // △ et □ : dessinés (icon)
}  // namespace

void Ui::open_takeover(const std::string& from, const std::string& user_id, const std::string& target_name) {
    tk_open_ = true; tk_loading_ = true; tk_row_ = 0; tk_ = Json(); tk_sel_.clear();
    tk_from_ = from; tk_user_ = user_id; tk_target_name_ = target_name;
    model_.command("GET", "/api/v2/players/takeover?from=" + from + "&user_id=" + user_id, "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_tk_ = j; has_pending_tk_ = true; }
        else { post_toast(e.empty() ? std::string(tr("Lecture impossible")) : e); tk_loading_ = false; }
    });
}

void Ui::open_keyboard(std::string title, std::string text, std::function<void(const std::string&)> apply) {
    kb_open_ = true;
    kb_title_ = std::move(title);
    kb_text_ = std::move(text);
    kb_apply_ = std::move(apply);
    kb_done_ = nullptr;
    kb_row_ = kb_col_ = 0;
    kb_shift_ = kb_sym_ = kb_secret_ = false;
}

void Ui::input_keyboard(std::uint32_t k) {
    auto changed = [&] { if (kb_apply_) kb_apply_(kb_text_); };
    auto backspace = [&] {
        if (kb_text_.empty()) return;
        std::size_t n = kb_text_.size() - 1;
        while (n > 0 && (static_cast<unsigned char>(kb_text_[n]) & 0xC0) == 0x80) --n;  // un caractère UTF-8 entier
        kb_text_.resize(n);
        changed();
    };
    const int cols = kb_row_ == 4 ? 5 : 10;
    const int from = kb_row_;
    if (k & key::up) kb_row_ = std::max(0, kb_row_ - 1);
    if (k & key::down) kb_row_ = std::min(4, kb_row_ + 1);
    if (kb_row_ != from) kb_col_ = kb_row_ == 4 ? std::min(4, kb_col_ / 2) : from == 4 ? std::min(9, kb_col_ * 2) : kb_col_;
    if (k & key::left) kb_col_ = (kb_col_ + cols - 1) % cols;
    if (k & key::right) kb_col_ = (kb_col_ + 1) % cols;
    if (k & key::square) backspace();
    if (k & key::triangle) { kb_text_ += ' '; changed(); }
    if (k & (key::circle | key::options | key::r2)) { kb_open_ = false; if (kb_done_) kb_done_(); return; }  // le texte reste appliqué
    if (k & key::l2) { kb_text_.clear(); changed(); }
    auto shift = [&] { kb_shift_ = !kb_shift_; kb_sym_ = false; };
    auto sym = [&] { kb_sym_ = !kb_sym_; kb_shift_ = false; };
    if (k & key::l1) shift();
    if (k & key::r1) sym();
    if (k & key::cross) {
        if (kb_row_ < 4) { if (kb_text_.size() < 60) { kb_text_ += (kb_sym_ ? kb_sym_keys : kb_shift_ ? kb_shift_keys : kb_keys)[kb_row_][kb_col_]; changed(); } }
        else if (kb_col_ == 0) shift();
        else if (kb_col_ == 1) sym();
        else if (kb_col_ == 2) { kb_text_ += ' '; changed(); }
        else if (kb_col_ == 3) backspace();
        else { kb_open_ = false; if (kb_done_) kb_done_(); }
    }
}

void Ui::draw_keyboard(Surface& s) {
    const int w = 1100, h = 520, x = (1920 - w) / 2, y = 1080 - h - 90;
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, rgb(18, 34, 58), 30);
    s.outline(x, y, w, h, rgb(124, 196, 255, 90), 30, 2);
    s.text(font_, x + 40, y + 26, kb_title_, body, color::muted);
    // Champ de texte avec un curseur.
    s.rect(x + 40, y + 66, w - 80, 64, rgb(4, 12, 24, 220), 16);
    const std::string shown = kb_secret_ ? std::string(kb_text_.size(), '*') : kb_text_;
    const int tw = s.text(font_, x + 62, y + 79, shown, large, color::text, w - 140);
    if ((now_ / 500) % 2 == 0 || kb_text_.empty()) s.rect(x + 64 + tw, y + 80, 3, 36, color::accent, 1);
    if (kb_text_.empty()) s.text(font_, x + 76, y + 79, tr("Tapez quelques lettres…"), large, rgb(110, 130, 155));
    const int kw = 92, kh = 62, gx = x + (w - (10 * kw + 9 * 8)) / 2, gy = y + 156;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 10; ++c) {
            const int kx = gx + c * (kw + 8), ky = gy + r * (kh + 8);
            const bool on = kb_row_ == r && kb_col_ == c;
            s.rect(kx, ky, kw, kh, on ? color::text : rgb(255, 255, 255, 22), 14);
            const std::string label = (kb_sym_ ? kb_sym_keys : kb_shift_ ? kb_shift_keys : kb_keys)[r][c];
            s.text(font_, kx + (kw - Surface::measure(label, large)) / 2, ky + 12, label, large, on ? color::bg : color::text);
        }
    // Rangée du bas : 5 boutons de même largeur, alignés sur les touches ; chacun montre sa touche de manette.
    const int aw = (10 * kw + 9 * 8 - 4 * 8) / 5, ay = gy + 4 * (kh + 8);
    for (int c = 0; c < 5; ++c) {
        const int ax = gx + c * (aw + 8);
        const bool on = kb_row_ == 4 && kb_col_ == c, lit = (c == 0 && kb_shift_) || (c == 1 && kb_sym_);
        s.rect(ax, ay, aw, kh, on ? color::text : lit ? rgb(61, 139, 255, 200) : c == 4 ? rgb(61, 139, 255, 120) : rgb(255, 255, 255, 22), 14);
        const char* label = c == 1 ? kb_actions[c] : tr(kb_actions[c]);
        const int bw = kb_action_keys[c] ? Surface::measure(kb_action_keys[c], small) + 20 : 34;
        const int lw = Surface::measure(label, body), cx = ax + (aw - (bw + 10 + lw)) / 2;
        if (kb_action_keys[c]) {  // L1, R1, R2 : petite pastille avec le nom de la touche
            s.rect(cx, ay + 15, bw, 32, on ? color::bg : rgb(12, 27, 46), 16);
            s.text(font_, cx + 10, ay + 17, kb_action_keys[c], small, on ? color::text : rgb(200, 214, 230));
        } else icon(s, c == 2 ? 3 : 2, cx + 4, ay + 18, color::text);  // △ Espace, □ Effacer
        s.text(font_, cx + bw + 10, ay + 16, label, body, on || lit ? (on ? color::bg : color::text) : color::text);
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);  // les aides de l'écran du dessous laissent la place
    draw_footer(s, {{0, tr("Taper")}, {-1, tr("L2 tout effacer")}, {1, tr("Fermer (le texte reste)")}});
}

void Ui::open_popup(int kind, std::string title, std::vector<std::string> options, int selected) {
    popup_ = kind;
    popup_title_ = std::move(title);
    popup_note_.clear();
    popup_subs_.clear();
    popup_items_ = std::move(options);
    popup_sel_ = std::max(0, std::min(selected, static_cast<int>(popup_items_.size()) - 1));
}

void Ui::input_popup(std::uint32_t k) {
    const int n = static_cast<int>(popup_items_.size());
    if (k & key::up) popup_sel_ = (popup_sel_ + n - 1) % n;
    if (k & key::down) popup_sel_ = (popup_sel_ + 1) % n;
    if (k & key::circle) { popup_ = 0; return; }
    if (!(k & key::cross)) return;
    if (popup_ == 1) { app_ = popup_sel_; lib_zone_ = 1; grid_ = grid_top_ = 0; }
    else if (popup_ == 2) { place_ = popup_sel_; lib_zone_ = 1; grid_ = grid_top_ = 0; }
    else if (popup_ == 4 && popup_sel_ == 0) {  // plan personnalisé : l'éditeur, vide
        Json p = Json::object();
        p.set("name", tr("Mon plan"));
        p.set("timing", "daily");
        p.set("minute_of_day", 21 * 60);
        p.set("weekdays", 0);
        p.set("rule", "");
        p.set("items", Json::array());
        p.set("parts", Json::array());
        p.set("keep", 5);
        p.set("enabled", true);
        popup_ = 0;
        open_plan_editor(p);
        return;
    }
    else if (popup_ == 4) {
        std::string body = plan_presets[popup_sel_ - 1].body;
        const auto& accs = overview_["accounts"];
        for (const auto& a : accs.items()) if (a["on_console"].as_bool()) { body += ",\"rule_arg\":" + json_str(a["user_id"].as_string()); break; }
        body += "}";
        model_.command("POST", "/api/v2/plans", body, [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Plan créé") : e); load_plans(); });
    }
    else if (popup_ == 9) {  // Par disque : mettre une copie à la corbeille
        popup_ = 0;
        if (popup_sel_ != 0) { toast(tr("Rien n'a été supprimé")); return; }
        Json b = Json::object();
        b.set("place", disk_del_place_);
        b.set("bundle", disk_del_bundle_);
        b.set("confirm", true);
        const std::string place = disk_del_place_;
        disk_loading_.insert(place);  // relu après la mise à la corbeille
        model_.command("POST", "/api/v2/backups/delete", b.dump(), [this, place](const std::string& e, const std::string&) {
            post_toast(e.empty() ? std::string(tr("Copie mise à la corbeille (30 jours)")) : trf("Suppression refusée : %s", e.c_str()));
            mg_sum_dirty_ = true;
            model_.command("GET", "/api/v2/backups?place=" + url_encode(place), "", [this, place](const std::string& e2, const std::string& body) {
                Json j;
                if (!e2.empty() || !Json::parse(body, j)) { j = Json::object(); j.set("error", e2.empty() ? std::string(tr("réponse illisible")) : e2); }
                std::lock_guard<std::mutex> l(async_mu_);
                pending_backups_ = std::move(j);
                pending_backups_place_ = place;
                has_pending_backups_ = true;
            });
        });
        overview_loaded_ = 0;
        toast(tr("Mise à la corbeille…"));
        return;
    }
    else if (popup_ == 11) {  // menu d'actions (Options)
        popup_ = 0;
        if (popup_sel_ < static_cast<int>(act_fns_.size())) { auto f = act_fns_[static_cast<std::size_t>(popup_sel_)]; f(); }
        return;
    }
    else if (popup_ == 6) {  // menu du jeu (Sauvegardes)
        const bool has_save = rs_entry_["console"].is_object();
        popup_ = 0;
        if (has_save && popup_sel_ == 0) {
            save_entries({rs_index_});
            return;
        }
        rs_versions_ = Json::array();
        std::vector<const Json*> vs;
        for (const auto& v : rs_entry_["backups"].items()) if (v["complete"].as_bool()) vs.push_back(&v);
        std::sort(vs.begin(), vs.end(), [](const Json* a, const Json* b) { return a->operator[]("created_unix").as_int() > b->operator[]("created_unix").as_int(); });
        std::vector<std::string> labels;
        for (const Json* v : vs) {
            rs_versions_.push(*v);
            std::time_t when = static_cast<std::time_t>((*v)["created_unix"].as_int());
            char b[64];
            std::strftime(b, sizeof b, "%d/%m/%Y %H:%M", std::localtime(&when));
            labels.push_back(std::string(b) + "  ·  " + (*v)["place_label"].as_string());
        }
        open_popup(7, trf("Quelle version de « %s » ?", rs_title_.c_str()), labels, 0);
        return;
    }
    else if (popup_ == 7) {  // version choisie : vérification d'abord
        popup_ = 0;
        if (popup_sel_ < static_cast<int>(rs_versions_.size())) rs_start(rs_versions_[static_cast<std::size_t>(popup_sel_)]);
        return;
    }
    else if (popup_ == 8) {  // confirmation après la vérification
        popup_ = 0;
        if (popup_sel_ != 0) { rs_stage_ = 0; toast(tr("Rien n'a été modifié")); return; }
        Json b = rs_body_;
        b.set("check_only", false);
        b.set("allow_resign", rs_resign_);
        rs_stage_ = 3;
        model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e, const std::string& resp) {
            Json j;
            std::lock_guard<std::mutex> l(async_mu_);
            if (!e.empty() || !Json::parse(resp, j)) { pending_toast_ = e.empty() ? tr("Restauration refusée") : e; rs_stage_ = 0; return; }
            rs_task_ = j["id"].as_string();
        });
        toast(tr("Restauration en cours… (la sauvegarde actuelle est copiée avant)"));
        return;
    }
    else if (popup_ == 5) {
        const std::string a = popup_sel_ < static_cast<int>(img_actions_.size()) ? img_actions_[static_cast<std::size_t>(popup_sel_)] : "";
        popup_ = 0;
        run_image_tool(a);
        return;
    }
    else if (popup_ == 10) {  // langue (Paramètres › Général) : appliquée tout de suite, enregistrée dans le moteur
        const auto& langs = i18n::languages();
        const std::string code = popup_sel_ == 0 || popup_sel_ > static_cast<int>(langs.size()) ? "auto" : langs[static_cast<std::size_t>(popup_sel_ - 1)].code;
        popup_ = 0;
        i18n::apply(code);
        model_.command("POST", "/api/v2/settings", "{\"language\":" + json_str(code) + "}", [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Langue enregistrée") : e); });
        return;
    }
    else if (popup_ == 3) dest_ = popup_sel_ == 0 || popup_sel_ > static_cast<int>(dest_places_.size()) ? 0 : dest_places_[static_cast<std::size_t>(popup_sel_ - 1)] + 1;
    popup_ = 0;
}

// Guide affiché après un envoi vers la Switch : la restauration se termine dans JKSV.
void Ui::draw_guide(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int x = 260, w = 1400, y = 110, h = 860;
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    s.text(font_, x + 48, y + 36, tr("Dernière étape, sur la Switch"), large, color::text);
    const std::string file = guide_file_.substr(guide_file_.rfind('/') + 1);
    const std::string game = guide_file_.size() > 6 ? guide_file_.substr(6, guide_file_.rfind('/') - 6) : "";
    const std::string steps[] = {
        tr("Sur la Switch, ouvrez JKSV (menu des homebrews)."),
        tr("À gauche, choisissez votre profil (son avatar)."),
        trf("À droite, allez sur « %s » et appuyez sur A.", game.c_str()),
        tr("Conseil : choisissez d'abord « Nouveau » pour garder une copie de la partie actuelle de la Switch."),
        trf("Choisissez le fichier « %s » (il se termine par « PBC ») et appuyez sur Y pour le restaurer.", file.c_str()),
        tr("Lancez le jeu avec ce profil : vous retrouvez la partie envoyée depuis la PS5."),
    };
    int yy = y + 120;
    for (int i = 0; i < 6; ++i) {
        s.rect(x + 48, yy, 52, 52, color::accent2, 26);
        const std::string n = std::to_string(i + 1);
        s.text(font_, x + 74 - Surface::measure(n, body) / 2, yy + 8, n, body, color::text);
        s.text(font_, x + 124, yy + 8, steps[i], body, color::text, w - 172);
        yy += Surface::measure(steps[i], body) > w - 172 ? 112 : 84;
    }
    s.text(font_, x + 48, y + h - 70, tr("Rien n'a été effacé sur la Switch. Si la partie ne convient pas, restaurez la copie faite à l'étape 4."), small, color::muted, w - 96);
    s.rect(0, 990, 1920, 90, color::bg);
    draw_footer(s, {{0, tr("Compris")}});
}

void Ui::draw_popup(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int n = static_cast<int>(popup_items_.size()), shown = std::min(n, 9);
    // Largeur : assez pour le titre (rien ne sort du cadre), entre 760 et 1600.
    int widest = Surface::measure(popup_title_, large) + 96;
    for (std::size_t i = 0; i < popup_items_.size(); ++i) {  // assez large pour chaque ligne (et son explication)
        const bool sub = i < popup_subs_.size() && !popup_subs_[i].empty();
        widest = std::max(widest, Surface::measure(popup_items_[i], body) + (sub ? Surface::measure(popup_subs_[i], small) + 24 : 0) + 116);
    }
    const int w = std::max(popup_note_.empty() ? 760 : 1000, std::min(1760, widest));
    // Paragraphe sous le titre, coupé aux mots.
    std::vector<std::string> note;
    {
        std::string line, word;
        auto flush = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && Surface::measure(t, small) > w - 80) { note.push_back(line); line = word; } else line = t;
            word.clear();
        };
        for (char c : popup_note_) {
            if (c == '\n') { flush(); note.push_back(line); line.clear(); continue; }  // retour à la ligne voulu
            if (c == ' ') flush(); else word += c;
        }
        flush();
        if (!line.empty()) note.push_back(line);
    }
    const int note_h = note.empty() ? 0 : static_cast<int>(note.size()) * 36 + 16;
    const bool timer = reopen_popup_ && reopen_deadline_;  // « rouvrir l'appli » : minuteur et barre
    const int h = 150 + note_h + shown * 64 + (timer ? 56 : 0), x = (1920 - w) / 2, y = (1080 - h) / 2;
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    s.text(font_, x + 40, y + 30, popup_title_, large, color::text, w - 80);
    for (std::size_t i = 0; i < note.size(); ++i) s.text(font_, x + 40, y + 96 + static_cast<int>(i) * 36, note[i], small, color::muted, w - 80);
    const int first = std::max(0, std::min(popup_sel_ - shown / 2, n - shown));
    for (int i = 0; i < shown; ++i) {
        const int idx = first + i, yy = y + 104 + note_h + i * 64;
        if (idx == popup_sel_) { s.rect(x + 24, yy, w - 48, 54, color::panel2, 27); s.outline(x + 20, yy - 4, w - 40, 62, color::accent, 31, 3); }
        s.text(font_, x + 52, yy + 11, popup_items_[static_cast<std::size_t>(idx)], body, idx == popup_sel_ ? color::text : color::muted, w - 104);
        if (static_cast<std::size_t>(idx) < popup_subs_.size() && !popup_subs_[static_cast<std::size_t>(idx)].empty()) {
            const int lx = x + 52 + Surface::measure(popup_items_[static_cast<std::size_t>(idx)], body) + 24;
            s.text(font_, lx, yy + 17, popup_subs_[static_cast<std::size_t>(idx)], small, color::muted, x + w - 40 - lx);
        }
    }
    if (timer) {
        const std::uint64_t left = reopen_deadline_ > now_ ? reopen_deadline_ - now_ : 0;
        const int bw = w - 80, fill = static_cast<int>(static_cast<double>(left) / 15000.0 * bw);
        s.text(font_, x + 40, y + h - 60, reopen_kind_ == 1 ? trf("Relance automatique du moteur dans %d s", static_cast<int>(left / 1000 + 1))
                                                            : trf("Réouverture automatique dans %d s", static_cast<int>(left / 1000 + 1)), small, color::warn, bw);
        s.rect(x + 40, y + h - 26, bw, 8, color::line, 4);
        s.rect(x + 40, y + h - 26, std::max(8, fill), 8, color::warn, 4);
    }
    s.rect(0, 990, 1920, 90, color::bg);  // la barre du bas de la liste remplace celle de l'écran
    draw_footer(s, {{0, tr("Choisir")}, {1, tr("Fermer")}});
}

void Ui::input_sheet(std::uint32_t k) {
    const Item* it = nullptr;
    for (const auto& x : snap_->items) if (x.key == sheet_key_) it = &x;
    if (!it || (k & key::circle)) { sheet_ = false; return; }
    const int comps = static_cast<int>(it->components.size());
    // Ordre de focus : composants, destination, Sauvegarder, Fermer, puis « Plans… » et « Parcourir ».
    // Deuxième rangée : Plans…, Parcourir, Toutes les versions, Ne plus sauvegarder… (comps + 3 à comps + 6).
    // Haut / Bas : composants un par un ; dans les boutons, la ligne au-dessus / en dessous, au bouton le plus proche
    // (les boutons passent à la ligne quand la place manque : on n'en saute aucun).
    auto btn = [&](int f) -> const SheetBtn* { for (const auto& b : sheet_btns_) if (b.focus == f) return &b; return nullptr; };
    auto nearest = [&](int line, int cx) {
        int best = -1, d = 1 << 30;
        for (const auto& b : sheet_btns_) if (b.line == line && std::abs(b.cx - cx) < d) { d = std::abs(b.cx - cx); best = b.focus; }
        return best;
    };
    const SheetBtn* cur = btn(sheet_focus_);
    if (k & key::up) {
        if (!cur) sheet_focus_ = std::max(0, sheet_focus_ - 1);
        else if (cur->line == 0) sheet_focus_ = comps > 0 ? comps - 1 : sheet_focus_;
        else { const int f = nearest(cur->line - 1, cur->cx); if (f >= 0) sheet_focus_ = f; }
    } else if (k & key::down) {
        if (!cur) sheet_focus_ = std::min(comps, sheet_focus_ + 1);
        else { const int f = nearest(cur->line + 1, cur->cx); if (f >= 0) sheet_focus_ = f; }
    } else if (cur && (k & (key::left | key::right))) {  // gauche / droite : le voisin sur la même ligne, puis la ligne d'à côté
        int i = 0;
        for (std::size_t j = 0; j < sheet_btns_.size(); ++j) if (sheet_btns_[j].focus == sheet_focus_) i = static_cast<int>(j);
        const int n = static_cast<int>(sheet_btns_.size());
        i = (k & key::left) ? std::max(0, i - 1) : std::min(n - 1, i + 1);
        sheet_focus_ = sheet_btns_[static_cast<std::size_t>(i)].focus;
    }
    if ((k & key::cross) && sheet_focus_ == comps + 8) { open_image_tools(*it); return; }
    if ((k & key::cross) && sheet_focus_ == comps + 9) { delete_console_save(it->title_id, it->title); return; }
    if ((k & key::cross) && sheet_focus_ == comps + 7) {  // envoyer ce jeu à une PS5 jumelée et l'y installer
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        const std::string key = it->key, title = it->title;
        for (const auto& pl : snap_->places) {
            if (pl.type != "peer") continue;
            const std::string id = pl.id, label = pl.label;
            acts.push_back({label + "  ·  " + trf("%s libres", size_text(pl.free).c_str()), [this, id, key, title, label] {
                Json ck = Json::object();
                ck.set("place", id);
                ck.set("item", key);
                system_check_then("/api/v2/link/send-check", ck, title, [this, id, key, title, label] { choose_format(trf("Format de « %s » sur %s", title.c_str(), label.c_str()), [this, id, key, title, label](const std::string& fmt) {
                    // Compte de l'autre PS5 qui reçoit la sauvegarde du jeu (comme pour une sauvegarde seule).
                    const std::string inst = id.substr(5, id.find(':', 5) - 5);
                    pick_account(inst, label, [this, id, key, title, label, fmt](const std::string& uid, const std::string&) {
                        auto go = [this, id, key, title, label, fmt, uid](bool keep) {
                            Json b = Json::object();
                            b.set("place", id);
                            b.set("item", key);
                            b.set("format", fmt);
                            b.set("user_id", uid);
                            b.set("keep_copy", keep);
                            model_.command("POST", "/api/v2/link/send-game", b.dump(), [this, title, label](const std::string& e, const std::string&) {
                                post_toast(e.empty() ? trf("%s part vers %s : copie, installation et sauvegarde, suivies dans Tâches ; il apparaît là-bas une fois l'application fermée", title.c_str(), label.c_str()) : e);
                            });
                        };
                        // Par défaut, pas de doublon : le jeu n'existera qu'une fois là-bas (dossier de jeux).
                        open_actions(trf("Envoyer « %s »", title.c_str()), {
                            {tr("Envoyer (le jeu n'est gardé qu'une fois, installé)"), [go] { go(false); }},
                            {tr("Envoyer et garder aussi une copie de secours là-bas"), [go] { go(true); }},
                        });
                    });
                }); });
            }});
        }
        if (acts.empty()) { toast(tr("Aucune autre PS5 jumelée et allumée : Appareils › PlayStation 5.")); return; }
        bool has_save = false;
        for (const auto& c : it->components) if (c.part == "save") has_save = true;
        if (!has_save) { open_actions(trf("Envoyer « %s » à…", it->title.c_str()), std::move(acts)); return; }
        const std::string ttl = trf("Envoyer « %s » à…", it->title.c_str());
        auto disks = std::make_shared<std::vector<std::pair<std::string, std::function<void()>>>>(std::move(acts));
        open_actions(it->title, {
            {tr("Le jeu, installé là-bas avec sa sauvegarde"), [this, disks, ttl] { open_actions(ttl, *disks); }},
            {tr("Sa sauvegarde seulement"), [this, key, title] { send_save_to_peer(key, title, ""); }},
        });
        return;
    }
    if ((k & key::cross) && sheet_focus_ == comps + 5) { open_versions("", it->title_id, it->title, it->key); return; }
    if ((k & key::cross) && sheet_focus_ == comps + 6) {
        if (it->ignored) { set_ignored(*it, false); return; }
        open_ignore({{it->title_id.empty() ? "key" : "title_id", it->title_id.empty() ? it->key : it->title_id, it->title}}, -1);
        return;
    }
    if ((k & key::triangle) && it->key.rfind("save:", 0) != 0) { open_image_tools(*it); return; }
    if ((k & key::cross) && sheet_focus_ == comps + 3) { pp_open_ = true; pp_row_ = 0; pp_item_ = it->key; load_plans(); return; }
    if ((k & key::cross) && sheet_focus_ == comps + 4) {
        if (it->location.empty()) { toast(tr("Emplacement inconnu pour cet élément")); return; }
        open_browser(it->title, it->location, false, nullptr);
        br_view_ = true;
        return;
    }
    // Destination : automatique, ou n'importe quel emplacement détecté (interne, USB, NAS / FTP…).
    const auto& places = snap_->places;
    if (sheet_focus_ == comps && (k & key::cross)) {
        std::vector<std::string> opts{tr("Automatique (premier disque USB, sinon interne)")};
        dest_places_.clear();
        int sel = 0;
        for (std::size_t i = 0; i < places.size(); ++i) {
            const auto& p = places[i];
            if (p.type == "switch" || p.use == "sync") continue;  // Switch : une source ; NAS « Synchro seulement » : pas une destination de backup
            dest_places_.push_back(static_cast<int>(i));
            if (dest_ == static_cast<int>(i) + 1) sel = static_cast<int>(dest_places_.size());
            opts.push_back(p.label + (p.type == "ftp" ? std::string("  ·  ") + tr("réseau") : "  ·  " + trf("%s libres", size_text(p.free).c_str())));
        }
        open_popup(3, tr("Où sauvegarder ?"), opts, sel);
        return;
    }
    if (!(k & key::cross)) return;
    if (sheet_focus_ < comps) { if (!unchecked_.erase(sheet_focus_)) unchecked_.insert(sheet_focus_); return; }
    if (sheet_focus_ == comps + 2) { sheet_ = false; return; }
    if (sheet_focus_ == comps + 1) {
        if (static_cast<int>(unchecked_.size()) == comps) { toast(tr("Choisissez au moins un élément à sauvegarder")); return; }
        std::string parts = "[";
        if (!unchecked_.empty()) {
            std::set<std::string> chosen;
            for (int i = 0; i < comps; ++i) if (!unchecked_.count(i)) chosen.insert(it->components[static_cast<std::size_t>(i)].part);
            for (const auto& p : chosen) { if (parts.size() > 1) parts += ","; parts += json_str(p); }
        }
        parts += "]";
        const std::string place = dest_ <= 0 || dest_ > static_cast<int>(places.size()) ? "auto" : places[static_cast<std::size_t>(dest_ - 1)].id;
        model_.command("POST", "/api/v2/tasks", "{\"type\":\"backup\",\"item\":" + json_str(it->key) + ",\"parts\":" + parts + ",\"place\":" + json_str(place) + "}",
                       [this](const std::string& err, const std::string&) { if (err.empty()) post_toast(tr("Sauvegarde ajoutée aux tâches")); });
        sheet_ = false;
    }
}

// Sauvegardes : à gauche les comptes (+ « Par disque »), à droite les jeux du compte.
// Croix : sauvegarder ce jeu · Triangle : tout le compte · Carré : filtre (ou disque suivant).
std::vector<std::size_t> Ui::saves_rows() const {
    std::vector<std::size_t> rows;
    const auto& accs = overview_["accounts"];
    if (saves_acc_ >= static_cast<int>(accs.size())) return rows;
    const std::string acc = accs[static_cast<std::size_t>(saves_acc_)]["key"].as_string();
    const auto& ents = overview_["entries"];
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const Json& e = ents[i];
        if (e["account"].as_string() != acc) continue;
        const std::string st = e["status"].as_string();
        const bool console = e["console"].is_object();
        const bool keep = saves_filter_ == 0 || (saves_filter_ == 1 && console) || (saves_filter_ == 2 && st == "backup_only") ||
                          (saves_filter_ == 3 && st == "console_newer") || (saves_filter_ == 4 && st == "backup_newer") ||
                          (saves_filter_ == 5 && st == "not_backed_up");
        if (keep) rows.push_back(i);
    }
    std::sort(rows.begin(), rows.end(), [&](std::size_t x, std::size_t y) { return ents[x]["title"].as_string() < ents[y]["title"].as_string(); });
    return rows;
}

void Ui::rs_start(const Json& v) {
    const auto& accs = overview_["accounts"];
    if (saves_acc_ >= static_cast<int>(accs.size()) || !accs[static_cast<std::size_t>(saves_acc_)]["on_console"].as_bool()) { toast(tr("Choisissez un compte de cette console")); return; }
    Json b = Json::object();
    b.set("type", "restore_save");
    b.set("place", v["place"].as_string());
    b.set("bundle", v["bundle"].as_string());
    b.set("user_id", accs[static_cast<std::size_t>(saves_acc_)]["user_id"].as_string());
    b.set("title_id", rs_entry_["title_id"].as_string());
    rs_body_ = b;
    b.set("check_only", true);
    rs_stage_ = 1;
    model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e, const std::string& resp) {
        Json j;
        std::lock_guard<std::mutex> l(async_mu_);
        if (!e.empty() || !Json::parse(resp, j)) { pending_toast_ = e.empty() ? tr("Vérification refusée") : e; rs_stage_ = 0; return; }
        rs_task_ = j["id"].as_string();
    });
    toast(tr("Vérification de la sauvegarde… (rien n'est modifié)"));
}

void Ui::save_entries(const std::vector<std::size_t>& rows) {
    const auto& accs = overview_["accounts"];
    if (saves_acc_ >= static_cast<int>(accs.size())) return;
    const std::string uid = accs[static_cast<std::size_t>(saves_acc_)]["user_id"].as_string();
    int queued = 0;
    for (const auto i : rows) {
        const Json& e = overview_["entries"][i];
        if (!e["console"].is_object()) continue;
        ++queued;
        model_.command("POST", "/api/v2/tasks", "{\"type\":\"backup\",\"item\":" + json_str(e["item"].as_string()) + ",\"parts\":[\"save\"],\"account\":" + json_str(uid) + ",\"place\":\"\"}",
                       [this](const std::string& err, const std::string&) { if (!err.empty()) post_toast(err); });
    }
    toast(queued ? (queued > 1 ? trf("%d sauvegardes ajoutées aux tâches", queued) : std::string(tr("Sauvegarde ajoutée aux tâches"))) : std::string(tr("Rien à sauvegarder ici")));
    overview_loaded_ = 0;  // relire l'état bientôt
}

// ---- Synchronisation (télé) : une entrée par Switch, comparée à ProsperoEden ----
std::vector<std::size_t> Ui::sync_rows() const {
    std::vector<std::size_t> rows;
    const auto& es = sync_["entries"];
    for (std::size_t i = 0; i < es.size(); ++i) {
        const std::string st = es[i]["status"].as_string();
        const bool keep = sync_filter_ == 3 || (sync_filter_ == 0 && (st == "a_newer" || st == "b_newer" || st == "same_day")) ||
                          (sync_filter_ == 1 && st == "identical") || (sync_filter_ == 2 && st.rfind("only", 0) == 0);
        if (keep) rows.push_back(i);
    }
    std::sort(rows.begin(), rows.end(), [&](std::size_t x, std::size_t y) { return es[x]["title"].as_string() + es[x]["profile"].as_string() < es[y]["title"].as_string() + es[y]["profile"].as_string(); });
    return rows;
}

void Ui::sync_start(bool check) {
    const Json& e = sync_["entries"][sync_entry_];
    const bool ab = sync_dir_ == 0;  // 0 : Switch → Eden ; 1 : Eden → Switch
    const Json& sw = e["a"];
    const Json& ed = e["b"];
    std::string body;
    if (ab) {
        std::string profile = ed.is_object() ? ed["where"]["profile"].as_string() : "";
        if (profile.empty())
            for (const auto& p : eden_profiles_.items())
                if (p["root"].as_string() == "/data/prosperoeden" && (e["profile"].as_string() == "device" ? p["profile"].as_string() == "00000000000000000000000000000000" : p["name"].as_string() == e["profile"].as_string() || p["name"].as_string() == sw["profile_name"].as_string())) profile = p["profile"].as_string();
        if (profile.empty()) { toast(trf("Aucun profil « %s » dans ProsperoEden", e["profile"].as_string().c_str())); sync_stage_ = 0; return; }
        body = "{\"type\":\"import_switch\",\"remote\":" + json_str(sync_remote_) + ",\"game\":" + json_str(sw["where"]["game"].as_string()) + ",\"file\":" + json_str(sw["where"]["file"].as_string()) +
               ",\"title_id\":" + json_str(e["title_id"].as_string()) + ",\"eden_root\":\"/data/prosperoeden\",\"profile\":" + json_str(profile);
    } else {
        body = "{\"type\":\"export_switch\",\"remote\":" + json_str(sync_remote_) + ",\"eden_root\":" + json_str(ed["where"]["eden_root"].as_string()) + ",\"profile\":" + json_str(ed["where"]["profile"].as_string()) +
               ",\"title_id\":" + json_str(e["title_id"].as_string()) + ",\"game_folder\":" + json_str(sw.is_object() ? sw["where"]["game"].as_string() : e["title"].as_string()) +
               ",\"profile_name\":" + json_str(sw.is_object() ? sw["profile_name"].as_string() : (e["profile"].as_string() == "device" ? std::string("Device") : ed["profile_name"].as_string()));
    }
    body += check ? std::string(",\"check_only\":true}") : std::string(",\"force_older\":") + (sync_check_["eden_newer"].as_bool() ? "true" : "false") + "}";
    sync_stage_ = check ? 1 : 3;
    model_.command("POST", "/api/v2/tasks", body, [this](const std::string& err, const std::string& resp) {
        Json j;
        std::lock_guard<std::mutex> l(async_mu_);
        if (!err.empty() || !Json::parse(resp, j)) { pending_toast_ = err.empty() ? tr("Demande refusée") : err; sync_task_.clear(); sync_stage_ = 0; return; }
        sync_task_ = j["id"].as_string();
    });
}

bool Ui::filter_bar(std::uint32_t k, int& value, int count, int& row) {
    if (count <= 0) { bar_ = false; return false; }
    if (bar_) {
        if (k & key::left) { value = std::max(0, value - 1); row = 0; }
        if (k & key::right) { value = std::min(count - 1, value + 1); row = 0; }
        if (k & (key::down | key::cross | key::circle)) bar_ = false;
        return true;
    }
    if (k & key::square) { value = (value + 1) % count; row = 0; return true; }  // raccourci gardé
    if ((k & key::up) && row == 0) { bar_ = true; return true; }
    return false;
}

void Ui::input_sync(std::uint32_t k) {
    if (sync_stage_ == 2) {  // confirmation après vérification
        if (k & key::cross) sync_start(false);
        if (k & key::circle) { sync_stage_ = 0; toast(tr("Annulé : rien n'a été modifié")); }
        return;
    }
    if (sync_stage_) return;  // vérification ou transfert en cours
    if (sync_menu_) {
        const Json& e = sync_["entries"][sync_entry_];
        if (k & (key::left | key::right)) { const int d = sync_dir_ ^ 1; if ((d == 0 && e["a"].is_object()) || (d == 1 && e["b"].is_object())) sync_dir_ = d; }
        if (k & key::circle) sync_menu_ = false;
        if (k & key::cross) { sync_menu_ = false; sync_start(true); }
        return;
    }
    const auto rows = sync_rows();
    const int n = static_cast<int>(rows.size());
    if ((k & key::triangle) && !sync_loading_) { sync_cache_.erase(sync_key_); sync_ = Json(); return; }  // relire les deux côtés
    if (k & key::up) saves_row_ = std::max(0, saves_row_ - 1);
    if (k & key::down) saves_row_ = std::min(std::max(0, n - 1), saves_row_ + 1);
    if ((k & key::cross) && saves_row_ < n) {
        sync_entry_ = rows[static_cast<std::size_t>(saves_row_)];
        const Json& e = sync_["entries"][sync_entry_];
        sync_dir_ = e["a"].is_object() ? 0 : 1;
        sync_menu_ = true;
    }
}

void Ui::input_tasks(std::uint32_t k) {
    std::vector<const Task*> list;
    for (auto it = snap_->tasks.rbegin(); it != snap_->tasks.rend(); ++it) if (in_group(it->state, tasks_group_)) list.push_back(&*it);
    if (filter_bar(k, tasks_group_, 5, tasks_row_)) { tasks_action_ = 0; tasks_sel_.clear(); return; }
    if (tasks_group_ == 4) { input_plans(k); return; }
    const int n = static_cast<int>(list.size());
    if (k & key::up) { tasks_row_ = std::max(0, tasks_row_ - 1); tasks_action_ = 0; }
    if (k & key::down) { tasks_row_ = std::min(std::max(0, n - 1), tasks_row_ + 1); tasks_action_ = 0; }
    if (k & key::left) tasks_action_ = std::max(0, tasks_action_ - 1);
    if (k & key::right) tasks_action_ = std::min(1, tasks_action_ + 1);
    if ((k & key::triangle) && tasks_group_ >= 1 && tasks_group_ <= 3 && n > 0) { clear_tasks(tasks_group_); return; }
    if ((k & key::l2) && tasks_row_ < n) {  // L2 : cocher (tâches finies seulement)
        const Task& t = *list[static_cast<std::size_t>(tasks_row_)];
        if (in_group(t.state, 0)) { toast(tr("Une tâche en cours ne se coche pas")); return; }
        if (!tasks_sel_.erase(t.id)) tasks_sel_.insert(t.id);
        return;
    }
    if ((k & key::options) && !tasks_sel_.empty()) { tasks_selection_menu(); return; }
    if ((k & key::options) && tasks_row_ < n) {  // Options : retirer cette tâche de la liste (terminée, en échec ou annulée)
        const Task& t = *list[static_cast<std::size_t>(tasks_row_)];
        if (t.state == "done" || t.state == "failed" || t.state == "cancelled") {
            const std::string id = t.id;
            open_actions(tr("Retirer cette tâche de la liste ?"), {
                {tr("Retirer"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}", [this](const std::string& e, const std::string&) { post_toast(e.empty() ? std::string(tr("Retirée de la liste")) : e); }); }},
                {tr("Annuler"), [] {}},
            });
            popup_note_ = tr("Seulement la liste : rien n'est retiré des disques.");
        }
        return;
    }
    if ((k & key::cross) && tasks_row_ < n) {
        const Task& t = *list[static_cast<std::size_t>(tasks_row_)];
        if ((t.type == "files_copy" || t.type == "files_move") && (t.state == "cancelled" || t.state == "failed")) { files_task_menu(t); return; }
        const char* actions[2] = {nullptr, nullptr};
        if (t.state == "running" || t.state == "queued") { actions[0] = "pause"; actions[1] = "cancel"; }
        else if (t.state == "paused") { actions[0] = "resume"; actions[1] = "cancel"; }
        else if (t.state == "failed") { actions[0] = "resume"; actions[1] = "details"; }
        else if (t.type == "link_send" && t.report["foreground"]["title_id"].is_string()) {
            confirm_close_remote(t.report["instance"].as_string(), t.report["peer"].as_string(), t.report["foreground"]);
            return;
        }
        else { actions[0] = "details"; actions[1] = "remove"; }
        const char* a = actions[std::min(tasks_action_, actions[1] ? 1 : 0)];
        if (a && std::string(a) == "details") { task_details(t); return; }
        if (a) model_.command("POST", "/api/v2/tasks/" + t.id + "/" + a, "{}");
    }
}

// Tâches cochées : retirer de la liste, mettre à la corbeille les sauvegardes qu'elles ont faites, tout (dé)cocher.
void Ui::tasks_selection_menu() {
    std::vector<std::string> ids, backups;
    for (const auto& t : snap_->tasks) {
        if (!tasks_sel_.count(t.id)) continue;
        ids.push_back(t.id);
        if (t.type == "backup" && t.state == "done") backups.push_back(t.id);
    }
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({trf("Retirer de la liste (%d)", static_cast<int>(ids.size())), [this, ids] {
        for (const auto& id : ids) model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}");
        tasks_sel_.clear();
        toast(trf("%d tâche(s) retirée(s) de la liste", static_cast<int>(ids.size())));
    }});
    if (!backups.empty())
        acts.push_back({trf("Mettre leurs sauvegardes à la corbeille… (%d)", static_cast<int>(backups.size())), [this, backups] {
            open_hold_confirm(trf("Mettre %d sauvegarde(s) à la corbeille ?", static_cast<int>(backups.size())),
                              tr("Les sauvegardes faites par ces tâches vont dans la corbeille de leur disque ou de leur NAS pendant 30 jours. Les tâches restent dans la liste."),
                              [this, backups] { tasks_trash_next(backups, 0, 0, 0); });
        }});
    std::vector<std::string> all;
    for (const auto& t : snap_->tasks) if (in_group(t.state, tasks_group_) && !in_group(t.state, 0)) all.push_back(t.id);
    acts.push_back({tr("Tout cocher"), [this, all] { tasks_sel_.insert(all.begin(), all.end()); }});
    acts.push_back({tr("Tout décocher"), [this] { tasks_sel_.clear(); }});
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(trf("%d coché(s)", static_cast<int>(ids.size())), std::move(acts));
    popup_note_ = tr("Retirer de la liste ne touche à aucun fichier.");
}

// Une sauvegarde après l'autre (le NAS répond une requête à la fois) : retrouvée par sa tâche, puis corbeille.
void Ui::tasks_trash_next(std::vector<std::string> ids, std::size_t i, int done, int failed) {
    if (i >= ids.size()) {
        post_toast(failed ? trf("%d sauvegarde(s) mise(s) à la corbeille · %d introuvable(s) ou refusée(s)", done, failed)
                          : trf("%d sauvegarde(s) mise(s) à la corbeille (30 jours)", done));
        post_ui([this] { tasks_sel_.clear(); overview_loaded_ = 0; });
        return;
    }
    model_.command("POST", "/api/v2/tasks/" + ids[i] + "/backup", "{}", [this, ids, i, done, failed](const std::string& e, const std::string& body) {
        pbc::Json j;
        if (!e.empty() || !pbc::Json::parse(body, j)) { tasks_trash_next(ids, i + 1, done, failed + 1); return; }
        pbc::Json b = pbc::Json::object();
        b.set("place", j["place"]);
        b.set("bundle", j["bundle"]);
        b.set("confirm", true);
        model_.command("POST", "/api/v2/backups/delete", b.dump(), [this, ids, i, done, failed](const std::string& e2, const std::string&) {
            tasks_trash_next(ids, i + 1, done + (e2.empty() ? 1 : 0), failed + (e2.empty() ? 0 : 1));
        });
    });
}

// ---- Plans programmés (télé) : liste, lancer maintenant, pause, et création par modèles ----

void Ui::load_plans() {
    plans_loaded_ = now_;
    model_.command("GET", "/api/v2/plans", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_plans_ = j; has_pending_plans_ = true; }
    });
}

void Ui::input_plans(std::uint32_t k) {
    const int n = static_cast<int>(plans_.size()) + 1;  // première ligne : « Nouveau plan »
    if (k & key::up) tasks_row_ = std::max(0, tasks_row_ - 1);
    if (k & key::down) tasks_row_ = std::min(n - 1, tasks_row_ + 1);
    if (tasks_row_ == 0) {
        if (k & key::cross) {
            std::vector<std::string> opts{tr("Plan personnalisé : je choisis tout")};
            for (const auto& p : plan_presets) opts.push_back(tr(p.label));
            open_popup(4, tr("Nouveau plan"), opts, 0);
        }
        return;
    }
    const Json& p = plans_[static_cast<std::size_t>(tasks_row_ - 1)];
    const std::string id = p["id"].as_string();
    if (k & key::cross) { open_plan_editor(p); return; }
    if (k & key::r2)
        model_.command("POST", "/api/v2/plans/" + id + "/run", "{}", [this](const std::string& e, const std::string& body) {
            Json j;
            post_toast(e.empty() && Json::parse(body, j) ? trf("%lld sauvegarde(s) ajoutée(s) aux tâches", static_cast<long long>(j["queued"].as_int())) : e);
            load_plans();
        });
    if (k & key::triangle) {
        Json q = p;
        q.set("enabled", !p["enabled"].as_bool(true));
        model_.command("POST", "/api/v2/plans", q.dump(), [this](const std::string&, const std::string&) { load_plans(); });
        toast(p["enabled"].as_bool(true) ? tr("Plan mis en pause") : tr("Plan réactivé"));
    }
}

void Ui::input_settings(std::uint32_t k) {
    if (support_big_) { if (k & (key::circle | key::cross)) support_big_ = false; return; }  // code QR agrandi : n'importe quel retour le ferme
    if (set_zone_ == 0) {
        if (k & key::up) set_section_ = std::max(0, set_section_ - 1);
        if (k & key::down) set_section_ = std::min(section_count - 1, set_section_ + 1);
        if (k & (key::right | key::cross)) { set_zone_ = 1; set_row_ = 0; }
        return;
    }
    // Paramètres › Général : 0 langue (Croix), 1 thème, 2 à 4 son (gauche / droite ou Croix).
    if (set_section_ == sec_general && (k & (key::up | key::down))) { set_row_ = std::clamp(set_row_ + (k & key::up ? -1 : 1), 0, 4); return; }
    if (set_section_ == sec_general && set_row_ >= 2 && (k & (key::left | key::right | key::cross))) { change_sound(set_row_ - 1, k & key::left ? -1 : 1); return; }
    if (set_section_ == sec_general && set_row_ == 1 && (k & (key::left | key::right | key::cross))) { cycle_theme(k & key::left ? -1 : 1); return; }
    if (k & (key::left | key::circle)) { set_zone_ = 0; return; }
    if (set_section_ == sec_sources) {  // émulateurs : Croix ouvre les réglages de l'émulateur choisi
        std::vector<const Emulator*> ems;
        for (const auto& e : snap_->emulators) if (e.installed) ems.push_back(&e);
        const int n = static_cast<int>(ems.size());
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(std::max(0, n - 1), set_row_ + 1);
        if ((k & key::cross) && set_row_ < n) open_emulator(*ems[static_cast<std::size_t>(set_row_)]);
        return;
    }
    if (set_section_ == sec_general && set_row_ == 0 && (k & key::cross)) {  // langue : Automatique (celle de la console) ou une langue
        const i18n::Language* sys = i18n::find(i18n::system_code());
        std::vector<std::string> opts{trf("Automatique (%s)", !sys ? "English" : sys->tv_ok ? i18n::label(*sys).c_str() : sys->english.c_str())};
        int sel = 0;
        for (const auto& l : i18n::languages()) {
            if (l.code == i18n::setting()) sel = static_cast<int>(opts.size());
            // Police de la télé encore latine : le nom anglais, et l'anglais à l'écran en attendant (le web l'affiche déjà).
            opts.push_back(l.tv_ok ? i18n::label(l) : trf("%s (bientôt sur la télé)", l.english.c_str()));
        }
        open_popup(10, tr("Langue"), opts, sel);
        return;
    }
    if (set_section_ == sec_ignored) {  // éléments ignorés : Croix = ne plus ignorer
        const auto rows = ignored_rows(*snap_, unignored_);
        const int n = static_cast<int>(rows.size());
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(std::max(0, n - 1), set_row_ + 1);
        if ((k & key::cross) && set_row_ < n) {
            const IgnRow& r = rows[static_cast<std::size_t>(set_row_)];
            const std::string name = r.item ? r.item->title : r.value, value = r.value;
            unignored_.insert(value);
            model_.command("POST", "/api/v2/ignore", "{\"" + r.field + "\":" + json_str(value) + ",\"on\":false}", [this, name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("« %s » n'est plus ignoré", name.c_str()) : e);
            });
            overview_loaded_ = 0;
            mg_sum_dirty_ = true;
        }
        return;
    }
    if (set_section_ == sec_accounts && tk_open_) {  // « Tout reprendre » : L2 coche, Croix valide, Rond annule
        const int n = static_cast<int>(tk_["games"].size());
        if (k & key::circle) { tk_open_ = false; return; }
        if (k & key::up) tk_row_ = std::max(0, tk_row_ - 1);
        if (k & key::down) tk_row_ = std::min(std::max(0, n - 1), tk_row_ + 1);
        if ((k & key::l2) && tk_row_ < n) {
            const Json& g = tk_["games"][static_cast<std::size_t>(tk_row_)];
            if (g["running"].as_bool()) { toast(tr("Jeu en cours : il sera laissé de côté")); return; }
            const std::string t = g["title_id"].as_string();
            if (!tk_sel_.erase(t)) tk_sel_.insert(t);
        }
        if (k & key::cross) {
            if (tk_sel_.empty()) { toast(tr("Cochez d'abord des jeux (L2)")); return; }
            Json b = Json::object(), ts = Json::array();
            b.set("from", tk_from_); b.set("user_id", tk_user_);
            for (const auto& t : tk_sel_) ts.push(t);
            b.set("title_ids", ts);
            const int count = static_cast<int>(tk_sel_.size());
            model_.command("POST", "/api/v2/players/takeover", b.dump(), [this, count](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("%d reprise(s) ajoutée(s) aux tâches", count) : e);
            });
            tk_open_ = false;
        }
        return;
    }
    if (set_section_ == sec_accounts) {  // comptes : Croix = renommer, c'est moi / ce n'est pas moi
        const auto rows = account_rows(players_);
        const int n = static_cast<int>(rows.size());
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(std::max(0, n - 1), set_row_ + 1);
        if ((k & key::cross) && set_row_ < n) {
            const pbc::Json& a = *rows[static_cast<std::size_t>(set_row_)].a;
            const std::string id = a["account_id"].as_string(), name = a["name"].as_string();
            const bool mine = rows[static_cast<std::size_t>(set_row_)].mine, current = a["current"].as_bool();
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            if (!current && players_["players"].size()) {  // reprendre ses parties sur le compte de la console
                const pbc::Json& p0 = players_["players"][0];
                const std::string uid = p0["user_id"].as_string(), pname = p0["name"].as_string();
                acts.push_back({trf("Tout reprendre sur %s…", pname.c_str()), [this, id, uid, pname] { open_takeover(id, uid, pname); }});
            }
            acts.push_back({tr("Renommer…"), [this, id, name] {
                acc_name_ = name;
                open_keyboard(tr("Nom de cet identifiant"), name, [this](const std::string& t) { acc_name_ = t; });
                kb_done_ = [this, id] {
                    model_.command("POST", "/api/v2/players/name", "{\"account_id\":" + json_str(id) + ",\"name\":" + json_str(acc_name_) + "}",
                                   [this](const std::string& e, const std::string&) { post_toast(e.empty() ? std::string(tr("Nom enregistré")) : e); players_at_ = 0; });
                };
            }});
            if (!mine) acts.push_back({tr("C'est moi (même utilisateur)"), [this, id] {
                model_.command("POST", "/api/v2/players/link", "{\"account_id\":" + json_str(id) + ",\"on\":true}",
                               [this](const std::string& e, const std::string&) { post_toast(e.empty() ? std::string(tr("Identifiant rattaché à cet utilisateur")) : e); players_at_ = 0; });
                mg_sum_dirty_ = true;
            }});
            else if (!current) acts.push_back({tr("Ce n'est pas moi"), [this, id] {
                model_.command("POST", "/api/v2/players/link", "{\"account_id\":" + json_str(id) + ",\"on\":false}",
                               [this](const std::string& e, const std::string&) { post_toast(e.empty() ? std::string(tr("Identifiant détaché")) : e); players_at_ = 0; });
                mg_sum_dirty_ = true;
            }});
            open_actions(name.empty() ? id : name, std::move(acts));
        }
        return;
    }
    if (set_section_ == sec_news) {  // nouveautés : une version à la fois en haut
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(std::max(0, static_cast<int>(news_.size()) - 1), set_row_ + 1);
    }
    if (set_section_ == sec_devices && (k & key::cross)) { tab_ = destinations; set_zone_ = 0; return; }  // réglages des appareils : onglet Appareils
    if (set_section_ == sec_goals) { if (k & key::left) set_zone_ = 0; return; }
    if (set_section_ == sec_report) {  // trois cartes : Croix agrandit le code
        if (k & key::left) { if (set_row_ == 0) set_zone_ = 0; else set_row_ = set_row_ - 1; return; }
        if (k & key::right) set_row_ = std::min(2, set_row_ + 1);
        if (k & key::cross) support_big_ = true;
        return;
    }
    if (set_section_ == sec_support) {  // soutenir : une carte par moyen, Croix agrandit son code QR
        const int n = static_cast<int>(support_["links"].items().size() + support_["crypto"].items().size());
        const int nf = static_cast<int>(support_["follow"].items().size());  // Suivez-nous : rangée du bas (lignes n à n + nf - 1)
        if (set_row_ >= n && nf > 0) {
            const int i = set_row_ - n;
            if (k & key::left) { if (i == 0) set_zone_ = 0; else --set_row_; return; }
            if (k & key::right) set_row_ = n + std::min(nf - 1, i + 1);
            if (k & key::up) set_row_ = 0;
            if (k & key::cross) {
                const Json& f = support_["follow"].items()[static_cast<std::size_t>(std::min(i, nf - 1))];
                if (f["qr"].items().empty()) toast(tr("Bientôt disponible.")); else support_big_ = true;
            }
            return;
        }
        if ((k & key::down) && nf > 0) { set_row_ = n; return; }
        if (k & key::left) { if (set_row_ == 0) set_zone_ = 0; else set_row_ = set_row_ - 1; return; }
        if (k & key::right) set_row_ = std::min(std::max(0, n - 1), set_row_ + 1);
        if ((k & key::cross) && n > 0) {
            const auto& li = support_["links"].items();
            const Json& c = set_row_ < static_cast<int>(li.size()) ? li[static_cast<std::size_t>(set_row_)] : support_["crypto"].items()[static_cast<std::size_t>(set_row_) - li.size()];
            if (c["qr"].items().empty()) toast(tr("Bientôt disponible.")); else support_big_ = true;
        }
        return;
    }
    if (set_section_ == sec_thanks) {  // remerciements : défilement
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(credits_rows_ - 1, set_row_ + 1);
    }
    if (set_section_ == sec_updates) {  // mises à jour
        const auto acts = update_actions();
        const int n = static_cast<int>(acts.size());
        if (k & key::up) set_row_ = std::max(0, set_row_ - 1);
        if (k & key::down) set_row_ = std::min(n - 1, set_row_ + 1);
        set_row_ = std::min(set_row_, n - 1);
        if ((k & key::cross) && set_row_ >= 0) run_update_action(acts[static_cast<std::size_t>(set_row_)]);
    }
}

// Copie locale du thème (lue au démarrage de l'app, avant que le moteur réponde).
void Ui::save_theme_copy() {
    if (FILE* f = std::fopen("/data/pbc-backup-center/tv-theme", "w")) { std::fputs(theme::current().id, f); std::fclose(f); }
}

// Thème : appliqué tout de suite sur la télé, puis enregistré dans le moteur (le web suit).
void Ui::cycle_theme(int step) {
    const int n = theme::count();
    theme::set_index((theme::index() + step + n) % n);
    theme_local_at_ = now_;  // le moteur n'a pas encore la nouvelle valeur : ne pas revenir en arrière
    save_theme_copy();
    model_.command("POST", "/api/v2/settings", std::string("{\"theme\":") + json_str(theme::current().id) + "}",
                   [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
}

// Son : effets (marche / arrêt), musique (marche / arrêt), volume par pas de 10.
void Ui::change_sound(int row, int step) {
    if (row == 1) sound_ui_ = !sound_ui_;
    else if (row == 2) music_ = !music_;
    else volume_ = std::clamp(volume_ + step * 10, 0, 100);
    sound_local_at_ = now_;
    audio::configure(sound_ui_, music_, volume_);
    if (row != 2) audio::play(audio::Sound::select);  // entendre le nouveau volume (ou que les effets reviennent)
    model_.command("POST", "/api/v2/settings", std::string("{\"sound_ui\":") + (sound_ui_ ? "true" : "false") + ",\"music\":" + (music_ ? "true" : "false") + ",\"volume\":" + std::to_string(volume_) + "}",
                   [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
}

std::vector<int> Ui::update_actions() const {
    const Update& u = snap_->update;
    std::vector<int> a{0};
    if (u.state == "available") a.push_back(1);
    if (u.engine_restart_needed) a.push_back(2);
    // L'app en cours est plus ancienne que celle installée : elle doit redémarrer pour en profiter.
    if (!u.installed.empty() && u.installed != app_version) a.push_back(3);
    a.push_back(4);
    a.push_back(6);  // installer automatiquement
    a.push_back(10);  // canal : stable ou bêta
    a.push_back(5);
    if (!u.previous.empty() && u.state != "downloading" && u.state != "installing" && u.state != "checking") a.push_back(8);  // revenir à la version d'avant
    a.push_back(9);  // versions disponibles (dépôt de versions)
    a.push_back(7);  // alimentation de la console  // démarrage du moteur (chargeurs, démarrer avec la console)
    return a;
}

void Ui::run_update_action(int action) {
    auto report = [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); };
    switch (action) {
        case 0:
            if (snap_->update.source.empty()) { toast(tr("Réglez d'abord la source depuis le web (Paramètres → Mises à jour)")); return; }
            model_.command("POST", "/api/v2/update/check", "{}", report);
            toast(tr("Recherche des mises à jour…"));
            break;
        case 1: model_.command("POST", "/api/v2/update/install", "{}", report); toast(tr("Téléchargement de la mise à jour…")); break;
        case 2: countdown_kind_ = 1; countdown_until_ = now_ + 10000; break;
        case 3: countdown_kind_ = 2; countdown_until_ = now_ + 5000; break;
        case 4:
            model_.command("POST", "/api/v2/settings", std::string("{\"update_auto_check\":") + (snap_->update_auto_check ? "false" : "true") + "}",
                           [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Réglage enregistré") : e); });
            break;
        case 5: open_launch_menu(); break;
        case 7: open_power_menu(); break;
        case 9: open_versions_menu(); break;
        case 8: {
            const std::string prev = snap_->update.previous;
            open_actions(trf("Revenir à la version %s ?", prev.c_str()), {
                {tr("Revenir"), [this, prev] {
                    model_.command("POST", "/api/v2/update/rollback", "{}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
                    toast(trf("Retour à la version %s…", prev.c_str()));
                }},
                {tr("Annuler"), [] {}},
            });
            popup_note_ = tr("La version d'avant est réinstallée (fichiers vérifiés), puis le moteur redémarre. La version actuelle ne sera plus proposée automatiquement ; une recherche à la main la montre toujours.");
            break;
        }
        case 10: {
            const bool to_beta = snap_->update_channel != "beta";
            auto apply = [this, to_beta] {
                model_.command("POST", "/api/v2/settings", std::string("{\"update_channel\":\"") + (to_beta ? "beta" : "stable") + "\"}",
                               [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Réglage enregistré") : e); });
            };
            if (!to_beta) { apply(); break; }
            open_actions(tr("Recevoir les versions bêta ?"), {{tr("Passer en bêta"), apply}, {tr("Annuler"), [] {}}});
            popup_note_ = tr("Les bêtas arrivent avant les versions stables, pour les essayer. Elles peuvent contenir des défauts ; « Revenir à la version d'avant » reste possible. Repassez en Stable à tout moment.");
            break;
        }
        case 6:
            model_.command("POST", "/api/v2/settings", std::string("{\"update_auto_install\":") + (snap_->update_auto_install ? "false" : "true") + "}",
                           [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Réglage enregistré") : e); });
            break;
        default: break;
    }
}


// Démarrage du moteur (web : même écran) : quand le moteur ne tourne pas, l'appli le fait démarrer par le premier
// chargeur présent, dans l'ordre choisi ici ; « Démarrer avec la console » le fait lancer par Payload Manager.
void Ui::open_launch_menu() {
    model_.command("GET", "/api/v2/launch", "", [this](const std::string& e, const std::string& body) {
        Json st;
        if (!e.empty() || !Json::parse(body, st) || !st.is_object()) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
        post_ui([this, st] {
            std::vector<std::pair<std::string, std::function<void()>>> c;
            const Json& pm = st["pldmgr"];
            if (!pm["present"].as_bool()) c.push_back({tr("Démarrer le moteur avec la console : Payload Manager absent"), [this] { toast(tr("Payload Manager n'est pas installé sur cette console")); }});
            else {
                const bool on = pm["enabled"].as_bool();
                c.push_back({trf("Démarrer le moteur avec la console (Payload Manager) : %s", on ? tr("Oui") : tr("Non")), [this, on] {
                    model_.command("POST", "/api/v2/launch/autostart", std::string("{\"enabled\":") + (on ? "false" : "true") + "}", [this, on](const std::string& e2, const std::string& b2) {
                        Json r;
                        const bool pm_off = e2.empty() && Json::parse(b2, r) && !r["pldmgr"]["autoload_enabled"].as_bool();
                        post_toast(!e2.empty() ? e2 : on ? std::string(tr("Retiré du démarrage de Payload Manager"))
                                                  : pm_off ? std::string(tr("Ajouté. Activez aussi le démarrage automatique dans Payload Manager.")) : std::string(tr("Ajouté au démarrage de Payload Manager")));
                    });
                }});
            }
            // À l'ouverture de l'appli : le chargeur du débridage (seul « chargeur » connu) ; activé = la liste le contient.
            bool open_on = false, present = false;
            for (const auto& l : st["launchers"].items()) if (l["id"].as_string() == "elfldr") { open_on = l["enabled"].as_bool(); present = l["present"].as_bool(); }
            c.insert(c.begin(), {trf("Démarrer le moteur à l'ouverture de l'appli : %s", open_on ? tr("Oui") : tr("Non")), [this, open_on] {
                model_.command("POST", "/api/v2/settings", open_on ? std::string("{\"launchers\":[]}") : std::string("{\"launchers\":[\"elfldr\"]}"),
                               [this](const std::string& e3, const std::string&) {
                                   post_toast(e3.empty() ? std::string(tr("Réglage enregistré")) : e3);
                                   post_ui([this] { open_launch_menu(); });
                               });
            }});
            c.push_back({tr("Fermer"), [] {}});
            open_actions(tr("Démarrage du moteur"), std::move(c));
            popup_note_ = std::string(tr("À l'ouverture : si le moteur ne tourne pas quand vous ouvrez Backup Center, l'appli le démarre toute seule. Avec la console : Payload Manager lance le moteur dès l'allumage, même sans ouvrir l'appli. Une fois lancé, le moteur se relance et se met à jour seul.")) + " " +
                          (present ? tr("Disponible sur cette console.") : tr("Indisponible sur cette console pour le moment."));
        });
    });
}

// « Versions disponibles » : la liste du dépôt de versions (versions.json), avec installée / recommandée / test ;
// choisir une version la fait installer après confirmation (plus ancienne : la version actuelle est mise de côté).
void Ui::open_versions_menu() {
    model_.command("GET", "/api/v2/update/versions", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
        post_ui([this, j] {
            const std::string inst = j["installed"].as_string(), rec = j["recommended"].as_string();
            std::vector<std::pair<std::string, std::function<void()>>> c;
            int n = 0;
            for (const auto& v : j["versions"].items()) {
                if (++n > 10) break;
                const std::string ver = v["version"].as_string(), title = v["title"].as_string();
                std::string tag = ver == inst ? tr("installée") : ver == rec ? tr("recommandée") : v["channel"].as_string() == "beta" ? tr("bêta") : !v["tested"].as_bool() ? tr("test") : "";
                std::string label = ver + "  ·  " + v["date"].as_string();
                if (!title.empty()) label += std::string("  ·  ") + tr(title.c_str());
                if (!tag.empty()) label += "  ·  " + tag;
                if (ver == inst) { c.push_back({label, [this] { toast(tr("Déjà installée")); }}); continue; }
                c.push_back({label, [this, ver, inst] {
                    const bool older = version_cmp(ver.c_str(), inst.c_str()) < 0;
                    open_actions(trf("Installer la version %s ?", ver.c_str()), {
                        {tr("Installer"), [this, ver] {
                            Json b = Json::object();
                            b.set("version", ver);
                            model_.command("POST", "/api/v2/update/version", b.dump(), [this](const std::string& e2, const std::string&) { if (!e2.empty()) post_toast(e2); });
                            toast(trf("Installation de la version %s…", ver.c_str()));
                        }},
                        {tr("Annuler"), [] {}},
                    });
                    popup_note_ = older ? trf("Version plus ancienne : la version %s ne sera plus proposée automatiquement ; une recherche à la main la montre toujours.", inst.c_str())
                                        : std::string(tr("Fichiers vérifiés, retour arrière en cas d'échec ; le moteur redémarre ensuite."));
                }});
            }
            if (c.empty()) c.push_back({tr("Aucune version dans la liste"), [] {}});
            c.push_back({tr("Fermer"), [] {}});
            open_actions(tr("Versions disponibles"), std::move(c));
        });
    });
}

// Alimentation de cette console : chaque action est confirmée ; refusée par le moteur pendant une partie ou une copie.
void Ui::open_power_menu() {
    auto ask = [this](const char* action, std::string title) {
        const std::string act = action;
        open_actions(title, {
            {tr("Confirmer"), [this, act] {
                model_.command("POST", "/api/v2/power", std::string("{\"action\":\"") + act + "\"}", [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? std::string(tr("Dans un instant…")) : e);
                });
            }},
            {tr("Annuler"), [] {}},
        });
        popup_note_ = tr("Refusé pendant une partie ou une copie en cours.");
    };
    open_actions(tr("Alimentation de la console"), {
        {tr("Redémarrer la console"), [ask] { ask("reboot", tr("Redémarrer la console ?")); }},
        {tr("Éteindre la console"), [ask] { ask("off", tr("Éteindre la console ?")); }},
        {tr("Fermer"), [] {}},
    });
}

// ---- Dessin -----------------------------------------------------------------------------

void Ui::icon(Surface& s, int which, int x, int y, std::uint32_t) {
    // Touches de la manette, à leurs vraies couleurs, sur une pastille sombre.
    // 0 croix (bleu), 1 rond (rouge), 2 carré (rose), 3 triangle (vert).
    static const std::uint32_t tone[4] = {rgb(122, 180, 255), rgb(255, 107, 107), rgb(243, 154, 215), rgb(63, 213, 176)};
    const std::uint32_t c = tone[which & 3];
    s.rect(x - 4, y - 4, 34, 34, rgb(12, 27, 46), 17);
    s.outline(x - 4, y - 4, 34, 34, rgb(255, 255, 255, 40), 17, 1);
    if (which == 0) for (int i = 0; i < 14; ++i) { s.rect(x + 5 + i, y + 5 + i, 3, 3, c); s.rect(x + 18 - i, y + 5 + i, 3, 3, c); }
    if (which == 1) s.outline(x + 4, y + 4, 18, 18, c, 9, 3);
    if (which == 2) s.outline(x + 5, y + 5, 16, 16, c, 2, 3);
    if (which == 3) { for (int i = 0; i < 15; ++i) { s.rect(x + 12 - i / 2, y + 4 + i, 3, 3, c); s.rect(x + 12 + i / 2, y + 4 + i, 3, 3, c); } s.rect(x + 5, y + 17, 17, 3, c); }
}

// Aide permanente en bas à droite : « pavé tactile : Accueil » (hors Accueil), puis « Maintenir 2 s : quitter ».
// « 12,3 Mo sur 27 Mo · fichier 5 sur 23 · 1,2 Mo/s · reste 10 s » (vitesse et reste dès qu'on les connaît).
std::string Ui::update_progress_text(const Update& u) const {
    std::string t = trf("%s sur %s", size_text(u.bytes_done).c_str(), size_text(u.bytes_total).c_str());
    if (u.file_index && u.files) t += " · " + trf("fichier %d sur %d", static_cast<int>(u.file_index), static_cast<int>(u.files));
    if (u.bytes_per_second) t += " · " + size_text(u.bytes_per_second) + "/s";
    if (u.eta_seconds) t += " · " + std::string(tr("reste")) + " " + duration_text(static_cast<double>(u.eta_seconds));
    return t;
}

bool Ui::engine_choice_needed() const {
    if (snap_->connected || offline_ok_ || now_ - started_ <= 5000) return false;
    return !snap_->ever_connected || (lost_at_ && now_ - lost_at_ > 30000);
}

int Ui::home_hint_width() const { return tab_ == home ? 0 : 52 + Surface::measure(tr("Accueil"), body) + 36; }
int Ui::quit_hint_width() const { return 36 + Surface::measure(tr("Maintenir 2 s : quitter"), body) + home_hint_width(); }

// Rond maintenu 2 s quitte l'app depuis n'importe quelle vue : l'aide est toujours en bas à droite.
void Ui::draw_quit_hint(Surface& s) {
    int x = 1920 - margin - quit_hint_width();
    const int y = 1012;
    if (tab_ != home) {  // le pavé tactile dessiné (rectangle arrondi), puis « Accueil »
        s.rect(x, y + 2, 40, 24, rgb(255, 255, 255, 30), 8);
        s.outline(x, y + 2, 40, 24, color::muted, 8, 2);
        s.text(font_, x + 52, y - 2, tr("Accueil"), body, color::muted);
        x += home_hint_width();
    }
    icon(s, 1, x, y, color::muted);
    s.text(font_, x + 36, y - 2, tr("Maintenir 2 s : quitter"), body, color::muted);
}

void Ui::load_disk(const std::string& place) {
    if (!disk_loading_.insert(place).second) return;  // déjà en lecture
    model_.command("GET", "/api/v2/backups?place=" + url_encode(place), "", [this, place](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { j = Json::object(); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e); }
        std::lock_guard<std::mutex> l(async_mu_);
        pending_backups_ = std::move(j);
        pending_backups_place_ = place;
        has_pending_backups_ = true;
    });
}

const pbc::Json& Ui::disk_list(const std::string& place) const {
    static const pbc::Json empty = pbc::Json::object();
    const auto it = disk_cache_.find(place);
    return it == disk_cache_.end() ? empty : it->second;
}

// « 3 h 12 », « 18 min ».
static std::string hours_text(std::int64_t s) {
    if (s < 3600) return std::to_string(std::max<std::int64_t>(1, s / 60)) + " min";
    char b[32];
    std::snprintf(b, sizeof b, "%lld h %02lld", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    return b;
}

void Ui::draw_footer(Surface& s, std::initializer_list<std::pair<int, const char*>> hints) {
    // Sur la rangée de filtres, les aides de la liste ne servent pas : on dit comment s'y déplacer.
    const std::initializer_list<std::pair<int, const char*>> on_bar = {{-1, tr("Gauche / droite")}, {2, tr("changer")}, {0, tr("Revenir à la liste")}};
    if (bar_ && tab_ == tasks && !popup_ && !sync_menu_ && !sync_stage_) hints = on_bar;
    int x = margin;
    const int y = 1012;
    // « Maintenir Rond : quitter » est dessiné à part, en bas à droite de chaque vue (draw_quit_hint).
    const std::string_view q1 = tr("Maintenir : quitter"), q2 = tr("Maintenir 2 s : quitter");
    const int right = 1920 - margin - quit_hint_width() - 48;
    for (const auto& [which, label] : hints) {
        if (!label || !*label || label == q1 || label == q2) continue;
        if (x + (which >= 0 ? 36 : 0) + Surface::measure(label, body) > right) break;  // jamais par-dessus « quitter »
        if (which >= 0) { icon(s, which, x, y, color::muted); x += 36; }
        x += s.text(font_, x, y - 2, label, body, color::muted) + 48;
    }
}

// Clé d'une jaquette dans le cache de l'app : la clé de l'élément et sa source (une jaquette officielle
// qui remplace l'image d'une sauvegarde est donc relue).
std::string Ui::cover_key(const Item& it) { return it.cover_ref.empty() ? it.key : it.key + '\x1f' + it.cover_ref; }

void Ui::cover(Surface& s, const Item& it, int x, int y, int sz, bool badges) {
    const Image img = it.has_cover ? covers_.get(cover_key(it)) : Image{};
    if (img.valid()) s.image(img, x, y, sz, sz, 16);
    else {
        // Pas de jaquette (ROM modifiée, homebrew…) : une carte à la couleur de la plateforme, avec le titre.
        const std::string& p = it.kind == "rom" ? it.platform : it.kind == "app" ? std::string("Application") : std::string("PS5");
        // Une carte bleu-violet pour toutes les consoles (dégradé vers le bleu en bas).
        (void)p;
        s.rect(x, y, sz, sz, rgb(91, 79, 208), 16);
        s.rect(x, y + sz / 3, sz, sz - sz / 3, rgb(47, 95, 196, 150), 16);
        s.rect(x, y + sz * 2 / 3, sz, sz / 3, rgb(20, 42, 94, 170), 16);
        // Titre sans parenthèses de région, sur 3 lignes au plus.
        std::string t;
        int depth = 0;
        for (char c : it.title) { if (c == '(' || c == '[') ++depth; else if ((c == ')' || c == ']') && depth) --depth; else if (!depth) t += c; }
        std::vector<std::string> lines;
        std::string line, word;
        const int maxw = sz - 32;
        const auto pf = sz < 160 ? small : body;
        const int lh = sz < 160 ? 26 : 34;
        auto push = [&] {
            if (word.empty()) return;
            const std::string tryl = line.empty() ? word : line + " " + word;
            if (!line.empty() && Surface::measure(tryl, pf) > maxw) { lines.push_back(line); line = word; } else line = tryl;
            word.clear();
        };
        for (char c : t) { if (c == ' ') push(); else word += c; }
        push();
        if (!line.empty()) lines.push_back(line);
        if (lines.size() > 3) { lines.resize(3); lines[2] += "…"; }
        int ty = y + sz - 16 - static_cast<int>(lines.size()) * lh;
        for (const auto& l : lines) { s.text(font_, x + 16, ty, l, pf, color::text, maxw); ty += lh; }
    }
    if (!badges) return;
    // Une seule petite pastille sombre en bas à gauche (comme Orbit) : la jaquette reste visible.
    static const std::pair<const char*, const char*> shorts[] = {
        {"Nintendo 64", "N64"}, {"Mega Drive", "MD"}, {"Genesis", "MD"}, {"PlayStation", "PS1"}, {"PlayStation 2", "PS2"}, {"PlayStation 3", "PS3"},
        {"Super Nintendo", "SNES"}, {"SNES", "SNES"}, {"NES", "NES"}, {"Game Boy", "GB"}, {"Game Boy Color", "GBC"}, {"Game Boy Advance", "GBA"},
        {"Nintendo DS", "DS"}, {"Nintendo 3DS", "3DS"}, {"GameCube", "GC"}, {"Wii", "Wii"}, {"Wii U", "Wii U"}, {"Dreamcast", "DC"}, {"Saturn", "Saturn"},
        {"Master System", "SMS"}, {"PC Engine", "PCE"}, {"PC Engine CD", "PCE CD"}, {"Atari 2600", "2600"}, {"Arcade", "Arcade"}, {"Neo Geo", "Neo Geo"},
        {"Switch", "Switch"}, {"Switch 2", "Switch 2"}, {"Xbox 360", "X360"}, {"WonderSwan", "WS"}, {"WonderSwan Color", "WSC"}};
    std::string a = it.kind == "app" ? "App" : it.kind == "game_ps4" ? "PS4" : it.kind == "rom" ? it.platform : "PS5";
    if (it.kind == "rom") for (const auto& [full, sh] : shorts) if (it.platform == full) a = sh;
    // Couleur de la famille : bleu PlayStation, rouge Nintendo, turquoise Sega, violet appli, ambre le reste.
    std::uint32_t tone = rgb(0, 112, 209);
    if (it.kind == "app") tone = rgb(124, 92, 230);
    else if (it.kind == "game_ps4") tone = rgb(0, 67, 156);
    else if (it.kind == "rom") {
        const std::string& p = it.platform;
        const bool nintendo = p.find("Nintendo") != std::string::npos || p == "Super Nintendo" || p == "SNES" || p == "NES" || p.rfind("Game Boy", 0) == 0 ||
                              p == "GameCube" || p.rfind("Wii", 0) == 0 || p.rfind("Switch", 0) == 0;
        const bool sega = p == "Mega Drive" || p == "Genesis" || p == "Dreamcast" || p == "Saturn" || p == "Master System" || p == "Game Gear";
        const bool sony = p.rfind("PlayStation", 0) == 0 || p == "PSP";
        tone = nintendo ? rgb(228, 0, 15) : sega ? rgb(0, 150, 170) : sony ? rgb(0, 90, 180) : rgb(214, 132, 20);
    }
    const int bw = Surface::measure(a, small) + 20, bh = 28, bx = x + 10, by = y + sz - bh - 10;
    s.rect(bx + 2, by + 3, bw, bh, rgb(0, 0, 0, 110), 9);                 // ombre
    s.rect(bx, by, bw, bh, tone, 9);
    s.rect(bx, by, bw, bh / 2, rgb(255, 255, 255, 34), 9);                // reflet en haut
    s.outline(bx, by, bw, bh, rgb(255, 255, 255, 70), 9, 1);
    s.text(font_, bx + 10, by + 3, a, small, rgb(255, 255, 255));
}

void Ui::draw_header(Surface& s) {
    // Logo (plaques de verre bleues détourées), sinon carré dégradé.
    if (logo_.valid()) {
        s.image(logo_, margin - 4, 30, 64, 64, 0);  // plaques détourées, sans cadre ni halo
    } else {
        for (int i = 0; i < 44; ++i) s.rect(margin + i, 40, 1, 44, rgb(185 - i * 2, 225 - i, 255), 0);
        s.outline(margin, 40, 44, 44, rgb(255, 255, 255, 60), 12, 2);
    }
    s.text(font_, margin + 72, 36, "PS5 Backup Center", body, color::text);
    // Le slogan garde la largeur du français (traductions plus longues : coupé), pour laisser la place aux onglets.
    const int brand_w = std::max(Surface::measure("PS5 Backup Center", body), Surface::measure(tr("Votre collection, préservée."), small));
    s.text(font_, margin + 72, 68, tr("Votre collection, préservée."), small, color::muted, brand_w);
    // État du moteur (point + version) : sur une ligne, juste à gauche de la loupe.
    const bool ok = snap_->connected;
    const std::string label = ok ? (snap_->scanning ? tr("Actualisation…") : "v" + snap_->version) : snap_->ever_connected ? (engine_choice_needed() ? tr("Moteur injoignable") : tr("Reconnexion…")) : tr("Connexion au moteur…");
    const int lx0_search = 1920 - margin - 56 - 70, hx0 = lx0_search - 70;  // cœur, loupe, roue
    const int lab_w = std::min(Surface::measure(label, small), 230);
    const int st_right = hx0 - 18, st_left = st_right - lab_w - 22;
    s.rect(st_left, 56, 12, 12, ok ? color::ok : snap_->ever_connected ? color::bad : color::warn, 6);
    s.text(font_, st_left + 22, 49, label, small, color::muted, lab_w);
    // Onglets : une pastille de largeur fixe entre la marque et l'état ; s'ils ne tiennent pas tous, elle défile (L1 / R1)
    // et de petites flèches ‹ › disent qu'il y en a d'autres de ce côté.
    const int px0 = margin + 72 + brand_w + 44, px1 = st_left - 26;
    s.rect(px0, 34, px1 - px0, 56, rgb(255, 255, 255, 16), 28);
    s.outline(px0, 34, px1 - px0, 56, rgb(255, 255, 255, 22), 28, 1);
    s.rect(px0 + 10, 48, 40, 28, rgb(255, 255, 255, 18), 8);
    s.text(font_, px0 + 16, 49, "L1", small, color::muted);
    s.rect(px1 - 50, 48, 40, 28, rgb(255, 255, 255, 18), 8);
    s.text(font_, px1 - 44, 49, "R1", small, color::muted);
    constexpr int pad = 22;
    std::vector<int> wv;
    int total = 0;
    for (int i = 0; i < settings; ++i) { wv.push_back(Surface::measure(tr(tab_names[i]), body) + pad); total += wv.back() + 2; }
    int a0 = px0 + 58, a1 = px1 - 58;  // zone des onglets
    const bool overflow = total > a1 - a0;
    if (overflow) { a0 += 22; a1 -= 22; }  // place des flèches
    const int nt = static_cast<int>(settings);
    if (!overflow) hdr_first_ = 0;
    else {
        const int active = tab_ < settings ? static_cast<int>(tab_) : -1;
        hdr_first_ = std::clamp(hdr_first_, 0, nt - 1);
        auto last_fit = [&](int first) { int x = a0, last = first - 1; for (int i = first; i < nt; ++i) { if (x + wv[static_cast<std::size_t>(i)] > a1) break; x += wv[static_cast<std::size_t>(i)] + 2; last = i; } return last; };
        if (active >= 0 && active < hdr_first_) hdr_first_ = active;
        while (active >= 0 && last_fit(hdr_first_) < active && hdr_first_ < active) ++hdr_first_;
        while (hdr_first_ > 0 && last_fit(hdr_first_ - 1) >= nt - 1) --hdr_first_;  // pas de vide à droite
    }
    // Onglets visibles, puis la place qui reste répartie entre eux : la pastille reste équilibrée (pas de vide à droite).
    int vis_last = hdr_first_ - 1, used = 0;
    for (int i = hdr_first_, xx = a0; i < nt; ++i) {
        if (xx + wv[static_cast<std::size_t>(i)] > a1) break;
        xx += wv[static_cast<std::size_t>(i)] + 2; used = xx - a0; vis_last = i;
    }
    const int nvis = vis_last - hdr_first_ + 1, extra = overflow && nvis > 0 ? std::max(0, (a1 - a0 - used) / nvis) : 0;
    int x = a0, last = hdr_first_ - 1;
    for (int i = hdr_first_; i <= vis_last; ++i) {
        const int w = wv[static_cast<std::size_t>(i)] + extra;
        last = i;
        if (i == tab_ && !loupe_) {  // onglet actif : verre éclairé, texte blanc, trait lumineux dessous
            s.rect(x, 40, w, 44, rgb(255, 255, 255, 34), 22);
            s.outline(x, 40, w, 44, rgb(255, 255, 255, 50), 22, 1);
            const int tw = Surface::measure(tr(tab_names[i]), body);
            s.rect(x + (w - tw) / 2 - 2, 80, tw + 4, 8, rgb(124, 196, 255, 70), 4);
            s.rect(x + (w - tw) / 2, 81, tw, 4, color::accent, 2);
        }
        s.text(font_, x + (w - Surface::measure(tr(tab_names[i]), body)) / 2, 46, tr(tab_names[i]), body, i == tab_ ? color::text : rgb(160, 180, 204));
        if (i == tasks) {
            int active = 0;
            for (const auto& t : snap_->tasks) if (in_group(t.state, 0)) ++active;
            if (active) { char b[8]; std::snprintf(b, sizeof b, "%d", active); s.rect(x + w - 14, 34, 26, 24, color::accent2, 12); s.text(font_, x + w - 6, 35, b, small, color::text); }
        }
        x += w + 2;
    }
    if (hdr_first_ > 0) s.text(font_, a0 - 20, 44, "‹", body, color::accent);   // d'autres onglets à gauche
    if (last < nt - 1) s.text(font_, a1 + 6, 44, "›", body, color::accent);     // et à droite
    // Soutenir le projet : un petit cœur, juste avant la loupe.
    {
        const int hx = 1920 - margin - 56 - 140, hy = 34, cx = hx + 28, cy = hy + 28;  // centre du rond
        if (heart_) for (int g = 3; g >= 1; --g) s.rect(hx - g * 4, hy - g * 4, 56 + g * 8, 56 + g * 8, rgb(255, 94, 91, 26), 28 + g * 4);
        s.rect(hx, hy, 56, 56, heart_ ? rgb(255, 255, 255, 40) : rgb(255, 255, 255, 16), 28);
        s.outline(hx, hy, 56, 56, heart_ ? rgb(255, 94, 91) : rgb(255, 255, 255, 30), 28, heart_ ? 3 : 1);
        // Le cœur « ♥ » de la police, comme dans « Quoi de neuf » (dessiné par le typographe : courbes parfaites), en grand
        // et centré dans le rond.
        const int gw = Surface::measure("♥", large);
        s.text(font_, cx - gw / 2, cy - 19, "♥", large, rgb(255, 94, 91));
        if (heart_) {
            const char* t = tr("Soutenir le projet");
            const int tw = 36 + Surface::measure(t, small), tx = std::min(hx + 28 - tw / 2, 1920 - margin - tw);
            s.rect(tx - 14, hy + 66, tw + 28, 44, color::panel2, 22);
            icon(s, 0, tx, hy + 74, color::accent);
            s.text(font_, tx + 36, hy + 72, t, small, color::text);
        }
    }
    // Recherche : une loupe (Triangle, depuis l'Accueil ou la Bibliothèque).
    {
        const int lx0 = 1920 - margin - 56 - 70, ly0 = 34, cx = lx0 + 28, cy = ly0 + 28;
        const bool on = kb_open_ || loupe_;
        if (loupe_) for (int g = 3; g >= 1; --g) s.rect(lx0 - g * 4, ly0 - g * 4, 56 + g * 8, 56 + g * 8, rgb(80, 150, 255, 24), 28 + g * 4);
        s.rect(lx0, ly0, 56, 56, on ? rgb(255, 255, 255, 40) : rgb(255, 255, 255, 16), 28);
        s.outline(lx0, ly0, 56, 56, on ? color::accent : rgb(255, 255, 255, 30), 28, on ? 3 : 1);
        const std::uint32_t c = on ? color::text : rgb(185, 203, 224);
        s.outline(cx - 12, cy - 12, 20, 20, c, 10, 3);                       // verre
        for (int i = 0; i < 8; ++i) s.rect(cx + 4 + i, cy + 4 + i, 4, 4, c, 2);  // manche
        if (loupe_) {  // sous la loupe sélectionnée : ce que fait Croix
            const char* t = tr("Rechercher");
            const int tw = 36 + Surface::measure(t, small), tx = std::min(lx0 + 28 - tw / 2, 1920 - margin - tw);
            s.rect(tx - 14, ly0 + 66, tw + 28, 44, color::panel2, 22);
            icon(s, 0, tx, ly0 + 74, color::accent);
            s.text(font_, tx + 36, ly0 + 72, t, small, color::text);
        }
    }
    // Paramètres : une roue dentée en haut à droite (R1 depuis Tâches, ou L1 depuis l'Accueil).
    {
        const int gx = 1920 - margin - 56, gy = 34, cx = gx + 28, cy = gy + 28;
        const bool on = tab_ == settings;
        s.rect(gx, gy, 56, 56, on ? rgb(255, 255, 255, 40) : rgb(255, 255, 255, 16), 28);
        s.outline(gx, gy, 56, 56, on ? color::accent : rgb(255, 255, 255, 30), 28, on ? 3 : 1);
        const std::uint32_t c = on ? color::text : rgb(185, 203, 224);
        static const int tx[8] = {0, 11, 15, 11, 0, -11, -15, -11}, ty[8] = {-15, -11, 0, 11, 15, 11, 0, -11};
        for (int i = 0; i < 8; ++i) s.rect(cx + tx[i] - 4, cy + ty[i] - 4, 8, 8, c, 2);
        s.rect(cx - 11, cy - 11, 22, 22, c, 11);
        s.rect(cx - 5, cy - 5, 10, 10, on ? rgb(40, 60, 90) : rgb(20, 36, 58), 5);
    }
}

// ---- Accueil ----

void Ui::load_overview() {
    overview_loaded_ = now_;
    if (overview_inflight_.exchange(true)) return;  // la demande précédente n'est pas revenue : on ne l'empile pas
    model_.command("GET", "/api/v2/saves/overview", "", [this](const std::string& e, const std::string& body) {
        Json j;
        overview_inflight_ = false;
        if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_overview_ = j; has_pending_overview_ = true; }
    });
}

// Carrousel : d'abord les jeux dont la sauvegarde n'est pas protégée (jamais sauvegardée, ou jouée
// depuis), puis le reste de la collection avec une jaquette. Cinq au plus.
std::vector<const Item*> Ui::hero_items() const {
    std::vector<const Item*> out;
    auto add = [&](const Item* it) {
        if (it && it->has_cover && !it->ignored && out.size() < 5 && std::find(out.begin(), out.end(), it) == out.end()) out.push_back(it);
    };
    auto find = [&](const std::string& key) -> const Item* {
        for (const auto& i : snap_->items) if (i.key == key) return &i;
        return nullptr;
    };
    for (const char* want : {"console_newer", "not_backed_up"})  // les jeux d'abord, les applications ensuite
        for (const auto& e : overview_["entries"].items())
            if (e["status"].as_string() == want)
                if (const Item* it = find(e["item"].as_string()); it && it->kind.rfind("game", 0) == 0) add(it);
    for (const auto& i : snap_->items) if (i.kind.rfind("game", 0) == 0) add(&i);
    for (const auto& i : snap_->items) add(&i);
    return out;
}

// Titre sans les parenthèses de région ni les crochets : « Aladdin (Europe) » → « Aladdin ».
static std::string clean_title(const std::string& title) {
    std::string t;
    int depth = 0;
    for (char c : title) { if (c == '(' || c == '[') ++depth; else if ((c == ')' || c == ']') && depth) --depth; else if (!depth) t += c; }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t.empty() ? title : t;
}

// Rangées de la Bibliothèque : les éléments filtrés, regroupés (jeux PS5, jeux PS4, une rangée par
// console rétro, applications, sauvegardes sans le jeu).
std::vector<Ui::Shelf> Ui::library_shelves() const {
    std::vector<Shelf> out, retro;
    Shelf ps5{tr("Jeux PS5"), {}}, ps4{tr("Jeux PS4"), {}}, apps{tr("Applications"), {}}, orphans{tr("Sauvegardes sans le jeu"), {}};
    for (const Item* i : visible_items()) {
        // Sauvegarde sans le jeu : clé « save:… » ici ; pour l'autre PS5, sa fiche le dit (here = save_only).
        const Json* m = lib_peer_.empty() ? nullptr : remote_meta_of(i);
        const bool save_only = i->key.rfind("save:", 0) == 0 || (m && (*m)["here"].as_string() == "save_only");
        if (save_only && i->kind != "app") orphans.items.push_back(i);
        else if (i->kind == "game_ps5") ps5.items.push_back(i);
        else if (i->kind == "game_ps4") ps4.items.push_back(i);
        else if (i->kind == "app") apps.items.push_back(i);
        else {
            Shelf* sh = nullptr;
            for (auto& r : retro) if (r.title == i->platform) sh = &r;
            if (!sh) { retro.push_back({i->platform, {}}); sh = &retro.back(); }
            sh->items.push_back(i);
        }
    }
    std::sort(retro.begin(), retro.end(), [](const Shelf& a, const Shelf& b) { return a.items.size() > b.items.size(); });
    std::vector<Shelf> cats;
    for (Shelf* sh : {&ps5, &ps4}) if (!sh->items.empty()) cats.push_back(*sh);
    for (auto& r : retro) cats.push_back(r);
    for (Shelf* sh : {&apps, &orphans}) if (!sh->items.empty()) cats.push_back(*sh);
    // Grille : 8 jaquettes par rangée ; les rangées suivantes d'une catégorie n'ont pas de titre (cont = true).
    for (const auto& c : cats)
        for (std::size_t i = 0; i < c.items.size(); i += lib_per_row) {
            Shelf row{i ? std::string() : c.title, {}};
            row.total = c.items.size();
            row.items.assign(c.items.begin() + static_cast<std::ptrdiff_t>(i), c.items.begin() + static_cast<std::ptrdiff_t>(std::min(c.items.size(), i + lib_per_row)));
            out.push_back(std::move(row));
        }
    return out;
}

// Rangées par catégorie de l'Accueil (comme avant le nouveau découpage) : jeux, une rangée par console rétro,
// applications, jeux dont il ne reste que la sauvegarde. D'où on peut ouvrir la fiche et sauvegarder.
std::vector<Ui::Shelf> Ui::category_shelves() const {
    std::vector<Shelf> out, retro;
    Shelf games{tr("Jeux PS5"), {}}, games4{tr("Jeux PS4"), {}}, apps{tr("Applications"), {}}, orphans{tr("Sauvegardes sans le jeu"), {}};
    for (const auto& i : snap_->items) {
        if (i.ignored) continue;
        if (i.key.rfind("save:", 0) == 0) { if (i.kind != "app") orphans.items.push_back(&i); continue; }
        if (i.kind == "game_ps4") games4.items.push_back(&i);
        else if (i.kind.rfind("game", 0) == 0) games.items.push_back(&i);
        else if (i.kind == "app") apps.items.push_back(&i);
        else if (i.kind == "rom") {
            Shelf* sh = nullptr;
            for (auto& r : retro) if (r.title == i.platform) sh = &r;
            if (!sh) { retro.push_back({i.platform, {}}); sh = &retro.back(); }
            sh->items.push_back(&i);
        }
    }
    std::sort(retro.begin(), retro.end(), [](const Shelf& a, const Shelf& b) { return a.items.size() > b.items.size(); });
    // Ordre : jeux, puis leurs sauvegardes sans le jeu (à restaurer ou à nettoyer), puis rétro et applications.
    if (!games.items.empty()) out.push_back(games);
    if (!games4.items.empty()) out.push_back(games4);
    if (!orphans.items.empty()) out.push_back(orphans);
    for (auto& r : retro) { r.later = true; out.push_back(r); }
    if (!apps.items.empty()) { apps.later = true; out.push_back(apps); }
    return out;
}

// Rangées de l'Accueil (comme « Découvrir ») : ce qui est à sauvegarder, les jeux, une rangée par
// console rétro, les applications, les jeux dont il ne reste que la sauvegarde.
// Rangées de l'Accueil : « À faire » (cartes vers l'endroit où agir), puis « Récemment joués ». Jamais d'élément ignoré.
std::vector<Ui::Shelf> Ui::home_shelves() const {
    std::vector<Shelf> out;
    Shelf todo{tr("À faire"), {}, true};
    todo.items.assign(home_cards().size(), nullptr);  // une case par carte (dessinées à part)
    out.push_back(std::move(todo));
    // Récemment joués : les sauvegardes de la console, de la plus récente à la plus ancienne.
    std::vector<std::pair<std::int64_t, const Item*>> played;
    for (const auto& e : overview_["entries"].items()) {
        if (!e["console"].is_object()) continue;
        const std::int64_t t = e["console"]["mtime"].as_int();
        for (const auto& i : snap_->items)
            if (i.key == e["item"].as_string() && !i.ignored) {
                bool dup = false;
                for (const auto& [t2, it2] : played) dup = dup || it2 == &i;
                if (!dup) played.push_back({t, &i});
                break;
            }
    }
    std::sort(played.begin(), played.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    Shelf recent{tr("Récemment joués"), {}};
    for (const auto& [t, it] : played) if (recent.items.size() < 16) recent.items.push_back(it);
    if (!recent.items.empty()) out.push_back(std::move(recent));
    // Sur cette console d'abord (jeux, sauvegardes sans le jeu), puis ce qui est AILLEURS (copies, autres PS5), puis le
    // rétro et les applications.
    auto cats = category_shelves();
    std::size_t ci = 0;
    for (; ci < cats.size() && !cats[ci].later; ++ci) out.push_back(std::move(cats[ci]));
    // Backups disponibles : sauvegarde absente de la console (ou jeu non installé) mais des copies quelque part —
    // ici, sur le NAS ou sur l'autre PS5. Croix : leurs versions, pour restaurer ou récupérer.
    {
        home_bk_items_.clear();
        home_bk_games_.clear();
        std::vector<std::pair<std::int64_t, const Json*>> avail;
        if (mg_sum_.is_object())
            for (const auto& g : mg_sum_["games"].items()) {
                if (g["copies"].size() == 0 || g["other_account"].as_bool()) continue;
                bool installed = false;
                if (g["full"].as_bool()) { for (const auto& i : snap_->items) if (i.title_id == g["title_id"].as_string() && i.key.rfind("save:", 0) != 0) installed = true; }
                else installed = g["console"].is_object();
                if (installed) continue;
                bool ign = false;
                for (const auto& i : snap_->items) if (i.ignored && !i.title_id.empty() && i.title_id == g["title_id"].as_string()) ign = true;
                if (ign) continue;
                std::int64_t newest = 0;
                for (const auto& c : g["copies"].items()) if (c["complete"].as_bool(true)) newest = std::max(newest, c["created_unix"].as_int());
                if (newest) avail.push_back({newest, &g});
            }
        std::sort(avail.begin(), avail.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (avail.size() > 16) avail.resize(16);
        // Place réservée pour les copies des autres PS5 aussi (les pointeurs des rangées doivent rester valables).
        std::size_t peer_copies = 0;
        if (mg_sum_.is_object()) peer_copies = mg_sum_["games"].size();
        home_bk_items_.reserve(avail.size() + peer_copies);
        for (const auto& [t, g] : avail) {
            Item tmp;
            const Item* f = find_or_stand_in((*g)["item"].as_string(), (*g)["title"].as_string(), (*g)["title_id"].as_string(), tmp, (*g)["kind"].as_string());
            home_bk_items_.push_back(*f);
            home_bk_games_.push_back(*g);
        }
        Shelf bk{tr("Sauvegardes disponibles"), {}};
        bk.bk_first = 0;
        {   // seulement les endroits qui ont vraiment une de ces sauvegardes : « Ici », « NAS », le nom de l'autre PS5
            bool here = false, nas = false;
            std::vector<std::string> peers;
            for (const auto& g : home_bk_games_)
                for (const auto& c : g["copies"].items()) {
                    if (c["on_peer"].as_bool()) { const std::string pn = c["peer_name"].as_string(); if (!pn.empty() && std::find(peers.begin(), peers.end(), pn) == peers.end()) peers.push_back(pn); }
                    else if (c["place"].as_string().rfind("remote:", 0) == 0) nas = true;
                    else here = true;
                }
            std::string w;
            auto add = [&w](const std::string& x) { w += (w.empty() ? "" : " · ") + x; };
            if (here) add(tr("Ici"));
            if (nas) add("NAS");
            for (const auto& pn : peers) add(pn);
            bk.away = w;
        }
        for (const auto& it : home_bk_items_) bk.items.push_back(&it);
        if (!bk.items.empty()) out.push_back(std::move(bk));
    }
    // Sur les autres PS5 : leurs jeux installés qui ne sont pas ici. Croix : les recevoir ici.
    {
        home_peer_items_.clear();
        home_peer_refs_.clear();
        std::size_t total = 0;
        for (const auto& p : peer_games_.items()) total += std::min<std::size_t>(24, p["games"].size());
        home_peer_items_.reserve(total);
        home_peer_refs_.reserve(total);
        for (const auto& p : peer_games_.items()) {
            if (p["games"].size() == 0) continue;
            Shelf sh{trf("%s · jeux installés", p["name"].as_string().c_str()), {}};
            sh.away = tr("sur l'autre PS5");
            sh.peer_first = static_cast<int>(home_peer_refs_.size());
            for (const auto& g : p["games"].items()) {
                if (sh.items.size() >= 24) break;
                Item tmp;
                const Item* f = find_or_stand_in("tid:" + std::string(g["kind"].as_string() == "game_ps4" ? "ps4:" : "ps5:") + g["title_id"].as_string(), g["title"].as_string(), g["title_id"].as_string(), tmp, g["kind"].as_string());
                home_peer_items_.push_back(*f);
                home_peer_refs_.push_back({p["instance"].as_string(), g});
                sh.items.push_back(&home_peer_items_.back());
            }
            out.push_back(std::move(sh));
        }
    }
    // Backups rangés sur chaque autre PS5 (même pour des jeux présents ici) : Croix → leurs versions.
    {
        std::map<std::string, std::pair<std::string, std::vector<std::size_t>>> per;  // instance → (nom, jeux)
        if (mg_sum_.is_object())
            for (std::size_t gi = 0; gi < mg_sum_["games"].size(); ++gi) {
                const Json& g = mg_sum_["games"][gi];
                std::set<std::string> seen;
                for (const auto& c : g["copies"].items())
                    if (c["on_peer"].as_bool() && seen.insert(c["peer"].as_string()).second) {
                        auto& e = per[c["peer"].as_string()];
                        e.first = c["peer_name"].as_string();
                        e.second.push_back(gi);
                    }
            }
        for (const auto& [inst, e] : per) {
            Shelf sh{trf("%s · sauvegardes", e.first.c_str()), {}};
            sh.away = tr("sur l'autre PS5");
            sh.bk_first = static_cast<int>(home_bk_games_.size());
            for (const std::size_t gi : e.second) {
                if (sh.items.size() >= 24 || home_bk_items_.size() == home_bk_items_.capacity()) break;
                const Json& g = mg_sum_["games"][gi];
                Item tmp;
                const Item* f = find_or_stand_in(g["item"].as_string(), g["title"].as_string(), g["title_id"].as_string(), tmp, g["kind"].as_string());
                home_bk_items_.push_back(*f);
                home_bk_games_.push_back(g);
                sh.items.push_back(&home_bk_items_.back());
            }
            if (!sh.items.empty()) out.push_back(std::move(sh));
        }
    }
    for (; ci < cats.size(); ++ci) out.push_back(std::move(cats[ci]));
    return out;
}

// Centre de notifications : une bulle par sorte de chose à faire (à sauvegarder, doublons, pas encore comparés, à
// vérifier, tâches), avec le nombre ; la jaquette et le nom des jeux concernés défilent dans la bulle.
std::vector<Ui::HomeCard> Ui::home_cards() const {
    using Games = std::vector<std::pair<std::string, std::string>>;
    std::vector<HomeCard> out;
    auto ignored = [&](const std::string& key) { for (const auto& i : snap_->items) if (i.key == key) return i.ignored; return false; };
    Games save;
    for (const auto& a : overview_["accounts"].items()) {
        if (!a["on_console"].as_bool()) continue;
        for (const auto& e : overview_["entries"].items()) {
            const std::string st = e["status"].as_string();
            if (e["account"].as_string() == a["key"].as_string() && (st == "console_newer" || st == "not_backed_up") && !ignored(e["item"].as_string()))
                save.push_back({e["item"].as_string(), e["title"].as_string()});
        }
    }
    if (!save.empty()) out.push_back({tr("À SAUVEGARDER"), trf("%d jeu(x)", static_cast<int>(save.size())), "", color::warn, 5, save});
    {   // Mise à jour de Backup Center : en cours (reçue d'une autre PS5 ou trouvée) ou installée mais pas encore en marche
        const auto& u = snap_->update;
        const std::string from = u.received_from.empty() ? std::string() : trf("reçue de %s", u.received_from.c_str());
        if (u.state == "downloading" || u.state == "installing") {
            const bool dl = u.state == "downloading" && u.bytes_total;
            const int pct = dl ? static_cast<int>(std::min<std::uint64_t>(100, u.bytes_done * 100 / u.bytes_total)) : 0;
            out.push_back({tr("MISE À JOUR"), dl ? trf("Téléchargement… %d %%", pct) : u.state == "downloading" ? std::string(tr("Téléchargement…")) : std::string(tr("Installation…")), "", color::accent, 8,
                           {{"pbc:logo", dl ? update_progress_text(u) : from.empty() ? std::string(tr("Backup Center se met à jour")) : from}}});
        } else if (u.engine_restart_needed && !u.installed.empty()) {
            out.push_back({tr("MISE À JOUR"), trf("%s installée", u.installed.c_str()), "", color::ok, 7,
                           {{"pbc:logo", (from.empty() ? std::string() : from + " · ") + tr("Croix : relancer le moteur")}}});
        } else if (u.state == "available" && !u.available.empty()) {  // trouvée par la recherche automatique ou manuelle
            out.push_back({tr("MISE À JOUR"), trf("%s disponible", u.available.c_str()), "", color::accent, 10,
                           {{"pbc:logo", tr("Croix : installer")}}});
        } else if (!snap_->version.empty() && version_cmp(snap_->version, app_version) > 0) {  // moteur à jour, appli ouverte ancienne (pas l'inverse)
            out.push_back({tr("MISE À JOUR"), trf("%s en marche", snap_->version.c_str()), "", color::ok, 9,
                           {{"pbc:logo", tr("Croix : rouvrir l'appli pour la voir")}}});
        }
    }
    {   // Une copie plus récente que la partie sur la console (jouée ailleurs, sur l'autre PS5…) : à restaurer ?
        Games newer;
        for (const auto& a : overview_["accounts"].items()) {
            if (!a["on_console"].as_bool()) continue;
            for (const auto& e : overview_["entries"].items())
                if (e["account"].as_string() == a["key"].as_string() && e["status"].as_string() == "backup_newer" && !ignored(e["item"].as_string()))
                    newer.push_back({e["item"].as_string(), e["title"].as_string()});
        }
        if (!newer.empty()) out.push_back({tr("COPIE PLUS RÉCENTE"), trf("%d jeu(x) à restaurer ?", static_cast<int>(newer.size())), "", color::accent, 6, newer});
    }
    if (mg_sum_.is_object()) {
        Games dup, cmp, bad;
        int copies = 0;
        for (const auto& g : mg_sum_["games"].items()) {
            if (ignored(g["item"].as_string())) continue;
            const std::pair<std::string, std::string> kt{g["item"].as_string(), g["title"].as_string()};
            if (g["remove"].size()) dup.push_back(kt);
            if (!g["compared"].as_bool() && !g["full"].as_bool()) cmp.push_back(kt);
            int n = 0;
            for (const auto& c : g["copies"].items()) n += copy_suspect(c);
            if (n) bad.push_back(kt);
            copies += n;
        }
        if (mg_sum_["to_remove"].as_int() > 0 && !dup.empty())
            out.push_back({tr("SAUVEGARDES · DOUBLONS"), trf("%s à libérer", size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, mg_sum_["freed_bytes"].as_int()))).c_str()), "", color::ok, 1, dup});
        if (mg_sum_["to_compare"].as_int() > 0 && !cmp.empty())
            out.push_back({tr("SAUVEGARDES · PAS ENCORE COMPARÉES"), trf("%lld jeu(x)", static_cast<long long>(mg_sum_["to_compare"].as_int())), "", color::accent, 2, cmp});
        if (!bad.empty()) out.push_back({tr("SAUVEGARDES · À VÉRIFIER"), trf("%d copie(s) à vérifier", copies), "", color::bad, 3, bad});
    }
    // Synchro des copies : en cours (avancement), sinon l'état avec chaque autre PS5 (à jour / N copies à échanger).
    if (sync_copies_status_["state"].as_string() == "running") {
        const Json& st = sync_copies_status_;
        HomeCard c{tr("SYNCHRO DES SAUVEGARDES"), trf("%lld / %lld copie(s)", static_cast<long long>(st["done"].as_int()), static_cast<long long>(st["total"].as_int())),
                   "", rgb(79, 209, 197), 4, {{"", st["current"].as_string().empty() ? st["name"].as_string() : st["current"].as_string()}}};
        c.progress.push_back(st["total"].as_int() ? static_cast<int>(st["done"].as_int() * 1000 / st["total"].as_int()) : -1);
        c.eta.push_back("");
        out.push_back(std::move(c));
    } else {
        // Une seule carte Synchro (comme Sauvegardes) : d'abord les appareils avec des jeux à mettre à niveau (leurs jaquettes
        // défilent), puis « au même niveau », et une seule ligne pour tous ceux sans jeu choisi. Croix : le premier à traiter.
        int need_total = 0, first = -1, same_first = -1;
        Games g, same;
        Games idle;  // un appareil sans jeu choisi par ligne, avec son image (PS5 ou NAS) : ils défilent dans la carte
        for (std::size_t i = 0; i < sync_plans_.size(); ++i) {
            const auto& [name, pl] = sync_plans_[i];
            const char* dev_key = pl["peer"].as_string().rfind("remote:", 0) == 0 ? "dev:nas" : "dev:ps5";  // l'image de l'autre côté : NAS ou PS5
            int chosen = 0, n = 0;
            for (const auto& t : pl["titles"].items()) {
                chosen += t["followed"].as_bool();
                if (!sy_needs(t)) continue;
                ++n;
                if (g.size() < 12) g.push_back({"title:" + t["title_id"].as_string(), std::string(t["state"].as_string() == "pull" ? "↓ " : "↑ ") + t["title"].as_string() + " · " + name});
            }
            if (!chosen) { idle.push_back({dev_key, trf("%s · aucun jeu choisi", name.c_str())}); continue; }
            if (n) { need_total += n; if (first < 0) first = static_cast<int>(i); continue; }
            same.push_back({dev_key, trf("%s : les mêmes sauvegardes des deux côtés", name.c_str())});
            if (same_first < 0) same_first = static_cast<int>(i);
        }
        if (!sync_plans_.empty()) {
            const std::string head = sync_plans_.size() > 1 ? trf("SYNCHRO · %d appareils", static_cast<int>(sync_plans_.size())) : std::string(tr("SYNCHRO"));
            if (need_total) {
                g.insert(g.end(), idle.begin(), idle.end());
                out.push_back({head, trf("%d jeu(x) à mettre à niveau", need_total), "", color::warn, 100 + first, g});
            } else if (!same.empty()) {
                same.insert(same.end(), idle.begin(), idle.end());
                out.push_back({head, tr("Tout est au même niveau"), "", color::ok, 100 + same_first, same});
            } else {
                out.push_back({head, tr("Aucun jeu choisi"), "", color::muted, 100, idle});  // titre court ; chaque appareil défile avec son image
            }
        }
    }
    {   // tâches : une bulle par sorte (compression, transfert, décompression…), en cours d'abord, avec l'avancement
        std::vector<std::string> kinds;
        for (const auto& t : snap_->tasks)
            if ((t.state == "running" || t.state == "queued" || t.state == "paused") && std::find(kinds.begin(), kinds.end(), task_kind(t.type)) == kinds.end()) kinds.push_back(task_kind(t.type));
        // Une couleur par famille : sauvegarde / restauration violet, transfert / synchro turquoise, compression / images rose.
        auto family_tone = [](const std::string& type) {
            if (type == "transfer" || type == "import_switch" || type == "export_switch" || type == "link_send") return rgb(79, 209, 197);
            if (type == "compress" || type == "uncompress" || type == "make_image" || type == "extract_image" || type == "validate_image") return rgb(244, 114, 182);
            return rgb(167, 123, 255);
        };
        for (const auto& k : kinds) {
            std::string type;
            for (const auto& t : snap_->tasks) if (task_kind(t.type) == k) { type = t.type; break; }
            HomeCard c{k, "", "", family_tone(type), 4, {}};
            int run = 0, wait = 0;
            for (int pass = 0; pass < 2; ++pass)
                for (const auto& t : snap_->tasks) {
                    if (task_kind(t.type) != k || (pass == 0) != (t.state == "running") || (pass == 1 && t.state != "queued" && t.state != "paused")) continue;
                    (pass == 0 ? run : wait)++;
                    c.games.push_back({t.item, t.title.empty() ? k : t.title});
                    const bool ph = t.state == "running" && t.phase_fraction() >= 0;
                    c.progress.push_back(ph ? static_cast<int>(t.phase_fraction() * 1000) : t.state == "running" && t.total ? static_cast<int>(std::min(t.done, t.total) * 1000 / t.total) : -1);
                    c.eta.push_back(ph ? "\t" + t.phase_text() : t.state == "running" && t.speed > 0 && t.total ? duration_text(static_cast<double>(t.total - std::min(t.done, t.total)) / t.speed) : std::string());
                }
            c.title = !run ? trf("%d en attente", wait) : wait ? trf("%d en cours", run) + " · " + trf("%d en attente", wait) : trf("%d en cours", run);
            out.push_back(std::move(c));
        }
    }
    if (out.empty()) out.push_back({"", tr("Tout est en ordre"), tr("Rien ne demande votre attention."), color::ok, -1, {}});
    return out;
}

void Ui::run_home_card(int action) {
    if (action >= 0 && action <= 3) { tab_ = backups; mg_screen_ = 0; mg_place_ = 0; mg_view_ = action; mg_row_ = 0; mg_zone_ = 1; mg_back_tab_ = -1; }
    else if (action == 4) tab_ = tasks;
    else if (action == 5) { tab_ = parties; pt_player_ = 0; pt_view_ = 1; pt_row_ = 0; pt_zone_ = 1; }
    else if (action == 6) { tab_ = parties; pt_player_ = 0; pt_view_ = 2; pt_row_ = 0; pt_zone_ = 1; }  // « Backup plus récent »
    else if (action == 7) {  // mise à jour installée : relancer le moteur (par la voie normale ; un refus est expliqué)
        model_.command("POST", "/api/v2/update/restart-engine", "{}", [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? std::string(tr("Relance du moteur…")) : trf("Relance refusée : %s — la nouvelle version sera active au prochain démarrage de la console", e.c_str()));
        });
    }
    else if (action == 9) offer_reopen(snap_->version);  // rouvrir l'appli : la même fenêtre de confirmation (avant : un compte à rebours invisible ici)
    else if (action == 8) tab_ = settings;  // mise à jour en cours : son avancement dans Paramètres
    else if (action == 10) {  // mise à jour disponible : l'installer (l'avancement remplace la bulle)
        model_.command("POST", "/api/v2/update/install", "{}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
        toast(tr("Téléchargement de la mise à jour…"));
    }
    else if (action >= 100 && action - 100 < static_cast<int>(sync_plans_.size())) {  // synchro des copies avec cette PS5
        // La page Synchro de cette PS5, le bouton « Mettre à niveau » choisi
        const std::string peer = sync_plans_[static_cast<std::size_t>(action - 100)].second["peer"].as_string();
        tab_ = synchro;
        sy_pick_ = false;
        const auto devs = sy_devices();
        for (std::size_t i = 0; i < devs.size(); ++i) if (devs[i].id == peer) sn_card_ = static_cast<int>(i);
        sn_zone_ = 1;
        sn_target_ = 0;
    }
}

void Ui::input_home(std::uint32_t k) {
    const auto hero = hero_items();
    const auto shelves = home_shelves();
    auto open = [&](const Item* it) { sheet_ = true; sheet_key_ = it->key; sheet_focus_ = 0; unchecked_.clear(); dest_ = 0; };
    if (home_cols_.size() != shelves.size()) home_cols_.assign(shelves.size(), 0);
    if (home_zone_ == 0) {
        const int n = static_cast<int>(hero.size());
        if (n && (k & key::left)) { hero_ = (hero_ + n - 1) % n; hero_at_ = now_; }
        if (n && (k & key::right)) { hero_ = (hero_ + 1) % n; hero_at_ = now_; }
        if ((k & key::down) && !shelves.empty()) home_zone_ = 1;
        if ((k & key::cross) && hero_ < n) open(hero[static_cast<std::size_t>(hero_)]);
        if (k & key::triangle) { tab_ = library; lib_zone_ = 1; input_library(key::triangle); }  // la loupe
        return;
    }
    const int row = home_zone_ - 1;
    if (row >= static_cast<int>(shelves.size())) { home_zone_ = 0; return; }
    int& col = home_cols_[static_cast<std::size_t>(row)];
    const int n = static_cast<int>(shelves[static_cast<std::size_t>(row)].items.size());
    if (k & key::left) col = std::max(0, col - 1);
    if (k & key::right) col = std::min(std::max(0, n - 1), col + 1);
    if (k & key::up) home_zone_ -= 1;
    if ((k & key::down) && home_zone_ < static_cast<int>(shelves.size())) home_zone_ += 1;
    if (k & key::circle) home_zone_ = 0;
    if ((k & key::cross) && col < n) {
        if (shelves[static_cast<std::size_t>(row)].todo) { const auto cards = home_cards(); if (col < static_cast<int>(cards.size())) run_home_card(cards[static_cast<std::size_t>(col)].action); }
        else if (shelves[static_cast<std::size_t>(row)].peer_first >= 0) {
            const std::size_t idx = static_cast<std::size_t>(shelves[static_cast<std::size_t>(row)].peer_first + col);
            if (idx < home_peer_refs_.size()) {
                std::string pname;
                for (const auto& p : peer_games_.items()) if (p["instance"].as_string() == home_peer_refs_[idx].first) pname = p["name"].as_string();
                Json g = home_peer_refs_[idx].second;
                g.set("here", "missing");  // la rangée « Sur … » ne montre que les jeux absents d'ici
                open_remote_sheet(home_peer_refs_[idx].first, pname, g, true);
            }
        }
        else if (shelves[static_cast<std::size_t>(row)].bk_first >= 0 && shelves[static_cast<std::size_t>(row)].bk_first + col < static_cast<int>(home_bk_games_.size())) {
            const Json g = home_bk_games_[static_cast<std::size_t>(shelves[static_cast<std::size_t>(row)].bk_first + col)];
            open_versions(g["account"].as_string(), g["title_id"].as_string(), g["title"].as_string(), g["item"].as_string());
        }
        else open(shelves[static_cast<std::size_t>(row)].items[static_cast<std::size_t>(col)]);
    }
    if ((k & key::options) && shelves[static_cast<std::size_t>(row)].peer_first >= 0 && col < n) {  // « Sur … » : la fiche distante
        const std::size_t idx = static_cast<std::size_t>(shelves[static_cast<std::size_t>(row)].peer_first + col);
        if (idx < home_peer_refs_.size()) {
            std::string pname;
            for (const auto& p : peer_games_.items()) if (p["instance"].as_string() == home_peer_refs_[idx].first) pname = p["name"].as_string();
            Json g = home_peer_refs_[idx].second;
            g.set("here", "missing");
            open_remote_sheet(home_peer_refs_[idx].first, pname, g, true);
        }
    }
    if (k & key::triangle) tab_ = library;  // « Tout voir »
    // Options : les mêmes actions que dans la Bibliothèque, sur l'élément sous le curseur (ex. écarter une
    // sauvegarde sans le jeu qui n'intéresse plus).
    if ((k & key::options) && !shelves[static_cast<std::size_t>(row)].todo && shelves[static_cast<std::size_t>(row)].bk_first < 0 && shelves[static_cast<std::size_t>(row)].peer_first < 0 && col < n) {
        const Item* focus = shelves[static_cast<std::size_t>(row)].items[static_cast<std::size_t>(col)];
        const std::string key2 = focus->key, tid = focus->title_id, title2 = focus->title;
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        acts.push_back({tr("Ouvrir la fiche"), [this, key2] { sheet_ = true; sheet_key_ = key2; sheet_focus_ = 0; unchecked_.clear(); dest_ = 0; }});
        acts.push_back({tr("Toutes les versions"), [this, key2, tid, title2] { open_versions("", tid, title2, key2); }});
        acts.push_back({tr("Ignorer…"), [this, key2, tid, title2] { open_ignore({{tid.empty() ? "key" : "title_id", tid.empty() ? key2 : tid, title2}}, -1); }});
        if (has_console_save(tid)) acts.push_back({tr("Supprimer la partie de la console…"), [this, tid, title2] { delete_console_save(tid, title2); }});
        open_actions(title2, std::move(acts));
    }
}

// Plans de synchro des copies, une fois par minute (ou tout de suite après un changement : sync_plans_at_ = 0).
void Ui::load_sync_plans() {
    bool has_nas = false;
    for (const auto& pl : snap_->places) has_nas = has_nas || pl.type == "ftp";
    bool failed = false;  // une section n'a pas eu de réponse : nouvel essai au bout de 20 s au lieu d'une minute
    for (const auto* v : {&parties_plans_, &apps_plans_, &dlc_plans_, &emu_plans_}) for (const auto& [n, pl] : *v) failed = failed || pl.has("error");
    if ((peer_games_.size() || has_nas) && (!sync_plans_at_ || now_ - sync_plans_at_ > (failed ? 10000u : 15000u)  /* le moteur prépare les plans d'avance : relire souvent ne coûte rien */)) {  // PS5 jumelées et NAS
        sync_plans_at_ = now_ ? now_ : 1;
        for (const auto& pl : snap_->places) {
            if (pl.type != "ftp" || pl.use == "backups" || !pl.same_as.empty()) continue;  // un NAS qui sert à la synchro (un dossier une fois) : la synchro des sauvegardes entre cette console et lui
            const std::string id = pl.id, name = sy_nas_name(pl);
            model_.command("GET", "/api/v2/sync/copies/plan?peer=" + id + "&full=1", "", [this, name](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || j.has("error")) return;
                post_ui([this, name, j] {
                    for (auto& [n, pl2] : sync_plans_) if (pl2["peer"].as_string() == j["peer"].as_string()) { n = name; pl2 = j; return; }
                    sync_plans_.push_back({name, j});
                });
            });
            // Le NAS comme dépôt : parties actives et applis (mêmes écrans qu'avec une autre PS5)
            for (int sec = 0; sec < 2; ++sec)
                model_.command("GET", std::string(sec ? "/api/v2/sync/apps/plan?peer=" : "/api/v2/sync/parties/plan?peer=") + id, "", [this, name, id, sec](const std::string& e, const std::string& body) {
                    Json j;
                    if (!e.empty() || !Json::parse(body, j) || !j.is_object()) { j = Json::object(); j.set("peer", id); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e); }
                    post_ui([this, name, j, sec] {
                        auto& v = sec ? apps_plans_ : parties_plans_;
                        for (auto& [n, pl2] : v) if (pl2["peer"].as_string() == j["peer"].as_string()) { n = name; pl2 = j; return; }
                        v.push_back({name, j});
                    });
                });
        }
        for (const auto& p : peer_games_.items()) {
            const std::string inst = p["instance"].as_string(), name = p["name"].as_string();
            model_.command("GET", "/api/v2/sync/copies/plan?peer=" + inst + "&full=1", "", [this, name](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || j.has("error")) return;
                post_ui([this, name, j] {
                    for (auto& [n, pl] : sync_plans_) if (pl["peer"].as_string() == j["peer"].as_string()) { n = name; pl = j; return; }
                    sync_plans_.push_back({name, j});
                });
            });
            model_.command("GET", "/api/v2/sync/parties/plan?peer=" + inst, "", [this, name, inst](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || !j.is_object()) {  // pas de réponse : on le dit (et on réessaie vite), jamais « lecture… » sans fin
                    Json d = Json::object();  // diagnostic : le moteur garde la raison (status › tv_app › recent)
                    d.set("what", "parties"); d.set("error", e.empty() ? "parse len=" + std::to_string(body.size()) + " head=" + body.substr(0, 60) : e);
                    model_.command("POST", "/api/v2/diag/tv", d.dump(), nullptr);
                    j = Json::object(); j.set("peer", inst); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e);
                }
                post_ui([this, name, j] {
                    for (auto& [n, pl] : parties_plans_) if (pl["peer"].as_string() == j["peer"].as_string()) { n = name; pl = j; return; }
                    parties_plans_.push_back({name, j});
                });
            });
            model_.command("GET", "/api/v2/sync/apps/plan?peer=" + inst, "", [this, name, inst](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || !j.is_object()) {  // pas de réponse : on le dit (et on réessaie vite), jamais « lecture… » sans fin
                    Json d = Json::object();  // diagnostic : le moteur garde la raison (status › tv_app › recent)
                    d.set("what", "apps"); d.set("error", e.empty() ? "parse len=" + std::to_string(body.size()) + " head=" + body.substr(0, 60) : e);
                    model_.command("POST", "/api/v2/diag/tv", d.dump(), nullptr);
                    j = Json::object(); j.set("peer", inst); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e);
                }
                post_ui([this, name, j] {
                    for (auto& [n, pl] : apps_plans_) if (pl["peer"].as_string() == j["peer"].as_string()) { n = name; pl = j; return; }
                    apps_plans_.push_back({name, j});
                });
            });
            model_.command("GET", "/api/v2/sync/emu/plan?peer=" + inst, "", [this, name, inst](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || !j.is_object()) { j = Json::object(); j.set("peer", inst); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e); }
                post_ui([this, name, j] {
                    for (auto& [n, pl] : emu_plans_) if (pl["peer"].as_string() == j["peer"].as_string()) { n = name; pl = j; return; }
                    emu_plans_.push_back({name, j});
                });
            });
            model_.command("GET", "/api/v2/sync/dlc/plan?peer=" + inst, "", [this, name, inst](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j) || !j.is_object()) {  // pas de réponse : on le dit (et on réessaie vite), jamais « lecture… » sans fin
                    Json d = Json::object();  // diagnostic : le moteur garde la raison (status › tv_app › recent)
                    d.set("what", "dlc"); d.set("error", e.empty() ? "parse len=" + std::to_string(body.size()) + " head=" + body.substr(0, 60) : e);
                    model_.command("POST", "/api/v2/diag/tv", d.dump(), nullptr);
                    j = Json::object(); j.set("peer", inst); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e);
                }
                post_ui([this, name, j] {
                    for (auto& [n, pl] : dlc_plans_) if (pl["peer"].as_string() == j["peer"].as_string()) { n = name; pl = j; return; }
                    dlc_plans_.push_back({name, j});
                });
            });
        }
    }
    const bool prun = parties_status_["state"].as_string() == "running";
    if (!parties_status_at_ || now_ - parties_status_at_ > (prun ? 2000u : 30000u)) {
        parties_status_at_ = now_ ? now_ : 1;
        model_.command("GET", "/api/v2/sync/parties/status", "", [this, prun](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) return;
            post_ui([this, j, prun] {
                if (prun && j["state"].as_string() != "running") { sync_plans_at_ = 0; overview_loaded_ = 0; }  // fini : tout relire
                parties_status_ = j;
            });
        });
    }
}

void Ui::draw_home(Surface& s) {
    if (!overview_loaded_ || now_ - overview_loaded_ > 15000) load_overview();
    if (overview_.is_object() && !mg_sum_at_) { mg_sum_account_ = mg_account(); mg_load_summary(); }  // cartes « À faire »
    // Synchro des copies : avancement (toutes les 2 s pendant qu'elle tourne), état par PS5 jumelée (toutes les 60 s).
    const bool sync_running = sync_copies_status_["state"].as_string() == "running";
    if (!sync_copies_at_ || now_ - sync_copies_at_ > (sync_running ? 2000u : 20000u)) {
        sync_copies_at_ = now_ ? now_ : 1;
        model_.command("GET", "/api/v2/sync/copies/status", "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) return;
            post_ui([this, j, was = sync_copies_status_["state"].as_string()] {
                sync_copies_status_ = j;
                if (was == "running" && j["state"].as_string() != "running") { sync_plans_at_ = 0; mg_sum_dirty_ = true; mg_sum_at_ = 0; }  // fini : tout relire
            });
        });
    }
    load_sync_plans();
    if (!peer_games_at_ || now_ - peer_games_at_ > 30000) {  // jeux des autres PS5 (rangées « Sur … »)
        peer_games_at_ = now_ ? now_ : 1;
        model_.command("GET", "/api/v2/link/peer-games", "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j) || !j.is_array()) return;
            std::lock_guard<std::mutex> l(async_mu_);
            pending_peer_games_ = std::move(j);
            has_pending_peer_games_ = true;
        });
    }
    const auto hero = hero_items();
    const auto shelves = home_shelves();
    if (home_cols_.size() != shelves.size()) home_cols_.assign(shelves.size(), 0);
    if (hero.empty()) {
        draw_header(s);
        s.text(font_, margin, 300, tr("Votre collection apparaît ici dès le premier relevé."), large, color::muted);
        draw_footer(s, {{-1, tr("L1 / R1  onglets")}});
        return;
    }
    hero_ = std::min(hero_, static_cast<int>(hero.size()) - 1);
    // Défilement automatique toutes les 8 s tant qu'on reste sur le carrousel.
    if (!hero_at_) hero_at_ = now_;
    if (home_zone_ == 0 && !sheet_ && now_ - hero_at_ > 8000) { hero_ = (hero_ + 1) % static_cast<int>(hero.size()); hero_at_ = now_; }
    // La page défile : sur une rangée, celle-ci remonte sous l'en-tête et le carrousel s'efface.
    constexpr int shelf_top = 548, shelf_h = 330;
    const int target = home_zone_ == 0 ? 0 : shelf_top - 150 + (home_zone_ - 1) * shelf_h;
    if (home_scroll_ != target) home_scroll_ += (target - home_scroll_) / 3 + (target > home_scroll_ ? 1 : -1);
    if (std::abs(home_scroll_ - target) < 3) home_scroll_ = target;
    animating_ = home_scroll_ != target;
    const int dy = -home_scroll_;
    // L'élément mis en avant : la jaquette du carrousel, ou celle de l'élément choisi dans une rangée.
    const Item* featured = hero[static_cast<std::size_t>(hero_)];
    if (home_zone_ > 0 && home_zone_ - 1 < static_cast<int>(shelves.size())) {
        const auto& sh = shelves[static_cast<std::size_t>(home_zone_ - 1)];
        const int c = std::min(home_cols_[static_cast<std::size_t>(home_zone_ - 1)], static_cast<int>(sh.items.size()) - 1);
        if (c >= 0 && !sh.todo) featured = sh.items[static_cast<std::size_t>(c)];
    }
    const Item& it = *hero[static_cast<std::size_t>(hero_)];
    {
        const Image big = covers_.get_large(cover_key(*featured));
        const Image img = big.valid() ? big : featured->has_cover ? covers_.get(cover_key(*featured)) : Image{};
        if (img.valid()) {
            s.image_soft(img, 820, -160, 1240, 1240, home_zone_ == 0 ? 150 : 90);
            s.fade(820, 0, 760, 1080, color::bg, true, true);
            s.fade(0, 560, 1920, 520, color::bg, false, false);
            s.rect(0, 0, 820, 1080, color::bg, 0);
            s.fade(0, 0, 1920, 120, rgb(6, 16, 29, 200), false, true);
        }
        // Jaquette nette du carrousel à droite, avec une ombre : de la profondeur.
        const Image hb = covers_.get_large(cover_key(it));
        const Image himg = hb.valid() ? hb : covers_.get(cover_key(it));
        if (himg.valid() && 170 + dy + 380 > 0) {
            s.rect(1400 + 10, 170 + 16 + dy, 380, 380, rgb(0, 0, 0, 110), 26);
            s.image(himg, 1400, 170 + dy, 380, 380, 24);
            s.outline(1400, 170 + dy, 380, 380, rgb(255, 255, 255, 40), 24, 2);
        }
    }
    s.push_clip(0, 110, 1920, 880);
    if (dy > -560) {
    // Pourquoi il est mis en avant.
    std::string why = tr("DANS VOTRE COLLECTION");
    std::uint32_t tone = color::accent;
    for (const auto& e : overview_["entries"].items())
        if (e["item"].as_string() == it.key) {
            const std::string st = e["status"].as_string();
            if (st == "console_newer") { why = tr("À SAUVEGARDER · JOUÉ DEPUIS LA DERNIÈRE SAUVEGARDE"); tone = color::warn; }
            else if (st == "not_backed_up") { why = tr("À SAUVEGARDER · JAMAIS SAUVEGARDÉ"); tone = color::warn; }
            else if (st == "up_to_date") { why = tr("SAUVEGARDE À JOUR"); tone = color::ok; }
            break;
        }
    const int bw = Surface::measure(why, small) + 28;
    s.rect(margin, 196 + dy, bw, 34, (tone & 0x00FFFFFFu) | 0x26000000u, 17);
    s.outline(margin, 196 + dy, bw, 34, (tone & 0x00FFFFFFu) | 0x60000000u, 17, 1);
    s.text(font_, margin + 14, 201 + dy, why, small, tone);
    // Titre sur deux lignes au plus.
    {
        std::string t;
        int depth = 0;
        for (char c : it.title) { if (c == '(' || c == '[') ++depth; else if ((c == ')' || c == ']') && depth) --depth; else if (!depth) t += c; }
        while (!t.empty() && t.back() == ' ') t.pop_back();
        std::vector<std::string> lines;
        std::string line, word;
        auto push = [&] {
            if (word.empty()) return;
            const std::string tryl = line.empty() ? word : line + " " + word;
            if (!line.empty() && Surface::measure(tryl, title) > 1100) { lines.push_back(line); line = word; } else line = tryl;
            word.clear();
        };
        for (char c : t) { if (c == ' ') push(); else word += c; }
        push();
        if (!line.empty()) lines.push_back(line);
        int ty = 246 + dy;
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i, ty += 66) s.text(font_, margin, ty, lines[i], title, color::text, 1200);
        // Pastilles : plateforme, emplacement.
        int cx = margin;
        const std::string plat = it.kind == "rom" ? it.platform : it.kind == "app" ? std::string(tr("Application")) : it.kind == "game_ps4" ? std::string("PS4") : std::string("PS5");
        std::string place = it.place;
        for (const auto& p : snap_->places) if (p.id == it.place) place = p.label;
        std::string last;
        for (const auto& e : overview_["entries"].items())
            if (e["item"].as_string() == it.key) {
                std::int64_t best = 0;
                for (const auto& b : e["backups"].items())
                    if (b["created_unix"].as_int() > best) {
                        best = b["created_unix"].as_int();
                        const std::time_t t = static_cast<std::time_t>(best);
                        std::tm tm{};
                        gmtime_r(&t, &tm);
                        char d[16];
                        std::strftime(d, sizeof d, "%d/%m/%Y", &tm);
                        last = trf("Dernière sauvegarde : %s · %s", d, b["place_label"].as_string().c_str());
                    }
            }
        for (const std::string& chip : {plat, place, last}) {
            if (chip.empty()) continue;
            const int cw = Surface::measure(chip, small) + 24;
            s.rect(cx, ty + 12, cw, 34, rgb(255, 255, 255, 22), 10);
            s.text(font_, cx + 12, ty + 17, chip, small, rgb(185, 203, 224));
            cx += cw + 10;
        }
        // Boutons.
        const int by = ty + 76;
        const std::string b1 = it.kind == "app" ? tr("Sauvegarder l'application…") : it.kind == "rom" ? tr("Sauvegarder la ROM…") : tr("Sauvegarder le jeu…");
        const int w1 = Surface::measure(b1, body) + 86;
        const bool f = home_zone_ == 0;
        s.rect(margin, by, w1, 60, f ? rgb(124, 196, 255) : rgb(255, 255, 255, 30), 30);
        icon(s, 0, margin + 17, by + 17, color::accent);  // pastille de 34 px centrée dans le bouton de 60 px
        s.text(font_, margin + 64, by + 15, b1, body, f ? rgb(4, 16, 31) : color::text);
        // Indicateur du carrousel : barres.
        int ix = margin;
        for (int i = 0; i < static_cast<int>(hero.size()); ++i) {
            const int iw = i == hero_ ? 48 : 16;
            s.rect(ix, by + 92, iw, 6, i == hero_ ? color::accent : rgb(255, 255, 255, 60), 3);
            ix += iw + 8;
        }
    }
    }
    // Rangées.
    const int cs = 200, gap = 24;
    for (int r = 0; r < static_cast<int>(shelves.size()); ++r) {
        const int ry = shelf_top + r * shelf_h + dy;
        if (ry > 1000 || ry + shelf_h < 100) continue;
        const auto& sh = shelves[static_cast<std::size_t>(r)];
        const bool row_on = home_zone_ == r + 1;
        const int tw = s.text(font_, margin, ry, sh.title, large, row_on ? color::text : rgb(200, 214, 230));
        const int nw = s.text(font_, margin + tw + 16, ry + 8, std::to_string(sh.items.size()), body, color::muted);
        if (!sh.away.empty()) {  // pas sur cette console : on le dit
            const int px = margin + tw + 16 + nw + 20, pw = Surface::measure(sh.away, small) + 28;
            s.rect(px, ry + 8, pw, 34, color::accent2, 17);
            s.text(font_, px + 14, ry + 11, sh.away, small, color::text);
        }
        const int col = home_cols_[static_cast<std::size_t>(r)];
        if (sh.todo) {  // « À faire » : cartes illustrées
            const auto cards = home_cards();
            constexpr int kw = 560, kh = 200, kgap = 24;  // trois cartes à l'écran
            const int kfirst = std::max(0, col - 2);
            for (int i = kfirst; i < static_cast<int>(cards.size()) && i < kfirst + 3; ++i) {
                const HomeCard& c = cards[static_cast<std::size_t>(i)];
                const int kx = margin + (i - kfirst) * (kw + kgap), ky = ry + 52;
                const bool focus = row_on && i == col;
                if (focus) for (int g = 3; g >= 1; --g) s.rect(kx - g * 5, ky - g * 5, kw + g * 10, kh + g * 10, rgb(80, 150, 255, 22), 22 + g * 5);
                s.rect(kx, ky, kw, kh, color::panel, 22);
                s.fade(kx, ky, kw, kh, (c.tone & 0x00FFFFFFu) | 0x30000000u, true, true);
                s.rect(kx, ky + 20, 8, kh - 40, c.tone, 4);
                s.outline(kx, ky, kw, kh, focus ? color::accent : color::line, 22, focus ? 4 : 2);
                int tx = kx + 36;
                std::string sub = c.sub;
                if (!c.games.empty()) {  // la jaquette et le nom défilent : un jeu toutes les 3 s
                    const std::size_t n = c.games.size(), at = static_cast<std::size_t>(now_ / 3000) % n;
                    const auto& [key, title] = c.games[at];
                    Item stand_in;
                    const int dev = key == "dev:ps5" ? dev_internal : key == "dev:switch" ? dev_switch : key == "dev:nas" ? dev_nas : -1;  // bulle d'un appareil : son image
                    if (dev >= 0 && devices_[dev].valid()) {
                        s.image(devices_[dev], kx + 30, ky + 28, kh - 56, kh - 56);  // sans fond : l'image seule
                    } else if (key == "pbc:logo" && logo_.valid()) {  // mise à jour de Backup Center : notre logo
                        s.image(logo_, kx + 30, ky + 28, kh - 56, kh - 56, 0);
                    } else {
                        const Item* found = find_or_stand_in(key, title, "", stand_in);
                        cover(s, *found, kx + 30, ky + 28, kh - 56, false);
                    }
                    tx = kx + 30 + (kh - 56) + 24;
                    sub = clean_title(title);
                    if (at < c.progress.size()) {  // tâche : barre d'avancement (ou « en attente »)
                        const int pm = c.progress[at], bw = kx + kw - 24 - tx;
                        s.rect(tx, ky + kh - 52, bw, 8, rgb(255, 255, 255, 30), 4);
                        if (pm >= 0) {
                            s.rect(tx, ky + kh - 52, std::max(8, bw * pm / 1000), 8, c.tone, 4);
                            char pc[16]; std::snprintf(pc, sizeof pc, "%d %%", pm / 10);
                            const std::string eta = at < c.eta.size() ? c.eta[at] : std::string();
                            if (!eta.empty() && eta[0] == '\t') sub = eta.substr(1) + " · " + sub;  // étape à part (vérification…)
                            else sub = std::string(pc) + (!eta.empty() ? " · " + std::string(tr("reste")) + " " + eta : std::string()) + " · " + sub;
                        } else sub = std::string(tr("En attente")) + " · " + sub;
                    } else if (n > 1) {  // où on en est dans la liste : petits points
                        const int dots = static_cast<int>(std::min<std::size_t>(n, 8));
                        for (int d = 0; d < dots; ++d)
                            s.rect(tx + d * 18, ky + kh - 40, d == static_cast<int>(at % 8) ? 14 : 8, 8, d == static_cast<int>(at % 8) ? c.tone : rgb(255, 255, 255, 40), 4);
                    }
                }
                const int tw = kx + kw - 24 - tx;
                if (!c.eyebrow.empty()) s.text(font_, tx, ky + 30, c.eyebrow, small, c.tone, tw);
                s.text(font_, tx, ky + (c.eyebrow.empty() ? 40 : 62), c.title, large, c.eyebrow.empty() ? color::text : c.tone, tw);  // titre dans la couleur de la bulle, comme le web
                s.text(font_, tx, ky + (c.eyebrow.empty() ? 102 : 118), sub, small, color::muted, tw);
            }
            if (static_cast<int>(cards.size()) > kfirst + 3) s.text(font_, 1920 - margin + 20, ry + 52 + kh / 2 - 16, "›", large, color::muted);
            continue;
        }
        const int first = std::max(0, col - 6);
        for (int i = first; i < static_cast<int>(sh.items.size()) && i < first + 8; ++i) {
            const int cx = margin + (i - first) * (cs + gap), cy = ry + 52;
            const bool focus = row_on && i == col;
            if (focus) for (int g = 3; g >= 1; --g) s.rect(cx - g * 5, cy - g * 5, cs + g * 10, cs + g * 10, rgb(80, 150, 255, 22), 22 + g * 5);
            cover(s, *sh.items[static_cast<std::size_t>(i)], cx, cy, cs, true);
            if (focus) s.outline(cx - 5, cy - 5, cs + 10, cs + 10, color::accent, 22, 4);
            s.text(font_, cx, cy + cs + 10, clean_title(sh.items[static_cast<std::size_t>(i)]->title), small, focus ? color::text : color::muted, cs);
        }
        if (static_cast<int>(sh.items.size()) > first + 8) s.text(font_, 1920 - margin - 30, ry + 52 + cs / 2 - 16, "›", large, color::muted);
    }
    s.pop_clip();
    if (home_scroll_ > 0) s.fade(0, 100, 1920, 60, color::bg, false, true);  // les rangées passent sous l'en-tête en fondu
    draw_header(s);
    if (sheet_) {}  // la fiche dessine ses propres aides
    else if (home_zone_ == 0) draw_footer(s, {{0, tr("Sauvegarder…")}, {3, tr("Rechercher")}, {-1, tr("Gauche / droite : défiler")}, {-1, tr("Bas : vos rangées")}});
    else draw_footer(s, {{0, tr("Ouvrir la fiche")}, {-1, tr("Options : actions")}, {-1, tr("Haut / bas : rangées")}, {3, tr("Tout voir")}, {1, tr("À la une")}});
}

void Ui::draw_library(Surface& s) {
    const auto items = visible_items();
    if (!link_peers_at_) load_link_peers();  // pour la pastille « Console » (une fois ; Appareils la tient ensuite à jour)
    char sub[160];
    if (lib_peer_.empty()) std::snprintf(sub, sizeof sub, tr("BIBLIOTHÈQUE  ·  %zu SUR %zu"), items.size(), snap_->items.size());
    else {
        const std::int64_t age = lib_peer_read_at_ ? std::max<std::int64_t>(0, static_cast<std::int64_t>(std::time(nullptr)) - lib_peer_read_at_) : -1;
        const std::string when = remote_loading_ ? std::string(tr("lecture…")) : age < 0 ? std::string() : age < 90 ? trf("lue il y a %d s", static_cast<int>(age)) : trf("lue il y a %d min", static_cast<int>(age / 60));
        std::snprintf(sub, sizeof sub, "%s  ·  %zu / %zu  ·  %s", trf("%s · À DISTANCE", lib_peer_name_.c_str()).c_str(), items.size(), remote_items_.size(),
                      lib_peer_online_ ? when.c_str() : tr("hors ligne : dernière liste connue"));
    }
    // Bandeau : la jaquette de l'élément choisi en grand, floutée, derrière l'en-tête (comme l'Accueil).
    const auto shelves = library_shelves();
    {  // filtres ou recherche changés : on repart de la première rangée
        const std::string sig = std::to_string(kind_) + "|" + std::to_string(app_) + "|" + std::to_string(place_) + "|" + search_ + "|" + std::to_string(shelves.size());
        if (sig != lib_sig_) { lib_sig_ = sig; lib_row_ = 0; lib_cols_.assign(shelves.size(), 0); }
        lib_cols_.resize(shelves.size(), 0);
        lib_row_ = std::min(lib_row_, std::max(0, static_cast<int>(shelves.size()) - 1));
    }
    const Item* focus_item = nullptr;
    if (!shelves.empty()) {
        const auto& sh = shelves[static_cast<std::size_t>(lib_row_)];
        focus_item = sh.items[static_cast<std::size_t>(std::min(lib_cols_[static_cast<std::size_t>(lib_row_)], static_cast<int>(sh.items.size()) - 1))];
    }
    if (focus_item) {
        const Image big = focus_item->has_cover ? covers_.get_large(cover_key(*focus_item)) : Image{};
        const Image img = big.valid() ? big : focus_item->has_cover ? covers_.get(cover_key(*focus_item)) : Image{};
        if (img.valid()) {
            s.image_soft(img, 760, -300, 1300, 1300, 130);
            s.fade(760, 0, 700, 560, color::bg, true, true);
            s.fade(0, 170, 1920, 330, color::bg, false, false);
            s.rect(0, 500, 1920, 580, color::bg, 0);
            s.fade(0, 0, 1920, 110, rgb(6, 16, 29, 190), false, true);
        }
    }
    draw_header(s);
    const int sw = s.text(font_, margin, 150, sub, small, color::accent);
    if (!search_.empty()) {  // recherche en cours : rappelée à côté du compteur
        const std::string q = trf("RECHERCHE : « %s »  ·  Triangle pour changer", search_.c_str());
        s.text(font_, margin + sw + 24, 150, q, small, color::warn);
    }
    std::string big = focus_item ? focus_item->title : std::string(tr("Bibliothèque"));
    {  // sans les parenthèses de région : « Banjo-Kazooie (Europe) (En,Fr,De) » → « Banjo-Kazooie »
        std::string t;
        int depth = 0;
        for (char c : big) { if (c == '(' || c == '[') ++depth; else if ((c == ')' || c == ']') && depth) --depth; else if (!depth) t += c; }
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (!t.empty()) big = t;
    }
    s.text(font_, margin, 192, big, title, color::text, 1400);
    if (!lib_peer_.empty()) {  // rappel permanent : on regarde l'autre console, les actions se font là-bas
        const std::string b = trf("Vous regardez %s : les actions se font là-bas", lib_peer_name_.c_str());
        const int bw = Surface::measure(b, small) + 40;
        s.rect(1920 - margin - bw, 146, bw, 38, rgb(90, 169, 255, 46), 19);
        s.outline(1920 - margin - bw, 146, bw, 38, rgb(90, 169, 255, 110), 19, 1);
        s.text(font_, 1920 - margin - bw + 20, 150, b, small, color::text);
    }
    // Filtres.
    const auto apps = app_filters();
    {   // éléments marqués (L2) disparus : oubliés
        for (auto m = lib_marks_.begin(); m != lib_marks_.end();) {
            bool found = false;
            for (const auto& it : snap_->items) if (it.key == *m) { found = true; break; }
            m = found ? std::next(m) : lib_marks_.erase(m);
        }
    }
    static const char* const presence[3] = {N_("Tous ses jeux"), N_("Absents d'ici"), N_("Aussi ici")};
    std::string chips[kind_count + 4] = {tr("Tout"), "PS5", "PS4", tr("Applications"), "ROM", tr("Ignorés"),
                                         // sur « tous », le nom seul : toute la rangée tient à l'écran
                                         app_ ? trf("Émulateur : %s", apps[static_cast<std::size_t>(app_ - 1)].c_str()) : std::string(tr("Émulateur")),
                                         !lib_peer_.empty() ? std::string(tr(presence[lib_presence_]))
                                         : place_ ? trf("Emplacement : %s", snap_->places[static_cast<std::size_t>(place_ - 1)].label.c_str()) : std::string(tr("Emplacement")),
                                         tr("Actualiser"),
                                         [&] {  // on voit qu'il y a d'autres consoles sans avoir à cliquer
                                             int others = 0;
                                             if (link_peers_.is_array()) for (const auto& p : link_peers_.items()) others += p["paired"].as_bool();
                                             if (!lib_peer_.empty()) others = std::max(0, others - 1) + 1;  // l'autre côté : cette PS5 compte
                                             std::string c = trf("Console : %s", lib_peer_.empty() ? tr("cette PS5") : lib_peer_name_.c_str());
                                             if (others) c += "  " + (others == 1 ? std::string(tr("· 1 autre ›")) : trf("· %d autres ›", others));
                                             return c;
                                         }()};
    // Pastilles resserrées si la rangée dépasse (nom de console long) ; en dernier recours elle glisse.
    int pad = 36;
    {
        int total = 34;
        for (int i = 0; i < kind_count + 4; ++i) total += Surface::measure(chips[i], body) + pad + (i >= kind_count ? 16 : 6);
        const int over = total - (1920 - 2 * margin);
        if (over > 0) pad = std::max(16, pad - (over + kind_count + 3) / (kind_count + 4));
    }
    int shift = 0;
    {
        int xx = margin, right = 0;
        for (int i = 0; i < kind_count + 4; ++i) {
            if (i == kind_count) xx += 34;
            const int w = Surface::measure(chips[i], body) + pad;
            if (i == lib_filter_) right = xx + w;
            xx += w + (i >= kind_count ? 16 : 6);
        }
        if (lib_zone_ == 0 && right > 1920 - margin) shift = right - (1920 - margin);
    }
    int x = margin - shift;
    const int y = 300;
    // Les types forment un groupe (une pastille de fond) ; les autres réglages sont des bulles séparées.
    {
        int gw = 12;
        for (int i = 0; i < kind_count; ++i) gw += Surface::measure(chips[i], body) + pad + 6;
        s.rect(x - 6, y - 6, gw, 56, rgb(255, 255, 255, 16), 28);
    }
    for (int i = 0; i < kind_count + 4; ++i) {
        const int w = Surface::measure(chips[i], body) + pad;
        const bool selected = (i < kind_count && kind_ == i) || (i == kind_count + 3 && !lib_peer_.empty());
        const bool focused = lib_zone_ == 0 && lib_filter_ == i;
        if (i == kind_count) x += 34;  // de l'air entre le groupe des types et les réglages
        if (selected) s.rect(x, y, w, 44, color::text, 22);
        else if (i >= kind_count) { s.rect(x, y, w, 44, rgb(255, 255, 255, 16), 22); s.outline(x, y, w, 44, rgb(255, 255, 255, 30), 22, 1); }
        if (focused) s.outline(x - 4, y - 4, w + 8, 52, color::accent, 26, 3);
        s.text(font_, x + pad / 2, y + 6, chips[i], body, selected ? color::bg : i >= kind_count ? color::text : color::muted);
        x += w + (i >= kind_count ? 16 : 6);
    }
    // Rangées, aérées comme l'Accueil ; la rangée choisie remonte sous les filtres.
    constexpr int top = 392, shelf_h = 330, cs = 200, gap = 24;
    const int target = lib_row_ * shelf_h;
    if (lib_scroll_ != target) lib_scroll_ += (target - lib_scroll_) / 3 + (target > lib_scroll_ ? 1 : -1);
    if (std::abs(lib_scroll_ - target) < 3) lib_scroll_ = target;
    animating_ = lib_scroll_ != target;
    s.push_clip(0, top - 16, 1920, 990 - top + 16);
    for (int r = 0; r < static_cast<int>(shelves.size()); ++r) {
        const int ry = top + r * shelf_h - lib_scroll_;
        if (ry > 990 || ry + shelf_h < top - 40) continue;
        const auto& sh = shelves[static_cast<std::size_t>(r)];
        const bool row_on = lib_zone_ == 1 && lib_row_ == r;
        if (!sh.title.empty()) {
            const int tw = s.text(font_, margin, ry, sh.title, large, row_on ? color::text : rgb(200, 214, 230));
            s.text(font_, margin + tw + 16, ry + 8, std::to_string(sh.total ? sh.total : sh.items.size()), body, color::muted);
        }
        const int col = lib_cols_[static_cast<std::size_t>(r)];
        const int first = std::max(0, col - 6);
        for (int i = first; i < static_cast<int>(sh.items.size()) && i < first + 8; ++i) {
            const int cx = margin + (i - first) * (cs + gap), cy = ry + 50;
            const bool focus = row_on && i == col;
            if (focus) for (int g = 3; g >= 1; --g) s.rect(cx - g * 5, cy - g * 5, cs + g * 10, cs + g * 10, rgb(80, 150, 255, 22), 22 + g * 5);
            const Item& ci = *sh.items[static_cast<std::size_t>(i)];
            cover(s, ci, cx, cy, cs, true);
            if (ci.ignored) {  // ignoré : voilé, petite pastille
                s.rect(cx, cy, cs, cs, rgb(6, 16, 29, 150), 16);
                const char* t = tr("Ignoré");
                const int tw2 = Surface::measure(t, small) + 20;
                s.rect(cx + 8, cy + cs - 40, tw2, 32, rgb(0, 0, 0, 190), 16);
                s.text(font_, cx + 18, cy + cs - 37, t, small, color::warn);
            }
            if (const Json* m = lib_peer_.empty() ? nullptr : remote_meta_of(&ci)) {  // autre console : aussi ici ? partie ?
                const std::string h = (*m)["here"].as_string(), sv = (*m)["save"].as_string();
                const char* t = sv == "there" ? tr("À jour là-bas") : sv == "here" ? tr("À jour ici") : h == "missing" ? tr("Manque ici") : h == "both" ? tr("Aussi ici") : nullptr;
                if (t) {
                    const std::uint32_t bg = sv == "there" || sv == "here" ? color::accent : h == "missing" ? color::warn : rgb(0, 0, 0, 170);
                    const int tw2 = std::min(cs - 16, Surface::measure(t, small) + 20);
                    s.rect(cx + 8, cy + 8, tw2, 32, bg, 16);
                    s.text(font_, cx + 18, cy + 11, t, small, h == "both" && sv.empty() ? color::ok : color::bg, tw2 - 20);
                }
            }
            if (lib_marks_.count(ci.key)) {  // marqué (L2) : coche en haut à droite
                s.rect(cx + cs - 46, cy + 8, 38, 38, color::accent2, 19);
                for (int d = 0; d < 7; ++d) s.rect(cx + cs - 37 + d, cy + 26 + d / 2, 4, 4, color::text, 2);
                for (int d = 0; d < 10; ++d) s.rect(cx + cs - 31 + d, cy + 30 - d, 4, 4, color::text, 2);
            }
            if (focus) s.outline(cx - 5, cy - 5, cs + 10, cs + 10, color::accent, 22, 4);
            s.text(font_, cx, cy + cs + 10, clean_title(sh.items[static_cast<std::size_t>(i)]->title), small, focus ? color::text : color::muted, cs);
        }
        if (static_cast<int>(sh.items.size()) > first + 8) s.text(font_, 1920 - margin - 30, ry + 50 + cs / 2 - 16, "›", large, color::muted);
    }
    s.pop_clip();
    if (items.empty()) s.text(font_, margin, 420, snap_->connected ? tr("Aucun élément ne correspond à ces filtres.") : tr("La bibliothèque s'affichera dès que le moteur répondra."), large, color::muted);
    if (!lib_marks_.empty() && !sheet_) draw_checked(s, static_cast<int>(lib_marks_.size()));
    if (!sheet_ && !rsheet_ && !lib_peer_.empty()) draw_footer(s, {{0, tr("Fiche et actions")}, {-1, tr("L2 : cocher")}, {-1, lib_marks_.empty() ? "" : tr("Options : actions")}, {2, trf("Voir : %s", chips[(kind_ + 1) % kind_count].c_str()).c_str()}, {3, tr("Rechercher")}});
    else if (!sheet_ && !rsheet_) draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("L2 : cocher")}, {-1, tr("Options : actions")}, {2, trf("Voir : %s", chips[(kind_ + 1) % kind_count].c_str()).c_str()}, {3, tr("Rechercher")}});
}

void Ui::draw_sheet(Surface& s) {
    const Item* it = nullptr;
    for (const auto& x : snap_->items) if (x.key == sheet_key_) it = &x;
    if (!it) return;
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 200));
    const int pw = 1180, ph = 860, px = (1920 - pw) / 2, py = (1080 - ph) / 2;
    s.rect(px, py, pw, ph, color::panel, 28);
    s.outline(px, py, pw, ph, color::line, 28, 2);
    cover(s, *it, px + 48, py + 48, 200, false);
    std::string eyebrow = it->kind == "rom" ? it->platform : it->kind == "app" ? tr("Application") : "PS5";
    if (!it->title_id.empty()) eyebrow += " · " + it->title_id;
    s.text(font_, px + 284, py + 52, eyebrow, small, color::muted);
    s.text(font_, px + 284, py + 80, it->title, large, color::text, pw - 330 - (it->ignored ? 140 : 0));
    if (it->ignored) {  // ignoré : plus sauvegardé ni proposé
        const char* t = tr("Ignoré");
        const int tw2 = Surface::measure(t, small) + 24;
        s.rect(px + pw - 48 - tw2, py + 52, tw2, 34, rgb(240, 184, 103, 40), 17);
        s.text(font_, px + pw - 36 - tw2 + 0, py + 57, t, small, color::warn);
    }
    int cx = px + 284;
    std::vector<std::string> chips;
    load_images();
    const auto img = tr_images_.find(it->key);
    if (img != tr_images_.end()) {  // le vrai disque des données, et comment le jeu est installé
        const Json& g = img->second;
        const std::string st = g["storage"].as_string();
        chips.push_back(g["disk"]["label"].as_string());
        chips.push_back(st == "native" ? std::string(tr("Installé (natif)")) : st == "image" ? trf("Image .%s", g["container"].as_string().c_str()) : std::string(tr("Dossier")));
        if (g["running"].as_bool()) chips.push_back(tr("En cours de jeu"));
        else if (g["mounted"].as_bool()) chips.push_back(tr("Monté"));
    } else {
        for (const auto& p : snap_->places) if (p.id == it->place) chips.push_back(p.label);
        if (!it->image_type.empty()) chips.push_back(it->image_type);
    }
    for (const auto& a : it->apps) chips.push_back(a);
    for (const auto& c : chips) {
        const int w = Surface::measure(c, small) + 20;
        s.rect(cx, py + 134, w, 30, color::panel2, 8);
        s.text(font_, cx + 10, py + 137, c, small, color::text);
        cx += w + 8;
    }
    // Compatibilité avec cette console : identité du jeu (version, minimum déclaré, SDK des exécutables) et état.
    if (it->kind == "game_ps5" || it->kind == "app") {
        if (!compat_asked_.count(it->key)) {
            compat_asked_.insert(it->key);
            const std::string key = it->key;
            model_.command("GET", "/api/v2/compat?item=" + url_encode(key), "", [this, key](const std::string& e, const std::string& body) {
                Json j;
                if (e.empty() && Json::parse(body, j)) post_ui([this, key, j] { compat_[key] = j; });
            });
        }
        const auto c = compat_.find(it->key);
        if (c != compat_.end() && c->second["state"].is_string()) {
            const Json& cj = c->second;
            const std::string st = cj["state"].as_string();
            auto col_of = [](const std::string& x) { return x == "system_ok" || x == "verified" ? color::ok : x == "backport_present" ? color::warn : x == "no_backport" || x == "incompatible_known" ? color::bad : color::muted; };
            const std::uint32_t col = col_of(st);
            const Json& id = cj["identity"];
            std::string idl;
            if (!id["version"].as_string().empty()) idl += trf("Version %s", id["version"].as_string().c_str());
            if (!id["exe_sdk_max"].as_string().empty()) idl += (idl.empty() ? "" : "  ·  ") + trf("SDK %s", id["exe_sdk_max"].as_string().c_str());
            if (!id["declared_min"].as_string().empty()) idl += (idl.empty() ? "" : "  ·  ") + trf("minimum déclaré %s", id["declared_min"].as_string().c_str());
            s.rect(px + 284, py + 186, 12, 12, col, 6);
            s.text(font_, px + 306, py + 178, compat_state_text(cj), small, col, pw - 360);
            if (!idl.empty()) s.text(font_, px + 306, py + 212, idl, small, color::muted, pw - 360);
            // Backport disponible pour ce jeu (lu dans l'image et sur la console), en court : rien ne sort du cadre.
            const std::string bt = compat_backport_text(cj);
            s.text(font_, px + 306, py + 244, bt.empty() ? std::string(tr("Aucun backport trouvé pour ce jeu")) : trf("Backport : %s", bt.c_str()), small, bt.empty() ? color::muted : color::text, pw - 360);
            // Les autres PS5 jumelées, avec leur système.
            int yy = py + 276;
            for (const auto& pj : cj["peers"].items()) {
                s.rect(px + 284, yy + 8, 12, 12, col_of(pj["state"].as_string()), 6);
                s.text(font_, px + 306, yy, compat_state_text(pj), small, col_of(pj["state"].as_string()), pw - 360);
                yy += 32;
            }
        }
    }
    int shift = 0;
    { const auto c = compat_.find(it->key); if (c != compat_.end()) shift = 34 * static_cast<int>(std::min<std::size_t>(c->second["peers"].items().size(), 2)); }
    s.text(font_, px + 48, py + 290 + shift, tr("Que sauvegarder ?"), large, color::text);
    int y = py + 344 + shift;
    std::uint64_t total = 0;
    const int comps = static_cast<int>(it->components.size());
    for (int i = 0; i < comps; ++i) {
        const auto& c = it->components[static_cast<std::size_t>(i)];
        const bool on = !unchecked_.count(i);
        if (on) total += c.bytes;
        s.rect(px + 48, y, pw - 96, 56, color::bg2, 14);
        if (sheet_focus_ == i) s.outline(px + 44, y - 4, pw - 88, 64, color::accent, 18, 3);
        s.rect(px + 68, y + 16, 24, 24, on ? color::accent2 : color::panel2, 6);
        if (on) for (int k = 0; k < 6; ++k) { s.rect(px + 73 + k, y + 27 + k, 3, 3, color::text); s.rect(px + 79 + k * 2 / 2 + k, y + 32 - k * 2, 3, 3, color::text); }
        s.text(font_, px + 108, y + 13, c.label.empty() ? part_name(c.part) : c.label, body, color::text);
        const auto sz = size_text(c.bytes);
        s.text(font_, px + pw - 72 - Surface::measure(sz, body), y + 13, sz, body, color::muted);
        y += 66;
    }
    y += 12;
    const auto& places = snap_->places;
    const bool chosen = dest_ > 0 && dest_ <= static_cast<int>(places.size());
    const std::string dest = trf("Destination : %s", (!chosen ? std::string(tr("automatique")) : places[static_cast<std::size_t>(dest_ - 1)].label +
                             (places[static_cast<std::size_t>(dest_ - 1)].type == "ftp" ? std::string(" · ") + tr("réseau") : " · " + trf("%s libres", size_text(places[static_cast<std::size_t>(dest_ - 1)].free).c_str()))).c_str());
    const int dw = Surface::measure(dest, body) + 40;
    s.rect(px + 48, y, dw, 52, color::bg2, 26);
    sheet_btns_.clear();
    if (sheet_focus_ == comps) s.outline(px + 44, y - 4, dw + 8, 60, color::accent, 30, 3);
    s.text(font_, px + 68, y + 10, dest, body, color::text);
    const std::string go = trf("Sauvegarder (%s)", size_text(total).c_str());
    const int gw = Surface::measure(go, body) + 48;
    s.rect(px + 64 + dw, y, gw, 52, color::text, 26);
    sheet_btns_.push_back({comps, px + 48 + dw / 2, 0});
    sheet_btns_.push_back({comps + 1, px + 64 + dw + gw / 2, 0});
    sheet_btns_.push_back({comps + 2, px + 80 + dw + gw + 75, 0});
    if (sheet_focus_ == comps + 1) s.outline(px + 60 + dw, y - 4, gw + 8, 60, color::accent, 30, 3);
    s.text(font_, px + 88 + dw, y + 10, go, body, color::bg);
    s.rect(px + 80 + dw + gw, y, 150, 52, color::panel2, 26);
    if (sheet_focus_ == comps + 2) s.outline(px + 76 + dw + gw, y - 4, 158, 60, color::accent, 30, 3);
    s.text(font_, px + 112 + dw + gw, y + 10, tr("Fermer"), body, color::text);
    // Deuxième rangée : plans programmés de ce jeu, et parcourir son emplacement.
    {
        int bx = px + 48;
        int by = y + 76;
        int line = 1;
        const bool tools = (it->kind == "game_ps5" || it->kind == "game_ps4") && it->key.rfind("save:", 0) != 0;  // sauvegarde sans le jeu : pas d'image
        std::vector<std::pair<int, const char*>> btns = {{0, tr("Plans programmés…")}, {1, tr("Parcourir")}, {2, tr("Toutes les versions")},
                                                         {3, it->ignored ? tr("Sauvegarder de nouveau") : tr("Ne plus sauvegarder…")}, {4, tr("Envoyer à une PS5…")}};
        if (tools) btns.push_back({5, tr("Outils de l'image…")});
        if (has_console_save(it->title_id)) btns.push_back({6, tr("Supprimer la partie de la console…")});
        for (const auto& [b, label] : btns) {
            const int bw2 = Surface::measure(label, body) + 28;
            if (bx + bw2 > px + pw - 48) { bx = px + 48; by += 64; ++line; }  // pas la place : ligne suivante
            sheet_btns_.push_back({comps + 3 + b, bx + bw2 / 2, line});
            s.rect(bx, by, bw2, 52, b == 6 ? color::panel : color::panel2, 26);
            if (sheet_focus_ == comps + 3 + b) s.outline(bx - 4, by - 4, bw2 + 8, 60, b == 6 ? color::bad : color::accent, 30, 3);
            s.text(font_, bx + 14, by + 10, label, body, b == 6 ? color::bad : color::text);
            bx += bw2 + 8;
        }
        int n = 0;
        for (const auto& p : plans_) for (const auto& k2 : p["items"].items()) if (k2.as_string() == it->key) ++n;
        if (n) { const std::string t = trf("dans %d plan(s)", n); s.text(font_, px + pw - 48 - Surface::measure(t, small), by + 74, t, small, color::ok); }
        s.text(font_, px + 48, by + 74, tr("La sauvegarde continue même si vous fermez l'application."), small, color::muted);
    }
    if ((it->kind == "game_ps5" || it->kind == "game_ps4") && it->key.rfind("save:", 0) != 0) draw_footer(s, {{0, tr("Cocher / choisir")}, {3, tr("Outils de l'image")}, {-1, tr("Haut / bas  parcourir")}, {1, tr("Fermer")}});
    else draw_footer(s, {{0, tr("Cocher / choisir")}, {-1, tr("Haut / bas  parcourir")}, {1, tr("Fermer")}});
}

void Ui::draw_destinations(Surface& s) {
    s.text(font_, margin, 112, tr("Appareils"), title, color::text);
    s.text(font_, margin, 190, tr("Les disques sont reconnus par leur témoin, même s'ils changent de port USB."), body, color::muted);
    if (!switches_loaded_ || now_ - switches_loaded_ > 15000) load_remotes();
    {   // pendant la mise à jour d'une autre PS5 : son avancement toutes les 2 s
        const bool pushing = !upd_push_["state"].as_string().empty() && !upd_push_.has("finished");
        if (!link_peers_at_ || now_ - link_peers_at_ > (pushing ? 2000u : 10000u)) load_link_peers();  // consoles jumelées : leur propre minuterie
    }
    // Hauteur des cartes selon le nombre de rangées : tout tient au-dessus des aides du bas (rangée « Ajouter » comprise).
    int n_cards = static_cast<int>(remotes_all_.size() + peer_cards().size());
    for (const auto& p : snap_->places) if (p.type == "usb" || p.type == "internal") ++n_cards;
    const int rows = std::max(1, (n_cards + 2) / 3), ah = 150, row_gap = 16;
    const int ch = std::clamp((975 - 244 - (36 + ah + 12) - (rows - 1) * row_gap) / rows, 120, 176), cw = 552, gap = 36;
    int x = margin, y = 244;
    auto next = [&] { x += cw + gap; if (x + cw > 1920 - margin + 1) { x = margin; y += ch + row_gap; } };
    // Carte : l'image de l'appareil à gauche (PS5, disque, Switch…), le reste à droite.
    int card_index = 0;
    auto card = [&](int dev, bool highlight) {
        const bool focus = dest_zone_ == 0 && card_index++ == dest_sel_;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 5, y - g * 5, cw + g * 10, ch + g * 10, rgb(80, 150, 255, 20), 22 + g * 5);
        s.rect(x + 6, y + 10, cw, ch, rgb(0, 0, 0, 70), 22);
        s.rect(x, y, cw, ch, color::panel, 22);
        s.fade(x, y, 200, ch, rgb(124, 196, 255, 28), true, true);
        s.outline(x, y, cw, ch, focus ? color::accent : highlight ? color::accent2 : color::line, 22, focus ? 4 : 2);
        const int im = std::min(150, ch - 16);
        if (devices_[dev].valid()) s.image(devices_[dev], x + 20 + (150 - im) / 2, y + (ch - im) / 2, im, im);
    };
    for (const auto& p : snap_->places) {
        if (p.type != "usb" && p.type != "internal") continue;  // les Switch ont leur propre carte plus bas
        card(p.type == "usb" ? dev_usb : dev_internal, snap_->default_place == p.id);
        const int tx = x + 200, tw = cw - 220;
        card_title(s, tx, y, p.label, tw - 110);
        const char* tag = snap_->default_place == p.id ? tr("Par défaut") : p.type == "usb" ? (p.marked ? tr("Reconnu") : tr("Nouveau")) : "PS5";
        s.text(font_, x + cw - 20 - Surface::measure(tag, small), y + 30, tag, small, color::muted);
        s.text(font_, tx, y + 66, p.type == "usb" ? trf("Disque USB · %s", p.mount.c_str()) : std::string(tr("Stockage interne de la PS5")), small, color::muted, tw);
        s.rect(tx, y + 104, tw, 8, rgb(255, 255, 255, 26), 4);
        const int used = p.total ? static_cast<int>(tw * static_cast<double>(p.total - p.free) / static_cast<double>(p.total)) : 0;
        s.rect(tx, y + 104, std::max(8, used), 8, color::accent2, 4);
        const int fw = s.text(font_, tx, y + 124, trf("%s libres", size_text(p.free).c_str()), body, color::text);
        s.text(font_, tx + fw + 12, y + 130, trf("sur %s", size_text(p.total).c_str()), small, color::muted);
        next();
    }
    for (const auto& r : remotes_all_.items()) {
        const bool sw = r["type"].as_string() == "switch";
        const bool drive = r["type"].as_string() == "gdrive" || r["type"].as_string() == "onedrive" || r["type"].as_string() == "dropbox";
        const bool s3 = r["type"].as_string() == "s3" || drive;  // stockage dans le nuage : image du nuage
        card(sw ? dev_switch : s3 ? dev_cloud : dev_nas, false);  // un appareil FTP enregistré = un NAS (boîtier)
        const int tx = x + 200, tw = cw - 220;
        const std::string service = r["config"]["service"].as_string();
        const char* tag = sw ? "Switch" : drive ? (r["type"].as_string() == "gdrive" ? "Drive" : r["type"].as_string() == "dropbox" ? "Dropbox" : "OneDrive") : s3 ? (service == "b2" ? "B2" : "S3") : r["type"].as_string() == "webdav" ? "WebDAV" : "FTP";
        card_title(s, tx, y, r["name"].as_string(), cw - 20 - Surface::measure(tag, small) - 16 - (tx - x));  // le nom s'arrête avant l'étiquette
        s.text(font_, x + cw - 20 - Surface::measure(tag, small), y + 30, tag, small, sw ? rgb(255, 120, 120) : color::accent);
        const std::string where = drive ? r["config"]["bucket"].as_string() : !s3 ? r["config"]["host"].as_string() + ":" + std::to_string(r["config"]["port"].as_int())
            : (service == "b2" ? std::string("Backblaze B2") : service == "other" ? r["config"]["host"].as_string() : std::string("Amazon S3")) +
              (r["config"]["region"].as_string().empty() ? std::string() : " · " + r["config"]["region"].as_string()) + " · " + r["config"]["bucket"].as_string();
        s.text(font_, tx, y + 66, where, small, color::muted, tw);
        if (sw) s.text(font_, tx, y + 110, tr("Options : ses parties, synchroniser"), small, color::text, tw);
        else {  // à quoi il sert : étiquettes allumées (vert) ou éteintes (grises)
            const std::string use = r["use"].as_string();
            int ux = tx;
            for (const auto& [label, on] : {std::pair<const char*, bool>{tr("Sauvegardes"), use != "sync"}, {tr("Synchro"), use != "backups"}}) {
                const int lw = Surface::measure(label, small) + 28;
                s.rect(ux, y + 104, lw, 36, on ? rgb(46, 160, 100, 60) : rgb(255, 255, 255, 18), 18);
                s.text(font_, ux + 14, y + 108, label, small, on ? color::ok : color::muted);
                ux += lw + 10;
            }
        }
        next();
    }
    for (const auto& pc : peer_cards()) {  // autres PS5 jumelées : leurs disques, comme s'ils étaient branchés ici
        const Json& p = pc.peer;
        const std::string pname = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
        card(dev_internal, false);
        const int tx = x + 200, tw = cw - 220;
        // Ses disques (« PS5-Salon (12.40) · USB 0 ») : la console reste en titre, ses disques sont listés dedans
        // avec leur place libre — on voit tout de suite que ce sont les disques DE L'AUTRE console.
        std::vector<const Place*> disks;
        const std::string prefix = "peer:" + p["instance"].as_string() + ":";
        for (const auto& pl : snap_->places) if (pl.type == "peer" && pl.id.rfind(prefix, 0) == 0) disks.push_back(&pl);
        const bool remote = p["remote"].as_bool();
        if (!disks.empty()) {
            const char* tag = remote ? tr("À distance") : tr("Autre PS5");
            std::string sysv;
            {
                const std::string& l0 = disks[0]->label;
                const auto cut = l0.rfind(" · ");
                const std::string head = cut == std::string::npos ? std::string() : l0.substr(0, cut);
                const auto o = head.rfind(" (");
                if (o != std::string::npos && head.back() == ')') sysv = head.substr(o + 2, head.size() - o - 3);
            }
            card_title(s, tx, y, pname, tw - Surface::measure(tag, small) - 16);
            s.text(font_, x + cw - 20 - Surface::measure(tag, small), y + 30, tag, small, remote ? color::ok : color::accent);
            // Son adresse IP en tête (comme le NAS et les Switch), puis son système ; ses disques sont listés dessous.
            // À distance : « par Internet » et son adresse publique (ou « elle nous appelle » si elle n'en a pas).
            // Puis son système, sa version de Backup Center et son temps de réponse : « 192.168.1.50 · 4.03 · v2.89.0 · 12 ms ».
            const std::string rip = !p["wan_host"].as_string().empty() ? p["wan_host"].as_string() : p["seen_ip"].as_string();
            // (l'étiquette « À distance » dit déjà « par Internet » : la ligne garde la place pour ses deux adresses)
            std::string ip = remote ? (rip.empty() ? std::string(tr("par Internet · elle nous appelle")) : rip)
                                    : p["host"].as_string();

            std::string info = !ip.empty() ? ip + (sysv.empty() || remote ? std::string() : "  ·  " + sysv)
                                           : sysv.empty() ? trf("%d disque(s)", static_cast<int>(disks.size())) : trf("système %s · %d disque(s)", sysv.c_str(), static_cast<int>(disks.size()));
            // Sa version de Backup Center et son temps de réponse : sur une ligne à part, sous ses disques (trop long pour la
            // première ligne) ; avec trois disques, plus de place : à la suite de la première ligne.
            std::string vms = p["version"].as_string().empty() ? std::string() : "v" + p["version"].as_string();
            const std::string ms = p["rtt_ms"].is_number() ? std::to_string(p["rtt_ms"].as_int()) + " ms" : std::string();
            if (!remote) { if (!ms.empty()) vms += (vms.empty() ? "" : "  ·  ") + ms; }
            else {
                // À distance : 1re ligne « adresse publique · ms » ; sous ses disques « locale … · système · version ».
                if (!ms.empty()) info += " · " + ms;
                if (!sysv.empty()) vms = sysv + (vms.empty() ? "" : " · " + vms);
                if (!p["lan_ip"].as_string().empty()) vms = trf("locale %s", p["lan_ip"].as_string().c_str()) + (vms.empty() ? "" : " · " + vms);
            }
            if (disks.size() >= 3 && !vms.empty()) info += "  ·  " + vms;
            s.text(font_, tx, y + 66, info, small, color::muted, tw);
            // Une ligne par disque (trois au plus) : « Interne   744 Go libres ».
            int ly = y + 92;
            for (std::size_t k2 = 0; k2 < disks.size() && k2 < 3; ++k2) {
                const Place& d = *disks[k2];
                const auto cut = d.label.rfind(" · ");
                std::string dn = cut == std::string::npos ? d.label : d.label.substr(cut + 3);
                while (!dn.empty() && dn.front() == ' ') dn.erase(dn.begin());
                // « Interne  ▬▬▬▬░░░  744 Go libres » : nom, petite barre d'occupation, place libre alignée à droite.
                const std::string fr = trf("%s libres", size_text(d.free).c_str());
                const int frw = Surface::measure(fr, small), bx = tx + 96, bw = std::max(40, tw - 96 - frw - 14);
                s.text(font_, tx, ly, dn, small, color::text, 90);
                s.rect(bx, ly + 11, bw, 6, rgb(255, 255, 255, 26), 3);
                const int used = d.total ? static_cast<int>(bw * static_cast<double>(d.total - std::min(d.free, d.total)) / static_cast<double>(d.total)) : 0;
                s.rect(bx, ly + 11, std::max(6, used), 6, color::accent2, 3);
                s.text(font_, tx + tw - frw, ly, fr, small, color::muted);
                ly += 24;
            }
            if (disks.size() < 3 && !vms.empty()) s.text(font_, tx, ly + 4, vms, small, color::muted, tw);
            {   // Pastille de version, posée sur le bord haut de la carte, au-dessus du nom : seulement quand c'est utile
                // (mise à jour en cours, échouée ou pas encore en marche, ou versions différentes). Mêmes versions : rien.
                std::string line;
                unsigned col = color::accent;
                const std::string mine = snap_->update.installed.empty() ? snap_->version : snap_->update.installed;
                const std::string theirs = p["version"].as_string();
                const bool ours = upd_push_["peer"].as_string() == p["instance"].as_string() && !upd_push_["state"].as_string().empty();
                const std::string st = ours ? upd_push_["state"].as_string() : std::string(), v = upd_push_["version"].as_string();
                const bool fin = upd_push_.has("finished");
                const auto& lu = snap_->update;  // récupération en cours ici, depuis elle
                if (!ours && (lu.state == "downloading" || lu.state == "installing") && lu.received_from == pname) {
                    line = lu.state == "downloading" ? std::string(tr("Mise à jour : téléchargement…")) : std::string(tr("Mise à jour : installation…"));
                } else if (ours && !fin) {
                    line = st == "downloading" ? std::string(tr("Mise à jour : téléchargement…")) : st == "installing" ? std::string(tr("Mise à jour : installation…"))
                         : st == "restarting" ? std::string(tr("Mise à jour : relance du moteur…")) : std::string(tr("Mise à jour : préparation…"));
                } else if (ours && st == "failed" && static_cast<std::int64_t>(std::time(nullptr)) - upd_push_["finished"].as_int() < 600) {
                    line = trf("Mise à jour impossible : %s", upd_push_["message"].as_string().c_str()); col = color::bad;
                } else if (ours && st == "done" && static_cast<std::int64_t>(std::time(nullptr)) - upd_push_["finished"].as_int() < 60) {
                    line = trf("Mise à jour : %s en marche", v.c_str()); col = color::ok;  // en vert une minute, puis elle disparaît
                } else if (!ours && lu.received_from == pname && engine_changed_at_ && now_ - engine_changed_at_ < 60000) {
                    line = trf("Mise à jour : %s en marche", snap_->version.c_str()); col = color::ok;  // récupérée ici depuis elle
                } else if (ours && st == "installed_not_restarted" && theirs != v) {
                    line = trf("Mise à jour : %s installée, active au prochain démarrage", v.c_str()); col = color::warn;
                } else if (const std::string inst = p["installed"].as_string(); !inst.empty() && inst != theirs && !mine.empty() && version_cmp(inst, mine) >= 0) {
                    line = trf("Mise à jour : %s installée, active au prochain démarrage", inst.c_str()); col = color::warn;  // déjà installée là-bas
                } else if (!theirs.empty() && !mine.empty() && version_cmp(theirs, mine) < 0) {
                    const std::string inst = p["installed"].as_string(), from = !inst.empty() && version_cmp(inst, theirs) > 0 ? inst : theirs;
                    line = trf("Mise à jour : %s → %s", from.c_str(), mine.c_str()); col = color::warn;
                } else if (!theirs.empty() && !mine.empty() && version_cmp(theirs, mine) > 0) {
                    line = trf("Mise à jour disponible : %s", theirs.c_str()); col = color::warn;
                }
                if (!line.empty()) {
                    // Centrée sur la colonne du texte (nom, disques) ; plus large que la colonne : calée à droite de la carte.
                    const int bh = 30, by = y - bh / 2, colx = tx, colw = x + cw - 20 - tx;
                    const int lw = std::min(Surface::measure(line, small), cw - 48 - 28), bw = lw + 28;
                    const int bx = bw <= colw ? colx + (colw - bw) / 2 : std::max(x + 24, x + cw - 20 - bw);
                    s.rect(bx, by, bw, bh, color::panel, bh / 2);
                    s.outline(bx, by, bw, bh, col, bh / 2, 2);
                    s.text(font_, bx + 14, by + 3, line, small, col, lw);
                }
            }
        } else {
            const char* tag = tr("Hors ligne");
            card_title(s, tx, y, pname, tw - Surface::measure(tag, small) - 16);
            s.text(font_, x + cw - 20 - Surface::measure(tag, small), y + 30, tag, small, color::muted);
            if (remote) {
                const std::string rip = !p["wan_host"].as_string().empty() ? p["wan_host"].as_string() : p["seen_ip"].as_string();
                s.text(font_, tx, y + 66, rip.empty() ? std::string(tr("PS5 à distance · elle nous appelle")) : trf("PS5 à distance · %s", rip.c_str()), small, color::muted, tw);
                s.text(font_, tx, y + 110, tr("Options : tester la connexion"), small, color::muted, tw);
            } else {
                s.text(font_, tx, y + 66, trf("PS5 jumelée · %s", p["host"].as_string().c_str()), small, color::muted, tw);
                s.text(font_, tx, y + 110, tr("Ses disques s'affichent quand elle est allumée."), small, color::muted, tw);
            }
        }
        next();
    }
    if (x != margin) { x = margin; y += ch + row_gap; }
    // Ajouter un appareil : tout ce que l'app saura joindre, avec sa vraie image.
    y = std::max(y + 6, 0);
    s.text(font_, margin, y, tr("AJOUTER UN APPAREIL"), small, color::accent);
    s.text(font_, margin + Surface::measure(tr("AJOUTER UN APPAREIL"), small) + 18, y, tr("Bas pour choisir, Croix pour ajouter"), small, color::muted);
    struct Add { int dev; const char* name; const char* sub; bool logo; };
    static const Add adds[] = {{dev_internal, "PlayStation 5", N_("une autre PS5"), false}, {dev_internal, N_("PS5 à distance"), N_("par Internet"), false},
                               {dev_usb, N_("Disque USB"), N_("disque externe"), false}, {dev_switch, "Switch", N_("réseau ou USB"), false},
                               {dev_synology, "Synology", "NAS", false}, {dev_qnap, "QNAP", "NAS", false},
                               {dev_network, "FTP / SMB", N_("PC, serveur"), false}, {dev_cloud, N_("Cloud"), N_("Drive, OneDrive, S3…"), false}};
    constexpr int count = 8;
    const int aw = (1920 - 2 * margin - (count - 1) * 14) / count;
    for (int i = 0; i < count; ++i) {
        const int ax = margin + i * (aw + 14), ay = y + 36;
        const bool focus = dest_zone_ == 1 && dest_add_ == i;
        s.rect(ax, ay, aw, ah, focus ? color::panel2 : color::panel, 18);
        s.outline(ax, ay, aw, ah, focus ? color::accent : color::line, 18, focus ? 4 : 1);
        const Image& img = devices_[adds[i].dev];
        if (adds[i].logo) {  // logo de marque, en grand (le logo est centré dans son carré)
            if (img.valid()) s.image(img, ax + aw / 2 - 70, ay + 58 - 70, 140, 140);  // centré sur la zone des images
        } else if (img.valid()) {
            // Nuage, FTP / SMB et Disque USB sont plus larges que hauts : dans le même carré de 84, ils ne prenaient que la moitié
            // de la hauteur. Carré plus grand, même centre : ils occupent la place des autres images, sans toucher au texte.
            // Hauteur visible ≈ celle des NAS (84) : Nuage (1,33 fois plus large que haut) 112, FTP / SMB (1,45) 122.
            const int is = adds[i].dev == dev_cloud ? 112 : adds[i].dev == dev_network ? 122 : adds[i].dev == dev_usb ? 90 : adds[i].dev == dev_switch ? 100 : 84;
            s.image(img, ax + aw / 2 - is / 2, ay + 48 - is / 2, is, is);
        }
        {  // logo de marque : « NAS » en blanc dessous (la marque est déjà dans le logo)
            const int nw = Surface::measure(tr(adds[i].name), body);
            s.text(font_, ax + std::max(8, (aw - nw) / 2), ay + 90, tr(adds[i].name), body, color::text, aw - 16);
        }
        const int sw = Surface::measure(tr(adds[i].sub), small);
        s.text(font_, ax + std::max(8, (aw - sw) / 2), ay + 118, tr(adds[i].sub), small, color::muted, aw - 16);
    }
    if (dest_zone_ == 0) draw_footer(s, {{0, tr("Disque par défaut / Réglages")}, {-1, tr("Options : actions")}, {3, tr("Parcourir")}, {-1, tr("Bas : ajouter un appareil")}});
    else draw_footer(s, {{0, tr("Ajouter")}, {-1, tr("Gauche / droite")}, {-1, tr("Haut : vos appareils")}});
}

// ---- Destinations : choisir, régler, ajouter ----

void Ui::input_destinations(std::uint32_t k) {
    std::vector<const Place*> places;
    for (const auto& p : snap_->places) if (p.type == "usb" || p.type == "internal") places.push_back(&p);
    const int remotes = static_cast<int>(remotes_all_.size());
    const auto peers = peer_cards();
    const int cards = static_cast<int>(places.size()) + remotes + static_cast<int>(peers.size());
    const int peer_at = static_cast<int>(places.size()) + remotes;  // première carte de console jumelée
    if (dest_zone_ == 0) {
        if (k & key::left) dest_sel_ = std::max(0, dest_sel_ - 1);
        if (k & key::right) dest_sel_ = std::min(std::max(0, cards - 1), dest_sel_ + 1);
        if (k & key::up) dest_sel_ = std::max(0, dest_sel_ - 3);
        if (k & key::down) { if (dest_sel_ + 3 < cards) dest_sel_ += 3; else dest_zone_ = 1; }
        if ((k & (key::cross | key::options | key::triangle)) && dest_sel_ >= peer_at && dest_sel_ < cards) { open_peer(peers[static_cast<std::size_t>(dest_sel_ - peer_at)].peer); return; }
        if ((k & key::triangle) && dest_sel_ < cards) {  // parcourir les dossiers de l'appareil
            if (dest_sel_ < static_cast<int>(places.size())) {
                const Place* p = places[static_cast<std::size_t>(dest_sel_)];
                open_browser(p->label, p->type == "internal" ? std::string("/data") : p->mount, false, nullptr);
            } else {
                const Json& r = remotes_all_[static_cast<std::size_t>(dest_sel_ - static_cast<int>(places.size()))];
                br_open_ = true;
                br_title_ = r["name"].as_string();
                br_files_ = false;
                br_pick_ = nullptr;
                br_remote_ = r["id"].as_string();
                br_config_ = Json();
                std::string root = r["config"]["root"].as_string();
                browse_to(root.empty() ? "/" : root);
            }
            br_view_ = true;
            return;
        }
        if ((k & key::options) && dest_sel_ < cards) {  // raccourcis de la carte : ses parties, ses backups, synchroniser, parcourir
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            std::string title, place_id, remote_id, type;
            if (dest_sel_ < static_cast<int>(places.size())) { const Place* p = places[static_cast<std::size_t>(dest_sel_)]; title = p->label; place_id = p->id; type = p->type; }
            else { const Json& r = remotes_all_[static_cast<std::size_t>(dest_sel_ - static_cast<int>(places.size()))]; title = r["name"].as_string(); remote_id = r["id"].as_string(); type = r["type"].as_string(); place_id = remote_id; }
            if (type == "switch") {
                acts.push_back({tr("Ses parties"), [this, remote_id] { open_switch_sync(remote_id); }});
                acts.push_back({tr("Synchroniser"), [this, remote_id] { open_switch_sync(remote_id); }});
            }
            if (type == "usb" || type == "ftp") acts.push_back({tr("Ses sauvegardes"), [this, place_id] {
                const auto pl = mg_places();
                for (std::size_t i = 0; i < pl.size(); ++i) if (pl[i]->id == place_id || pl[i]->id == "remote:" + place_id) mg_place_ = static_cast<int>(i) + 1;
                tab_ = backups; mg_screen_ = 0; mg_zone_ = 1; mg_row_ = 0;
            }});
            if (type == "internal") acts.push_back({tr("Ses parties"), [this] { tab_ = parties; pt_player_ = 0; pt_zone_ = 1; }});
            acts.push_back({tr("Parcourir"), [this] { input_destinations(key::triangle); }});
            open_actions(title, std::move(acts));
            return;
        }
        if ((k & key::cross) && dest_sel_ < cards) {
            if (dest_sel_ < static_cast<int>(places.size())) {
                const std::string id = places[static_cast<std::size_t>(dest_sel_)]->id;
                model_.command("POST", "/api/v2/settings", "{\"default_place\":" + json_str(id) + "}", [this](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? tr("Destination par défaut enregistrée") : e);
                });
            } else {
                const Json& r = remotes_all_[static_cast<std::size_t>(dest_sel_ - static_cast<int>(places.size()))];
                open_editor(r["id"].as_string(), r["type"].as_string());
            }
        }
        return;
    }
    if (k & key::left) dest_add_ = std::max(0, dest_add_ - 1);
    if (k & key::right) dest_add_ = std::min(7, dest_add_ + 1);
    if (k & (key::up | key::circle)) dest_zone_ = 0;
    if (k & key::cross) {
        // 0 PS5, 1 PS5 à distance, 2 USB, 3 Switch, 4 Synology, 5 QNAP, 6 FTP / SMB, 7 Cloud
        if (dest_add_ == 0) open_other_ps5();
        else if (dest_add_ == 1) open_remote_ps5();
        else if (dest_add_ == 2) toast(tr("Branchez le disque : il apparaît tout seul, et reste reconnu même sur un autre port."));
        else if (dest_add_ == 3) open_editor("", "switch");
        else if (dest_add_ == 4 || dest_add_ == 5 || dest_add_ == 6) open_editor("", "ftp");
        else {  // Nuage : Amazon S3, Backblaze B2, autre service S3 ; Google Drive et OneDrive ensuite
            auto s3 = [this](const char* svc) { return [this, svc] { ed_service_ = svc; open_editor("", "s3"); }; };
            // WebDAV : réglage du NAS, adresse et dossier déjà remplis.
            auto dav = [this](const char* name, const char* host, const char* root, const char* hint) {
                return [this, name, host, root, hint] {
                    open_editor("", "webdav");
                    ed_fields_[0].second = name;
                    ed_fields_[1].second = host;
                    ed_fields_[2].second = "443";
                    ed_fields_[5].second = root;
                    toast(tr(hint));
                };
            };
            std::vector<std::pair<std::string, std::function<void()>>> items = {{"Google Drive", [this] { open_cloud_link("gdrive"); }}, {"OneDrive", [this] { open_cloud_link("onedrive"); }},
                                                                           {"Dropbox", [this] { open_cloud_link("dropbox"); }}};
            std::vector<std::string> subs = {tr("connexion par code"), tr("connexion par code"), tr("depuis le téléphone")};
            for (const auto& x : kS3Services) {
                if (std::string(x.id) == "other") continue;
                items.push_back({x.name, s3(x.id)});
                subs.push_back(tr("clés S3"));
            }
            items.push_back({"pCloud", dav("pCloud", "webdav.pcloud.com", "/", N_("pCloud : e-mail et mot de passe du compte. Compte européen : ewebdav.pcloud.com."))});
            items.push_back({"Koofr", dav("Koofr", "app.koofr.net", "/dav/Koofr", N_("Koofr : e-mail et un mot de passe d'appli (Koofr › Réglages › Mots de passe)."))});
            items.push_back({"Nextcloud", dav("Nextcloud", "", "/remote.php/dav/files/", N_("Nextcloud : adresse de votre serveur ; ajoutez votre identifiant à la fin du dossier."))});
            for (int i = 0; i < 3; ++i) subs.push_back(tr("compte WebDAV"));
            items.push_back({tr("Autre S3"), s3("other")});
            subs.push_back(tr("MinIO, iDrive e2…"));
            open_actions(tr("Ajouter un stockage dans le nuage"), std::move(items));
            popup_subs_ = std::move(subs);
        }
    }
}

// ---- Une autre PS5 (liaison entre consoles, docs/PROTOCOLE-link.md) ----

void Ui::load_link_peers() {
    link_peers_at_ = now_;
    // Toutes les 30 s, demander son état à chaque PS5 jumelée en ligne : le moteur en tire son temps de réponse
    // (« 12 ms » sur sa carte, réseau local ou Internet) et sa version à jour.
    if (link_peers_.size() && (!peer_ping_at_ || now_ - peer_ping_at_ > 30000u)) {
        peer_ping_at_ = now_;
        if (link_peers_.is_array())
            for (const auto& p : link_peers_.items())
                if (p["paired"].as_bool() && p["online"].as_bool())
                    model_.command("GET", "/api/v2/link/peer?instance=" + p["instance"].as_string() + "&what=status", "", [](const std::string&, const std::string&) {});
    }
    model_.command("GET", "/api/v2/link/peers", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_link_peers_ = j; has_pending_link_peers_ = true; }
    });
    model_.command("GET", "/api/v2/update/push", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty() && Json::parse(body, j) && j.is_object()) post_ui([this, j] { upd_push_ = j; });
    });
}

namespace {
int version_cmp(const std::string& a, const std::string& b) {  // « 2.87.2 » contre « 2.85.5 »
    std::size_t i = 0, k = 0;
    while (i < a.size() || k < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') { if (a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i] - '0'); ++i; }
        while (k < b.size() && b[k] != '.') { if (b[k] >= '0' && b[k] <= '9') y = y * 10 + (b[k] - '0'); ++k; }
        if (x != y) return x < y ? -1 : 1;
        ++i; ++k;
    }
    return 0;
}
}  // namespace

// Mettre à jour une autre PS5 depuis celle-ci : elle télécharge notre version (vérifiée chez elle), l'installe, relance
// son moteur par la voie normale. Confirmation d'abord ; l'avancement s'affiche sur sa carte (Appareils).
void Ui::push_update(const std::string& inst, const std::string& name, const std::string& theirs) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({trf("Mettre à jour %s", name.c_str()), [this, inst, name] {
        model_.command("POST", "/api/v2/update/push", "{\"peer\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("Mise à jour de %s lancée : l'avancement s'affiche sur sa carte", name.c_str()) : e);
            post_ui([this] { link_peers_at_ = 0; });
        });
    }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Mettre %s en %s ?", name.c_str(), snap_->version.c_str()), std::move(acts));
    popup_note_ = trf("Elle est en %s. Elle télécharge la version de cette PS5, vérifie chaque fichier et garde l'ancienne si quelque chose échoue. Son moteur est ensuite relancé ; s'il refuse, la nouvelle version sera active au prochain démarrage de la console. Le jeu en cours n'est jamais touché.", theirs.c_str());
}

// « Backup Center X est prête » : rouvrir maintenant / plus tard, avec 15 s de minuteur si Backup Center est au premier plan.
void Ui::offer_reopen(const std::string& v) {
    model_.command("GET", "/api/v2/app/foreground", "", [this, v](const std::string& e, const std::string& body) {
        Json j;
        const bool front = e.empty() && Json::parse(body, j) && j["title_id"].as_string() == "PPSA99950";
        post_ui([this, v, front] {
            if (popup_ != 0) { reopen_offered_.clear(); return; }  // une autre fenêtre est ouverte : on reproposera plus tard
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            acts.push_back({tr("Rouvrir maintenant"), [this] { reopen_popup_ = false; reopen_deadline_ = 0; countdown_kind_ = 2; countdown_until_ = now_; }});
            acts.push_back({tr("Plus tard"), [this] { reopen_popup_ = false; reopen_deadline_ = 0; }});
            open_actions(trf("Backup Center %s est prête", v.c_str()), std::move(acts));
            popup_note_ = trf("L'appli ouverte est encore en %s : rouvrez-la pour voir les nouveautés (environ 5 s). Le jeu en cours n'est pas touché.", app_version);
            reopen_popup_ = true;
            reopen_kind_ = 2;
            reopen_deadline_ = front ? now_ + 15000 : 0;
        });
    });
}

// Mise à jour installée, moteur encore à l'ancienne version : relancer le moteur (minuteur de 15 s si Backup Center est
// au premier plan ; sinon la fenêtre attend). Une fois le moteur relancé, « rouvrir l'appli » est proposé à son tour.
void Ui::offer_engine_restart(const std::string& v) {
    model_.command("GET", "/api/v2/app/foreground", "", [this, v](const std::string& e, const std::string& body) {
        Json j;
        const bool front = e.empty() && Json::parse(body, j) && j["title_id"].as_string() == "PPSA99950";
        post_ui([this, v, front] {
            if (popup_ != 0) { engine_offered_.clear(); return; }  // une autre fenêtre est ouverte : on reproposera plus tard
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            acts.push_back({tr("Relancer le moteur maintenant"), [this] { reopen_popup_ = false; reopen_deadline_ = 0; countdown_kind_ = 1; countdown_until_ = now_; }});
            acts.push_back({tr("Plus tard"), [this] { reopen_popup_ = false; reopen_deadline_ = 0; }});
            open_actions(trf("Mise à jour %s installée", v.c_str()), std::move(acts));
            popup_note_ = trf("Le moteur tourne encore en %s. Il se relance en quelques secondes pour passer en %s ; rien n'est perdu, le jeu en cours n'est pas touché.",
                              snap_->version.c_str(), v.c_str());
            reopen_popup_ = true;
            reopen_kind_ = 1;
            reopen_deadline_ = front ? now_ + 15000 : 0;
        });
    });
}

// Le miroir : cette PS5 récupère la version plus récente d'une PS5 jumelée (elle nous la pousse, mêmes vérifications).
void Ui::pull_update(const std::string& inst, const std::string& name, const std::string& theirs) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({trf("Récupérer la %s", theirs.c_str()), [this, inst, name] {
        model_.command("POST", "/api/v2/link/peer-update-me", "{\"instance\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("Mise à jour demandée à %s : l'avancement s'affiche sur sa carte et à l'Accueil", name.c_str()) : e);
            post_ui([this] { link_peers_at_ = 0; });
        });
    }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Mettre cette PS5 en %s ?", theirs.c_str()), std::move(acts));
    popup_note_ = trf("Cette PS5 est en %s. Elle télécharge la version de %s, vérifie chaque fichier et garde l'ancienne si quelque chose échoue. Son moteur est ensuite relancé ; s'il refuse, la nouvelle version sera active au prochain démarrage. Le jeu en cours n'est jamais touché.", snap_->version.c_str(), name.c_str());
}

// Code de l'autre console (affiché chez elle dans Appareils › PlayStation 5), puis jumelage.
void Ui::link_pair(const std::string& host, const std::string& name) {
    link_host_ = host;
    link_code_.clear();
    open_keyboard(trf("Code affiché par %s", name.c_str()), "", [this](const std::string& t) { link_code_ = t; });
    kb_done_ = [this, name] {
        std::string code;
        for (char c : link_code_) if (c >= '0' && c <= '9') code += c;
        if (code.size() != 6) { post_toast(tr("Le code a 6 chiffres")); return; }
        Json b = Json::object();
        b.set("host", link_host_);
        b.set("port", static_cast<std::int64_t>(52370));
        b.set("code", code);
        post_toast(trf("Jumelage avec %s…", name.c_str()));
        model_.command("POST", "/api/v2/link/pair", b.dump(), [this, name](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("%s est jumelée", name.c_str()) : e);
            load_link_peers();
        });
    };
}

std::vector<Json> Ui::paired_peers() const {
    std::vector<Json> v;
    for (const auto& p : link_peers_.items()) if (p["paired"].as_bool()) v.push_back(p);
    return v;
}

// Appareils : chaque disque d'une PS5 jumelée est une carte, comme un disque de cette console ; si on ne voit pas ses
// disques (hors ligne), une seule carte pour la console.
std::vector<Ui::PeerCard> Ui::peer_cards() const {
    std::vector<PeerCard> v;
    for (const auto& p : paired_peers()) v.push_back({p, nullptr});  // une carte par console ; ses disques sont listés dedans
    return v;
}

// Menu d'une console jumelée (carte d'Appareils ou ligne de « Une autre PS5 »).
void Ui::open_peer(const Json& p) {
    // Son état d'abord (version en marche et version installée), pour proposer la bonne action ; sans réponse, le menu
    // s'ouvre quand même avec ce que l'annonce dit.
    const std::string inst = p["instance"].as_string();
    model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=status", "", [this, p](const std::string& e, const std::string& body) {
        Json st;
        if (!e.empty() || !Json::parse(body, st) || !st.is_object()) st = Json::object();
        post_ui([this, p, st] { open_peer_menu(p, st); });
    });
}

void Ui::open_peer_menu(const Json& p, const Json& st) {
    const std::string inst = p["instance"].as_string(), name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
    std::vector<std::pair<std::string, std::function<void()>>> sub;
    {   // version en marche / installée là-bas, comparées à la nôtre
        const std::string running = st["version"].as_string().empty() ? p["version"].as_string() : st["version"].as_string();
        const std::string installed = st["update"]["installed"].as_string().empty() ? running : st["update"]["installed"].as_string();
        const std::string mine = snap_->version;
        if (!installed.empty() && !mine.empty() && version_cmp(installed, mine) < 0)  // plus ancienne : la mettre à jour d'ici
            sub.push_back({trf("Mettre à jour vers %s (elle est en %s)", mine.c_str(), running.c_str()), [this, inst, name, running] { push_update(inst, name, running); }});
        else if (!installed.empty() && !mine.empty() && version_cmp(installed, mine) > 0)  // plus récente : récupérer sa version ici
            sub.push_back({trf("Récupérer la %s depuis %s", installed.c_str(), name.c_str()), [this, inst, name, installed] { pull_update(inst, name, installed); }});
        else if (!installed.empty() && !running.empty() && version_cmp(running, installed) < 0)  // installée, pas encore en marche
            sub.push_back({trf("Relancer son moteur (%s installée, %s en marche)", installed.c_str(), running.c_str()), [this, inst, name, installed] {
                model_.command("POST", "/api/v2/link/peer-restart-engine", "{\"instance\":" + json_str(inst) + "}", [this, name, installed](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? trf("%s : relance du moteur demandée (%s)", name.c_str(), installed.c_str())
                                         : trf("%s refuse la relance : %s — %s sera active à son prochain démarrage", name.c_str(), e.c_str(), installed.c_str()));
                    post_ui([this] { link_peers_at_ = 0; });
                });
            }});
        else  // même version : relancer son moteur quand même (bloqué, lent…) — le jeu en cours n'est pas touché
            sub.push_back({trf("Relancer le moteur de Backup Center sur %s", name.c_str()), [this, inst, name] {
                model_.command("POST", "/api/v2/link/peer-restart-engine", "{\"instance\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? trf("%s : relance du moteur demandée", name.c_str()) : trf("%s refuse la relance : %s", name.c_str(), e.c_str()));
                    post_ui([this] { link_peers_at_ = 0; });
                });
            }});
    }
    sub.push_back({trf("Fermer Backup Center sur %s", name.c_str()), [this, inst, name] {
        std::vector<std::pair<std::string, std::function<void()>>> c;
        c.push_back({trf("Fermer sur %s", name.c_str()), [this, inst, name] {
            model_.command("POST", "/api/v2/link/close-app", "{\"instance\":" + json_str(inst) + ",\"title_id\":\"PPSA99950\"}", [this, name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("Backup Center se ferme sur %s", name.c_str()) : trf("%s : %s", name.c_str(), e.c_str()));
            });
        }});
        c.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Fermer Backup Center sur %s ?", name.c_str()), std::move(c));
        popup_note_ = tr("Seulement si Backup Center est l'application ouverte là-bas : un jeu n'est jamais fermé. Son moteur continue (sauvegardes, synchro).");
    }});
    sub.push_back({trf("Ouvrir Backup Center sur %s", name.c_str()), [this, inst, name] {
        std::vector<std::pair<std::string, std::function<void()>>> c;
        c.push_back({trf("Ouvrir sur %s", name.c_str()), [this, inst, name] {
            model_.command("POST", "/api/v2/link/peer-open-app", "{\"instance\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("Backup Center s'ouvre sur %s", name.c_str()) : e);
            });
        }});
        c.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Ouvrir Backup Center sur %s ?", name.c_str()), std::move(c));
        popup_note_ = tr("S'il y a un jeu en cours là-bas, il passe en arrière-plan (sa partie n'est pas fermée). À faire seulement si personne n'y joue.");
    }});
    sub.push_back({trf("Désinstaller un jeu sur %s…", name.c_str()), [this, inst, name] { peer_uninstall_list(inst, name); }});
    // Ses disques : une vraie liste (nom, place libre sur le total).
    if (p["remote"].as_bool()) sub.push_back({tr("Tester la connexion"), [this, inst, name] { wan_test(inst, name); }});
    if (p["remote"].as_bool()) {
        const std::string d = p["wan_dir"].as_string();
        const std::string cur = d == "out" ? trf("Cette PS5 appelle %s", name.c_str()) : d == "in" ? trf("%s appelle cette PS5", name.c_str()) : std::string(tr("Automatique"));
        const Json pc = p;
        sub.push_back({trf("Qui ouvre la session : %s", cur.c_str()), [this, pc] { wan_session(pc); }});
    }
    sub.push_back({tr("Ses disques"), [this, inst, name] {
        model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=places", "", [this, inst, name](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Lecture impossible")) : e); return; }
            post_ui([this, inst, name, j] {
                std::vector<std::pair<std::string, std::function<void()>>> rows;
                for (const auto& pl : j.items()) {
                    if (pl["type"].as_string() != "usb" && pl["type"].as_string() != "internal") continue;
                    const std::string label = pl["label"].as_string(), start = pl["type"].as_string() == "internal" ? std::string("/data") : pl["mount"].as_string();
                    rows.push_back({label + "  ·  " + trf("%s libres", size_text(static_cast<std::uint64_t>(pl["free"].as_int())).c_str()) +
                                    (pl["total"].as_int() ? "  ·  " + trf("sur %s", size_text(static_cast<std::uint64_t>(pl["total"].as_int())).c_str()) : std::string()),
                                    [this, inst, name, label, start] {  // parcourir ce disque de l'autre PS5 (lecture seule)
                                        open_browser(name + " · " + label, start, false, nullptr);
                                        br_remote_ = "peer:" + inst;
                                        browse_to(start);
                                    }});
                }
                open_actions(trf("Disques de %s", name.c_str()), std::move(rows));
                popup_note_ = tr("Choisissez un disque pour le parcourir (lecture seule).");
            });
        });
    }});
    // Ses parties : ce que l'autre console a comme sauvegardes (lecture seule), avant d'envoyer.
    sub.push_back({tr("Ses parties"), [this, inst, name] {
        model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=overview", "", [this, name](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Lecture impossible")) : e); return; }
            post_ui([this, name, j] {
                std::vector<std::pair<std::string, std::function<void()>>> rows;
                for (const auto& x : j["entries"].items()) {
                    if (!x["console"].is_object()) continue;  // seulement ce qui est vraiment sur la console
                    std::string line = x["title"].as_string();
                    const std::string acc = x["account"].as_string();
                    line += "  ·  " + std::string(acc.rfind("ps4:", 0) == 0 ? "PS4" : "PS5");
                    const std::string pr = x["console"]["progress"]["text"].as_string();
                    if (x["console"]["progress"]["percent"].is_number()) line += "  ·  " + trf("%d %%", static_cast<int>(x["console"]["progress"]["percent"].as_int()));
                    (void)pr;
                    rows.push_back({line, [] {}});
                }
                if (rows.empty()) { toast(trf("Aucune partie sur %s", name.c_str())); return; }
                open_actions(trf("Parties sur %s (%d)", name.c_str(), static_cast<int>(rows.size())), std::move(rows));
            });
        });
    }});
    sub.push_back({tr("Envoyer un jeu…"), [this, name] {
        tab_ = library;
        toast(trf("Choisissez un jeu, puis « Envoyer à une PS5… » sur sa fiche (vers %s).", name.c_str()));
    }});
    sub.push_back({tr("Envoyer une sauvegarde…"), [this, name] {
        tab_ = parties; pt_zone_ = 1;
        toast(trf("Choisissez une partie, puis Options › « Envoyer la sauvegarde à une PS5… » (vers %s).", name.c_str()));
    }});
    sub.push_back({tr("Fermer l'application ouverte…"), [this, inst, name] {
        model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=foreground", "", [this, inst, name](const std::string& e, const std::string& body) {
            Json fg;
            if (!e.empty() || !Json::parse(body, fg)) { post_toast(e.empty() ? std::string(tr("Lecture impossible")) : e); return; }
            post_ui([this, inst, name, fg] { confirm_close_remote(inst, name, fg); });
        });
    }});
    sub.push_back({tr("Oublier cette console…"), [this, inst, name] {
        std::vector<std::pair<std::string, std::function<void()>>> confirm;
        confirm.push_back({trf("Oui, oublier %s", name.c_str()), [this, inst, name] {
            model_.command("POST", "/api/v2/link/forget", "{\"instance\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("%s n'est plus jumelée", name.c_str()) : e);
                load_link_peers();
            });
        }});
        confirm.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Défaire le lien avec %s ?", name.c_str()), std::move(confirm));
    }});
    open_actions(name, std::move(sub));
}

void Ui::open_other_ps5() {
    load_link_peers();
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& p : link_peers_.items()) {
        const std::string inst = p["instance"].as_string(), host = p["host"].as_string();
        const std::string name = p["name"].as_string().empty() ? host : p["name"].as_string();
        if (p["paired"].as_bool()) {  // une ligne par console jumelée ; ses actions dans un sous-menu
            const bool online = p["online"].as_bool();
            const Json pc = p;
            acts.push_back({trf(online ? "%s — jumelée ✓, en ligne" : "%s — jumelée ✓, hors ligne", name.c_str()), [this, pc] { open_peer(pc); }});
        } else if (p["online"].as_bool()) {
            acts.push_back({trf("Jumeler %s (%s)…", name.c_str(), host.c_str()), [this, host, name] { link_pair(host, name); }});
        }
    }
    acts.push_back({tr("Saisir l'adresse IP de l'autre PS5…"), [this] {
        link_host_.clear();
        open_keyboard(tr("Adresse IP de l'autre PS5"), "", [this](const std::string& t) { link_host_ = t; });
        kb_done_ = [this] {
            std::string ip;
            for (char c : link_host_) if ((c >= '0' && c <= '9') || c == '.') ip += c;
            if (ip.empty()) return;
            link_pair(ip, ip);
        };
    }});
    const std::string code = snap_->pair_code.size() == 6 ? snap_->pair_code.substr(0, 3) + " " + snap_->pair_code.substr(3) : std::string("—");
    acts.insert(acts.begin(), {trf("Code de cette console : %s (à saisir sur l'autre PS5)", code.c_str()), [this] { post_toast(tr("Sur l'autre PS5 : Appareils › PlayStation 5, puis ce code.")); }});
    acts.insert(acts.begin(), {tr("Comment ça marche ?"), [this] {
        std::vector<std::pair<std::string, std::function<void()>>> steps;
        steps.push_back({tr("1. Installez Backup Center sur les deux PS5, sur le même réseau."), [] {}});
        steps.push_back({tr("2. Sur l'une, choisissez l'autre PS5 dans cette liste."), [] {}});
        steps.push_back({tr("3. Tapez le code affiché sur la télé de l'autre PS5. C'est tout : le lien marche dans les deux sens."), [] {}});
        steps.push_back({tr("Ensuite : Sauvegardes › un jeu › Options › « Envoyer à … »."), [] {}});
        open_actions(tr("Deux PS5 qui travaillent ensemble"), std::move(steps));
    }});
    open_actions(tr("Une autre PS5"), std::move(acts));
}

// ---- PS5 à distance : liaison par Internet (docs/PROTOCOLE-link.md §11) ----
namespace {
// Textes du test de connexion (envoyés par le moteur, traduits ici).
[[maybe_unused]] const char* const kWanTexts[] = {
    N_("Liaison par Internet désactivée ici"), N_("Appareils › PS5 à distance : activez-la pour que l'autre PS5 puisse vous joindre."),
    N_("Écoute impossible sur le port %s"), N_("Un autre programme utilise ce port : choisissez-en un autre dans Appareils › PS5 à distance."),
    N_("Liaison par Internet active ici (port %s)"), N_("Adresse de « %s » inconnue"),
    N_("Elle n'a pas d'adresse joignable (LTE, Starlink…) : c'est elle qui appelle cette PS5."),
    N_("Port %s fermé ou injoignable chez « %s » (%s)"),
    N_("Il faut une redirection du port %s (TCP) vers sa PS5 dans son routeur ou son pare-feu (Fortinet : « Virtual IP »), ou que son adresse soit la bonne."),
    N_("« %s » répond : %s, %s ms"), N_("Canal chiffré refusé par « %s »"), N_("Le jumelage n'est plus reconnu là-bas : oubliez-la et invitez-la de nouveau."),
    N_("Canal chiffré établi avec « %s »"), N_("Cette PS5 est joignable depuis Internet (%s:%s)"), N_("Cette PS5 n'est pas joignable depuis Internet (%s)"),
    N_("Pas grave : elle garde une connexion ouverte vers « %s », qui s'en sert pour l'appeler."), N_("Pas de réponse dans le canal chiffré"),
    N_("« %s » garde une connexion ouverte vers cette PS5"), N_("Cette PS5 garde une connexion ouverte vers « %s »"), N_("Aucun chemin vers « %s »"),
    N_("Il faut qu'au moins une des deux PS5 soit joignable depuis Internet (redirection de port)."),
};
// Texte traduit, « %s » remplacés dans l'ordre.
std::string fill_args(const std::string& key, const Json& args) {
    std::string t = tr(key.c_str()), out;
    std::size_t i = 0, at = 0;
    for (std::size_t p; (p = t.find("%s", at)) != std::string::npos; at = p + 2) { out += t.substr(at, p - at); out += args[i++].as_string(); }
    return out + t.substr(at);
}
}  // namespace

void Ui::open_remote_ps5() {
    model_.command("GET", "/api/v2/wan", "", [this](const std::string& e, const std::string& body) {
        Json w;
        if (!e.empty() || !Json::parse(body, w) || !w.is_object()) { post_toast(e.empty() ? tr("Réponse illisible") : e); return; }
        post_ui([this, w] { open_remote_menu(w); });
    });
}

void Ui::open_remote_menu(const Json& w) {
    load_link_peers();
    const bool on = w["enabled"].as_bool();
    const std::string port = std::to_string(w["port"].as_int(52373)), host = w["public_host"].as_string(), seen = w["observed_ip"].as_string();
    auto set = [this](const std::string& json, const std::string& ok) {
        model_.command("POST", "/api/v2/settings", json, [this, ok](const std::string& e, const std::string&) {
            post_toast(e.empty() ? ok : e);
            if (e.empty()) post_ui([this] { open_remote_ps5(); });
        });
    };
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({tr("Comment ça marche ?"), [this] {
        std::vector<std::pair<std::string, std::function<void()>>> steps;
        steps.push_back({tr("1. Activez la liaison par Internet sur les deux PS5."), [] {}});
        steps.push_back({tr("2. Il suffit qu'UNE des deux soit joignable : redirigez son port (TCP) vers elle dans le routeur ou le pare-feu."), [] {}});
        steps.push_back({tr("3. Sur celle-là, « Créer une invitation » ; donnez le code à l'autre personne."), [] {}});
        steps.push_back({tr("4. Sur l'autre PS5 : « J'ai un code d'invitation ». Ensuite, c'est une PS5 jumelée comme les autres."), [] {}});
        steps.push_back({tr("Tout passe chiffré ; seules les PS5 invitées peuvent entrer."), [] {}});
        open_actions(tr("PS5 à distance"), std::move(steps));
    }});
    acts.push_back({on ? trf("Liaison par Internet : activée (port %s)", port.c_str()) : std::string(tr("Liaison par Internet : désactivée")),
                    [set, on] { set(std::string("{\"wan_enabled\":") + (on ? "false" : "true") + "}", on ? tr("Liaison par Internet désactivée") : tr("Liaison par Internet activée")); }});
    if (on && !w["error"].as_string().empty()) acts.push_back({w["error"].as_string(), [] {}});
    acts.push_back({tr("Créer une invitation…"), [this] {
        model_.command("POST", "/api/v2/wan/invite", "{}", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? tr("Réponse illisible") : e); return; }
            post_ui([this, j] {
                std::vector<std::pair<std::string, std::function<void()>>> c;
                c.push_back({tr("Fermer"), [] {}});
                open_actions(j["code"].as_string(), std::move(c));
                popup_note_ = trf("Donnez ce code à la personne qui a l'autre PS5 (message, téléphone ; il se copie depuis le web). À usage unique, valable 24 h. Il mène à %s, port %s : ce port doit être redirigé vers cette PS5.",
                                  j["host"].as_string().c_str(), std::to_string(j["port"].as_int()).c_str());
            });
        });
    }});
    acts.push_back({tr("J'ai un code d'invitation…"), [this] {
        wan_code_.clear();
        open_keyboard(tr("Code d'invitation (PBC-…)"), "", [this](const std::string& t) { wan_code_ = t; });
        kb_done_ = [this] {
            if (wan_code_.empty()) return;
            post_toast(tr("Connexion à l'autre PS5…"));
            Json b = Json::object();
            b.set("code", wan_code_);
            model_.command("POST", "/api/v2/wan/join", b.dump(), [this](const std::string& e, const std::string& body) {
                Json j;
                Json::parse(body, j);
                post_toast(e.empty() ? trf("%s est jumelée par Internet", j["name"].as_string().c_str()) : e);
                post_ui([this] { link_peers_at_ = 0; load_link_peers(); });
            });
        };
    }});
    for (const auto& p : link_peers_.items()) {  // les PS5 à distance déjà jumelées
        if (!p["remote"].as_bool() || !p["paired"].as_bool()) continue;
        const std::string name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
        const Json pc = p;
        acts.push_back({trf(p["online"].as_bool() ? "%s — à distance, en ligne" : "%s — à distance, hors ligne", name.c_str()), [this, pc] { open_peer(pc); }});
    }
    acts.push_back({trf("Port : %s", port.c_str()), [this, set] {
        wan_code_.clear();
        open_keyboard(tr("Port TCP (1024 à 65535)"), "", [this](const std::string& t) { wan_code_ = t; });
        kb_done_ = [this, set] {
            const int v = std::atoi(wan_code_.c_str());
            if (v < 1024 || v > 65535) { post_toast(tr("Port invalide")); return; }
            set("{\"wan_port\":" + std::to_string(v) + "}", trf("Port %d", v));
        };
    }});
    acts.push_back({host.empty() ? std::string(tr("Adresse publique : non réglée")) : trf("Adresse publique : %s", host.c_str()), [this, set] {
        wan_code_.clear();
        open_keyboard(tr("Adresse publique ou nom (ex. maison.exemple.net)"), "", [this](const std::string& t) { wan_code_ = t; });
        kb_done_ = [this, set] {
            std::string h;
            for (char c : wan_code_) if (c != ' ') h += c;
            Json b = Json::object();
            b.set("wan_public_host", h);
            set(b.dump(), tr("Adresse enregistrée"));
        };
    }});
    acts.push_back({seen.empty() ? std::string(tr("Détecter l'adresse publique")) : trf("Détecter l'adresse publique (vue par l'autre PS5 : %s)", seen.c_str()), [this, set] {
        model_.command("POST", "/api/v2/wan/detect-ip", "{}", [this, set](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? tr("Réponse illisible") : e); return; }
            Json b = Json::object();
            b.set("wan_public_host", j["ip"].as_string());
            post_ui([set, b, ip = j["ip"].as_string()] { set(b.dump(), trf("Adresse publique : %s", ip.c_str())); });
        });
    }});
    open_actions(tr("PS5 à distance"), std::move(acts));
    popup_note_ = on ? tr("Une PS5 jumelée par Internet : ses disques, ses parties, envoyer un jeu ou une sauvegarde, la synchro et les mises à jour, comme avec une PS5 du salon. Tout est chiffré.")
                     : tr("Désactivée : rien n'écoute sur Internet. Activez-la sur les deux PS5 pour en jumeler une à distance.");
}

// Qui ouvre la session avec une PS5 à distance : au choix, d'un côté ou de l'autre, et changeable quand on se déplace.
// L'autre PS5 reçoit le choix inversé. Celle qui est derrière un pare-feu devrait appeler : son trafic sort.
void Ui::wan_session(const Json& p) {
    const std::string inst = p["instance"].as_string(), name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
    const std::string host = p["wan_host"].as_string(), cur = p["wan_dir"].as_string();
    auto choose = [this, inst, name](const std::string& dir, const std::string& h) {
        Json b = Json::object();
        b.set("instance", inst);
        b.set("dir", dir);
        if (!h.empty()) b.set("host", h);
        model_.command("POST", "/api/v2/wan/peer", b.dump(), [this, name](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("Choix enregistré, aussi sur %s", name.c_str()) : e);
            post_ui([this] { link_peers_at_ = 0; });
        });
    };
    std::vector<std::pair<std::string, std::function<void()>>> c;
    c.push_back({std::string(cur == "auto" || cur.empty() ? "✓ " : "") + tr("Automatique"), [choose] { choose("auto", ""); }});
    c.push_back({std::string(cur == "out" ? "✓ " : "") + trf("Cette PS5 appelle %s", name.c_str()), [this, choose, host, name] {
        if (!host.empty()) { choose("out", ""); return; }
        wan_code_.clear();  // son adresse d'abord (elle doit être joignable)
        open_keyboard(trf("Adresse publique de %s (ex. maison.exemple.net)", name.c_str()), "", [this](const std::string& t) { wan_code_ = t; });
        kb_done_ = [this, choose] { std::string h; for (char ch : wan_code_) if (ch != ' ') h += ch; if (!h.empty()) choose("out", h); };
    }});
    c.push_back({std::string(cur == "in" ? "✓ " : "") + trf("%s appelle cette PS5", name.c_str()), [choose] { choose("in", ""); }});
    c.push_back({host.empty() ? trf("Adresse de %s : inconnue", name.c_str()) : trf("Adresse de %s : %s", name.c_str(), host.c_str()), [this, choose, cur, name] {
        wan_code_.clear();
        open_keyboard(trf("Adresse publique de %s (ex. maison.exemple.net)", name.c_str()), "", [this](const std::string& t) { wan_code_ = t; });
        kb_done_ = [this, choose, cur] { std::string h; for (char ch : wan_code_) if (ch != ' ') h += ch; if (!h.empty()) choose(cur.empty() ? "auto" : cur, h); };
    }});
    c.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Qui ouvre la session avec %s ?", name.c_str()), std::move(c));
    popup_note_ = tr("Celle qui est derrière un pare-feu, en LTE ou sur Starlink devrait appeler l'autre : son trafic sort, rien à ouvrir chez elle. Celle qu'on appelle doit être joignable (redirection du port). Le choix est aussi fait sur l'autre PS5 ; changez-le si vous vous déplacez.");
}

void Ui::wan_test(const std::string& inst, const std::string& name) {
    post_toast(trf("Test de la connexion avec %s…", name.c_str()));
    model_.command("POST", "/api/v2/wan/test", "{\"instance\":" + json_str(inst) + "}", [this, name](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? tr("Réponse illisible") : e); return; }
        post_ui([this, j, name] {
            std::vector<std::pair<std::string, std::function<void()>>> lines;
            std::string note;
            for (const auto& st : j["steps"].items()) {
                const std::string state = st["state"].as_string();
                const char* mark = state == "ok" ? "✓ " : state == "bad" ? "✕ " : "! ";
                lines.push_back({mark + fill_args(st["key"].as_string(), st["args"]), [] {}});
                if (!st["detail_key"].as_string().empty() && note.empty() && state != "ok") note = fill_args(st["detail_key"].as_string(), st["detail_args"]);
            }
            open_actions(trf(j["ok"].as_bool() ? "Connexion avec %s : OK" : "Connexion avec %s : à corriger", name.c_str()), std::move(lines));
            popup_note_ = note;
        });
    });
}

void Ui::open_editor(const std::string& id, const std::string& type) {
    ed_open_ = true;
    ed_link_.clear();
    ed_busy_ = ed_forget_armed_ = false;
    ed_id_ = id;
    ed_type_ = type.empty() ? "ftp" : type;
    ed_row_ = 0;
    const bool sw = ed_type_ == "switch";
    std::string name, host, port = sw ? "5000" : "21", user = sw ? "anonymous" : "", root = "/", layout = "pbc", proto = "ftp", use, region, bucket;
    for (const auto& r : remotes_all_.items())
        if (r["id"].as_string() == id) {
            name = r["name"].as_string();
            host = r["config"]["host"].as_string();
            port = std::to_string(r["config"]["port"].as_int());
            user = r["config"]["user"].as_string();
            root = r["config"]["root"].as_string();
            if (r["config"]["layout"].as_string() == "platform") layout = "platform";
            if (r["type"].as_string() == "smb" || r["type"].as_string() == "webdav") ed_type_ = r["type"].as_string();
            if (r["config"]["proto"].as_string() == "smb") proto = "smb";
            if (r["type"].as_string() == "gdrive" || r["type"].as_string() == "onedrive" || r["type"].as_string() == "dropbox") { ed_type_ = r["type"].as_string(); bucket = r["config"]["bucket"].as_string(); }
            if (r["type"].as_string() == "s3") {
                ed_type_ = "s3";
                ed_service_ = r["config"]["service"].as_string();
                region = r["config"]["region"].as_string();
                bucket = r["config"]["bucket"].as_string();
            }
            use = r["use"].as_string();
        }
    if (ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox") {  // compte connecté par code : rien à saisir que le nom et l'usage
        ed_fields_ = {{N_("Compte"), bucket}, {N_("Nom"), name.empty() ? bucket : name}, {N_("Dossier"), root},
                      {N_("Utilisé pour"), id.empty() || use == "backups" ? N_("Sauvegardes seulement") : use == "sync" ? N_("Synchro seulement") : N_("Sauvegardes et Synchro")}};
        return;
    }
    if (ed_type_ == "s3") {  // stockage S3 : clés au lieu d'une adresse et d'un mot de passe ; « Sauvegardes seulement » par défaut
        ed_service_ = s3_service(ed_service_).id;
        if (id.empty()) region = s3_service(ed_service_).region;
        ed_fields_ = {{N_("Nom"), name}, {N_("Service"), s3_service(ed_service_).name},
                      {N_("Adresse du service"), s3_host_typed(ed_service_) ? host : ""}, {N_("Région"), region}, {N_("Compartiment"), bucket},
                      {N_("Clé d'accès"), user == "anonymous" ? "" : user}, {N_("Clé secrète"), ""}, {N_("Dossier"), root},
                      {N_("Utilisé pour"), id.empty() || use == "backups" ? N_("Sauvegardes seulement") : use == "sync" ? N_("Synchro seulement") : N_("Sauvegardes et Synchro")}};
        ed_qr_ = Json();
        model_.command("GET", "/api/v2/web-link?place=cloud", "", [this](const std::string& e, const std::string& body) {  // coller les clés depuis un téléphone
            Json j;
            if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_ed_qr_ = j; has_pending_ed_qr_ = true; }
        });
        return;
    }
    ed_fields_ = {{N_("Nom"), name}, {N_("Adresse IP"), host}, {N_("Port"), port}, {N_("Utilisateur"), user}, {N_("Mot de passe"), ""}, {N_("Dossier"), root}};
    // Rangement des backups (NAS / FTP) : dossier « PBC Backup » ou par plateforme (« PS5 backup/… »).
    if (!sw) ed_fields_.push_back({N_("Rangement"), layout == "platform" ? N_("Par plateforme (PS5 backup…)") : N_("Dossier PBC Backup")});
    // Protocole du NAS : FTP (FTPS si proposé), SMB (partages Windows / Synology / QNAP) ou WebDAV (https) — Croix : suivant.
    // Switch : FTP (ftpd, sphaira) ou SMB (serveur SMB sur la Switch, ou dossier JKSV partagé ailleurs).
    ed_fields_.push_back({N_("Protocole"), (sw ? proto == "smb" : ed_type_ == "smb") ? "SMB" : ed_type_ == "webdav" ? "WebDAV" : "FTP"});
    // À quoi sert ce NAS : Sauvegardes, Synchro ou les deux (une autre entrée du même NAS peut servir à autre chose).
    if (!sw) ed_fields_.push_back({N_("Utilisé pour"), use == "backups" ? N_("Sauvegardes seulement") : use == "sync" ? N_("Synchro seulement") : N_("Sauvegardes et Synchro")});
}

Json Ui::ed_s3_body() const {
    Json c = Json::object();
    const std::string use = ed_field("Utilisé pour");
    c.set("type", "s3");
    c.set("id", ed_id_);
    c.set("service", ed_service_);
    c.set("name", ed_field("Nom"));
    c.set("host", s3_host_typed(ed_service_) ? ed_field("Adresse du service") : std::string());
    c.set("region", ed_field("Région"));
    c.set("bucket", ed_field("Compartiment"));
    c.set("user", ed_field("Clé d'accès"));
    c.set("password", ed_field("Clé secrète"));
    c.set("root", ed_field("Dossier").empty() ? std::string("/") : ed_field("Dossier"));
    c.set("use", use == "Sauvegardes seulement" ? "backups" : use == "Synchro seulement" ? "sync" : "both");
    return c;
}

Json Ui::ed_drive_body() const {
    Json c = Json::object();
    const std::string use = ed_field("Utilisé pour");
    c.set("type", ed_type_);
    c.set("id", ed_id_);
    if (!ed_link_.empty()) c.set("link", ed_link_);
    c.set("name", ed_field("Nom"));
    c.set("root", ed_field("Dossier").empty() ? std::string("/") : ed_field("Dossier"));
    c.set("use", use == "Sauvegardes seulement" ? "backups" : use == "Synchro seulement" ? "sync" : "both");
    return c;
}

// ---- Google Drive, OneDrive : connexion par code ----

void Ui::open_cloud_link(const std::string& provider) {
    cl_open_ = true;
    cl_provider_ = provider;
    cl_id_.clear(); cl_code_.clear(); cl_url_.clear(); cl_error_.clear();
    cl_state_ = "starting";
    cl_qr_ = Json();
    cl_polling_ = false;
    Json b = Json::object();
    b.set("provider", provider);
    model_.command("POST", "/api/v2/cloud/link", b.dump(), [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty()) Json::parse(body, j);
        if (!e.empty()) j.set("error", e);
        j.set("started", true);
        std::lock_guard<std::mutex> l(async_mu_);
        pending_cl_ = j;
        has_pending_cl_ = true;
    });
}

void Ui::input_cloud_link(std::uint32_t k) {
    if (k & key::circle) { cl_open_ = false; return; }
    if ((k & key::square) && cl_state_ != "starting") open_cloud_link(cl_provider_);  // nouveau code
}

void Ui::draw_cloud_link(Surface& s) {
    {
        std::lock_guard<std::mutex> l(async_mu_);
        if (has_pending_cl_) {
            has_pending_cl_ = false;
            const Json j = std::move(pending_cl_);
            if (j["started"].as_bool()) {
                if (!j["error"].as_string().empty()) { cl_state_ = "error"; cl_error_ = j["error"].as_string(); }
                else { cl_state_ = "pending"; cl_next_ = 0; cl_id_ = j["id"].as_string(); cl_code_ = j["user_code"].as_string(); cl_url_ = j["url"].as_string(); cl_qr_ = j["qr"]; }
            } else {
                cl_polling_ = false;
                const std::string st = j["state"].as_string();
                if (!st.empty()) cl_state_ = st;
                cl_status_ = j;
                if (st == "error") cl_error_ = j["error"].as_string();
            }
        }
    }
    if (cl_state_ == "ok") {  // accepté : le réglage du compte s'ouvre, connexion reprise à l'enregistrement
        cl_open_ = false;
        const std::string link = cl_id_, account = cl_status_["account"].as_string();
        open_editor("", cl_provider_);
        ed_link_ = link;
        for (auto& f : ed_fields_) if (f.first == "Compte" || f.first == "Nom") f.second = account;
        if (cl_status_["total"].as_int() > 0)
            toast(trf("Compte connecté : %s libres sur %s", size_text(static_cast<std::uint64_t>(cl_status_["free"].as_int())).c_str(), size_text(static_cast<std::uint64_t>(cl_status_["total"].as_int())).c_str()));
        return;
    }
    const bool waiting = cl_state_ == "pending" || cl_state_ == "waiting_code" || cl_state_ == "checking";  // Dropbox : le téléphone fait le reste
    if (waiting && !cl_polling_ && now_ >= cl_next_ && !cl_id_.empty()) {  // la télé demande toutes les 2 s si c'est accepté
        cl_polling_ = true;
        cl_next_ = now_ + 2000;
        model_.command("GET", "/api/v2/cloud/link?id=" + cl_id_, "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (e.empty()) Json::parse(body, j);
            std::lock_guard<std::mutex> l(async_mu_);
            pending_cl_ = j;
            has_pending_cl_ = true;
        });
    }
    const bool g = cl_provider_ == "gdrive", dbx = cl_provider_ == "dropbox";
    const char* name = g ? "Google Drive" : dbx ? "Dropbox" : "OneDrive";
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1240, h = 640, x = (1920 - w) / 2, y = 170;
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, color::panel, 30);
    s.outline(x, y, w, h, color::line, 30, 2);
    if (devices_[dev_cloud].valid()) s.image(devices_[dev_cloud], x + 36, y + 26, 96, 96);
    s.text(font_, x + 150, y + 36, trf("Connecter %s", name), large, color::text);
    s.text(font_, x + 150, y + 84, tr("Rien à taper ici : la connexion se fait sur votre téléphone, chez le service."), small, color::muted, w - 190);
    const int qs = 340, qx = x + 50, qy = y + 160;
    const auto& rows = cl_qr_.items();
    const int m = static_cast<int>(rows.size());
    if (m) {
        const double cell = qs / (m + 4.0);  // fractionnaire : tous les codes ont la même taille
        s.exact_colors(true);
        s.rect(qx, qy, qs, qs, rgb(255, 255, 255), 12);
        for (int r = 0; r < m; ++r) {
            const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
            for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
        }
        s.exact_colors(false);
    } else if (cl_state_ == "starting") spinner(s, qx + qs / 2, qy + qs / 2, 30);
    const int tx = qx + qs + 50, tw = x + w - 50 - tx;
    int ty = qy;
    if (cl_state_ == "error") {
        for (const auto& line : wrap(trf("Connexion impossible : %s", cl_error_.c_str()), body, tw)) { s.text(font_, tx, ty, line, body, color::bad); ty += 40; }
    } else if (dbx) {  // pas de code court : tout se fait sur la page de la console, ouverte par le code QR
        for (const char* step : {N_("1. Scannez le code avec votre téléphone."), N_("2. Sur le téléphone : « Se connecter à Dropbox », votre compte, « Autoriser »."),
                                 N_("3. Dropbox affiche un code : copiez-le et collez-le sur la page.")})
            for (const auto& line : wrap(tr(step), body, tw)) { s.text(font_, tx, ty, line, body, color::muted); ty += 40; }
        const int sy = qy + qs - 30;
        if (cl_state_ == "expired") s.text(font_, tx, sy, tr("Code expiré. Carré : nouveau code."), body, color::warn, tw);
        else { spinner(s, tx + 16, sy + 18, 14); s.text(font_, tx + 44, sy, tr("En attente du téléphone…"), body, color::accent, tw - 44); }
    } else {
        std::string url = cl_url_;
        for (const char* pre : {"https://", "http://", "www."}) if (url.rfind(pre, 0) == 0) url = url.substr(std::strlen(pre));
        s.text(font_, tx, ty, tr("1. Scannez le code avec votre téléphone, ou ouvrez :"), body, color::muted, tw);
        s.text(font_, tx, ty + 42, url.empty() ? std::string("…") : url, large, color::accent, tw);
        s.text(font_, tx, ty + 110, tr("2. Saisissez ce code :"), body, color::muted, tw);
        s.rect(tx, ty + 154, std::min(tw, 420), 84, rgb(255, 255, 255, 18), 18);
        s.text(font_, tx + 28, ty + 168, cl_code_.empty() ? std::string("…") : cl_code_, large, color::text);
        for (const auto& line : wrap(tr("3. Connectez-vous (e-mail, mot de passe, code de validation…) et acceptez."), body, tw)) { s.text(font_, tx, ty + 262, line, body, color::muted); ty += 40; }
        const int sy = qy + qs - 30;
        if (cl_state_ == "denied") s.text(font_, tx, sy, tr("Refusé sur le téléphone. Carré : nouveau code."), body, color::bad, tw);
        else if (cl_state_ == "expired") s.text(font_, tx, sy, tr("Code expiré. Carré : nouveau code."), body, color::warn, tw);
        else { spinner(s, tx + 16, sy + 18, 14); s.text(font_, tx + 44, sy, tr("En attente de votre accord…"), body, color::accent, tw - 44); }
    }
    s.text(font_, x + 50, y + h - 56, tr("L'appli n'aura accès qu'à son propre dossier, jamais à vos autres fichiers."), small, color::muted, w - 100);
    s.rect(0, 996, 1920, 84, color::bg, 0);
    draw_footer(s, {{2, tr("Nouveau code")}, {1, tr("Annuler")}});
}

std::string Ui::ed_field(const char* name) const {
    for (const auto& f : ed_fields_) if (f.first == name) return f.second;
    return {};
}

void Ui::input_editor(std::uint32_t k) {
    if (ed_busy_) return;
    const int fields = static_cast<int>(ed_fields_.size());
    // Actions : Enregistrer, Tester (+ Ranger sous PBC Backup pour un NAS / FTP, Oublier pour un appareil existant)
    std::vector<int> acts{0, 1};
    if (!ed_id_.empty()) acts.push_back(3);  // mêmes actions pour tous les appareils (NAS, FTP, Switch)
    if (!ed_id_.empty()) acts.push_back(2);
    const int actions = static_cast<int>(acts.size());
    if (k & key::up) ed_row_ = std::max(0, ed_row_ - 1);
    if (k & key::down) ed_row_ = std::min(fields + actions - 1, ed_row_ + 1);
    if (k & key::circle) { ed_open_ = false; return; }
    if (!(k & key::cross)) return;
    if (ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Compte") { toast(tr("Pour changer de compte : Oublier, puis reconnecter.")); return; }
    if (ed_type_ == "s3" && ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Service") {  // Amazon → Backblaze → autre
        const std::string before = ed_service_;
        constexpr int count = sizeof kS3Services / sizeof kS3Services[0];
        int i = 0;
        while (i < count && before != kS3Services[i].id) ++i;
        ed_service_ = kS3Services[(i + 1) % count].id;
        ed_fields_[1].second = s3_service(ed_service_).name;
        if (ed_fields_[3].second == s3_service(before).region) ed_fields_[3].second = s3_service(ed_service_).region;
        if (s3_host_typed(before) != s3_host_typed(ed_service_)) ed_fields_[2].second.clear();
        if (ed_service_ == "other") toast(tr("Autre S3 : saisissez l'adresse du service (ex. https://s3.example.com)."));
        if (ed_service_ == "r2") toast(tr("Cloudflare R2 : saisissez votre n° de compte (tableau de bord Cloudflare › R2)."));
        return;
    }
    if (ed_type_ == "s3" && ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Adresse du service" && !s3_host_typed(ed_service_)) {
        toast(tr("Ce service : l'adresse se déduit de la région."));
        return;
    }
    if (ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Protocole") {  // FTP → SMB → WebDAV (le port suit)
        auto& v = ed_fields_[static_cast<std::size_t>(ed_row_)].second;
        const bool sw = ed_type_ == "switch";  // une Switch reste une Switch : FTP ↔ SMB seulement
        const std::string next = v == "FTP" ? "SMB" : v == "SMB" && !sw ? "WebDAV" : "FTP";
        auto port_of = [sw](const std::string& p) { return p == "SMB" ? "445" : p == "WebDAV" ? "5006" : sw ? "5000" : "21"; };
        if (ed_fields_[2].second == port_of(v)) ed_fields_[2].second = port_of(next);
        v = next;
        if (!sw) ed_type_ = next == "SMB" ? "smb" : next == "WebDAV" ? "webdav" : "ftp";
        if (next == "SMB") toast(tr("SMB : le premier dossier est le partage (ex. /Partage/Sauvegardes)."));
        if (next == "WebDAV") toast(tr("WebDAV : activez « WebDAV Server » sur le NAS (Synology : port 5006 en https)."));
        return;
    }
    if (ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Utilisé pour") {  // les deux → Sauvegardes → Synchro
        auto& v = ed_fields_[static_cast<std::size_t>(ed_row_)].second;
        v = v == "Sauvegardes et Synchro" ? N_("Sauvegardes seulement") : v == "Sauvegardes seulement" ? N_("Synchro seulement") : N_("Sauvegardes et Synchro");
        return;
    }
    if (ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Rangement") {  // bascule, sans clavier
        auto& v = ed_fields_[static_cast<std::size_t>(ed_row_)].second;
        v = v == "Dossier PBC Backup" ? N_("Par plateforme (PS5 backup…)") : N_("Dossier PBC Backup");
        return;
    }
    // Dossier : on se connecte avec les réglages saisis et on le choisit en parcourant (Carré = ce dossier),
    // même avant d'enregistrer l'appareil.
    if (ed_row_ < fields && ed_fields_[static_cast<std::size_t>(ed_row_)].first == "Dossier") {
        const int row = ed_row_;
        const std::string start = ed_fields_[static_cast<std::size_t>(row)].second;
        const bool cloudy = ed_type_ == "s3" || ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox";
        if (!cloudy && ed_fields_[1].second.empty()) { toast(tr("Saisissez d'abord l'adresse IP de l'appareil")); return; }
        Json c = Json::object();
        c.set("type", ed_type_);
        c.set("id", ed_id_);
        c.set("host", ed_fields_[1].second);
        c.set("port", std::atoi(ed_fields_[2].second.c_str()));
        c.set("user", ed_fields_[3].second);
        c.set("password", ed_fields_[4].second);
        c.set("proto", ed_field("Protocole") == "SMB" ? "smb" : "ftp");
        if (ed_type_ == "s3") c = ed_s3_body();
        if (ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox") c = ed_drive_body();
        br_open_ = true;
        br_title_ = ed_fields_[0].second.empty() ? ed_fields_[1].second : ed_fields_[0].second;
        br_files_ = false;
        br_view_ = false;
        br_remote_.clear();
        br_config_ = c;
        br_pick_ = [this, row](const std::string& p) { ed_fields_[static_cast<std::size_t>(row)].second = p; toast(tr("Dossier choisi : Enregistrer pour le garder")); };
        browse_to(start.empty() ? "/" : start);
        return;
    }
    if (ed_row_ < fields) {
        auto& f = ed_fields_[static_cast<std::size_t>(ed_row_)];
        const int row = ed_row_;
        open_keyboard(tr(f.first.c_str()), f.second, [this, row](const std::string& t) { ed_fields_[static_cast<std::size_t>(row)].second = t; });
        kb_secret_ = f.first == "Mot de passe" || f.first == "Clé secrète";
        return;
    }
    const int a = acts[static_cast<std::size_t>(std::min(actions - 1, ed_row_ - fields))];
    if (a == 3) {  // ranger « PS5 backup », « Switch backup »… de la racine sous « PBC Backup » (renommage, rien n'est copié)
        ed_busy_ = true;
        model_.command("POST", "/api/v2/remotes/" + ed_id_ + "/adopt", "{}", [this](const std::string& e, const std::string& body) {
            Json j;
            Json::parse(body, j);
            post_toast(!e.empty() ? e : j["moved"].as_int() + j["merged"].as_int() == 0 ? std::string(tr("Rien à ranger : tout est déjà sous PBC Backup"))
                                                        : trf("Rangé sous PBC Backup : %lld dossier(s) déplacé(s)", static_cast<long long>(j["moved"].as_int())));
            std::lock_guard<std::mutex> l(async_mu_);
            ed_busy_ = false;
        });
        return;
    }
    if (a == 0) {  // Enregistrer : le moteur teste la connexion avant
        std::string body = "{\"type\":" + json_str(ed_type_) + ",\"name\":" + json_str(ed_fields_[0].second) + ",\"host\":" + json_str(ed_fields_[1].second) +
                           ",\"port\":" + std::to_string(std::atoi(ed_fields_[2].second.c_str())) + ",\"user\":" + json_str(ed_fields_[3].second) +
                           ",\"password\":" + json_str(ed_fields_[4].second) + ",\"root\":" + json_str(ed_fields_[5].second) +
                           ",\"layout\":" + json_str(ed_field("Rangement") == "Par plateforme (PS5 backup…)" ? std::string("platform") : std::string("pbc")) +
                           ",\"proto\":" + json_str(ed_field("Protocole") == "SMB" ? std::string("smb") : std::string("ftp")) +
                           ",\"use\":" + json_str(ed_field("Utilisé pour") == "Sauvegardes seulement" ? std::string("backups") : ed_field("Utilisé pour") == "Synchro seulement" ? std::string("sync") : std::string("both")) + "}";
        if (ed_type_ == "s3") body = ed_s3_body().dump();
        if (ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox") body = ed_drive_body().dump();
        ed_busy_ = true;
        const std::string path = ed_id_.empty() ? "/api/v2/remotes" : "/api/v2/remotes/" + ed_id_ + "/update";
        model_.command("POST", path, body, [this](const std::string& e, const std::string&) {
            // Deux synchros dans le même dossier : le moteur dit laquelle a déjà ce dossier.
            if (e.rfind("sync_taken:", 0) == 0) post_toast(trf("Ce dossier sert déjà à la synchro de « %s ». Choisissez un autre dossier, ou « Sauvegardes seulement ».", e.substr(11).c_str()));
            else post_toast(e.empty() ? tr("Appareil enregistré : connexion vérifiée") : trf("Connexion impossible : %s", e.c_str()));
            std::lock_guard<std::mutex> l(async_mu_);
            ed_busy_ = false;
            if (e.empty()) { ed_open_ = false; switches_loaded_ = 0; }
        });
    } else if (a == 1) {  // Tester avec ce qui est saisi (avant ou après l'enregistrement) : rien n'est gardé
        ed_busy_ = true;
        std::string body = "{\"type\":" + json_str(ed_type_) + ",\"id\":" + json_str(ed_id_) + ",\"host\":" + json_str(ed_fields_[1].second) +
                           ",\"port\":" + std::to_string(std::atoi(ed_fields_[2].second.c_str())) + ",\"user\":" + json_str(ed_fields_[3].second) +
                           ",\"password\":" + json_str(ed_fields_[4].second) + ",\"root\":" + json_str(ed_fields_[5].second) +
                           ",\"layout\":" + json_str(ed_field("Rangement") == "Par plateforme (PS5 backup…)" ? std::string("platform") : std::string("pbc")) +
                           ",\"proto\":" + json_str(ed_field("Protocole") == "SMB" ? std::string("smb") : std::string("ftp")) + "}";
        if (ed_type_ == "s3") body = ed_s3_body().dump();
        if (ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox") body = ed_drive_body().dump();
        model_.command("POST", "/api/v2/remotes/test-config", body, [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Connexion réussie : vous pouvez enregistrer") : e);
            std::lock_guard<std::mutex> l(async_mu_);
            ed_busy_ = false;
        });
    } else {
        if (!ed_forget_armed_) { ed_forget_armed_ = true; toast(tr("Appuyez encore sur Croix pour oublier cet appareil (rien n'est effacé dessus)")); return; }
        model_.command("POST", "/api/v2/remotes/" + ed_id_ + "/forget", "{}", [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Appareil oublié") : e);
            std::lock_guard<std::mutex> l(async_mu_);
            switches_loaded_ = 0;
        });
        ed_open_ = false;
        dest_sel_ = 0;
    }
}

void Ui::draw_editor(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1000, h = 690 + std::max(0, static_cast<int>(ed_fields_.size()) - 6) * 66, x = (1920 - w) / 2, y = std::max(40, 150 - std::max(0, static_cast<int>(ed_fields_.size()) - 7) * 50);  // une ligne de plus par champ (Rangement, Protocole)
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, color::panel, 30);
    s.outline(x, y, w, h, color::line, 30, 2);
    const bool sw = ed_type_ == "switch", s3 = ed_type_ == "s3", drive = ed_type_ == "gdrive" || ed_type_ == "onedrive" || ed_type_ == "dropbox";
    const int dev = sw ? dev_switch : s3 || drive ? dev_cloud : dev_nas;
    if (devices_[dev].valid()) s.image(devices_[dev], x + 36, y + 26, 96, 96);
    const std::string s3_title = trf("Ajouter un stockage %s", ed_service_ == "other" ? "S3" : s3_service(ed_service_).name);
    s.text(font_, x + 150, y + 36, drive ? (ed_type_ == "gdrive" ? "Google Drive" : ed_type_ == "dropbox" ? "Dropbox" : "OneDrive") : ed_id_.empty() ? (sw ? tr("Ajouter une Switch") : s3 ? s3_title.c_str() : ed_type_ == "smb" ? tr("Ajouter un partage SMB") : ed_type_ == "webdav" ? tr("Ajouter un NAS en WebDAV") : tr("Ajouter un serveur FTP")) : tr("Régler l'appareil"), large, color::text);
    if (s3 || drive) {  // deux lignes : la mise en garde reste lisible en entier
        int ly = y + 78;
        for (const auto& line : wrap(tr("Par Internet : plus lent qu'un NAS. Le service peut facturer le stockage et les téléchargements."), small, w - 190)) { s.text(font_, x + 150, ly, line, small, color::muted); ly += 30; }
    } else s.text(font_, x + 150, y + 82, sw ? tr("Sur la Switch : ftpd ouvert, l'adresse s'affiche en haut de son écran.") : tr("Adresse, port et compte du serveur (NAS, PC…)."), small, color::muted, w - 190);
    int yy = y + 146;
    for (int i = 0; i < static_cast<int>(ed_fields_.size()); ++i) {
        const bool on = ed_row_ == i;
        s.rect(x + 36, yy, w - 72, 58, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
        if (on) s.outline(x + 36, yy, w - 72, 58, color::accent, 14, 3);
        const bool r2_account = ed_type_ == "s3" && ed_service_ == "r2" && ed_fields_[static_cast<std::size_t>(i)].first == "Adresse du service";
        s.text(font_, x + 60, yy + 14, r2_account ? tr("N° de compte") : tr(ed_fields_[static_cast<std::size_t>(i)].first.c_str()), body, color::muted);
        std::string v = ed_fields_[static_cast<std::size_t>(i)].second;
        const std::string& fname = ed_fields_[static_cast<std::size_t>(i)].first;
        if (fname == "Mot de passe" || fname == "Clé secrète") v = v.empty() ? (ed_id_.empty() ? tr("(aucun)") : tr("(inchangé)")) : std::string(v.size(), '*');
        else if (fname == "Rangement" || fname == "Utilisé pour" || fname == "Service") v = tr(v.c_str());
        else if (fname == "Adresse du service" && !s3_host_typed(ed_service_)) v = tr("d'après la région");
        s.text(font_, x + 330, yy + 14, v.empty() ? "—" : v, body, color::text, w - 400);
        yy += 66;
    }
    static const char* const names[4] = {N_("Enregistrer"), N_("Tester la connexion"), N_("Oublier"), N_("Ranger sous PBC Backup")};
    std::vector<int> acts{0, 1};
    if (!ed_id_.empty()) acts.push_back(3);
    if (!ed_id_.empty()) acts.push_back(2);
    int ax = x + 36;
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) {
        const int a = acts[static_cast<std::size_t>(i)];
        const bool on = ed_row_ == static_cast<int>(ed_fields_.size()) + i;
        const char* label = a == 1 && acts.size() > 3 ? tr("Tester") : tr(names[a]);  // 4 boutons : libellé court pour tenir
        const int aw = Surface::measure(label, body) + 44;
        s.rect(ax, yy + 16, aw, 56, on ? color::text : a == 0 ? rgb(61, 139, 255, 140) : a == 2 ? rgb(255, 123, 123, 60) : rgb(255, 255, 255, 22), 28);
        s.text(font_, ax + 22, yy + 29, label, body, on ? color::bg : color::text);
        ax += aw + 14;
    }
    if (s3) {  // à droite : coller les clés depuis un téléphone ou un ordinateur (page web, formulaire déjà ouvert)
        {
            std::lock_guard<std::mutex> l(async_mu_);
            if (has_pending_ed_qr_) { ed_qr_ = std::move(pending_ed_qr_); has_pending_ed_qr_ = false; }
        }
        const auto& rows = ed_qr_["qr"].items();
        const int m = static_cast<int>(rows.size()), px = x + w + 30, pw = 1920 - px - 30;
        if (m && pw >= 300) {
            const int qs = std::min(pw, 340);
            int ty = y + 40;
            for (const auto& line : wrap(tr("Plus simple : vos clés depuis le téléphone"), body, pw)) { s.text(font_, px, ty, line, body, color::text); ty += 40; }
            ty += 6;
            for (const auto& line : wrap(tr("Scannez : la page web s'ouvre sur ce formulaire, où vous collez les clés."), small, pw)) { s.text(font_, px, ty, line, small, color::muted); ty += 30; }
            const int top = ty + 16;
            const double cell = qs / (m + 4.0);  // fractionnaire : tous les codes ont la même taille
            s.exact_colors(true);  // un code QR se scanne en noir sur blanc
            s.rect(px, top, qs, qs, rgb(255, 255, 255), 10);
            for (int r = 0; r < m; ++r) {
                const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
                for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                    if (line[static_cast<std::size_t>(c)] == '1') s.rect(px + static_cast<int>((c + 2) * cell), top + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
            }
            s.exact_colors(false);
            s.text(font_, px, top + qs + 16, ed_qr_["url"].as_string(), small, color::muted, pw);
        }
    }
    if (ed_busy_) { spinner(s, x + 56, y + h - 40, 16); s.text(font_, x + 86, y + h - 52, tr("Connexion à l'appareil…"), body, color::accent, w - 120); }
    else s.text(font_, x + 36, y + h - 50, tr("Enregistrer teste d'abord la connexion : rien n'est gardé si elle échoue."), small, color::muted, w - 72);
    s.rect(0, 996, 1920, 84, color::bg, 0);
    draw_footer(s, {{0, tr("Modifier / valider")}, {-1, tr("Haut / bas")}, {1, tr("Fermer sans enregistrer")}});
}

// ---- Réglages d'un émulateur ----

void Ui::open_emulator(const Emulator& e) {
    em_open_ = true;
    em_id_ = e.id;
    em_name_ = e.name;
    em_row_ = 0;
    em_roms_ = e.manual_roms;
    if (em_roms_.empty()) for (const auto& r : e.roots) em_roms_.push_back(r.path);  // on part de ce qui est détecté
    em_config_default_ = e.config_files.empty() ? std::string() : e.config_files[0];
    em_config_ = e.manual_config.empty() ? em_config_default_ : e.manual_config;
    em_saves_ = e.manual_saves;
}

void Ui::input_emulator(std::uint32_t k) {
    const int roms = static_cast<int>(em_roms_.size());
    const int rows = roms + 1 + 2 + 2;  // dossiers, « ajouter », réglages, sauvegardes, Enregistrer, Automatique
    if (k & key::up) em_row_ = std::max(0, em_row_ - 1);
    if (k & key::down) em_row_ = std::min(rows - 1, em_row_ + 1);
    if (k & key::circle) { em_open_ = false; return; }
    if ((k & key::square) && em_row_ < roms) { em_roms_.erase(em_roms_.begin() + em_row_); return; }
    if (!(k & key::cross)) return;
    auto start_of = [](const std::string& p) { const auto sl = p.rfind('/'); return sl == std::string::npos || sl == 0 ? std::string("/") : p.substr(0, sl); };
    if (em_row_ < roms) {
        const int i = em_row_;
        open_browser(tr("Dossier de jeux"), em_roms_[static_cast<std::size_t>(i)], false, [this, i](const std::string& p) { em_roms_[static_cast<std::size_t>(i)] = p; });
    } else if (em_row_ == roms) {
        open_browser(tr("Ajouter un dossier de jeux"), em_roms_.empty() ? "/mnt" : start_of(em_roms_.back()), false, [this](const std::string& p) { em_roms_.push_back(p); });
    } else if (em_row_ == roms + 1) {
        open_browser(tr("Fichier de réglages de l'émulateur"), em_config_.empty() ? "/data" : start_of(em_config_), true, [this](const std::string& p) { em_config_ = p; });
    } else if (em_row_ == roms + 2) {
        open_browser(tr("Dossier des sauvegardes"), em_saves_.empty() ? "/data" : em_saves_, false, [this](const std::string& p) { em_saves_ = p; });
    } else if (em_row_ == roms + 3) {
        std::string body = "{\"roms\":[";
        for (std::size_t i = 0; i < em_roms_.size(); ++i) body += (i ? "," : "") + json_str(em_roms_[i]);
        body += "],\"config\":" + json_str(em_config_ == em_config_default_ ? std::string() : em_config_) + ",\"saves\":" + json_str(em_saves_) + "}";
        model_.command("POST", "/api/v2/emulators/" + em_id_, body, [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Réglages enregistrés : la collection est relue") : e);
        });
        em_open_ = false;
    } else {
        model_.command("POST", "/api/v2/emulators/" + em_id_ + "/reset", "{}", [this](const std::string& e, const std::string&) {
            post_toast(e.empty() ? tr("Retour à l'automatique : la collection est relue") : e);
        });
        em_open_ = false;
    }
}

void Ui::draw_emulator(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1160, x = (1920 - w) / 2, y = 130, roms = static_cast<int>(em_roms_.size());
    const int h = std::min(830, 124 + (roms + 3) * 62 + 110);
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, color::panel, 30);
    s.outline(x, y, w, h, color::line, 30, 2);
    s.text(font_, x + 40, y + 30, trf("Régler %s", em_name_.c_str()), large, color::text, w - 80);
    s.text(font_, x + 40, y + 78, tr("Vos choix remplacent la détection automatique. « Revenir à l'automatique » les efface."), small, color::muted, w - 80);
    int yy = y + 124;
    auto row = [&](int i, const std::string& label, const std::string& value, std::uint32_t vc) {
        const bool on = em_row_ == i;
        s.rect(x + 36, yy, w - 72, 56, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
        if (on) s.outline(x + 36, yy, w - 72, 56, color::accent, 14, 3);
        s.text(font_, x + 60, yy + 13, label, body, color::muted);
        s.text(font_, x + 360, yy + 13, value, body, vc, w - 430);
        yy += 62;
    };
    for (int i = 0; i < roms; ++i) row(i, trf("Dossier de jeux %d", i + 1), em_roms_[static_cast<std::size_t>(i)], color::text);
    row(roms, tr("+ Ajouter un dossier"), tr("choisir sur la console"), color::accent);
    row(roms + 1, tr("Fichier de réglages"), em_config_.empty() ? "—" : em_config_, color::text);
    row(roms + 2, tr("Sauvegardes"), em_saves_.empty() ? tr("automatique") : em_saves_, em_saves_.empty() ? color::muted : color::text);
    const char* acts[2] = {tr("Enregistrer"), tr("Revenir à l'automatique")};
    int ax = x + 36;
    for (int a = 0; a < 2; ++a) {
        const bool on = em_row_ == roms + 3 + a;
        const int aw = Surface::measure(acts[a], body) + 44;
        s.rect(ax, yy + 12, aw, 56, on ? color::text : a == 0 ? rgb(61, 139, 255, 140) : rgb(255, 255, 255, 22), 28);
        s.text(font_, ax + 22, yy + 25, acts[a], body, on ? color::bg : color::text);
        ax += aw + 14;
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);
    if (em_row_ < roms) draw_footer(s, {{0, tr("Changer le dossier")}, {2, tr("Retirer")}, {1, tr("Fermer sans enregistrer")}});
    else draw_footer(s, {{0, tr("Choisir / valider")}, {-1, tr("Haut / bas")}, {1, tr("Fermer sans enregistrer")}});
}

// ---- Explorateur de dossiers ----

void Ui::open_browser(std::string title, std::string start, bool files, std::function<void(const std::string&)> pick) {
    br_open_ = true;
    br_title_ = std::move(title);
    br_files_ = files;
    br_remote_.clear();
    br_config_ = Json();
    br_view_ = false;
    br_pick_ = std::move(pick);
    browse_to(start.empty() ? "/" : start);
}

void Ui::browse_to(const std::string& path) {
    br_path_ = path;
    br_sel_ = br_top_ = 0;
    br_loading_ = true;
    br_entries_ = pbc::Json();
    const bool cfg = br_config_.is_object();
    std::string body_in;
    if (cfg) { Json b = br_config_; b.set("path", path); body_in = b.dump(); }
    const std::string url = cfg ? "/api/v2/remotes/browse" : br_remote_.empty() ? "/api/v2/fs/list?path=" + url_encode(path)
                          : br_remote_.rfind("peer:", 0) == 0 ? "/api/v2/link/peer-list?instance=" + br_remote_.substr(5) + "&path=" + url_encode(path)  // autre PS5
                          : "/api/v2/remotes/" + br_remote_ + "/list?path=" + url_encode(path);
    model_.command(cfg ? "POST" : "GET", url, body_in, [this, path](const std::string& e, const std::string& body) {
        Json j;
        std::lock_guard<std::mutex> l(async_mu_);
        if (e.empty() && Json::parse(body, j)) pending_br_ = j;
        else { pending_br_ = Json::object(); pending_br_.set("error", e.empty() ? std::string(tr("dossier illisible")) : e); }
        has_pending_br_ = true;
    });
}

void Ui::input_browser(std::uint32_t k) {
    auto parent = [&] {
        if (br_path_ == "/" || br_path_.empty()) { br_open_ = false; return; }
        const auto sl = br_path_.rfind('/');
        browse_to(sl == 0 || sl == std::string::npos ? "/" : br_path_.substr(0, sl));
    };
    if (k & key::triangle) { br_open_ = false; return; }
    if (k & key::circle) { parent(); return; }
    if (br_loading_) return;
    const auto& list = br_entries_["entries"];
    const int n = static_cast<int>(list.size());
    if (k & key::up) br_sel_ = std::max(0, br_sel_ - 1);
    if (k & key::down) br_sel_ = std::min(std::max(0, n - 1), br_sel_ + 1);
    if (k & key::r2) br_sel_ = std::min(std::max(0, n - 1), br_sel_ + 10);
    if (k & key::l2) br_sel_ = std::max(0, br_sel_ - 10);
    if ((k & key::square) && !br_view_ && !br_files_ && br_path_ != "/") { if (br_pick_) br_pick_(br_path_); br_open_ = false; return; }
    if ((k & key::cross) && br_sel_ < n) {
        const Json& e = list[static_cast<std::size_t>(br_sel_)];
        const std::string next = (br_path_ == "/" ? "" : br_path_) + "/" + e["name"].as_string();
        if (e["dir"].as_bool()) browse_to(next);
        else if (br_files_ && !br_view_) { if (br_pick_) br_pick_(next); br_open_ = false; }
    }
}

void Ui::draw_browser(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 150));
    const int w = 1100, h = 800, x = (1920 - w) / 2, y = 120;
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, rgb(18, 34, 58), 30);
    s.outline(x, y, w, h, rgb(124, 196, 255, 90), 30, 2);
    s.text(font_, x + 40, y + 28, br_title_, large, color::text, w - 80);
    s.rect(x + 40, y + 80, w - 80, 48, rgb(4, 12, 24, 220), 12);
    s.text(font_, x + 60, y + 89, br_path_, body, color::accent, w - 120);
    const auto& list = br_entries_["entries"];
    const int n = static_cast<int>(list.size()), per = 10;
    if (br_sel_ < br_top_) br_top_ = br_sel_;
    if (br_sel_ >= br_top_ + per) br_top_ = br_sel_ - per + 1;
    int yy = y + 146;
    if (br_loading_) { spinner(s, x + 80, yy + 30); s.text(font_, x + 124, yy + 14, tr("Lecture du dossier…"), body, color::muted); }
    else if (!br_entries_["error"].as_string().empty()) s.text(font_, x + 60, yy + 10, br_entries_["error"].as_string(), body, color::bad, w - 120);
    else if (n == 0) s.text(font_, x + 60, yy + 10, tr("Dossier vide"), body, color::muted);
    for (int i = br_top_; i < n && i < br_top_ + per; ++i) {
        const Json& e = list[static_cast<std::size_t>(i)];
        const bool on = i == br_sel_, dir = e["dir"].as_bool();
        s.rect(x + 40, yy, w - 80, 52, on ? color::panel2 : rgb(255, 255, 255, 8), 12);
        if (on) s.outline(x + 40, yy, w - 80, 52, color::accent, 12, 3);
        // Petit dossier (ou feuille pour un fichier).
        if (dir) { s.rect(x + 60, yy + 16, 30, 22, rgb(240, 184, 103), 4); s.rect(x + 60, yy + 12, 14, 8, rgb(240, 184, 103), 3); }
        else s.rect(x + 64, yy + 12, 22, 28, rgb(185, 203, 224), 3);
        s.text(font_, x + 106, yy + 11, e["name"].as_string(), body, dir || br_files_ || br_view_ ? color::text : color::muted, w - 300);
        if (!dir && e["size"].as_int() > 0) {
            const std::string sz = size_text(static_cast<std::uint64_t>(e["size"].as_int()));
            s.text(font_, x + w - 60 - Surface::measure(sz, small), yy + 15, sz, small, color::muted);
        }
        yy += 58;
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);
    if (br_view_) draw_footer(s, {{0, tr("Ouvrir")}, {1, tr("Dossier parent")}, {3, tr("Fermer")}});
    else if (br_files_) draw_footer(s, {{0, tr("Ouvrir / choisir ce fichier")}, {1, tr("Dossier parent")}, {3, tr("Annuler")}});
    else draw_footer(s, {{0, tr("Ouvrir")}, {2, tr("Choisir ce dossier")}, {1, tr("Dossier parent")}, {3, tr("Annuler")}});
}

// Roue de chargement : 12 points, l'un éclairé qui tourne (l'écran est redessiné tant qu'elle est visible).
void Ui::spinner(Surface& s, int cx, int cy, int r) {
    static const int ox[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50}, oy[12] = {-100, -87, -50, 0, 50, 87, 100, 87, 50, 0, -50, -87};
    const int step = static_cast<int>((now_ / 90) % 12), dot = std::max(6, r / 3);
    for (int i = 0; i < 12; ++i) {
        const int age = (step - i + 12) % 12;
        s.rect(cx + ox[i] * r / 100 - dot / 2, cy + oy[i] * r / 100 - dot / 2, dot, dot, age == 0 ? color::accent : age < 4 ? color::text : color::line, dot / 2);
    }
    animating_ = true;
}

// ---- Plans : depuis la fiche d'un jeu, et éditeur ----

namespace {
// Avec un point : « Jeu » seul est déjà « Jeu » (un jeu), une autre traduction.
const char* const weekday_short[7] = {N_("Lun."), N_("Mar."), N_("Mer."), N_("Jeu."), N_("Ven."), N_("Sam."), N_("Dim.")};
const char* const rule_keys[5] = {"", "ps5_saves", "all_games", "roms", "apps"};
const char* const rule_names[5] = {N_("Jeux choisis"), N_("Toutes mes sauvegardes PS5"), N_("Tous mes jeux"), N_("Toutes mes ROM"), N_("Toutes mes applications")};
bool has_item(const Json& plan, const std::string& key) {
    for (const auto& k : plan["items"].items()) if (k.as_string() == key) return true;
    return false;
}
}  // namespace

void Ui::input_item_plans(std::uint32_t k) {
    const int n = static_cast<int>(plans_.size()) + 1;  // plans existants, puis « Nouveau plan avec ce jeu »
    if (k & key::up) pp_row_ = std::max(0, pp_row_ - 1);
    if (k & key::down) pp_row_ = std::min(n - 1, pp_row_ + 1);
    if (k & key::circle) { pp_open_ = false; return; }
    if (!(k & key::cross)) return;
    if (pp_row_ == n - 1) {
        Json p = Json::object(), items = Json::array();
        items.push(pp_item_);
        std::string title = pp_item_;
        for (const auto& it : snap_->items) if (it.key == pp_item_) title = it.title;
        p.set("name", "Plan · " + title.substr(0, 40));
        p.set("timing", "daily");
        p.set("minute_of_day", 21 * 60);
        p.set("rule", "");
        p.set("items", items);
        p.set("parts", Json::array());
        p.set("keep", 5);
        p.set("enabled", true);
        pp_open_ = false;
        open_plan_editor(p);
        return;
    }
    // Cocher / décocher ce jeu dans le plan choisi (enregistré tout de suite).
    Json p = plans_[static_cast<std::size_t>(pp_row_)];
    Json items = Json::array();
    const bool in = has_item(p, pp_item_);
    for (const auto& x : p["items"].items()) if (x.as_string() != pp_item_) items.push(x);
    if (!in) items.push(pp_item_);
    p.set("items", items);
    plans_[static_cast<std::size_t>(pp_row_)] = p;
    model_.command("POST", "/api/v2/plans", p.dump(), [this, in](const std::string& e, const std::string&) {
        post_toast(!e.empty() ? e : in ? tr("Retiré du plan") : tr("Ajouté au plan"));
        load_plans();
    });
}

void Ui::draw_item_plans(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int n = static_cast<int>(plans_.size()) + 1, w = 1000, h = std::min(800, 190 + n * 70), x = (1920 - w) / 2, y = (1080 - h) / 2;
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    s.text(font_, x + 40, y + 30, tr("Plans programmés de ce jeu"), large, color::text);
    s.text(font_, x + 40, y + 78, tr("Croix coche ou décoche : le jeu est sauvegardé à chaque passage du plan."), small, color::muted, w - 80);
    int yy = y + 120;
    for (int i = 0; i < n && yy < y + h - 70; ++i) {
        const bool on = pp_row_ == i;
        s.rect(x + 36, yy, w - 72, 58, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
        if (on) s.outline(x + 36, yy, w - 72, 58, color::accent, 14, 3);
        if (i < n - 1) {
            const Json& p = plans_[static_cast<std::size_t>(i)];
            const bool in = has_item(p, pp_item_);
            s.rect(x + 56, yy + 17, 24, 24, in ? color::accent2 : color::panel2, 6);
            if (in) s.text(font_, x + 60, yy + 13, "✓", body, color::text);
            s.text(font_, x + 100, yy + 14, p["name"].as_string(), body, color::text, w - 400);
            const std::string rule = p["rule"].as_string();
            if (!rule.empty()) s.text(font_, x + w - 330, yy + 18, tr("inclus par sa règle"), small, color::muted);
        } else s.text(font_, x + 60, yy + 14, tr("+ Nouveau plan avec ce jeu"), body, color::accent);
        yy += 66;
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);
    draw_footer(s, {{0, tr("Cocher / créer")}, {-1, tr("Haut / bas")}, {1, tr("Fermer")}});
}

void Ui::open_plan_editor(Json plan) {
    pe_open_ = true;
    pe_ = std::move(plan);
    pe_row_ = pe_day_ = pe_action_ = 0;
}

void Ui::input_plan_editor(std::uint32_t k) {
    // Lignes : 0 nom, 1 quand, 2 jours, 3 heure, 4 contenu, 5 quoi, 6 destination, 7 versions, 8 actif, 9 actions
    constexpr int rows = 10;
    if (k & key::up) pe_row_ = std::max(0, pe_row_ - 1);
    if (k & key::down) pe_row_ = std::min(rows - 1, pe_row_ + 1);
    if (k & key::circle) { pe_open_ = false; return; }
    const int dir = (k & key::right) ? 1 : (k & key::left) ? -1 : 0;
    const std::string timing = pe_["timing"].as_string().empty() ? "manual" : pe_["timing"].as_string();
    switch (pe_row_) {
        case 0:
            if (k & key::cross) open_keyboard(tr("Nom du plan"), pe_["name"].as_string(), [this](const std::string& t) { pe_.set("name", t); });
            break;
        case 1: {
            static const char* t[3] = {"manual", "daily", "weekly"};
            int i = timing == "daily" ? 1 : timing == "weekly" ? 2 : 0;
            if (dir || (k & key::cross)) { i = (i + (dir ? dir : 1) + 3) % 3; pe_.set("timing", t[i]); }
            if (std::string(t[i]) == "weekly" && pe_["weekdays"].as_int() == 0) pe_.set("weekdays", 1 << 6);  // dimanche par défaut
            break;
        }
        case 2:
            if (dir) pe_day_ = (pe_day_ + dir + 7) % 7;
            if (k & key::cross) { pe_.set("weekdays", pe_["weekdays"].as_int() ^ (1 << pe_day_)); pe_.set("timing", "weekly"); }
            break;
        case 3: {
            int m = static_cast<int>(pe_["minute_of_day"].as_int(8 * 60));
            if (dir) m = (m + dir * 15 + 1440) % 1440;
            if (k & key::cross) m = (m + 60) % 1440;
            pe_.set("minute_of_day", m);
            break;
        }
        case 4: {
            int i = 0;
            for (int r = 0; r < 5; ++r) if (pe_["rule"].as_string() == rule_keys[r]) i = r;
            if (dir || (k & key::cross)) { i = (i + (dir ? dir : 1) + 5) % 5; pe_.set("rule", rule_keys[i]); }
            if (i == 1) {  // sauvegardes PS5 d'un compte : celui de la console
                for (const auto& a : overview_["accounts"].items()) if (a["on_console"].as_bool()) { pe_.set("rule_arg", a["user_id"].as_string()); break; }
            }
            break;
        }
        case 5: {  // quoi : tout / sauvegardes seules / jeu seul
            const Json& parts = pe_["parts"];
            int i = parts.size() == 0 ? 0 : parts[0].as_string() == "save" ? 1 : 2;
            if (dir || (k & key::cross)) {
                i = (i + (dir ? dir : 1) + 3) % 3;
                Json np = Json::array();
                if (i == 1) np.push("save");
                if (i == 2) np.push("game");
                pe_.set("parts", np);
            }
            break;
        }
        case 6: {  // destination : par défaut, ou un disque / appareil réseau
            std::vector<std::string> ids{""};
            for (const auto& p : snap_->places) if (p.type != "switch") ids.push_back(p.id);
            int i = 0;
            for (std::size_t j = 0; j < ids.size(); ++j) if (ids[j] == pe_["place"].as_string()) i = static_cast<int>(j);
            if (dir || (k & key::cross)) { i = (i + (dir ? dir : 1) + static_cast<int>(ids.size())) % static_cast<int>(ids.size()); pe_.set("place", ids[static_cast<std::size_t>(i)]); }
            break;
        }
        case 7: {
            int keep = static_cast<int>(pe_["keep"].as_int(5));
            if (dir) keep = std::max(0, std::min(30, keep + dir));
            pe_.set("keep", keep);
            break;
        }
        case 8:
            if (dir || (k & key::cross)) pe_.set("enabled", !pe_["enabled"].as_bool(true));
            break;
        default: {
            const int actions = pe_["id"].as_string().empty() ? 1 : 3;
            if (dir) pe_action_ = std::max(0, std::min(actions - 1, pe_action_ + dir));
            if (!(k & key::cross)) break;
            if (pe_action_ == 0) {
                model_.command("POST", "/api/v2/plans", pe_.dump(), [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Plan enregistré") : e); load_plans(); });
                pe_open_ = false;
            } else if (pe_action_ == 1) {
                model_.command("POST", "/api/v2/plans/" + pe_["id"].as_string() + "/run", "{}", [this](const std::string& e, const std::string& body) {
                    Json j;
                    post_toast(e.empty() && Json::parse(body, j) ? trf("%lld sauvegarde(s) ajoutée(s) aux tâches", static_cast<long long>(j["queued"].as_int())) : e);
                });
            } else {
                model_.command("POST", "/api/v2/plans/" + pe_["id"].as_string() + "/delete", "{}", [this](const std::string& e, const std::string&) { post_toast(e.empty() ? tr("Plan supprimé") : e); load_plans(); });
                pe_open_ = false;
                tasks_row_ = 0;
            }
        }
    }
}

void Ui::draw_plan_editor(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1160, h = 860, x = (1920 - w) / 2, y = 110;
    s.rect(x + 10, y + 14, w, h, rgb(0, 0, 0, 120), 30);
    s.rect(x, y, w, h, color::panel, 30);
    s.outline(x, y, w, h, color::line, 30, 2);
    s.text(font_, x + 40, y + 26, pe_["id"].as_string().empty() ? tr("Nouveau plan") : tr("Modifier le plan"), large, color::text);
    const std::string timing = pe_["timing"].as_string().empty() ? "manual" : pe_["timing"].as_string();
    const int m = static_cast<int>(pe_["minute_of_day"].as_int(8 * 60));
    char hour[16];
    std::snprintf(hour, sizeof hour, tr("%02d h %02d"), m / 60, m % 60);
    std::string rule = tr(rule_names[0]);
    for (int r = 0; r < 5; ++r) if (pe_["rule"].as_string() == rule_keys[r]) rule = tr(rule_names[r]);
    if (pe_["rule"].as_string().empty()) rule += " (" + std::to_string(pe_["items"].size()) + ")";
    const Json& parts = pe_["parts"];
    const std::string what = parts.size() == 0 ? tr("Tout (jeu et sauvegarde)") : parts[0].as_string() == "save" ? tr("Sauvegardes seulement") : tr("Jeu seulement");
    std::string dest = tr("Par défaut");
    for (const auto& p : snap_->places) if (p.id == pe_["place"].as_string()) dest = p.label;
    const int keep = static_cast<int>(pe_["keep"].as_int(5));
    const std::string vals[9] = {pe_["name"].as_string(), timing == "daily" ? tr("Chaque jour") : timing == "weekly" ? tr("Chaque semaine") : tr("À la demande"), "", hour, rule, what, dest,
                                 keep ? trf("%d versions gardées", keep) : std::string(tr("Toutes les versions")), pe_["enabled"].as_bool(true) ? tr("Oui") : tr("En pause")};
    const char* labels[9] = {tr("Nom"), tr("Quand"), tr("Jours"), tr("Heure"), tr("Contenu"), tr("Sauvegarder"), tr("Destination"), tr("Versions"), tr("Actif")};
    int yy = y + 82;
    for (int i = 0; i < 9; ++i) {
        const bool on = pe_row_ == i;
        const bool dim = (i == 2 && timing != "weekly") || ((i == 2 || i == 3) && timing == "manual");
        s.rect(x + 36, yy, w - 72, 58, on ? color::panel2 : rgb(255, 255, 255, 10), 14);
        if (on) s.outline(x + 36, yy, w - 72, 58, color::accent, 14, 3);
        s.text(font_, x + 60, yy + 14, labels[i], body, dim ? rgb(90, 108, 130) : color::muted);
        if (i == 2) {  // sept pastilles de jours
            int dx = x + 300;
            const unsigned wd = static_cast<unsigned>(pe_["weekdays"].as_int());
            for (int d = 0; d < 7; ++d) {
                const bool set = wd & (1u << d), cur = on && pe_day_ == d;
                s.rect(dx, yy + 9, 80, 40, set ? color::accent2 : rgb(255, 255, 255, 18), 20);
                if (cur) s.outline(dx - 3, yy + 6, 86, 46, color::text, 23, 2);
                s.text(font_, dx + (80 - Surface::measure(tr(weekday_short[d]), small)) / 2, yy + 17, tr(weekday_short[d]), small, set ? color::text : color::muted);
                dx += 92;
            }
        } else {
            s.text(font_, x + 300, yy + 14, vals[i], body, dim ? rgb(90, 108, 130) : color::text, w - 420);
            if (on && i != 0) s.text(font_, x + w - 110, yy + 14, "‹  ›", body, color::accent);
        }
        yy += 64;
    }
    const char* acts[3] = {tr("Enregistrer"), tr("Lancer maintenant"), tr("Supprimer")};
    const int actions = pe_["id"].as_string().empty() ? 1 : 3;
    int ax = x + 36;
    for (int a = 0; a < actions; ++a) {
        const bool on = pe_row_ == 9 && pe_action_ == a;
        const int aw = Surface::measure(acts[a], body) + 44;
        s.rect(ax, yy + 14, aw, 56, on ? color::text : a == 0 ? rgb(61, 139, 255, 140) : a == 2 ? rgb(255, 123, 123, 60) : rgb(255, 255, 255, 22), 28);
        s.text(font_, ax + 22, yy + 27, acts[a], body, on ? color::bg : color::text);
        ax += aw + 14;
    }
    s.rect(0, 996, 1920, 84, color::bg, 0);
    if (pe_row_ == 2) draw_footer(s, {{-1, tr("Gauche / droite : jour")}, {0, tr("Cocher le jour")}, {1, tr("Fermer sans enregistrer")}});
    else draw_footer(s, {{-1, tr("Gauche / droite : changer")}, {0, tr("Valider")}, {1, tr("Fermer sans enregistrer")}});
}

void Ui::load_remotes() {
    switches_loaded_ = now_;
    model_.command("GET", "/api/v2/remotes", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) return;
        std::vector<std::pair<std::string, std::string>> list;
        for (const auto& r : j.items()) if (r["type"].as_string() == "switch") list.push_back({r["id"].as_string(), r["name"].as_string()});
        std::lock_guard<std::mutex> l(async_mu_);
        pending_remotes_all_ = j;
        pending_switches_ = std::move(list);
        has_pending_switches_ = true;
    });
    model_.command("GET", "/api/v2/eden/profiles", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); eden_profiles_ = j; }
    });
}

// ---- Parties : « Où en sont mes parties ? » -------------------------------------------------------
// En haut, une carte par joueur (compte PS5 de la console et des backups, chaque Switch, Eden / ProsperoEden),
// en dessous les jeux du joueur choisi en grandes jaquettes ; pour une Switch, la comparaison avec Eden.
namespace {
const char* const party_views[4] = {N_("Tout"), N_("À sauvegarder"), N_("Sauvegarde plus récente"), N_("Jamais sauvegardée")};
}

int Ui::pt_players() const { return static_cast<int>(overview_["accounts"].size()) + static_cast<int>(switches_.size()) + 1; }

std::vector<std::size_t> Ui::pt_rows() const {
    std::vector<std::size_t> rows;
    const auto& accs = overview_["accounts"];
    if (pt_player_ >= static_cast<int>(accs.size())) return rows;
    const std::string acc = accs[static_cast<std::size_t>(pt_player_)]["key"].as_string();
    const auto& ents = overview_["entries"];
    for (std::size_t i = 0; i < ents.size(); ++i) {
        const Json& e = ents[i];
        if (e["account"].as_string() != acc) continue;
        const std::string st = e["status"].as_string();
        const bool keep = pt_view_ == 0 || (pt_view_ == 1 && (st == "console_newer" || st == "not_backed_up")) || (pt_view_ == 2 && st == "backup_newer") ||
                          (pt_view_ == 3 && st == "not_backed_up");
        if (keep) rows.push_back(i);
    }
    // À sauvegarder d'abord, puis par titre.
    auto rank = [&](std::size_t i) { const std::string st = ents[i]["status"].as_string(); return st == "console_newer" || st == "not_backed_up" ? 0 : st == "backup_newer" ? 1 : 2; };
    std::sort(rows.begin(), rows.end(), [&](std::size_t x, std::size_t y) { return rank(x) != rank(y) ? rank(x) < rank(y) : ents[x]["title"].as_string() < ents[y]["title"].as_string(); });
    return rows;
}

void Ui::draw_parties(Surface& s) {
    {   // ce que montre l'onglet, en une phrase (à côté du titre)
        const int tw = s.text(font_, margin, 112, tr("Parties"), title, color::text);
        s.text(font_, margin + tw + 28, 140, tr("Les parties actives, par joueur, en vraie synchro : PS5 ↔ PS5, Switch ↔ émulateur, Switch ↔ Switch. À sauvegarder, à restaurer, à nettoyer."), small, color::muted, 1920 - 2 * margin - tw - 28 - 200);
    }
    if (!overview_loaded_ || now_ - overview_loaded_ > 5000) load_overview();
    if (!switches_loaded_ || now_ - switches_loaded_ > 15000) load_remotes();
    const auto& accs = overview_["accounts"];
    const int accounts = static_cast<int>(accs.size()), switches = static_cast<int>(switches_.size()), n = pt_players();
    pt_player_ = std::clamp(pt_player_, 0, n - 1);
    saves_zone_ = pt_zone_;  // la vue Synchro (draw_sync) suit la même mise en évidence
    if (pt_player_ < accounts) saves_acc_ = pt_player_;
    // Cartes des joueurs.
    const int cw = 400, ch = 120, gap = 20, cy = 196;
    const int per = (1920 - 2 * margin + gap) / (cw + gap), first = std::clamp(pt_player_ - per + 1, 0, std::max(0, n - per));
    for (int i = first; i < n && i < first + per; ++i) {
        const int x = margin + (i - first) * (cw + gap);
        const bool sel = i == pt_player_, focus = sel && pt_zone_ == 0;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 5, cy - g * 5, cw + g * 10, ch + g * 10, rgb(80, 150, 255, 20), 22 + g * 5);
        s.rect(x, cy, cw, ch, sel ? color::panel2 : color::panel, 20);
        s.outline(x, cy, cw, ch, focus ? color::accent : sel ? color::accent2 : color::line, 20, focus ? 4 : 2);
        std::string name, sub, state;
        std::uint32_t tone = color::muted;
        int dev = dev_consoles;
        if (i < accounts) {
            const Json& a = accs[static_cast<std::size_t>(i)];
            name = a["name"].as_string();
            sub = a["platform"].as_string() + " · " + (a["on_console"].as_bool() ? tr("cette console") : tr("autre console"));
            dev = a["on_console"].as_bool() || a["platform"].as_string() == "PS5" ? dev_internal : dev_consoles;  // un compte PS5, ici ou ailleurs : la PS5
            int todo = 0;
            for (const auto& e : overview_["entries"].items())
                if (e["account"].as_string() == a["key"].as_string() && (e["status"].as_string() == "console_newer" || e["status"].as_string() == "not_backed_up")) ++todo;
            state = todo ? trf("%d jeu(x) à sauvegarder", todo) : std::string(tr("À jour"));
            tone = todo ? color::warn : color::ok;
        } else if (i < accounts + switches) {
            name = switches_[static_cast<std::size_t>(i - accounts)].second;
            sub = "Switch";
            dev = dev_switch;
            state = tr("Synchro avec ProsperoEden");
            tone = color::accent;
        } else {
            name = "Eden / ProsperoEden";
            sub = tr("Émulateur Switch sur la PS5");
            dev = dev_switch;
            state = switches ? std::string(tr("Comparé avec chaque Switch")) : std::string(tr("Aucune Switch ajoutée"));
        }
        if (devices_[dev].valid()) s.image(devices_[dev], x + 12, cy + 12, 96, 96);
        s.text(font_, x + 122, cy + 12, name, body, color::text, cw - 140);
        s.text(font_, x + 122, cy + 48, sub, small, color::muted, cw - 140);
        s.text(font_, x + 122, cy + 80, state, small, tone, cw - 140);
    }
    if (n > per) s.text(font_, 1920 - margin - 30, cy + ch / 2 - 18, "›", large, color::muted);
    const int x = margin, w = 1920 - 2 * margin;
    if (pt_player_ >= accounts && pt_player_ < accounts + switches) {  // une Switch : la comparaison avec Eden, sous les cartes
        sync_dy_ = 150;
        draw_sync(s, switches_[static_cast<std::size_t>(pt_player_ - accounts)], x, w);
        sync_dy_ = 0;
        return;
    }
    if (pt_player_ >= accounts) {
        s.text(font_, x, 380, tr("Les parties d'Eden se comparent avec une Switch : choisissez sa carte."), large, color::muted, w);
        draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("Gauche / droite : joueurs")}});
        return;
    }
    // Vues (Carré).
    int px = x;
    for (int i = 0; i < 4; ++i) {
        const int pw = Surface::measure(tr(party_views[i]), small) + 32;
        if (i == pt_view_) s.rect(px, 344, pw, 38, color::text, 19);
        s.text(font_, px + 16, 350, tr(party_views[i]), small, i == pt_view_ ? color::bg : color::muted);
        px += pw + 6;
    }
    const auto rows = pt_rows();
    const int nr = static_cast<int>(rows.size());
    pt_row_ = std::min(pt_row_, std::max(0, nr - 1));
    if (nr) { const std::string pos = std::to_string(pt_row_ + 1) + " / " + std::to_string(nr); s.text(font_, x + w - Surface::measure(pos, small), 352, pos, small, color::muted); }
    // Grandes jaquettes : 8 par rangée, deux rangées visibles.
    constexpr int cs = 180, cgap = 36, cols = 8, rh = 290, gy = 404;
    const int top_row = std::max(0, pt_row_ / cols - 1);
    const auto& ents = overview_["entries"];
    for (int r = top_row * cols; r < nr && r < (top_row + 2) * cols; ++r) {
        const Json& e = ents[rows[static_cast<std::size_t>(r)]];
        const int gx = x + (r % cols) * (cs + cgap), yy = gy + (r / cols - top_row) * rh;
        const bool focus = pt_zone_ == 1 && r == pt_row_;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(gx - g * 5, yy - g * 5, cs + g * 10, cs + g * 10, rgb(80, 150, 255, 22), 22 + g * 5);
        {
            Item stand_in;
            const Item* found = find_or_stand_in(e["item"].as_string(), e["title"].as_string(), e["title_id"].as_string(), stand_in);
            cover(s, *found, gx, yy, cs, true);
        }
        if (pt_sel_.count(e["account"].as_string() + "|" + e["item"].as_string())) {  // coché (L2)
            s.rect(gx + cs - 44, yy + 8, 36, 36, color::accent2, 18);
            for (int d = 0; d < 7; ++d) s.rect(gx + cs - 35 + d, yy + 25 + d / 2, 4, 4, color::text, 2);
            for (int d = 0; d < 10; ++d) s.rect(gx + cs - 29 + d, yy + 29 - d, 4, 4, color::text, 2);
        }
        if (focus) s.outline(gx - 5, yy - 5, cs + 10, cs + 10, color::accent, 22, 4);
        s.text(font_, gx, yy + cs + 8, e["title"].as_string(), small, focus ? color::text : color::muted, cs);
        const std::string st = e["status"].as_string();
        const char* label = st == "up_to_date" ? tr("À jour") : st == "console_newer" || st == "not_backed_up" ? tr("À sauvegarder") : st == "backup_newer" ? tr("Sauvegarde plus récente") : st == "system" ? tr("Données système") : tr("Seulement en sauvegarde");
        if (st == "not_backed_up") label = tr("Jamais sauvegardée");
        const std::uint32_t tone = st == "up_to_date" ? color::ok : st == "backup_newer" || st == "console_newer" || st == "not_backed_up" ? color::warn : color::muted;
        const int lw = std::min(cs, Surface::measure(label, small) + 20);
        s.rect(gx, yy + cs + 38, lw, 30, (tone & 0x00FFFFFFu) | 0x30000000u, 15);
        s.text(font_, gx + 10, yy + cs + 40, label, small, tone, cs - 20);
        std::string adv = e["console"]["progress"]["text"].as_string();
        const std::int64_t pts = e["console"]["progress"]["play_time_seconds"].as_int(-1);
        if (pts > 0 && adv.find(':') == std::string::npos) adv += (adv.empty() ? "" : " · ") + trf("%s de jeu", hours_text(pts).c_str());
        if (!adv.empty()) s.text(font_, gx, yy + cs + 74, adv, small, color::accent, cs);
    }
    if (rows.empty()) s.text(font_, x, 440, overview_.is_object() ? tr("Rien dans ce filtre.") : tr("Lecture des sauvegardes…"), large, color::muted);
    if (!pt_sel_.empty()) draw_checked(s, static_cast<int>(pt_sel_.size()));
    const std::string next = trf("Voir : %s", tr(party_views[(pt_view_ + 1) % 4]));
    if (pt_zone_ == 0) draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("Gauche / droite : joueurs")}, {2, next.c_str()}, {3, tr("Tout sauvegarder")}});
    else draw_footer(s, {{0, tr("Sauvegarder / restaurer")}, {-1, tr("L2 : cocher")}, {-1, tr("Options : actions")}, {2, next.c_str()}, {3, tr("Tout sauvegarder")}});
}

void Ui::input_parties(std::uint32_t k) {
    const int accounts = static_cast<int>(overview_["accounts"].size()), switches = static_cast<int>(switches_.size()), n = pt_players();
    const bool sw = pt_player_ >= accounts && pt_player_ < accounts + switches;
    if (pt_zone_ == 0) {
        if (k & key::left) { pt_player_ = std::max(0, pt_player_ - 1); pt_row_ = saves_row_ = 0; }
        if (k & key::right) { pt_player_ = std::min(n - 1, pt_player_ + 1); pt_row_ = saves_row_ = 0; }
        if ((k & key::square) && !sw) { pt_view_ = (pt_view_ + 1) % 4; pt_row_ = 0; }
        if ((k & key::square) && sw && !sync_menu_ && !sync_stage_) { sync_filter_ = (sync_filter_ + 1) % 4; saves_row_ = 0; }
        if ((k & key::triangle) && pt_player_ < accounts) { saves_acc_ = pt_player_; save_entries(pt_rows()); }
        if ((k & (key::down | key::cross)) && pt_player_ < accounts + switches) pt_zone_ = 1;
        return;
    }
    if (sw) {  // une Switch : la synchronisation existante
        if (!sync_menu_ && !sync_stage_) {
            if (k & key::square) { sync_filter_ = (sync_filter_ + 1) % 4; saves_row_ = 0; return; }
            if ((k & (key::circle | key::left)) || ((k & key::up) && saves_row_ == 0)) { pt_zone_ = 0; return; }
        }
        input_sync(k);
        return;
    }
    saves_acc_ = pt_player_;
    const auto rows = pt_rows();
    const int nr = static_cast<int>(rows.size()), cols = 8;
    if (k & key::left) pt_row_ = std::max(0, pt_row_ - 1);
    if (k & key::right) pt_row_ = std::min(std::max(0, nr - 1), pt_row_ + 1);
    if (k & key::down) pt_row_ = std::min(std::max(0, nr - 1), pt_row_ + cols);
    if (k & key::up) { if (pt_row_ < cols) { pt_zone_ = 0; return; } pt_row_ -= cols; }
    if (k & key::circle) { pt_zone_ = 0; return; }
    if (k & key::square) { pt_view_ = (pt_view_ + 1) % 4; pt_row_ = 0; return; }
    if (k & key::triangle) { save_entries(rows); return; }
    if (pt_row_ >= nr) return;
    const std::size_t idx = rows[static_cast<std::size_t>(pt_row_)];
    const Json& e = overview_["entries"][idx];
    const std::string key2 = e["account"].as_string() + "|" + e["item"].as_string();
    if (k & key::l2) { if (!pt_sel_.erase(key2)) pt_sel_.insert(key2); return; }
    if (k & key::options) {  // actions : sur les jeux cochés, sinon sur celui-ci
        std::vector<std::size_t> targets;
        if (!pt_sel_.empty()) { for (std::size_t i = 0; i < overview_["entries"].size(); ++i) { const Json& x = overview_["entries"][i]; if (pt_sel_.count(x["account"].as_string() + "|" + x["item"].as_string())) targets.push_back(i); } }
        else targets.push_back(idx);
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        acts.push_back({targets.size() > 1 ? trf("Sauvegarder maintenant (%d)", static_cast<int>(targets.size())) : std::string(tr("Sauvegarder maintenant")), [this, targets] { save_entries(targets); }});
        if (targets.size() == 1) acts.push_back({tr("Toutes les versions"), [this, idx] { const Json& x = overview_["entries"][idx]; open_versions(x["account"].as_string(), x["title_id"].as_string(), x["title"].as_string(), x["item"].as_string()); }});
        if (targets.size() == 1 && e["console"].is_object() && e["account"].as_string().rfind("ps5:", 0) == 0) {
            const std::string item = e["item"].as_string(), title = e["title"].as_string(), uid = e["account"].as_string().substr(4);
            acts.push_back({tr("Envoyer la sauvegarde à une PS5…"), [this, item, title, uid] { send_save_to_peer(item, title, uid); }});
        }
        if (targets.size() == 1 && e["console"].is_object()) {
            const std::string tid = e["title_id"].as_string(), title = e["title"].as_string();
            acts.push_back({tr("Supprimer la partie de la console…"), [this, tid, title] { delete_console_save(tid, title); }});
        }
        acts.push_back({tr("Ignorer…"), [this, targets] {
            std::vector<IgnTarget> t;
            for (auto i : targets) { const Json& x = overview_["entries"][i]; if (!x["title_id"].as_string().empty()) t.push_back({"title_id", x["title_id"].as_string(), x["title"].as_string()}); }
            open_ignore(std::move(t), -1);
        }});
        acts.push_back({pt_sel_.empty() ? tr("Tout cocher") : tr("Tout décocher"), [this] {
            if (!pt_sel_.empty()) { pt_sel_.clear(); return; }
            for (auto i : pt_rows()) { const Json& x = overview_["entries"][i]; pt_sel_.insert(x["account"].as_string() + "|" + x["item"].as_string()); }
        }});
        open_actions(targets.size() > 1 ? trf("%d jeux cochés", static_cast<int>(targets.size())) : e["title"].as_string(), std::move(acts));
        return;
    }
    if (k & key::cross) {  // menu du jeu : sauvegarder maintenant, ou restaurer une version
        rs_entry_ = e;
        rs_index_ = idx;
        std::vector<std::string> opts;
        if (e["console"].is_object()) opts.push_back(tr("Sauvegarder maintenant"));
        int versions = 0;
        for (const auto& v : e["backups"].items()) if (v["complete"].as_bool()) ++versions;
        if (versions && e["account"].as_string().rfind("ps5:", 0) == 0) opts.push_back(trf("Restaurer une version… (%d)", versions));
        if (opts.empty()) { toast(tr("Rien à faire pour ce jeu ici")); return; }
        if (opts.size() == 1 && e["console"].is_object()) { save_entries({idx}); return; }
        rs_title_ = e["title"].as_string();
        open_popup(6, rs_title_, opts, 0);
    }
}

// Menu d'actions (Options) : une liste, chaque entrée lance sa fonction.
std::string Ui::compat_state_text(const Json& c) const {
    const std::string st = c["state"].as_string();
    const std::string fw = c["firmware"].as_string();
    const std::string who = c["console"].as_string().empty() ? std::string(tr("cette console")) : c["console"].as_string();
    const char* label = st == "system_ok" ? tr("Compatible : système suffisant") : st == "verified" ? tr("Compatibilité vérifiée")
                      : st == "backport_present" ? tr("Backport nécessaire : présent") : st == "no_backport" ? tr("Risque de ne pas fonctionner : aucun backport")
                      : st == "incompatible_known" ? tr("Incompatibilité connue") : tr("Informations insuffisantes");
    return fw.empty() ? trf("%s (%s)", label, who.c_str()) : trf("%s (%s, système %s)", label, who.c_str(), fw.c_str());
}

std::string Ui::compat_backport_text(const Json& c) const {
    const Json& bp = c["backport"];
    std::string out;
    const int n = static_cast<int>(bp["game_libs"].items().size());
    if (n) out = bp["game_libs_sdk"].as_string().empty() ? trf("fakelib du jeu, %d bibliothèque(s)", n) : trf("fakelib du jeu, %d bibliothèque(s), SDK %s", n, bp["game_libs_sdk"].as_string().c_str());
    for (const auto& b : bp["console"].items()) out += (out.empty() ? "" : " · ") + trf("backport ShadowMountPlus sur la console : %d fichier(s)", static_cast<int>(b["files"].items().size()));
    if (bp["apr"].is_object()) out += (out.empty() ? "" : " · ") + std::string(bp["apr"]["index"].as_bool() ? tr("APR-EMU (index présent)") : tr("APR-EMU (sans index)"));
    return out;
}

std::string Ui::compat_reasons_text(const Json& c, std::size_t max) const {
    std::vector<std::string> out;
    for (const auto& r : c["reasons"].items()) {
        const std::string code = r["code"].as_string();
        if (code == "declared") out.push_back(trf("le jeu déclare le système %s", r["declared"].as_string().c_str()));
        else if (code == "exe_sdk") out.push_back(trf("exécutables : SDK %s", r["sdk"].as_string().c_str()));
        else if (code == "exe_signed") out.push_back(trf("%d exécutable(s) signé(s), SDK non lisible", static_cast<int>(r["count"].as_int())));
        else if (code == "own_libs") out.push_back(r["sdk"].as_string().empty() ? trf("backport du jeu (fakelib) : %d bibliothèque(s)", static_cast<int>(r["count"].as_int()))
                                                                            : trf("backport du jeu (fakelib) : %d bibliothèque(s), SDK %s", static_cast<int>(r["count"].as_int()), r["sdk"].as_string().c_str()));
        else if (code == "backport") out.push_back(trf("backport ShadowMountPlus sur la console : %d fichier(s)", static_cast<int>(r["count"].as_int())));
        else if (code == "smp_fakelib") out.push_back(r["on"].as_bool() ? tr("backport fakelib activé dans ShadowMountPlus") : tr("backport fakelib désactivé dans ShadowMountPlus"));
        else if (code == "format_failed") out.push_back(trf(".%s ne s'est pas monté sur cette console : image non compressée conseillée", r["ext"].as_string().c_str()));
        else if (code == "unreadable") out.push_back(tr("image non lisible ici"));
        else if (code == "no_firmware") out.push_back(tr("système de la console inconnu"));
        if (out.size() >= max) break;
    }
    std::string s;
    for (const auto& x : out) s += (s.empty() ? "" : " · ") + x;
    return s;
}

void Ui::system_check_then(const std::string& path, const Json& body, const std::string& title, std::function<void()> then) {
    model_.command("POST", path, body.dump(), [this, title, then](const std::string& e, const std::string& resp) {
        Json j;
        if (!e.empty() || !Json::parse(resp, j) || j["ok"].as_bool(true)) { post_ui(then); return; }  // inconnu ou suffisant : on n'empêche rien
        post_ui([this, title, then, j] {
            // Constat, pas un verdict : le minimum déclaré n'est qu'un indice (un backport peut suffire).
            const std::string st = j["state"].as_string();
            const std::string who = j["console"].as_string().empty() ? std::string(tr("cette console")) : j["console"].as_string();
            open_actions(compat_state_text(j), {{tr("Continuer quand même"), then}, {tr("Annuler"), [] {}}});
            std::string note = trf("« %s » demande le système %s ; %s est en %s.", title.c_str(), j["required"].as_string().c_str(), who.c_str(), j["firmware"].as_string().c_str());
            if (st == "backport_present") note += " " + trf("Il faut un backport, et il existe : %s.", compat_backport_text(j).c_str());
            else if (st == "no_backport") note += " " + std::string(tr("Aucun backport trouvé : le jeu risque de ne pas fonctionner. PS5 Center pourra aider (fonctions expérimentales de backport)."));
            popup_note_ = note;
        });
    });
}

// Titre d'une carte d'appareil : en grand s'il tient, sinon un cran plus petit (rien ne sort du cadre).
void Ui::card_title(Surface& s, int x, int y, const std::string& t, int max_w) {
    if (Surface::measure(t, large) <= max_w) s.text(font_, x, y + 22, t, large, color::text, max_w);
    else s.text(font_, x, y + 30, t, body, color::text, max_w);
}

void Ui::open_hold_confirm(std::string title, std::string note, std::function<void()> fn) {
    hold_title_ = std::move(title);
    hold_note_ = std::move(note);
    hold_fn_ = std::move(fn);
    hold_since_ = 0;
    hold_open_ = true;
}

void Ui::draw_hold_confirm(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int w = 1200, x = (1920 - w) / 2;
    std::vector<std::string> lines;  // titre et explication, coupés aux mots
    auto wrap = [&](const std::string& text, FontSize font_size, int width) {
        std::vector<std::string> out;
        std::string line, word;
        auto flush = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && Surface::measure(t, font_size) > width) { out.push_back(line); line = word; } else line = t;
            word.clear();
        };
        for (char c : text) { if (c == ' ') flush(); else word += c; }
        flush();
        if (!line.empty()) out.push_back(line);
        return out;
    };
    const auto title = wrap(hold_title_, large, w - 80), note = wrap(hold_note_, small, w - 80);
    const int h = 120 + static_cast<int>(title.size()) * 54 + static_cast<int>(note.size()) * 36 + 96, y = (1080 - h) / 2;
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::bad, 28, 3);
    int yy = y + 30;
    for (const auto& l : title) { s.text(font_, x + 40, yy, l, large, color::bad, w - 80); yy += 54; }
    yy += 10;
    for (const auto& l : note) { s.text(font_, x + 40, yy, l, small, color::muted, w - 80); yy += 36; }
    yy += 24;
    const std::uint64_t held = hold_since_ ? now_ - hold_since_ : 0;
    const int bw = w - 80, fill = static_cast<int>(std::min<std::uint64_t>(held, 5000) * static_cast<std::uint64_t>(bw) / 5000);
    s.rect(x + 40, yy, bw, 22, color::bg2, 11);
    if (fill > 0) s.rect(x + 40, yy, std::max(22, fill), 22, color::bad, 11);
    yy += 44;
    const std::string left = hold_since_ ? trf("Encore %d s…", static_cast<int>((5000 - std::min<std::uint64_t>(held, 5000) + 999) / 1000)) : std::string(tr("Maintenez Carré 5 secondes pour supprimer"));
    icon(s, 2, x + 40, yy, color::text);
    s.text(font_, x + 80, yy - 4, left, body, color::text, w - 120);
    animating_ = true;  // la barre avance à chaque image
    s.rect(0, 990, 1920, 90, color::bg);
    draw_footer(s, {{2, tr("Maintenir : supprimer")}, {1, tr("Annuler")}});
}

const Item* Ui::find_or_stand_in(const std::string& key, const std::string& title, const std::string& title_id, Item& stand_in, const std::string& kind) const {
    for (const auto& it : snap_->items) if (!key.empty() && it.key == key) return &it;
    std::string tid = title_id;
    if (tid.empty() && key.rfind("title:", 0) == 0) tid = key.substr(6);
    if (tid.empty() && key.rfind("tid:ps", 0) == 0 && key.size() > 8) tid = key.substr(8);
    if (!tid.empty()) for (const auto& it : snap_->items) if (it.title_id == tid && it.has_cover) return &it;
    const bool ps4 = tid.rfind("CUSA", 0) == 0 || key.rfind("tid:ps4:", 0) == 0;
    const bool sw = tid.size() == 16 && std::all_of(tid.begin(), tid.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
    if (sw) {  // jeu Switch absent d'ici : jaquette d'Eden de l'autre PS5, sinon l'icône officielle (le moteur s'en charge)
        stand_in = Item{};
        stand_in.key = "tid:switch:" + tid;
        stand_in.title = title;
        stand_in.title_id = tid;
        stand_in.kind = "rom";
        stand_in.platform = "Switch";
        stand_in.has_cover = true;
        return &stand_in;
    }
    stand_in = Item{};
    stand_in.key = ui_util::cover_stand_in_key(key);
    if (stand_in.key.rfind("tid:", 0) != 0 && tid.size() == 9) stand_in.key = std::string(ps4 ? "tid:ps4:" : "tid:ps5:") + tid;
    stand_in.title = title;
    stand_in.title_id = tid;
    // Une application reste une application (ex. Prospero Themes) : jamais étiquetée « jeu ».
    stand_in.kind = kind == "app" ? "app" : ps4 ? "game_ps4" : "game_ps5";
    stand_in.platform = ps4 ? "PS4" : "PS5";
    stand_in.has_cover = stand_in.key.rfind("tid:", 0) == 0;
    return &stand_in;
}

// Recevoir ici un jeu installé sur une autre PS5 : disque d'ici, format, compte qui reçoit la sauvegarde ; l'autre PS5
// l'envoie et il est installé ici (même chemin que « Envoyer à une PS5 », dans l'autre sens).
void Ui::receive_peer_game(const std::string& peer, const std::string& peer_name, const Json& g) {
    const std::string title = g["title"].as_string(), item = g["key"].as_string();
    const std::uint64_t bytes = static_cast<std::uint64_t>(std::max<std::int64_t>(0, g["bytes"].as_int()));
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& p : snap_->places) {
        if (p.type != "internal" && p.type != "usb") continue;
        const std::string disk = p.type == "internal" ? std::string(tr("Interne")) : p.label, place = p.id;
        acts.push_back({trf("Recevoir sur %s · %s libres", disk.c_str(), size_text(p.free).c_str()), [this, peer, peer_name, item, title, place, disk] {
            choose_format(trf("Format de « %s » sur %s", title.c_str(), disk.c_str()), [this, peer, peer_name, item, title, place](const std::string& fmt) {
                pick_account("", tr("cette console"), [this, peer, peer_name, item, title, place, fmt](const std::string& uid, const std::string&) {
                    Json b = Json::object();
                    b.set("peer", peer); b.set("item", item); b.set("place", place); b.set("format", fmt); b.set("user_id", uid);
                    model_.command("POST", "/api/v2/link/pull-game", b.dump(), [this, title, peer_name](const std::string& e, const std::string&) {
                        post_toast(e.empty() ? trf("« %s » arrive de %s (suivi dans Tâches) ; le jeu apparaît sur l'accueil une fois l'application fermée", title.c_str(), peer_name.c_str()) : e);
                    });
                });
            });
        }});
    }
    if (acts.empty()) { toast(tr("Aucun disque pour recevoir ce jeu")); return; }
    open_actions(trf("Recevoir « %s » depuis %s", title.c_str(), peer_name.c_str()), std::move(acts));
    popup_note_ = trf("Installé sur %s, %s. Il sera copié ici et installé ; sa sauvegarde suit, sur le compte choisi.", peer_name.c_str(), size_text(bytes).c_str());
}

bool Ui::has_console_save(const std::string& title_id) const {
    if (title_id.empty() || !overview_.is_object()) return false;
    for (const auto& e : overview_["entries"].items()) if (e["title_id"].as_string() == title_id && e["console"].is_object()) return true;
    return false;
}

// Supprimer la sauvegarde d'un jeu sur la console. Le moteur refuse s'il n'y a pas de copie à jour dans Backups, et fait
// une copie de sécurité juste avant : on le dit avant de confirmer.
void Ui::delete_console_save(const std::string& title_id, const std::string& title) {
    struct Acc { std::string uid, name, status; };
    std::vector<Acc> accs;
    for (const auto& e : overview_["entries"].items()) {
        if (e["title_id"].as_string() != title_id || !e["console"].is_object()) continue;
        const std::string key = e["account"].as_string();
        std::string name = key.substr(key.find(':') + 1);
        for (const auto& a : overview_["accounts"].items()) if (a["key"].as_string() == key && !a["name"].as_string().empty()) name = a["name"].as_string();
        accs.push_back({key.substr(key.find(':') + 1), name, e["status"].as_string()});
    }
    if (accs.empty()) { toast(tr("Aucune sauvegarde de ce jeu sur la console")); return; }
    auto run = [this, title_id, title](const std::string& uid, bool force) {
        Json b = Json::object();
        b.set("title_id", title_id);
        b.set("user_id", uid);
        if (force) b.set("force", true);
        model_.command("POST", "/api/v2/saves/delete", b.dump(), [this, title](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("Sauvegarde de « %s » supprimée de la console", title.c_str()) : e);
        });
        overview_loaded_ = 0;
        mg_sum_dirty_ = true;
        sheet_ = false;
    };
    auto confirm = [this, title_id, title, run](const Acc& a) {
        const bool safe = a.status == "up_to_date" || a.status == "backup_newer";
        const std::string uid = a.uid, who = a.name;
        if (!safe) {
            // Pas de copie à jour : on le dit, on propose de sauvegarder ; supprimer reste possible, mais en trois temps,
            // et au dernier « Annuler » est choisi d'office (une Croix trop rapide ne supprime rien).
            open_actions(trf("Supprimer la sauvegarde de « %s » ?", title.c_str()), {
                {tr("Annuler"), [] {}},
                {tr("Supprimer quand même…"), [this, title, who, uid, run] {
                    open_hold_confirm(trf("Supprimer DÉFINITIVEMENT la sauvegarde de « %s » (%s) ?", title.c_str(), who.c_str()),
                                      std::string(tr("C'est la partie active sur la console, pas une copie de l'onglet Sauvegardes.")) + " " +
                                      tr("Aucune sauvegarde à jour ne pourra la remettre. Seule une copie de sécurité brute est gardée sur la console (dossier de l'application)."),
                                      [uid, run] { run(uid, true); });
                }},
            });
            popup_note_ = a.status == "console_newer" ? tr("La partie active est plus récente que vos sauvegardes : sauvegardez-la d'abord (fiche › Sauvegarder), puis supprimez.")
                                                      : tr("Aucune copie dans Sauvegardes : sauvegardez-la d'abord (fiche › Sauvegarder), puis supprimez.");
            return;
        }
        open_actions(trf("Supprimer la sauvegarde de « %s » (%s) ?", title.c_str(), who.c_str()), {
            {tr("Annuler"), [] {}},
            {tr("Supprimer de la console…"), [this, title, who, uid, run] {
                open_hold_confirm(trf("Supprimer la sauvegarde de « %s » (%s) ?", title.c_str(), who.c_str()),
                                  std::string(tr("C'est la partie active sur la console, pas une copie de l'onglet Sauvegardes.")) + " " +
                                  tr("Une copie à jour reste dans Sauvegardes : vous pourrez la restaurer. Une copie de sécurité est aussi faite juste avant."),
                                  [uid, run] { run(uid, false); });
            }},
        });
        popup_note_ = std::string(tr("C'est la partie active sur la console, pas une copie de l'onglet Sauvegardes.")) + " " +
                      tr("Une copie à jour reste dans Sauvegardes : vous pourrez la restaurer. Une copie de sécurité est aussi faite juste avant.");
    };
    if (accs.size() == 1) { confirm(accs[0]); return; }
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& a : accs) acts.push_back({a.name, [confirm, a] { confirm(a); }});
    open_actions(trf("Supprimer la sauvegarde de « %s » : quel compte ?", title.c_str()), std::move(acts));
}

void Ui::confirm_close_remote(const std::string& instance, const std::string& console_name, const Json& fg) {
    const std::string tid = fg["title_id"].as_string(), name = fg["title"].as_string().empty() ? tid : fg["title"].as_string();
    if (tid.empty()) { toast(trf("Aucune application ouverte sur %s", console_name.c_str())); return; }
    open_actions(trf("Fermer « %s » sur %s ?", name.c_str(), console_name.c_str()), {
        {tr("Fermer maintenant"), [this, instance, tid, name, console_name] {
            Json b = Json::object();
            b.set("instance", instance);
            b.set("title_id", tid);
            model_.command("POST", "/api/v2/link/close-app", b.dump(), [this, name, console_name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("« %s » est fermé sur %s", name.c_str(), console_name.c_str()) : e);
            });
        }},
        {tr("Annuler"), [] {}},
    });
    if (fg["game"].as_bool()) popup_note_ = tr("C'est un jeu : la partie en cours, si elle n'est pas sauvegardée, sera perdue.");
    else if (fg["ours"].as_bool()) popup_note_ = tr("Le jeu envoyé apparaîtra sur l'accueil de cette console dès que l'application sera fermée.");
}

void Ui::pick_account(const std::string& instance, const std::string& console_name, std::function<void(const std::string&, const std::string&)> then) {
    const std::string path = instance.empty() ? std::string("/api/v2/accounts") : "/api/v2/link/peer?instance=" + instance + "&what=accounts";
    model_.command("GET", path, "", [this, console_name, then](const std::string& e, const std::string& body) {
        Json accs;
        if (!e.empty() || !Json::parse(body, accs) || !accs.items().size()) { post_toast(e.empty() ? trf("Aucun compte sur %s", console_name.c_str()) : e); return; }
        post_ui([this, console_name, then, accs] {
            std::vector<std::pair<std::string, std::function<void()>>> who;
            for (const auto& a : accs.items()) {
                const std::string uid = a["user_id"].as_string(), an = a["name"].as_string().empty() ? uid : a["name"].as_string();
                who.push_back({an, [then, uid, an] { then(uid, an); }});
            }
            open_actions(trf("Sur quel compte de %s ?", console_name.c_str()), std::move(who));
        });
    });
}

void Ui::send_save_to_peer(const std::string& item_key, const std::string& title, const std::string& local_uid) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& p : paired_peers()) {
        if (!p["online"].as_bool()) continue;
        const std::string inst = p["instance"].as_string(), name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
        acts.push_back({name, [this, inst, name, item_key, title, local_uid] {
            // Les comptes de l'autre console : la sauvegarde y sera restaurée sur celui qu'on choisit.
            model_.command("GET", "/api/v2/link/peer?instance=" + inst + "&what=accounts", "", [this, inst, name, item_key, title, local_uid](const std::string& e, const std::string& body) {
                Json accs;
                if (!e.empty() || !Json::parse(body, accs) || !accs.items().size()) { post_toast(e.empty() ? trf("Aucun compte sur %s", name.c_str()) : e); return; }
                post_ui([this, inst, name, item_key, title, local_uid, accs] {
                    std::vector<std::pair<std::string, std::function<void()>>> who;
                    for (const auto& a : accs.items()) {
                        const std::string uid = a["user_id"].as_string(), an = a["name"].as_string().empty() ? uid : a["name"].as_string();
                        who.push_back({an, [this, inst, name, item_key, title, local_uid, uid, an] {
                            Json b = Json::object();
                            b.set("place", "peer:" + inst + ":internal");
                            b.set("item", item_key);
                            b.set("account", local_uid);
                            b.set("user_id", uid);
                            model_.command("POST", "/api/v2/link/send-current-save", b.dump(), [this, title, name, an](const std::string& e2, const std::string&) {
                                post_toast(e2.empty() ? trf("La sauvegarde de « %s » part vers %s (%s) : suivie dans Tâches", title.c_str(), name.c_str(), an.c_str()) : e2);
                            });
                        }});
                    }
                    open_actions(trf("Sur quel compte de %s ?", name.c_str()), std::move(who));
                });
            });
        }});
    }
    if (acts.empty()) { toast(tr("Aucune autre PS5 jumelée et allumée : Appareils › PlayStation 5.")); return; }
    open_actions(trf("Envoyer la sauvegarde de « %s » à…", title.c_str()), std::move(acts));
}

void Ui::choose_format(const std::string& title, std::function<void(const std::string&)> then) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    const std::pair<const char*, const char*> opts[] = {
        {"auto", "Automatique (selon le système de la console)"},
        {"ffpfsc", "Compressé (.ffpfsc) · prend moins de place"},
        {"exfat", "Image non compressée (.exfat) · pour les anciens systèmes"},
        {"keep", "Tel quel (comme la copie)"},
    };
    for (const auto& [id, label] : opts) {
        const std::string f = id;
        acts.push_back({tr(label), [then, f] { then(f); }});
    }
    open_actions(title, std::move(acts));
}

void Ui::open_actions(std::string title, std::vector<std::pair<std::string, std::function<void()>>> items) {
    if (items.empty()) return;
    std::vector<std::string> labels;
    act_fns_.clear();
    for (auto& [l, f] : items) { labels.push_back(std::move(l)); act_fns_.push_back(std::move(f)); }
    open_popup(11, std::move(title), std::move(labels), 0);
}

// « 3 cochés » au-dessus de l'aide du bas.
void Ui::draw_checked(Surface& s, int n) {
    const std::string t = trf("%d coché(s)", n);
    const int w = Surface::measure(t, small) + 32;
    s.rect(margin, 948, w, 40, color::accent2, 20);
    s.text(font_, margin + 16, 954, t, small, color::text);
}

void Ui::draw_sync(Surface& s, const std::pair<std::string, std::string>& sw, int x, int w) {
    const std::string key = "switch:" + sw.first + "|eden:/data/prosperoeden";
    if (sync_key_ != key) {
        sync_key_ = key;
        sync_remote_ = sw.first;
        const auto c = sync_cache_.find(key);
        sync_ = c != sync_cache_.end() ? c->second : Json();
    }
    if (!sync_.is_object() && !sync_loading_) {  // une seule lecture à la fois ; le résultat est rangé sous sa clé
        sync_loading_ = true;
        sync_loading_key_ = key;
        sync_load_started_ = now_;
        model_.command("GET", "/api/v2/sync/compare?a=" + url_encode("switch:" + sw.first) + "&b=" + url_encode("eden:/data/prosperoeden"), "", [this, key](const std::string& e, const std::string& body) {
            Json j;
            std::lock_guard<std::mutex> l(async_mu_);
            if (e.empty() && Json::parse(body, j)) pending_sync_ = j; else { pending_sync_ = Json::object(); pending_sync_.set("error", e.empty() ? std::string(tr("comparaison impossible")) : e); }
            pending_sync_key_ = key;
            has_pending_sync_ = true;
        });
    }
    s.text(font_, x, 204 + sync_dy_, sw.second + "  ↔  ProsperoEden", large, color::text, w);
    static const char* const filters[] = {N_("Différentes"), N_("Identiques"), N_("D'un seul côté"), N_("Tout")};
    int fx = x;
    for (int i = 0; i < 4; ++i) {
        const int fw = Surface::measure(tr(filters[i]), small) + 32;
        if (i == sync_filter_) s.rect(fx, 262 + sync_dy_, fw, 38, color::text, 19);
        if (i == sync_filter_ && bar_ && saves_zone_ == 1) s.outline(fx - 5, 257 + sync_dy_, fw + 10, 48, color::accent, 24, 3);
        s.text(font_, fx + 16, 268 + sync_dy_, tr(filters[i]), small, i == sync_filter_ ? color::bg : color::muted);
        fx += fw + 6;
    }
    if (!sync_.is_object()) {
        // Lecture en cours : roue qui tourne, ce qui est lu, et le temps écoulé.
        const int cx = x + 60, cy = 420 + sync_dy_;
        const int step = static_cast<int>((now_ / 110) % 12);
        static const int ox[12] = {0, 20, 35, 40, 35, 20, 0, -20, -35, -40, -35, -20}, oy[12] = {-40, -35, -20, 0, 20, 35, 40, 35, 20, 0, -20, -35};
        for (int i = 0; i < 12; ++i) {
            const int age = (step - i + 12) % 12;
            s.rect(cx + ox[i] - 6, cy + oy[i] - 6, 12, 12, age == 0 ? color::accent : age < 4 ? color::text : color::line, 6);
        }
        s.text(font_, x + 140, 372 + sync_dy_, trf("Lecture de « %s » et de ProsperoEden…", sw.second.c_str()), large, color::text, w - 160);
        const unsigned secs = static_cast<unsigned>((now_ - sync_load_started_) / 1000);
        s.text(font_, x + 140, 430 + sync_dy_, tr("Le contenu de chaque sauvegarde est vérifié, pas seulement la date. Environ 10 secondes") +
                   (sync_loading_ && secs ? "  ·  " + std::to_string(secs) + " s" : std::string()), body, color::muted, w - 160);
        draw_footer(s, {{2, trf("Voir : %s", tr(filters[(sync_filter_ + 1) % 4])).c_str()}, {1, tr("Retour")}});
        return;
    }
    if (!sync_["error"].as_string().empty()) { s.text(font_, x, 340 + sync_dy_, sync_["error"].as_string(), body, color::bad, w); draw_footer(s, {{3, tr("Relire la Switch")}, {2, trf("Voir : %s", tr(filters[(sync_filter_ + 1) % 4])).c_str()}, {1, tr("Retour")}}); return; }
    const auto rows = sync_rows();
    saves_row_ = std::min(saves_row_, std::max(0, static_cast<int>(rows.size()) - 1));
    const int per_page = sync_dy_ ? 4 : 6, first = std::max(0, saves_row_ - per_page + 1);
    auto date = [](const Json& side) -> std::string {
        if (!side.is_object() || !side["time"].as_int()) return "—";
        const std::time_t t = static_cast<std::time_t>(side["time"].as_int());
        std::tm tm{};
        gmtime_r(&t, &tm);
        char b[16];
        std::strftime(b, sizeof b, "%d/%m/%Y", &tm);
        return b;
    };
    int yy = 320 + sync_dy_;
    for (int r = first; r < static_cast<int>(rows.size()) && r < first + per_page; ++r) {
        const Json& e = sync_["entries"][rows[static_cast<std::size_t>(r)]];
        s.rect(x, yy, w, 100, color::panel, 18);
        if (r == saves_row_ && saves_zone_ == 1 && !bar_) s.outline(x - 4, yy - 4, w + 8, 108, color::accent, 22, 3);
        const std::string prof = e["profile"].as_string() == "device" ? std::string(tr("commun à la console")) : e["profile"].as_string();
        {
            const Item* it = nullptr;
            for (const auto& i : snap_->items) if (i.platform == "Switch" && i.title_id == e["title_id"].as_string()) { it = &i; break; }
            Item stand_in;
            if (!it || !it->has_cover) {  // jeu absent de la bibliothèque : icône par son numéro (title ID)
                stand_in.title = e["title"].as_string(); stand_in.kind = "rom"; stand_in.platform = "Switch";
                stand_in.key = "tid:switch:" + e["title_id"].as_string(); stand_in.has_cover = e["title_id"].as_string().size() == 16;
                it = &stand_in;
            }
            cover(s, *it, x + 10, yy + 10, 80, false);
        }
        s.text(font_, x + 108, yy + 12, e["title"].as_string() + "  ·  " + prof, body, color::text, w - 600);
        const std::string st = e["status"].as_string();
        const char* label = st == "identical" ? tr("Identiques") : st == "a_newer" ? tr("Plus récente sur la Switch") : st == "b_newer" ? tr("Plus récente dans Eden") : st == "same_day" ? tr("Même jour, contenu différent") : st == "only_a" ? tr("Seulement sur la Switch") : tr("Seulement dans Eden");
        const auto tone = st == "identical" ? color::ok : st == "same_day" ? color::warn : st.rfind("only", 0) == 0 ? color::muted : color::accent;
        s.text(font_, x + w - 24 - Surface::measure(label, small), yy + 16, label, small, tone);
        s.text(font_, x + 108, yy + 56, "Switch : " + date(e["a"]) + "     ProsperoEden : " + date(e["b"]), small, color::muted, w - 132);
        yy += 112;
    }
    if (rows.empty()) s.text(font_, x, 340 + sync_dy_, tr("Rien dans ce filtre."), large, color::muted);
    else { const std::string pos = std::to_string(saves_row_ + 1) + " / " + std::to_string(rows.size()); s.text(font_, x + w - Surface::measure(pos, small), 214 + sync_dy_, pos, small, color::muted); }
    // Choix du sens, vérification, confirmation.
    if (sync_menu_ || sync_stage_) {
        const Json& e = sync_["entries"][sync_entry_];
        s.rect(0, 0, 1920, 1080, color::shadow);
        const int bx = 460, bw = 1000, by = 300, bh = 420;
        s.rect(bx, by, bw, bh, color::panel, 28);
        s.outline(bx, by, bw, bh, color::line, 28, 2);
        s.text(font_, bx + 44, by + 34, e["title"].as_string(), large, color::text, bw - 88);
        s.text(font_, bx + 44, by + 88, trf("Profil : %s", e["profile"].as_string() == "device" ? tr("commun à la console") : e["profile"].as_string().c_str()), body, color::muted);
        if (sync_menu_) {
            const char* opts[2] = {"Switch  →  ProsperoEden", "ProsperoEden  →  Switch"};
            for (int i = 0; i < 2; ++i) {
                const bool ok = i == 0 ? e["a"].is_object() : e["b"].is_object();
                const int ox = bx + 44 + i * 470;
                s.rect(ox, by + 160, 440, 80, i == sync_dir_ ? color::panel2 : color::bg2, 20);
                if (i == sync_dir_) s.outline(ox - 4, by + 156, 448, 88, color::accent, 24, 3);
                s.text(font_, ox + 28, by + 184, opts[i], body, ok ? color::text : color::muted);
            }
            s.text(font_, bx + 44, by + 280, sync_dir_ == 0 ? tr("La sauvegarde d'Eden est copiée avant d'être remplacée.") : tr("Un nouveau fichier est déposé dans JKSV sur la Switch : rien n'y est effacé."), small, color::muted, bw - 88);
            draw_footer(s, {{0, tr("Vérifier")}, {-1, tr("Gauche / droite  sens")}, {1, tr("Annuler")}});
        } else if (sync_stage_ == 1 || sync_stage_ == 3) {
            s.text(font_, bx + 44, by + 180, sync_stage_ == 1 ? tr("Vérification…") : tr("Transfert…"), large, color::accent);
            draw_footer(s, {{-1, tr("Patientez")}});
        } else {
            const auto files = sync_check_["files"].size();
            s.text(font_, bx + 44, by + 160, trf("%d fichier(s)", static_cast<int>(files)) + "  ·  " + size_text(static_cast<std::uint64_t>(sync_check_["bytes"].as_int())), body, color::text);
            s.text(font_, bx + 44, by + 210, sync_check_["target"].as_string(), small, color::muted, bw - 88);
            if (sync_check_["eden_newer"].as_bool()) s.text(font_, bx + 44, by + 270, tr("Attention : la sauvegarde dans Eden est plus récente."), body, color::warn, bw - 88);
            draw_footer(s, {{0, tr("Confirmer")}, {1, tr("Annuler")}});
        }
        return;
    }
    draw_footer(s, {{0, tr("Synchroniser")}, {3, tr("Relire la Switch")}, {2, trf("Voir : %s", tr(filters[(sync_filter_ + 1) % 4])).c_str()}, {1, tr("Retour")}});
}

void Ui::draw_plans(Surface& s) {
    auto when = [](const Json& p) -> std::string {
        const int m = static_cast<int>(p["minute_of_day"].as_int());
        char hm[8];
        std::snprintf(hm, sizeof hm, "%02d:%02d", m / 60, m % 60);
        const std::string t = p["timing"].as_string();
        if (t == "daily") return trf("Tous les jours à %s", hm);
        if (t == "weekly") {
            static const char* const d[] = {N_("lun"), N_("mar"), N_("mer"), N_("jeu"), N_("ven"), N_("sam"), N_("dim")};
            std::string days;
            for (int i = 0; i < 7; ++i) if (p["weekdays"].as_int() & (1 << i)) days += (days.empty() ? "" : ", ") + std::string(tr(d[i]));
            return trf("%s à %s", days.c_str(), hm);
        }
        if (t == "once") return tr("Une seule fois");
        return tr("À la demande");
    };
    auto rule = [](const Json& p) -> std::string {
        const std::string r = p["rule"].as_string();
        return r == "ps5_saves" ? tr("Sauvegardes PS5") : r == "roms" ? "ROM" + (p["rule_arg"].as_string().empty() ? std::string() : " " + p["rule_arg"].as_string()) : r == "apps" ? tr("Applications") : r == "all_games" ? tr("Tous les jeux") : tr("Éléments choisis");
    };
    int y = 290;
    const int w = 1920 - 2 * margin;
    // « Nouveau plan »
    s.rect(margin, y, w, 90, color::panel2, 18);
    if (tasks_row_ == 0 && !bar_) s.outline(margin - 4, y - 4, w + 8, 98, color::accent, 22, 3);
    s.rect(margin + 22, y + 21, 48, 48, color::accent2, 24);
    s.text(font_, margin + 35, y + 22, "+", large, color::text);
    s.text(font_, margin + 92, y + 24, tr("Nouveau plan"), body, color::text);
    s.text(font_, margin + 92 + Surface::measure(tr("Nouveau plan"), body) + 24, y + 30, tr("sauvegardes automatiques : chaque soir, chaque semaine…"), small, color::muted, w - 140 - Surface::measure(tr("Nouveau plan"), body));
    y += 106;
    const int first = std::max(0, tasks_row_ - 5);
    for (int i = first; i < static_cast<int>(plans_.size()) && y < 900; ++i) {
        const Json& p = plans_[static_cast<std::size_t>(i)];
        const bool on = p["enabled"].as_bool(true);
        s.rect(margin, y, w, 112, color::panel, 18);
        if (tasks_row_ == i + 1 && !bar_) s.outline(margin - 4, y - 4, w + 8, 120, color::accent, 22, 3);
        // Icône : horloge (programmé) ou pause.
        s.outline(margin + 22, y + 26, 60, 60, on ? color::ok : color::muted, 30, 3);
        if (on) { s.rect(margin + 50, y + 38, 4, 20, color::ok, 2); s.rect(margin + 50, y + 54, 18, 4, color::ok, 2); }
        else { s.rect(margin + 42, y + 42, 7, 28, color::muted, 3); s.rect(margin + 56, y + 42, 7, 28, color::muted, 3); }
        s.text(font_, margin + 104, y + 14, p["name"].as_string(), body, color::text, 900);
        if (!on) s.text(font_, margin + 104 + Surface::measure(p["name"].as_string(), body) + 18, y + 20, tr("en pause"), small, color::warn);
        std::string line = when(p) + "   ·   " + rule(p) + " (" + std::to_string(p["item_count"].as_int()) + ")   ·   " +
                           (p["keep"].as_int() ? trf("%d versions gardées", static_cast<int>(p["keep"].as_int())) : std::string(tr("toutes les versions")));
        s.text(font_, margin + 104, y + 58, line, small, color::muted, w - 140);
        if (!p["last_result"].as_string().empty()) s.text(font_, margin + 104, y + 84, trf("Dernière fois : %s", p["last_result"].as_string().c_str()), small, color::muted, w - 140);
        y += 128;
    }
    if (tasks_row_ == 0) draw_footer(s, {{0, tr("Créer")}, {2, trf("Voir : %s", tr(group_names[(tasks_group_ + 1) % 5])).c_str()}, {1, tr("Maintenir 2 s : quitter")}});
    else draw_footer(s, {{0, tr("Modifier")}, {-1, tr("R2 lancer maintenant")}, {3, tr("Pause / reprendre")}, {2, trf("Voir : %s", tr(group_names[(tasks_group_ + 1) % 5])).c_str()}});
}

void Ui::draw_tasks(Surface& s) {
    int counts[5] = {0, 0, 0, 0, static_cast<int>(plans_.size())};
    for (const auto& t : snap_->tasks) for (int g = 0; g < 4; ++g) if (in_group(t.state, g)) ++counts[g];
    s.text(font_, margin, 112, tr("Tâches"), title, color::text);
    int x = margin;
    {  // fond des listes : à la largeur des libellés (traductions plus longues)
        int bw = 4;
        for (int g = 0; g < 5; ++g) { char l[96]; std::snprintf(l, sizeof l, "%s %d", tr(group_names[g]), counts[g]); bw += Surface::measure(l, body) + 36 + 8; }
        s.rect(x - 6, 194, std::max(1010, bw), 56, color::panel, 28);
    }
    for (int g = 0; g < 5; ++g) {
        char label[96];
        std::snprintf(label, sizeof label, "%s %d", tr(group_names[g]), counts[g]);
        const int w = Surface::measure(label, body) + 36;
        if (g == tasks_group_) s.rect(x, 200, w, 44, color::text, 22);
        if (g == tasks_group_ && bar_) s.outline(x - 5, 195, w + 10, 54, color::accent, 27, 3);
        s.text(font_, x + 18, 206, label, body, g == tasks_group_ ? color::bg : color::muted);
        x += w + 8;
    }
    if (tasks_group_ >= 1 && tasks_group_ <= 3 && counts[tasks_group_] > 0) {  // le bouton se voit, pas seulement dans le pied de page
        const std::string t = tr("Vider la liste");
        const int bw = Surface::measure(t, body) + 80, bx = 1920 - margin - bw;
        s.rect(bx, 200, bw, 44, color::panel2, 22);
        s.outline(bx, 200, bw, 44, color::line, 22, 1);
        icon(s, 3, bx + 16, 210, color::accent);
        s.text(font_, bx + 52, 206, t, body, color::text);
    }
    if (!plans_loaded_ || now_ - plans_loaded_ > 10000) load_plans();
    if (tasks_group_ == 4) { draw_plans(s); return; }
    std::vector<const Task*> list;
    for (auto it = snap_->tasks.rbegin(); it != snap_->tasks.rend(); ++it) if (in_group(it->state, tasks_group_)) list.push_back(&*it);
    int y = 290;
    for (std::size_t i = 0; i < list.size() && y < 900; ++i) {
        const Task& t = *list[i];
        const bool focused = static_cast<int>(i) == tasks_row_ && !bar_;
        s.rect(margin, y, 1920 - 2 * margin, 124, color::panel, 18);
        if (focused) s.outline(margin - 4, y - 4, 1920 - 2 * margin + 8, 132, color::accent, 22, 3);
        // Image de l'élément à gauche (s'il est connu de la bibliothèque). Cochée (L2) : une case devant.
        int tx = margin + 28;
        if (!tasks_sel_.empty() && !in_group(t.state, 0)) { draw_checkbox(s, margin + 14, y + 62, tasks_sel_.count(t.id) != 0, false); }
        {
            const Item* found = nullptr;
            for (const auto& it : snap_->items) if (!t.item.empty() && (it.key == t.item || it.title_id == t.item)) { found = &it; break; }
            // Toujours une image : celle de l'élément, sinon une carte du type de tâche (jeu, sauvegarde, Switch…).
            Item stand_in;
            const std::string short_title = t.title.substr(t.title.find(" · ") == std::string::npos ? 0 : t.title.find(" · ") + 3);
            if (!found) for (const auto& it : snap_->items) if (it.title == short_title && it.has_cover) { found = &it; break; }
            if (!found) {
                stand_in.title = short_title;
                stand_in.kind = t.type == "import_switch" || t.type == "export_switch" ? "rom" : "game_ps5";
                stand_in.platform = t.type == "import_switch" || t.type == "export_switch" ? "Switch" : "PS5";
                found = &stand_in;
            }
            cover(s, *found, margin + 16, y + 12, 100, false);
            tx = margin + 136;
        }
        const std::string title = (t.type == "restore" ? std::string(tr("Restauration")) + " · " : std::string()) + t.title;
        const int tw = s.text(font_, tx, y + 16, title, body, color::text, 1000) + (tx - margin - 28);
        const char* st = state_name(t.state);
        const std::uint32_t sc = t.state == "running" ? color::accent : t.state == "done" ? color::ok : t.state == "failed" || t.state == "cancelled" ? color::bad : color::warn;
        const int sw = Surface::measure(st, small) + 18;
        s.rect(margin + 44 + tw, y + 18, sw, 28, rgb(sc & 255, (sc >> 8) & 255, (sc >> 16) & 255, 40), 7);
        s.text(font_, margin + 53 + tw, y + 20, st, small, sc);
        std::string detail = trf("%s sur %s", size_text(t.done).c_str(), size_text(t.total).c_str()) + " · " + trf("%d/%d fichiers", static_cast<int>(t.files_done), static_cast<int>(t.files_total));
        const bool ph = t.state == "running" && t.phase_fraction() >= 0;
        if (ph) detail += " · " + t.phase_text();
        else if (t.state == "running" && t.speed > 0) detail += " · " + size_text(static_cast<std::uint64_t>(t.speed)) + "/s · " + tr("reste") + " " + duration_text(static_cast<double>(t.total - t.done) / t.speed);
        if (!t.error.empty()) detail += " · " + t.error;
        if (t.state == "done" && t.report["mount_deferred"].is_string()) detail += " · " + std::string(tr("apparaît sur l'accueil une fois l'application fermée"));
        if (t.state == "done" && t.report["bytes_per_second"].is_number() && t.report["seconds"].is_number())
            detail += " · " + trf("lecture %s/s", size_text(static_cast<std::uint64_t>(t.report["bytes_per_second"].as_number())).c_str()) +
                      (t.report["note"].is_string() ? " · " + std::string(tr("jeu trop petit : mesure peu fiable")) : std::string());
        s.text(font_, tx, y + 54, detail, small, color::muted, 1300 - (tx - margin - 28));
        if (in_group(t.state, 0)) {
            const int bw = 1920 - margin - 28 - tx;
            s.rect(tx, y + 96, bw, 8, rgb(255, 255, 255, 26), 4);
            const int w = ph ? static_cast<int>(bw * t.phase_fraction()) : t.total ? static_cast<int>(static_cast<double>(bw) * static_cast<double>(t.done) / static_cast<double>(t.total)) : 0;
            s.rect(tx, y + 96, std::max(8, w), 8, ph ? color::ok : color::accent2, 4);
        }
        const char* actions[2] = {nullptr, nullptr};
        std::string close_label;
        if (t.state == "running" || t.state == "queued") { actions[0] = tr("Pause"); actions[1] = tr("Annuler"); }
        else if (t.state == "paused") { actions[0] = tr("Reprendre"); actions[1] = tr("Annuler"); }
        else if (t.state == "failed") { actions[0] = tr("Reprendre"); actions[1] = tr("Détails"); }
        else if (t.type == "link_send" && t.report["foreground"]["title_id"].is_string()) {  // l'app ouverte là-bas
            close_label = trf("Fermer « %s » là-bas", t.report["foreground"]["title"].as_string().c_str());
            actions[0] = close_label.c_str();
        }
        else { actions[0] = tr("Détails"); actions[1] = tr("Retirer"); }
        int bx = 1920 - margin - 28;
        for (int a = 1; a >= 0; --a) {
            if (!actions[a]) continue;
            const int bw = Surface::measure(actions[a], small) + 32;
            bx -= bw;
            const bool af = focused && std::min(tasks_action_, actions[1] ? 1 : 0) == a;
            s.rect(bx, y + 14, bw, 40, af ? color::text : color::panel2, 20);
            s.text(font_, bx + 16, y + 20, actions[a], small, af ? color::bg : color::text);
            bx -= 10;
        }
        y += 140;
    }
    if (list.empty()) s.text(font_, margin, 320, tr("Rien ici. Choisissez un élément dans la bibliothèque pour lancer une sauvegarde."), large, color::muted);
    if (!tasks_sel_.empty()) draw_checked(s, static_cast<int>(tasks_sel_.size()));
    const bool finished = tasks_group_ >= 1 && tasks_group_ <= 3 && !list.empty();
    draw_footer(s, {{0, tr("Action")}, {-1, finished ? tr("L2 : cocher") : ""}, {2, trf("Voir : %s", tr(group_names[(tasks_group_ + 1) % 5])).c_str()},
                    {3, finished ? tr("Vider la liste") : ""},
                    {-1, !tasks_sel_.empty() ? tr("Options : la sélection") : finished ? tr("Options : retirer") : ""}, {1, tr("Maintenir 2 s : quitter")}});
}

namespace {
std::string when_text(std::int64_t t) {
    if (t <= 0) return "?";
    std::time_t tt = static_cast<std::time_t>(t);
    char b[32];
    std::strftime(b, sizeof b, "%d/%m/%Y %H:%M", std::localtime(&tt));
    return b;
}
}  // namespace

// Détails d'une tâche finie : quand, combien de temps, à quelle vitesse, et ce qui s'est passé (« Fichiers » : copiés,
// déjà là, renommés, erreurs).
void Ui::task_details(const Task& t) {
    std::string n;
    auto line = [&](const std::string& l) { if (!n.empty()) n += "\n"; n += l; };
    line(std::string(state_name(t.state)) + " · " + tr("Début :") + " " + when_text(t.started) + (t.finished > 0 ? " · " + std::string(tr("Fin :")) + " " + when_text(t.finished) : std::string()));
    const std::int64_t secs = t.started > 0 && t.finished >= t.started ? t.finished - t.started : 0;
    std::string vol = trf("%s sur %s", size_text(t.done).c_str(), size_text(t.total).c_str());
    if (t.files_total) vol += " · " + trf("%d/%d fichiers", static_cast<int>(t.files_done), static_cast<int>(t.files_total));
    line(vol);
    if (secs > 0) line(std::string(tr("Durée :")) + " " + duration_text(static_cast<double>(secs)) +
                       (t.done > 0 ? " · " + trf("%s/s en moyenne", size_text(t.done / static_cast<std::uint64_t>(secs)).c_str()) : std::string()));
    const pbc::Json& r = t.report;
    if (t.type == "files_copy" || t.type == "files_move") {
        line(trf("Copiés : %d · déjà là : %d · renommés : %d · erreurs : %d", static_cast<int>(r["copied"].as_int()), static_cast<int>(r["skipped"].as_int()),
                 static_cast<int>(r["renamed"].size()), static_cast<int>(r["errors"].size())));
        if (r["removed"].as_int() > 0) line(trf("Originaux retirés (déplacement) : %d", static_cast<int>(r["removed"].as_int())));
        int k = 0;
        for (const auto& x : r["renamed"].items()) { if (++k > 4) break; line("↪ " + x["from"].as_string() + " → " + x["to"].as_string()); }
        k = 0;
        for (const auto& x : r["errors"].items()) { if (++k > 4) break; line("✕ " + x["file"].as_string() + " : " + x["error"].as_string()); }
        if (r["cleaned"].is_object()) line(trf("Fichiers copiés retirés : %d", static_cast<int>(r["cleaned"]["removed"].as_int())));
    }
    if (!t.error.empty()) line(std::string(tr("Erreur :")) + " " + t.error);
    const std::string id = t.id;
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (t.type == "backup" && t.state == "done") {  // la sauvegarde faite par cette tâche : la voir, la mettre à la corbeille
        acts.push_back({tr("Voir la sauvegarde (Toutes les versions)"), [this, id] {
            model_.command("POST", "/api/v2/tasks/" + id + "/backup", "{}", [this](const std::string& e, const std::string& body) {
                pbc::Json j;
                if (!e.empty() || !pbc::Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
                post_ui([this, j] { open_versions("", j["title_id"].as_string(), j["title"].as_string(), j["item"].as_string()); });
            });
        }});
        acts.push_back({tr("Mettre cette sauvegarde à la corbeille…"), [this, id] {
            model_.command("POST", "/api/v2/tasks/" + id + "/backup", "{}", [this](const std::string& e, const std::string& body) {
                pbc::Json j;
                if (!e.empty() || !pbc::Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
                post_ui([this, j] {
                    open_hold_confirm(trf("Mettre « %s » à la corbeille ?", j["title"].as_string().c_str()),
                                      tr("La sauvegarde faite par cette tâche va dans la corbeille de son disque ou de son NAS pendant 30 jours. La tâche reste dans la liste."),
                                      [this, j] {
                                          pbc::Json b = pbc::Json::object();
                                          b.set("place", j["place"]);
                                          b.set("bundle", j["bundle"]);
                                          b.set("confirm", true);
                                          model_.command("POST", "/api/v2/backups/delete", b.dump(), [this](const std::string& e2, const std::string&) {
                                              post_toast(e2.empty() ? tr("Sauvegarde mise à la corbeille (30 jours)") : e2);
                                              overview_loaded_ = 0;
                                          });
                                      });
                });
            });
        }});
    }
    if (t.state != "running" && t.state != "queued" && t.state != "paused")
        acts.push_back({tr("Retirer de la liste (la sauvegarde reste)"), [this, id] { model_.command("POST", "/api/v2/tasks/" + id + "/remove", "{}"); }});
    acts.push_back({tr("Fermer"), [] {}});
    open_actions(t.title.empty() ? std::string(task_kind(t.type)) : t.title, std::move(acts));
    popup_note_ = n;
}

// « Vider la liste » (Terminées, Échecs, Annulées) : une confirmation ; seulement la liste, aucun fichier.
void Ui::clear_tasks(int group) {
    const char* st = group == 1 ? "done" : group == 2 ? "failed" : "cancelled";
    int n = 0;
    for (const auto& t : snap_->tasks) if (in_group(t.state, group)) ++n;
    open_actions(trf("Vider « %s » (%d) ?", tr(group_names[group]), n), {
        {tr("Oui, vider la liste"), [this, st] {
            model_.command("POST", "/api/v2/tasks/clear", std::string("{\"state\":\"") + st + "\"}", [this](const std::string& e, const std::string& body) {
                pbc::Json j;
                if (!e.empty() || !pbc::Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Réponse illisible")) : e); return; }
                std::string msg = trf("%d tâche(s) retirée(s) de la liste", static_cast<int>(j["removed"].as_int()));
                if (j["kept"].as_int() > 0) msg += " · " + trf("%d copie(s) gardée(s) : fichiers copiés encore à retirer", static_cast<int>(j["kept"].as_int()));
                post_toast(msg);
            });
            tasks_row_ = 0;
        }},
        {tr("Tout vider : Terminées, Échecs et Annulées"), [this] {
            for (const char* s2 : {"done", "failed", "cancelled"})
                model_.command("POST", "/api/v2/tasks/clear", std::string("{\"state\":\"") + s2 + "\"}", [this](const std::string& e, const std::string&) { if (!e.empty()) post_toast(e); });
            post_toast(tr("Listes vidées : Terminées, Échecs et Annulées"));
            tasks_row_ = 0;
        }},
        {tr("Annuler"), [] {}},
    });
    popup_note_ = group == 1 ? tr("Seulement la liste : rien n'est retiré des disques.")
                             : tr("Seulement la liste : rien n'est retiré des disques. Ces tâches ne pourront plus être reprises.");
}

void Ui::draw_settings(Surface& s) {
    s.text(font_, margin, 112, tr("Paramètres"), title, color::text);
    for (int i = 0; i < section_count; ++i) {
        const int y = 210 + i * 64;
        if (i == set_section_) s.rect(margin, y, 420, 52, color::panel2, 26);
        if (i == set_section_ && set_zone_ == 0) s.outline(margin - 4, y - 4, 428, 60, color::accent, 30, 3);
        s.text(font_, margin + 24, y + 11, tr(section_names[i]), body, i == set_section_ ? color::text : color::muted, 380);
    }
    const int x = margin + 480, w = 1920 - margin - x;
    int y = 210;
    auto heading = [&](const char* h, const char* sub) {
        s.text(font_, x, y, h, large, color::text);
        y += 48;
        if (sub) { y += 4; s.text(font_, x, y, sub, small, color::muted, w); y += 40; }
    };
    auto card = [&](int height) { s.rect(x, y, w, height, color::panel, 18); s.outline(x, y, w, height, color::line, 18, 2); };
    auto kv = [&](const std::string& k, const std::string& v, int yy) {
        s.text(font_, x + 24, yy, k, body, color::muted);
        s.text(font_, x + w - 24 - std::min(Surface::measure(v, body), w / 2), yy, v, body, color::text, w / 2);
    };
    switch (set_section_) {
        case sec_news: {
            if (!news_requested_) {
                news_requested_ = true;
                model_.command("GET", "/api/v2/changelog", "", [this](const std::string& e, const std::string& body) {
                    Json j;
                    if (!e.empty() || !Json::parse(body, j)) { news_requested_ = false; return; }
                    std::vector<ChangelogEntry> list;
                    for (const auto& v : j.items()) {
                        ChangelogEntry c;
                        c.version = v["version"].as_string();
                        c.date = v["date"].as_string();
                        c.title = v["title"].as_string();
                        for (const auto& n : v["new"].items()) c.added.push_back(n.as_string());
                        for (const auto& n : v["improved"].items()) c.improved.push_back(n.as_string());
                        for (const auto& n : v["fixed"].items()) c.fixed.push_back(n.as_string());
                        list.push_back(std::move(c));
                    }
                    std::lock_guard<std::mutex> l(async_mu_);
                    pending_news_ = std::move(list);
                    has_pending_news_ = true;
                });
            }
            heading(tr("Nouveautés"), tr("Ce qui est nouveau, amélioré et corrigé dans chaque version."));
            if (news_.empty()) { s.text(font_, x, y, tr("Chargement…"), small, color::muted); break; }
            const int first = set_zone_ == 1 ? std::min(set_row_, static_cast<int>(news_.size()) - 1) : 0;
            std::vector<ChangelogEntry> shown(news_.begin() + first, news_.end());
            draw_changelog(s, shown, x, y, w, 975);
            break;
        }
        case sec_general:
        {
            heading(tr("Général"), tr("Langue, apparence et son."));
            const i18n::Language* sys = i18n::find(i18n::system_code());
            const i18n::Language* chosen = i18n::find(i18n::setting());
            auto shown_name = [](const i18n::Language* l) { return !l ? std::string("English") : l->tv_ok ? i18n::label(*l) : l->english; };
            card(80);
            if (set_zone_ == 1 && set_row_ == 0) s.outline(x - 4, y - 4, w + 8, 88, color::accent, 22, 3);
            kv(tr("Langue"), i18n::setting() == "auto" || !chosen ? trf("Automatique (%s)", shown_name(sys).c_str()) : shown_name(chosen), y + 24);
            y += 100;
            if (i18n::fallback()) {
                s.text(font_, x, y, tr("Cette langue arrive bientôt sur la télé : l'anglais s'affiche en attendant. Le web l'affiche déjà."), small, color::warn, w);
                y += 34;
            }
            // Thème : gauche / droite pour changer, appliqué tout de suite ; aperçu de chaque palette.
            const int th_h = 236;
            card(th_h);
            if (set_zone_ == 1 && set_row_ == 1) s.outline(x - 4, y - 4, w + 8, th_h + 8, color::accent, 22, 3);
            s.text(font_, x + 24, y + 22, tr("Thème"), body, color::muted);
            const std::string name = std::string("←  ") + tr(theme::current().name) + "  →";
            s.text(font_, x + w - 24 - Surface::measure(name, body), y + 22, name, body, color::text);
            const int n = theme::count(), gap = 16, sw = std::min(150, (w - 48 - gap * (n - 1)) / n), sh = 112;
            for (int i = 0; i < n; ++i) {
                const Theme& t = theme::at(i);
                const int sx = x + 24 + i * (sw + gap), sy = y + 72;
                s.exact_colors(true);
                if (i == theme::index()) s.outline(sx - 5, sy - 5, sw + 10, sh + 10, t.accent, 18, 3);
                s.rect(sx, sy, sw, sh, t.bg, 14);
                s.rect(sx, sy, sw, sh / 2, t.top, 14);
                s.rect(sx + 12, sy + 16, sw - 24, 34, t.panel, 8);
                s.rect(sx + 20, sy + 27, sw / 3, 12, t.text, 6);
                s.rect(sx + 12, sy + 62, sw / 2, 34, t.accent, 17);
                s.rect(sx + 12 + sw / 2 + 10, sy + 70, 18, 18, t.accent2, 9);
                s.outline(sx, sy, sw, sh, t.line, 14, 1);
                s.exact_colors(false);
                s.text(font_, sx, sy + sh + 10, tr(t.name), small, i == theme::index() ? color::text : color::muted, sw + gap + 24);
            }
            y += th_h + 20;
            // Son : trois lignes (effets, musique, volume), gauche / droite ou Croix pour changer.
            const char* labels[3] = {tr("Sons de l'interface"), tr("Musique d'ambiance"), tr("Volume")};
            const std::string values[3] = {sound_ui_ ? tr("Activés") : tr("Coupés"), music_ ? tr("Activée") : tr("Coupée"), "←  " + std::to_string(volume_) + " %  →"};
            card(3 * 64 + 16);
            for (int i = 0; i < 3; ++i) {
                const int ry = y + 8 + i * 64;
                if (set_zone_ == 1 && set_row_ == i + 2) s.outline(x - 4, ry - 4, w + 8, 64 + 8, color::accent, 22, 3);
                s.text(font_, x + 24, ry + 14, labels[i], body, color::muted);
                const int vw = Surface::measure(values[i], body);
                s.text(font_, x + w - 24 - vw, ry + 14, values[i], body, (i < 2 && !(i ? music_ : sound_ui_)) ? color::muted : color::text);
                if (i == 2) {  // jauge du volume
                    const int bw = 240, bx = x + w - 24 - vw - 24 - bw;
                    s.rect(bx, ry + 28, bw, 8, color::line, 4);
                    if (volume_ > 0) s.rect(bx, ry + 28, bw * volume_ / 100, 8, color::accent, 4);
                }
            }
            break;
        }
        case sec_sources: {
            heading(tr("Sources et émulateurs"), tr("Automatique : réglages de chaque émulateur, sinon ses dossiers habituels. Croix pour régler à la main."));
            int idx = -1;
            for (const auto& e : snap_->emulators) {
                if (!e.installed) continue;
                ++idx;
                if (idx < set_row_ - 2) continue;  // la liste défile pour garder l'émulateur choisi visible
                const int h = 60 + 36 * static_cast<int>(std::max<std::size_t>(1, e.roots.size()));
                if (y + h > 980) break;
                card(h);
                if (set_zone_ == 1 && idx == set_row_) s.outline(x - 4, y - 4, w + 8, h + 8, color::accent, 22, 3);
                if (e.is_manual) s.text(font_, x + w - 24 - Surface::measure(tr("réglé à la main"), small), y + 20, tr("réglé à la main"), small, color::warn);
                // Icône de l'émulateur (son application installée), sinon carte « Application ».
                const Item* app = nullptr;
                for (const auto& it : snap_->items)
                    for (const auto& t : e.title_ids) if (it.title_id == t && it.has_cover) app = &it;
                Item stand_in;
                bool app_installed = false;
                for (const auto& it : snap_->items) for (const auto& t : e.title_ids) if (it.title_id == t) app_installed = true;
                if (!app) { stand_in.title = e.name; stand_in.kind = "app"; app = &stand_in; }
                cover(s, *app, x + 20, y + 16, 48, false);
                const int nw = s.text(font_, x + 80, y + 16, e.name, body, color::text);
                // Dossier présent mais aucune application (ancienne version, app désinstallée…).
                if (!app_installed) s.text(font_, x + 80 + nw + 16, y + 22, tr("dossier trouvé · aucune application installée"), small, color::warn);
                int yy = y + 52;
                if (e.roots.empty()) s.text(font_, x + 80, yy, tr("Aucun dossier de jeux trouvé"), small, color::muted);
                for (const auto& r : e.roots) {
                    s.text(font_, x + 80, yy, r.platform, small, color::muted);
                    const std::string v = r.path + "  " + (r.source == "config" ? tr("(réglages)") : r.source == "manuel" ? tr("(choisi)") : tr("(par défaut)"));
                    s.text(font_, x + 340, yy, v, small, color::text, w - 370);
                    yy += 36;
                }
                y += h + 14;
            }
            break;
        }
        case sec_accounts: {
            if (tk_open_) {  // « Tout reprendre sur ce compte » : la partie de la console contre la meilleure copie de l'identifiant
                heading(trf("Tout reprendre sur %s", tk_target_name_.c_str()).c_str(), tr("Pour chaque jeu, la partie la plus avancée est cochée. L2 : cocher · Croix : reprendre · Rond : annuler."));
                if (tk_loading_) { s.text(font_, x, y + 10, tr("Lecture…"), large, color::muted); break; }
                const auto& games = tk_["games"];
                if (!games.size()) { s.text(font_, x, y + 10, tr("Aucune copie de cet identifiant à reprendre."), large, color::muted); break; }
                const int n = static_cast<int>(games.size()), vis = 6;
                tk_row_ = std::min(tk_row_, n - 1);
                const int first = std::max(0, tk_row_ - vis + 1);
                const int c1 = x + 70, c2 = x + 70 + (w - 70) * 34 / 100, c3 = x + 70 + (w - 70) * 67 / 100;
                s.text(font_, c1, y, tr("Jeu"), small, color::muted);
                s.text(font_, c2, y, tr("Sur la console"), small, color::muted);
                s.text(font_, c3, y, tr("Copie de cet identifiant"), small, color::muted);
                y += 36;
                auto play = [](std::int64_t sec) { if (sec < 0) return std::string(); char b[32]; if (sec < 3600) std::snprintf(b, sizeof b, "%lld min", static_cast<long long>(std::max<std::int64_t>(1, sec / 60))); else std::snprintf(b, sizeof b, "%lld h %02lld", static_cast<long long>(sec / 3600), static_cast<long long>(sec / 60 % 60)); return std::string(b); };
                for (int i = first; i < n && i < first + vis; ++i) {
                    const Json& g = games[static_cast<std::size_t>(i)];
                    const bool f = set_zone_ == 1 && i == tk_row_, on = tk_sel_.count(g["title_id"].as_string()) != 0;
                    s.rect(x, y, w, 76, f ? color::panel2 : color::panel, 14);
                    if (f) s.outline(x - 4, y - 4, w + 8, 84, color::accent, 18, 3);
                    draw_checkbox(s, x + 18, y + 38, on, g["running"].as_bool());
                    s.text(font_, c1, y + 10, g["title"].as_string(), body, color::text, c2 - c1 - 16);
                    if (g["running"].as_bool()) s.text(font_, c1, y + 44, tr("jeu en cours : laissé de côté"), small, color::warn, c2 - c1 - 16);
                    const Json& con = g["console"];
                    std::string ct = con.is_object() ? con["progress_text"].as_string() : std::string(tr("pas de partie"));
                    if (con.is_object()) { const std::string p = play(con["play_time_seconds"].as_int(-1)); if (!p.empty()) ct += (ct.empty() ? "" : " · ") + p; }
                    s.text(font_, c2, y + 24, ct.empty() ? std::string(tr("partie sans détail")) : ct, small, g["suggest"].as_bool() ? color::muted : color::ok, c3 - c2 - 16);
                    const Json& cp = g["copy"];
                    std::string pt = cp["progress_text"].as_string();
                    { const std::string p = play(cp["play_time_seconds"].as_int(-1)); if (!p.empty()) pt += (pt.empty() ? "" : " · ") + p; }
                    if (pt.empty()) pt = cp["place_label"].as_string();
                    s.text(font_, c3, y + 24, pt, small, g["suggest"].as_bool() ? color::ok : color::muted, x + w - c3 - 16);
                    y += 86;
                }
                break;
            }
            heading(tr("Utilisateurs"), tr("Un formatage ou une autre console donne un nouvel identifiant. Croix : le renommer ou le rattacher."));
            if (!players_at_ || now_ - players_at_ > 30000) {
                players_at_ = now_;
                model_.command("GET", "/api/v2/players", "", [this](const std::string& e, const std::string& body) {
                    pbc::Json j;
                    if (e.empty() && pbc::Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_players_ = j; has_pending_players_ = true; }
                });
            }
            const auto rows = account_rows(players_);
            if (rows.empty()) { s.text(font_, x, y + 10, players_.is_object() ? tr("Aucun identifiant connu") : tr("Lecture…"), large, color::muted); break; }
            const int n = static_cast<int>(rows.size()), vis = 6;
            set_row_ = std::min(set_row_, n - 1);
            const int first = std::max(0, set_row_ - vis + 1);
            bool others_head = false;
            for (int i = first; i < n && i < first + vis; ++i) {
                const AccRow& r = rows[static_cast<std::size_t>(i)];
                const pbc::Json& a = *r.a;
                if (!r.mine && !others_head) {  // séparation : les identifiants qui ne sont pas (encore) rattachés
                    others_head = true;
                    s.text(font_, x, y + 4, tr("Autres identifiants vus dans vos sauvegardes"), body, color::muted);
                    y += 46;
                }
                const bool f = set_zone_ == 1 && i == set_row_;
                s.rect(x, y, w, 96, f ? color::panel2 : color::panel, 16);
                if (f) s.outline(x - 4, y - 4, w + 8, 104, color::accent, 20, 3);
                const std::string id = a["account_id"].as_string();
                const std::string nm = a["name"].as_string().empty() ? std::string(tr("Sans nom")) : a["name"].as_string();
                s.text(font_, x + 24, y + 10, nm, body, color::text, w - 460);
                // état : actuel / ancien identifiant / probablement vous
                const char* tag = r.mine ? (a["current"].as_bool() ? tr("Sur cette console") : tr("Absent de cette console")) : a["suggest_player"].is_string() ? tr("Probablement vous") : tr("Absent de cette console");
                const std::uint32_t tone = r.mine ? (a["current"].as_bool() ? color::ok : color::accent) : color::warn;
                if (tag) {
                    const int tw = Surface::measure(tag, small) + 30;
                    s.rect(x + w - 24 - tw, y + 14, tw, 34, (tone & 0x00FFFFFFu) | 0x24000000u, 17);
                    s.text(font_, x + w - 24 - tw + 15, y + 19, tag, small, tone);
                }
                std::string l2 = id + "  ·  " + trf("%lld copie(s)", static_cast<long long>(a["images"].as_int())) + " · " + trf("%lld jeu(x)", static_cast<long long>(a["games"].as_int()));
                if (a["first"].as_int() > 0) {
                    auto day = [](std::int64_t t) { std::time_t tt = static_cast<std::time_t>(t); char b[16]; std::strftime(b, sizeof b, "%d/%m/%Y", std::localtime(&tt)); return std::string(b); };
                    l2 += " · " + day(a["first"].as_int()) + " → " + day(a["last"].as_int());
                }
                if (!r.mine && a["identical_games"].as_int() > 0) l2 += " · " + trf("même partie : %lld jeu(x)", static_cast<long long>(a["identical_games"].as_int()));
                s.text(font_, x + 24, y + 54, l2, small, color::muted, w - 48);
                y += 106;
            }
            break;
        }
        case sec_devices:
            heading(tr("Appareils"), tr("Disques, NAS, Switch et consoles se règlent dans l'onglet Appareils (Croix pour y aller)."));
            heading(tr("Associer un téléphone ou un ordinateur"), tr("Parcourez et lancez vos sauvegardes depuis un appareil du même réseau."));
            card(320);
            s.text(font_, x + 24, y + 20, std::string("1   ") + tr("Ouvrez cette adresse dans un navigateur"), body, color::muted);
            s.text(font_, x + 64, y + 62, address_, large, color::text);
            s.text(font_, x + 24, y + 130, std::string("2   ") + tr("Saisissez ce code"), body, color::muted);
            s.text(font_, x + 64, y + 172, snap_->pair_code.size() == 6 ? snap_->pair_code.substr(0, 3) + " " + snap_->pair_code.substr(3) : "—", title, color::text);
            s.text(font_, x + 24, y + 262, tr("Le code change quand le moteur redémarre. Les appareils associés le restent."), small, color::muted);
            break;
        case sec_ignored: {
            heading(tr("Éléments ignorés"), tr("Jeux, applications, homebrews et ROM qui ne sont plus sauvegardés ni proposés. Croix : ne plus ignorer."));
            const auto rows = ignored_rows(*snap_, unignored_);
            for (auto it = unignored_.begin(); it != unignored_.end();) {  // les réglages relus ne l'ont plus : oublié
                bool still = std::find(snap_->ignored.begin(), snap_->ignored.end(), *it) != snap_->ignored.end();
                for (const auto& i : snap_->items) if (i.ignored && (i.title_id == *it || i.key == *it)) still = true;
                it = still ? std::next(it) : unignored_.erase(it);
            }
            if (rows.empty()) { s.text(font_, x, y + 10, tr("Aucun élément ignoré"), large, color::muted); break; }
            const int n = static_cast<int>(rows.size()), vis = 8;
            set_row_ = std::min(set_row_, n - 1);
            const int first = std::max(0, set_row_ - vis + 1);
            for (int i = first; i < n && i < first + vis; ++i) {
                const IgnRow& r = rows[static_cast<std::size_t>(i)];
                const Item* found = r.item;
                const std::string& tid = r.value;
                const bool f = set_zone_ == 1 && i == set_row_;
                s.rect(x, y, w, 76, f ? color::panel2 : color::panel, 16);
                if (f) s.outline(x - 4, y - 4, w + 8, 84, color::accent, 20, 3);
                Item stand_in;
                if (!found) { stand_in.key = "tid:ps5:" + tid; stand_in.title = tid; stand_in.kind = "game_ps5"; stand_in.has_cover = r.field == "title_id"; }
                cover(s, found ? *found : stand_in, x + 10, y + 8, 60, false);
                s.text(font_, x + 88, y + 8, found ? found->title : tid, body, color::text, w - 420);
                s.text(font_, x + 88, y + 44, tid, small, color::muted, w - 420);
                const char* act = tr("Croix : ne plus ignorer");
                s.text(font_, x + w - 24 - Surface::measure(act, small), y + 26, act, small, f ? color::accent : color::muted);
                y += 86;
            }
            break;
        }
        case sec_updates: {
            heading(tr("Mises à jour"), tr("Deux parties : l'application télé et le moteur, qui continue de tourner quand vous la fermez."));
            const Update& u = snap_->update;
            auto state_text = [](const std::string& st) -> const char* {
                if (st == "checking") return tr("Recherche en cours…");
                if (st == "up_to_date") return tr("À jour");
                if (st == "available") return tr("Mise à jour disponible");
                if (st == "downloading") return tr("Téléchargement…");
                if (st == "installing") return tr("Installation…");
                if (st == "installed") return tr("Installée");
                if (st == "error") return tr("Échec");
                return tr("Pas encore recherché");
            };
            const bool progress = u.state == "downloading" && u.bytes_total;
            const bool msg = !u.message.empty();
            const int h = 196 + (msg ? 40 : 0) + (progress ? 56 : 0);
            card(h);
            kv(tr("Application télé / moteur"), std::string(app_version) + "  /  " + (snap_->version.empty() ? "—" : snap_->version), y + 20);
            kv(tr("Installée sur la console"), u.installed.empty() ? "—" : u.installed, y + 62);
            kv(tr("Dernière version"), u.available.empty() ? "—" : u.available, y + 104);
            // Installée mais le moteur tourne encore sur l'ancienne version : le dire, plutôt qu'« À jour ».
            kv(tr("État"), u.engine_restart_needed && !u.installed.empty() ? tr("Installée, active au redémarrage du moteur") : state_text(u.state), y + 146);
            int yy = y + 188;
            if (msg) { s.text(font_, x + 24, yy, u.message, small, u.state == "error" ? color::bad : color::muted, w - 48); yy += 40; }
            if (progress) {
                const int bw = w - 48, done = static_cast<int>(static_cast<double>(u.bytes_done) / static_cast<double>(u.bytes_total) * bw);
                s.rect(x + 24, yy + 6, bw, 8, color::line, 4);
                s.rect(x + 24, yy + 6, std::max(8, done), 8, color::accent2, 4);
                s.text(font_, x + 24, yy + 22, update_progress_text(u), small, color::muted, w - 48);
            }
            y += h + 20;
            if (countdown_until_) {
                const auto left = (countdown_until_ > now_ ? countdown_until_ - now_ : 0) / 1000 + 1;
                const std::string t = (countdown_kind_ == 1 ? trf("Redémarrage du moteur dans %d s", static_cast<int>(left)) : trf("Redémarrage de l'application dans %d s", static_cast<int>(left))) + "  ·  " +
                                      tr("Croix ou Rond pour annuler");
                s.rect(x, y, w, 64, color::panel2, 16);
                s.text(font_, x + 24, y + 16, t, body, color::warn, w - 48);
                y += 80;
            }
            const auto acts = update_actions();
            const int fit = std::max(1, (990 - y) / 72);  // lignes qui tiennent au-dessus du bas de l'écran
            const int first = std::clamp(set_zone_ == 1 ? set_row_ - fit + 1 : 0, 0, std::max(0, static_cast<int>(acts.size()) - fit));
            if (first > 0) s.text(font_, x + w - 40, y - 34, "▲", small, color::accent);
            for (std::size_t i = static_cast<std::size_t>(first); i < acts.size(); ++i) {
                if (y + 60 > 990) { s.text(font_, x + w - 40, y - 8, "▼", small, color::accent); break; }
                const int act = acts[i];
                const bool focus = set_zone_ == 1 && set_row_ == static_cast<int>(i);
                s.rect(x, y, w, 60, focus ? color::panel2 : color::panel, 16);
                if (focus) s.outline(x - 4, y - 4, w + 8, 68, color::accent, 20, 3);
                std::string label, detail;
                switch (act) {
                    case 0: label = tr("Rechercher des mises à jour"); detail = u.source.empty() ? tr("source à régler depuis le web") : ""; break;
                    case 1: label = tr("Installer la mise à jour"); detail = trf("%d fichier(s)", static_cast<int>(u.files)) + ", " + size_text(u.bytes_total); break;
                    case 2: label = tr("Redémarrer le moteur"); detail = tr("les sauvegardes en cours reprennent ensuite"); break;
                    case 3: label = tr("Redémarrer l'application"); detail = tr("elle se rouvre toute seule"); break;
                    case 5: label = tr("Démarrage du moteur"); detail = tr("à l'ouverture, avec la console"); break;
                    case 8: label = trf("Revenir à la version %s", u.previous.c_str()); detail = tr("si la version actuelle pose problème"); break;
                    case 9: label = tr("Versions disponibles"); detail = tr("choisir une version à installer"); break;
                    case 7: label = tr("Alimentation de la console"); detail = tr("redémarrer, éteindre"); break;
                    case 10: label = tr("Canal des mises à jour"); detail = snap_->update_channel == "beta" ? tr("Bêta") : tr("Stable (recommandé)"); break;
                    case 6: label = tr("Installer automatiquement (jamais pendant une partie)"); detail = snap_->update_auto_install ? tr("Oui") : tr("Non"); break;
                    default: label = tr("Rechercher automatiquement"); detail = snap_->update_auto_check ? tr("Oui") : tr("Non"); break;
                }
                s.text(font_, x + 24, y + 14, label, body, color::text, w - 72 - Surface::measure(detail, small));
                if (!detail.empty()) s.text(font_, x + w - 24 - Surface::measure(detail, small), y + 18, detail, small, color::muted);
                y += 72;
            }
            if (!u.changelog.empty() && (u.state == "available" || u.state == "installed") && y < 900) {
                s.text(font_, x, y + 6, tr("Ce qu'apporte cette mise à jour"), body, color::text);
                y += 48;
                draw_changelog(s, u.changelog, x, y, w, 975);
            }
            break;
        }
        case sec_report: {  // trois cartes : un bug (formulaire), une question ou une idée (Discussions), une étoile ; versions dessous
            if (!status_requested_) {
                status_requested_ = true;
                model_.command("GET", "/api/v2/status", "", [this](const std::string& e, const std::string& body) {
                    Json j;
                    if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_status_ = j; has_pending_status_ = true; }
                    else status_requested_ = false;
                });
            }
            if (!support_requested_) {  // les codes QR viennent du moteur (avec la liste de soutien)
                support_requested_ = true;
                model_.command("GET", "/api/v2/support", "", [this](const std::string& e, const std::string& body) {
                    Json j;
                    if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_support_ = j; has_pending_support_ = true; }
                    else support_requested_ = false;
                });
            }
            heading(tr("Signaler un problème"), tr("Scannez le code qui correspond avec votre téléphone : la page s'ouvre sur GitHub."));
            const Json& cq = support_["community"];
            struct Card { const char* qr; const char* title; const char* text; };
            static const Card cards[3] = {{"issues_qr", N_("Un bug"), N_("Le formulaire de signalement. Recopiez les versions ci-dessous.")},
                                          {"discussions_qr", N_("Une question, une idée"), N_("Les Discussions : posez votre question ou proposez une idée.")},
                                          {"star_qr", N_("★ Une étoile"), N_("Le projet vous plaît : une étoile sur GitHub l'aide à être trouvé.")}};
            auto draw_qr = [&](const Json& q, int qx, int qy, int size) {
                const auto& rows = q.items();
                s.exact_colors(true);  // noir sur blanc, quel que soit le thème
                s.rect(qx, qy, size, size, rgb(255, 255, 255), 12);
                const int m = static_cast<int>(rows.size());
                if (m) {
                    const double cell = size / (m + 4.0);  // fractionnaire : tous les codes ont la même taille
                    for (int r = 0; r < m; ++r) {
                        const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
                        for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                            if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
                    }
                }
                s.exact_colors(false);
            };
            const int gap = 24, cw = (w - 2 * gap) / 3, qs = std::min(cw - 48, 300), ch = qs + 190;
            set_row_ = std::clamp(set_row_, 0, 2);
            for (int i = 0; i < 3; ++i) {
                const int cx = x + i * (cw + gap);
                const bool f = set_zone_ == 1 && set_row_ == i;
                s.rect(cx, y, cw, ch, f ? color::panel2 : color::panel, 22);
                s.outline(cx, y, cw, ch, f ? color::accent : color::line, 22, f ? 3 : 1);
                draw_qr(cq[cards[i].qr], cx + (cw - qs) / 2, y + 24, qs);
                s.text(font_, cx + 24, y + qs + 44, tr(cards[i].title), body, color::text, cw - 48);
                para(s, font_, cx + 24, y + qs + 92, tr(cards[i].text), small, color::muted, cw - 48, 3, 28);
            }
            y += ch + 24;
            {   // à recopier dans un signalement
                const std::string v = std::string(tr("Version de l'appli")) + "  " + PBC_VERSION + "     " + tr("Moteur") + "  " +
                                      (snap_->version.empty() ? std::string(tr("non connecté")) : snap_->version) + "     " + tr("Système de la console") + "  " +
                                      (status_info_["firmware"].as_string().empty() ? std::string("—") : status_info_["firmware"].as_string());
                s.rect(x, y, w, 64, color::panel, 18);
                s.text(font_, x + 24, y + 14, v, small, color::text, w - 48);
            }
            if (support_big_) {  // code agrandi, à scanner de loin
                s.rect(0, 0, 1920, 1080, rgb(4, 10, 20, 235), 0);
                draw_qr(cq[cards[set_row_].qr], (1920 - 720) / 2, 120, 720);
                const std::string t = tr(cards[set_row_].title);
                s.text(font_, (1920 - Surface::measure(t, large)) / 2, 870, t, large, color::text);
            }
            break;
        }
        case sec_goals:
        case sec_support: {
            if (!support_requested_) {
                support_requested_ = true;
                model_.command("GET", "/api/v2/support", "", [this](const std::string& e, const std::string& body) {
                    Json j;
                    if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_support_ = j; has_pending_support_ = true; }
                    else support_requested_ = false;
                });
            }
            if (set_section_ == sec_goals) {  // une carte par produit : points forts, jauge en dollars US
                heading(tr("Projets à financer"), tr("Aidez à faire naître les prochaines applis. Pour contribuer : Soutenir le projet, en précisant le produit."));
                const auto& goals = support_["goals"].items();
                if (goals.empty()) { para(s, font_, x, y, tr("Bientôt : PS5 Center et PS5 Connector, avec leurs objectifs et une jauge pour suivre la cagnotte."), body, color::muted, w, 3, 40); break; }
                for (const auto& g : goals) {
                    if (y > 990 - 250) break;
                    const int ch = 270;  // titre, accroche, 3 points forts, jauge
                    s.rect(x, y, w, ch, color::panel, 22);
                    s.outline(x, y, w, ch, color::line, 22, 2);
                    s.text(font_, x + 28, y + 20, g["name"].as_string(), large, color::text, w / 2);
                    s.text(font_, x + 28, y + 74, g["pitch"].as_string(), small, color::accent, w - 56);
                    int fy = y + 112;
                    for (const auto& f : g["features"].items()) { if (fy > y + 180) break; s.text(font_, x + 28, fy, "•  " + f.as_string(), small, color::muted, w - 56); fy += 34; }
                    const std::int64_t tg = std::max<std::int64_t>(1, g["target"].as_int()), rs = g["raised"].as_int();
                    const int gw = w - 56, gy = y + ch - 52, fill = static_cast<int>(std::min<std::int64_t>(gw, gw * rs / tg));
                    s.rect(x + 28, gy, gw, 16, rgb(255, 255, 255, 30), 8);
                    if (fill > 0) s.rect(x + 28, gy, std::max(16, fill), 16, color::ok, 8);
                    const std::string nums = "$" + std::to_string(rs) + " / $" + std::to_string(tg) + "  ·  " + std::to_string((rs * 100 + tg / 2) / tg) + " %"  /* arrondi, comme la page web */ +
                                             (g["supporters"].as_int() ? "  ·  " + trf("%lld soutien(s)", static_cast<long long>(g["supporters"].as_int())) : std::string());
                    s.text(font_, x + w - 28 - Surface::measure(nums, small), y + 26, nums, small, color::text);
                    y += ch + 18;
                }
                break;
            }
            heading(tr("Merci d'utiliser PS5 Backup Center"), nullptr);
            y += para(s, font_, x, y, tr("L'appli est gratuite et le restera : rien n'est bloqué, tout le monde a les mêmes fonctions et les mêmes mises à jour. Elle est faite par un développeur indépendant, sur son temps libre et ses propres consoles. Si elle a déjà sauvé une de vos parties, un petit coup de pouce aide à payer les consoles de test, les disques et les outils. Merci, vraiment."), small, color::muted, w, 4, 30) + 18;
            std::vector<const Json*> all;
            for (const auto& l : support_["links"].items()) all.push_back(&l);
            for (const auto& c : support_["crypto"].items()) all.push_back(&c);
            const int n = static_cast<int>(all.size()), gap = 16;
            if (!n) { s.text(font_, x, y, tr("Chargement…"), small, color::muted); break; }
            const int cw = (w - gap * (n - 1)) / n, qs = cw - 32;
            auto draw_qr = [&](const Json& q, int qx, int qy, int size) {
                const auto& rows = q.items();
                const int m = static_cast<int>(rows.size());
                s.rect(qx, qy, size, size, rgb(255, 255, 255), 10);
                if (!m) return;
                const double cell = size / (m + 4.0);  // fractionnaire : tous les codes ont la même taille
                s.exact_colors(true);  // le thème ne doit pas éclaircir le noir : un code QR se scanne en noir sur blanc
                s.rect(qx, qy, size, size, rgb(255, 255, 255), 10);
                for (int r = 0; r < m; ++r) {
                    const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
                    for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                        if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
                }
                s.exact_colors(false);
            };
            const int ch = qs + 172;
            for (int i = 0; i < n; ++i) {
                const Json& it = *all[static_cast<std::size_t>(i)];
                const int cx = x + i * (cw + gap);
                const bool f = set_zone_ == 1 && set_row_ == i, link = it.has("name");
                s.rect(cx, y, cw, ch, f ? color::panel2 : color::panel, 18);
                s.outline(cx, y, cw, ch, f ? color::accent : color::line, 18, f ? 3 : 1);
                if (!it["qr"].items().empty()) draw_qr(it["qr"], cx + 16, y + 16, qs);
                else { s.rect(cx + 16, y + 16, qs, qs, color::line, 10); const std::string b = tr("bientôt"); s.text(font_, cx + 16 + (qs - Surface::measure(b, small)) / 2, y + 16 + qs / 2 - 14, b, small, color::warn); }
                const std::string name = link ? std::string(tr(it["name"].as_string().c_str())) : it["coin"].as_string();
                const std::string sub = link ? std::string(tr(it["detail"].as_string().c_str())) : it["network"].as_string();
                s.text(font_, cx + 16, y + qs + 28, name, body, color::text, cw - 32);
                para(s, font_, cx + 16, y + qs + 70, sub, small, link ? color::muted : color::accent, cw - 32, 2, 28);
                const std::string a = link ? (it["active"].as_bool() ? it["url"].as_string() : std::string()) : it["address"].as_string();
                if (!a.empty()) s.text(font_, cx + 16, y + qs + 128, a.size() > 18 ? a.substr(0, 8) + "…" + a.substr(a.size() - 6) : a, small, color::muted, cw - 32);
            }
            y += ch + 18;
            s.text(font_, x, y, tr("Crypto : envoyez uniquement sur le réseau indiqué sous chaque code, sinon l'envoi est perdu."), small, color::warn, w);
            y += 52;
            s.text(font_, x, y, tr("Messages de la communauté"), body, color::accent);
            const int wall_y = y;
            y += 46;
            const auto& wall = support_["supporters"].items();
            const int mq = 170, mw = w - mq - 40;  // à droite : « Laisser un message » (Discussions GitHub)
            if (wall.empty()) s.text(font_, x, y, tr("Les premiers messages apparaîtront ici. Laissez le vôtre : scannez le code."), small, color::muted, mw);
            const int fy = 990 - 62;  // rangée « Suivez-nous », en bas à gauche (titre juste au-dessus)
            for (std::size_t i = 0; i < wall.size() && y < fy - 90; ++i) {
                const Json& m = wall[i];
                int lx = x + s.text(font_, x, y, m["name"].as_string(), small, color::text, mw / 3) + 14;
                if (m["backer"].as_bool()) {
                    const std::string b = tr("a soutenu le projet");
                    const int bw = Surface::measure(b, small) + 20;
                    s.rect(lx, y + 2, bw, 30, rgb(60, 190, 120, 40), 8);
                    s.text(font_, lx + 10, y + 2, b, small, color::ok);
                    lx += bw + 14;
                }
                std::string rest = m["message"].as_string().empty() ? std::string() : "« " + m["message"].as_string() + " »";
                if (!m["date"].as_string().empty()) rest += (rest.empty() ? "" : "  ·  ") + m["date"].as_string();
                if (!rest.empty()) s.text(font_, lx, y, rest, small, color::muted, x + mw - lx);
                y += 42;
            }
            {   // Suivez-nous : une pastille par plateforme (logo, nom, compte) ; grisée « bientôt » tant qu'il n'y a pas de lien
                const auto& fl = support_["follow"].items();
                s.text(font_, x, fy - 44, tr("Suivez-nous"), body, color::accent);
                int fx = x;
                static const int icons[] = {dev_x, dev_youtube, dev_reddit, dev_discord};
                for (std::size_t i = 0; i < fl.size() && i < 4; ++i) {
                    const Json& f = fl[i];
                    const bool on = f["active"].as_bool(), focus = set_zone_ == 1 && set_row_ == n + static_cast<int>(i);
                    const std::string name = f["name"].as_string(), sub = on ? f["handle"].as_string() : std::string(tr("bientôt"));
                    const int pw = 52 + std::max(Surface::measure(name, small), Surface::measure(sub, small)) + 16;
                    if (fx + pw > x + w - mq - 20) break;
                    s.rect(fx, fy, pw, 56, focus ? color::panel2 : color::panel, 28);
                    s.outline(fx, fy, pw, 56, focus ? color::accent : color::line, 28, focus ? 3 : 1);
                    const Image& ic = devices_[icons[i]];
                    if (ic.valid()) s.image(ic, fx + 9, fy + 10, 36, 36);
                    s.text(font_, fx + 52, fy + 2, name, small, on ? color::text : color::muted);
                    s.text(font_, fx + 52, fy + 26, sub, small, on ? color::muted : color::warn);
                    fx += pw + 10;
                }
            }
            if (!support_["community"]["messages_qr"].items().empty()) {
                draw_qr(support_["community"]["messages_qr"], x + w - mq, wall_y + 4, mq);
                s.text(font_, x + w - mq, wall_y + mq + 12, tr("Laisser un message"), small, color::text, mq + 40);
            }
            const Json* big = set_row_ < n ? all[static_cast<std::size_t>(set_row_)]
                            : set_row_ - n < static_cast<int>(support_["follow"].items().size()) ? &support_["follow"].items()[static_cast<std::size_t>(set_row_ - n)] : nullptr;
            if (support_big_ && big && (*big)["qr"].items().empty()) support_big_ = false;
            if (support_big_ && big && set_row_ >= n) {  // Suivez-nous : code agrandi, plateforme et compte
                const Json& f = *big;
                s.rect(0, 0, 1920, 1080, rgb(4, 10, 20, 235), 0);
                draw_qr(f["qr"], (1920 - 720) / 2, 120, 720);
                const std::string t = f["name"].as_string() + "  ·  " + f["handle"].as_string();
                s.text(font_, (1920 - Surface::measure(t, large)) / 2, 870, t, large, color::text);
                s.text(font_, (1920 - Surface::measure(f["url"].as_string(), small)) / 2, 936, f["url"].as_string(), small, color::muted);
            }
            if (support_big_ && set_row_ < n && !(*all[static_cast<std::size_t>(set_row_)])["qr"].items().empty()) {  // code agrandi, à scanner de loin
                const Json& it = *all[static_cast<std::size_t>(set_row_)];
                s.rect(0, 0, 1920, 1080, rgb(4, 10, 20, 235), 0);
                draw_qr(it["qr"], (1920 - 720) / 2, 120, 720);
                const std::string t = it.has("name") ? it["name"].as_string() : it["coin"].as_string() + "  ·  " + it["network"].as_string();
                s.text(font_, (1920 - Surface::measure(t, large)) / 2, 870, t, large, color::text);
                const std::string a = it.has("name") ? it["url"].as_string() : it["address"].as_string();
                s.text(font_, (1920 - Surface::measure(a, small)) / 2, 936, a, small, color::muted);
            }
            break;
        }
        case sec_thanks: {
            if (!credits_requested_) {
                credits_requested_ = true;
                model_.command("GET", "/credits.json", "", [this](const std::string& e, const std::string& body) {
                    Json j;
                    if (e.empty() && Json::parse(body, j)) { std::lock_guard<std::mutex> l(async_mu_); pending_credits_ = j; has_pending_credits_ = true; }
                    else credits_requested_ = false;
                });
            }
            heading(tr("Remerciements"), credits_["license"].as_string().empty() ? nullptr : credits_["license"].as_string().c_str());
            // Une ligne par composant ; Haut/Bas fait défiler.
            struct Row { std::string title, line1, line2; bool header; };
            std::vector<Row> rows;
            rows.push_back({tr("Composants utilisés"), "", "", true});
            for (const auto& x : credits_["used"].items())
                rows.push_back({x["name"].as_string(), x["author"].as_string() + "  ·  " + x["license"].as_string(), x["role"].as_string() + (x["url"].as_string().empty() ? "" : "  —  " + x["url"].as_string()), false});
            rows.push_back({tr("Merci aussi"), "", "", true});
            for (const auto& x : credits_["thanks"].items())
                rows.push_back({x["name"].as_string(), x["author"].as_string(), x["role"].as_string(), false});
            credits_rows_ = static_cast<int>(rows.size());
            const int first = set_zone_ == 1 ? std::min(set_row_, std::max(0, credits_rows_ - 1)) : 0;
            for (int i = first; i < credits_rows_ && y < 990 - 108; ++i) {
                const Row& r = rows[static_cast<std::size_t>(i)];
                if (r.header) { s.text(font_, x, y + 8, r.title, body, color::accent); y += 52; continue; }
                card(96);
                s.text(font_, x + 24, y + 12, r.title, body, color::text);
                if (!r.line1.empty()) s.text(font_, x + w - 24 - std::min(Surface::measure(r.line1, small), w / 2), y + 16, r.line1, small, color::muted, w / 2);
                s.text(font_, x + 24, y + 56, r.line2, small, color::muted, w - 48);
                y += 108;
            }
            if (credits_rows_ <= 2) s.text(font_, x, y, tr("Chargement…"), small, color::muted);
            break;
        }
        default: {
            heading(tr("Diagnostic et à propos"), nullptr);
            card(170);
            kv(tr("Moteur"), snap_->version.empty() ? std::string(tr("non connecté")) : snap_->version, y + 20);
            kv(tr("Éléments"), std::to_string(snap_->items.size()), y + 64);
            kv(tr("Adresse web"), address_, y + 108);
        }
    }
    draw_footer(s, {{0, tr("Valider")}, {1, tr("Retour au menu")}, {-1, tr("L1 / R1  onglets")}});
}

void Ui::draw_engine_overlay(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 210));
    const char* choices[3] = {tr("Démarrer le moteur"), tr("Réessayer"), tr("Continuer sans le moteur")};
    // La carte s'élargit avec les traductions (boutons et textes plus longs qu'en français).
    int need = std::max(Surface::measure(tr("Il garde la bibliothèque à jour et fait les sauvegardes, même app fermée."), small),
                        Surface::measure(tr("Sans le moteur, la bibliothèque et les sauvegardes restent consultables ; les actions seront grisées."), small));
    need = std::max(need, Surface::measure(tr("Le moteur de sauvegarde ne répond pas"), large));
    int bt = 32;
    for (const char* c : choices) bt += Surface::measure(c, body) + 64;
    const int pw = std::min(1760, std::max({900, need + 96, bt + 96})), ph = 380, px = (1920 - pw) / 2, py = (1080 - ph) / 2;
    s.rect(px, py, pw, ph, color::panel, 28);
    s.text(font_, px + 48, py + 44, tr("Le moteur de sauvegarde ne répond pas"), large, color::text, pw - 96);
    s.text(font_, px + 48, py + 100, tr("Il garde la bibliothèque à jour et fait les sauvegardes, même app fermée."), small, color::muted, pw - 96);
    int x = px + 48;
    for (int i = 0; i < 3; ++i) {
        const int w = Surface::measure(choices[i], body) + 48;
        s.rect(x, py + 200, w, 56, i == engine_choice_ ? color::text : color::panel2, 28);
        s.text(font_, x + 24, py + 211, choices[i], body, i == engine_choice_ ? color::bg : color::text);
        x += w + 16;
    }
    s.text(font_, px + 48, py + 300, tr("Sans le moteur, la bibliothèque et les sauvegardes restent consultables ; les actions seront grisées."), small, color::muted, pw - 96);
}

// Nouveautés à la façon d'Orbit : version et titre, puis Nouveautés / Améliorations / Corrections.
void Ui::draw_changelog(Surface& s, const std::vector<ChangelogEntry>& list, int x, int& y, int w, int bottom) {
    auto part = [&](const char* label, std::uint32_t tone, const std::vector<std::string>& items) {
        if (items.empty() || y > bottom - 40) return;
        s.text(font_, x, y, label, small, tone);
        y += 34;
        for (const auto& it : items) {
            // Retour à la ligne au mot près : jamais de phrase coupée par « … ».
            std::vector<std::string> lines;
            std::string line, word;
            const int max = w - 60;
            auto push_word = [&] {
                if (word.empty()) return;
                const std::string tryl = line.empty() ? word : line + " " + word;
                if (!line.empty() && Surface::measure(tryl, small) > max) { lines.push_back(line); line = word; }
                else line = tryl;
                word.clear();
            };
            for (const char* c = tr(it.c_str()); *c; ++c) { if (*c == ' ') push_word(); else word += *c; }  // traduit à l'affichage
            push_word();
            if (!line.empty()) lines.push_back(line);
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (y > bottom - 30) return;
                s.text(font_, x + 18, y, i == 0 ? "•" : "", small, color::muted);
                s.text(font_, x + 44, y, lines[i], small, color::muted, max);
                y += 30;
            }
            y += 4;
        }
        y += 6;
    };
    for (const auto& v : list) {
        if (y > bottom - 60) break;
        // « Version 2.1.0 · 7 novembre 2026 », puis le titre de la version.
        std::string date;
        int yy = 0, mm = 0, dd = 0;
        static const char* const months[] = {N_("janvier"), N_("février"), N_("mars"), N_("avril"), N_("mai"), N_("juin"), N_("juillet"), N_("août"), N_("septembre"), N_("octobre"), N_("novembre"), N_("décembre")};
        if (std::sscanf(v.date.c_str(), "%d-%d-%d", &yy, &mm, &dd) == 3 && mm >= 1 && mm <= 12) date = "  ·  " + trf("%d %s %d", dd, tr(months[mm - 1]), yy);
        s.text(font_, x, y, trf("Version %s", v.version.c_str()) + date, body, color::accent, w);
        y += 42;
        if (!v.title.empty()) { s.text(font_, x, y, tr(v.title.c_str()), body, color::text, w); y += 44; }
        part(tr("Nouveautés"), color::ok, v.added);
        part(tr("Améliorations"), color::accent, v.improved);
        part(tr("Corrections"), color::warn, v.fixed);
        y += 8;
    }
}

void Ui::draw_whats_new(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int x = 300, w = 1320, top = 120, h = 840;
    s.rect(x, top, w, h, color::panel, 28);
    s.outline(x, top, w, h, color::line, 28, 2);
    s.text(font_, x + 48, top + 36, tr("Quoi de neuf"), title, color::text);
    int y = top + 140;
    draw_changelog(s, whats_new_, x + 48, y, w - 96, top + h - 176);
    // Une seule ligne, jamais une fenêtre de plus : le soutien est dans Paramètres.
    s.text(font_, x + 48, top + h - 158, "♥", small, rgb(255, 94, 91));
    para(s, font_, x + 82, top + h - 158, tr("PS5 Backup Center reste gratuit. Si l'appli vous aide, vous pouvez soutenir son développement : Paramètres › Soutenir le projet."), small, color::muted, w - 130, 2, 30);
    const int cw = std::max(220, Surface::measure(tr("Compris"), body) + 64);
    s.rect(x + 48, top + h - 76, cw, 52, color::accent2, 26);
    s.text(font_, x + 80, top + h - 66, tr("Compris"), body, color::text);
    icon(s, 0, x + 70 + cw, top + h - 64, color::muted);
}

void Ui::draw(Surface& s) {
    animating_ = false;  // remis à vrai par ce qui bouge encore (défilement, roue de chargement)
    s.backdrop();
    if (tab_ != home && tab_ != library) draw_header(s);  // ces deux-là posent d'abord leur fond
    switch (tab_) {
        case home: draw_home(s); break;
        case library: draw_library(s); break;
        case destinations: draw_destinations(s); break;
        case parties: draw_parties(s); break;
        case backups: draw_backups_tab(s); break;
        case synchro: draw_synchro(s); break;
        case transfers: draw_transfers(s); break;
        case tasks: draw_tasks(s); break;
        case settings: draw_settings(s); break;
        default: break;
    }
    if (sheet_) draw_sheet(s);
    if (rsheet_) draw_remote_sheet(s);
    draw_quit_hint(s);
    if (engine_choice_needed()) draw_engine_overlay(s);
    if (popup_) draw_popup(s);
    if (mg_preview_) draw_mg_preview(s);
    if (mg_ign_open_) draw_mg_ignore(s);
    if (hold_open_) draw_hold_confirm(s);  // par-dessus l'aperçu qui l'a ouvert
    if (ed_open_) draw_editor(s);
    if (cl_open_) draw_cloud_link(s);
    if (em_open_) draw_emulator(s);
    if (pp_open_) draw_item_plans(s);
    if (pe_open_) draw_plan_editor(s);
    if (br_open_) draw_browser(s);
    if (kb_open_) draw_keyboard(s);
    if (guide_) draw_guide(s);
    if (!whats_new_.empty()) draw_whats_new(s);
    if (!connected_pad_) {
        s.rect(1920 - margin - 420, 120, 420, 56, color::panel2, 28);
        s.text(font_, 1920 - margin - 396, 131, tr("Manette non détectée"), body, color::warn);
    }
    if (quit_) {
        // Petite carte « Quitter » : Rond au centre d'un anneau qui se remplit, secondes qui restent.
        const int w = 360, h = 96, x = (1920 - w) / 2, y = 900;
        s.rect(x + 6, y + 8, w, h, rgb(0, 0, 0, 120), 24);
        s.rect(x, y, w, h, color::panel2, 24);
        s.outline(x, y, w, h, rgb(255, 123, 123, 120), 24, 2);
        const int cx = x + 52, cy = y + h / 2;
        static const int dx[16] = {0, 12, 22, 29, 31, 29, 22, 12, 0, -12, -22, -29, -31, -29, -22, -12};
        static const int dy[16] = {-31, -29, -22, -12, 0, 12, 22, 29, 31, 29, 22, 12, 0, -12, -22, -29};
        const int lit = static_cast<int>(16 * quit_ / 1000);
        for (int i = 0; i < 16; ++i) s.rect(cx + dx[i] - 4, cy + dy[i] - 4, 8, 8, i < lit ? color::bad : rgb(255, 255, 255, 40), 4);
        icon(s, 1, cx - 13, cy - 13, color::bad);
        const unsigned left = (1000 - std::min(1000u, quit_)) * 2;  // millisecondes restantes (2 s en tout)
        char secs[96];
        std::snprintf(secs, sizeof secs, tr("Quitter dans %u,%u s"), left / 1000, (left % 1000) / 100);
        s.text(font_, x + 104, y + 18, secs, body, color::text);
        s.rect(x + 104, y + 62, w - 128, 6, rgb(255, 255, 255, 30), 3);
        s.rect(x + 104, y + 62, static_cast<int>((w - 128) * quit_ / 1000), 6, color::bad, 3);
    }
    if (!toast_.empty()) {
        const int w = Surface::measure(toast_, body) + 56;
        s.rect((1920 - w) / 2, 900, w, 60, color::panel2, 30);
        s.outline((1920 - w) / 2, 900, w, 60, color::line, 30, 2);
        s.text(font_, (1920 - w) / 2 + 28, 914, toast_, body, color::text);
    }
}

// ---- Bibliothèque d'une autre PS5 ----------------------------------------------------------------------------------
const Json* Ui::remote_meta_of(const Item* it) const {
    if (remote_items_.empty() || it < remote_items_.data() || it >= remote_items_.data() + remote_items_.size()) return nullptr;
    const std::size_t i = static_cast<std::size_t>(it - remote_items_.data());
    return i < remote_meta_.size() ? &remote_meta_[i] : nullptr;
}

// Le moteur répond depuis son cache (aucun appel réseau) ; fresh : il relit l'autre PS5 maintenant, si elle est en ligne.
void Ui::load_peer_library(bool fresh) {
    if (lib_peer_.empty()) return;
    remote_loading_ = true;
    const std::string inst = lib_peer_;
    model_.command("GET", "/api/v2/link/peer-library?instance=" + url_encode(inst) + (fresh ? "&fresh=1" : ""), "", [this, inst, fresh](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j) && j.is_object();
        post_ui([this, inst, fresh, ok, j, e] {
            if (lib_peer_ != inst) return;  // on a changé de console entre-temps
            remote_loading_ = false;
            if (!ok || j["error"].is_string()) { toast(!e.empty() ? e : j["error"].as_string()); return; }
            lib_peer_online_ = j["online"].as_bool();
            lib_peer_read_at_ = j["read_at"].as_int();
            if (j["name"].is_string() && !j["name"].as_string().empty()) lib_peer_name_ = j["name"].as_string();
            remote_items_.clear();
            remote_meta_.clear();
            remote_items_.reserve(j["items"].size());
            for (const auto& x : j["items"].items()) {
                Item it{x["cover_key"].as_string(), x["kind"].as_string(), x["title"].as_string(), x["title_id"].as_string(), x["platform"].as_string(),
                        x["place"].as_string(), x["image_type"].as_string(), {}, {}, true, {}, {}};
                for (const auto& a : x["apps"].items()) it.apps.push_back(a.as_string());
                for (const auto& c : x["components"].items())
                    it.components.push_back({c["part"].as_string(), ui_util::engine_label(c["label"].as_string()), static_cast<std::uint64_t>(c["bytes"].as_int())});
                remote_items_.push_back(std::move(it));
                remote_meta_.push_back(x);
            }
            if (j["read_error"].is_string()) toast(trf("%s : %s", lib_peer_name_.c_str(), j["read_error"].as_string().c_str()));
            // Rien en cache (elle était éteinte au dernier tour du moteur) mais en ligne maintenant : une lecture, une seule.
            if (!fresh && remote_items_.empty() && lib_peer_online_) load_peer_library(true);
        });
    });
}

// Pastille « Console » : cette PS5, puis chaque PS5 jumelée (une éteinte reste choisissable : dernière liste connue).
void Ui::cycle_lib_console() {
    std::vector<std::pair<std::string, std::string>> list{{"", ""}};
    std::vector<bool> online{true};
    if (link_peers_.is_array())
        for (const auto& p : link_peers_.items())
            if (p["paired"].as_bool()) {
                list.push_back({p["instance"].as_string(), p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string()});
                online.push_back(p["online"].as_bool());
            }
    if (list.size() == 1) { toast(tr("Aucune autre PS5 jumelée (Appareils › Ajouter une PS5)")); return; }
    std::size_t i = 0;
    for (std::size_t n = 0; n < list.size(); ++n) if (list[n].first == lib_peer_) i = n;
    i = (i + 1) % list.size();
    lib_peer_ = list[i].first;
    lib_marks_.clear();  // les coches valent pour une seule console
    lib_peer_name_ = list[i].second;
    lib_peer_online_ = online[i];
    lib_peer_read_at_ = 0;
    lib_presence_ = 0;
    if (kind_ == kind_ignored) kind_ = 0;
    remote_items_.clear();
    remote_meta_.clear();
    lib_row_ = 0;
    lib_marks_.clear();
    if (!lib_peer_.empty()) load_peer_library(false);
}

void Ui::open_remote_sheet(const std::string& inst, const std::string& name, const Json& item, bool online) {
    rs_inst_ = inst; rs_name_ = name; rs_item_ = item; rs_online_ = online;
    rsheet_ = true; rsheet_focus_ = 0;
    rs_acts_.clear();
    const std::string kind = item["kind"].as_string(), key = item["key"].as_string(), tid = item["title_id"].as_string(), title = item["title"].as_string();
    const std::string here = item["here"].as_string(), save = item["save"].as_string();
    const bool save_only = key.rfind("save:", 0) == 0;
    bool has_save = save_only;
    std::int64_t bytes = item["bytes"].as_int();
    for (const auto& c : item["components"].items()) { if (c["part"].as_string() == "save") has_save = true; if (!item["bytes"].is_number()) bytes += c["bytes"].as_int(); }
    if (online) {
        if ((kind == "game_ps5" || kind == "game_ps4") && !save_only && here != "both") {
            Json g = Json::object();
            g.set("key", key); g.set("title", title); g.set("title_id", tid); g.set("kind", kind); g.set("bytes", bytes);
            rs_acts_.push_back({tr("Récupérer ici…"), [this, inst, name, g] { receive_peer_game(inst, name, g); }});
        }
        if (!tid.empty() && (save == "there" || save == "here")) {
            rs_acts_.push_back({save == "there" ? std::string(tr("Prendre ici sa partie (plus récente là-bas)")) : std::string(tr("Envoyer là-bas la partie d'ici (plus récente ici)")),
                                [this, inst, tid, title] {
                Json b = Json::object(), ids = Json::array();
                ids.push(tid);
                b.set("peer", inst); b.set("title_ids", ids); b.set("one", true);
                model_.command("POST", "/api/v2/sync/parties/start", b.dump(), [this, title](const std::string& e, const std::string&) {
                    post_toast(!e.empty() ? e : trf("Parties de %s : mise à niveau lancée (l'ancienne est sauvegardée d'abord)", title.c_str()));
                });
            }});
        }
        auto remote_backup = [this, inst, name, key, title](const char* action) {
            Json b = Json::object();
            b.set("instance", inst); b.set("action", action); b.set("item", key);
            model_.command("POST", "/api/v2/link/peer-action", b.dump(), [this, name, title](const std::string& e, const std::string&) {
                post_toast(!e.empty() ? e : trf("%s : sauvegarde de %s lancée là-bas", name.c_str(), title.c_str()));
            });
        };
        if (has_save) rs_acts_.push_back({tr("Sauvegarder sa partie là-bas"), [remote_backup] { remote_backup("backup_saves"); }});
        if (!save_only && !item["components"].items().empty()) rs_acts_.push_back({tr("Faire un backup complet là-bas"), [remote_backup] { remote_backup("backup"); }});
        if ((kind == "game_ps5" || kind == "game_ps4" || kind == "app") && !save_only && tid.size() == 9 && tid != "PPSA99950" &&
            (tid.rfind("PPSA", 0) == 0 || tid.rfind("CUSA", 0) == 0)) {
            const std::string detail = item["image_type"].as_string();
            rs_acts_.push_back({trf("Désinstaller de %s…", name.c_str()), [this, inst, name, tid, title, detail] { uninstall_flow(inst, name, tid, title, detail); }});
        }
    }
    rs_acts_.push_back({tr("Fermer"), [] {}});
}

void Ui::input_remote_sheet(std::uint32_t k) {
    const int n = static_cast<int>(rs_acts_.size());
    if (k & key::up) rsheet_focus_ = std::max(0, rsheet_focus_ - 1);
    if (k & key::down) rsheet_focus_ = std::min(n - 1, rsheet_focus_ + 1);
    if (k & key::circle) rsheet_ = false;
    if ((k & key::cross) && rsheet_focus_ < n) {
        const auto fn = rs_acts_[static_cast<std::size_t>(rsheet_focus_)].second;
        rsheet_ = false;
        if (fn) fn();
    }
}

void Ui::draw_remote_sheet(Surface& s) {
    const Json& it = rs_item_;
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 200));
    const int pw = 1180, ph = std::min(860, 290 + 72 * static_cast<int>(rs_acts_.size()) + 40), px = (1920 - pw) / 2, py = (1080 - ph) / 2;
    s.rect(px, py, pw, ph, color::panel, 28);
    s.outline(px, py, pw, ph, color::line, 28, 2);
    const std::string kind = it["kind"].as_string(), tid = it["title_id"].as_string();
    std::string ck = it["cover_key"].as_string();
    if (ck.empty() && !tid.empty()) ck = std::string(kind == "game_ps4" ? "tid:ps4:" : "tid:ps5:") + tid;
    Item tmp{ck, kind, it["title"].as_string(), tid, it["platform"].as_string(), {}, {}, {}, {}, !ck.empty(), {}, {}};
    cover(s, tmp, px + 48, py + 48, 200, false);
    std::string eyebrow = kind == "rom" ? it["platform"].as_string() : kind == "app" ? std::string(tr("Application")) : kind == "game_ps4" ? std::string("PS4") : std::string("PS5");
    if (!tid.empty()) eyebrow += " · " + tid;
    eyebrow += "  ·  " + trf("sur %s", rs_name_.c_str());
    s.text(font_, px + 284, py + 52, eyebrow, small, color::accent);
    s.text(font_, px + 284, py + 80, clean_title(it["title"].as_string()), large, color::text, pw - 330);
    std::int64_t bytes = it["bytes"].as_int();
    if (!it["bytes"].is_number()) for (const auto& c : it["components"].items()) bytes += c["bytes"].as_int();
    std::vector<std::string> chips;
    if (!it["image_type"].as_string().empty()) chips.push_back(it["image_type"].as_string());
    if (bytes > 0) chips.push_back(size_text(static_cast<std::uint64_t>(bytes)));
    const std::string here = it["here"].as_string(), save = it["save"].as_string();
    chips.push_back(here == "both" ? std::string(tr("Aussi ici")) : here == "missing" ? std::string(tr("Absent d'ici")) : std::string(tr("Sauvegarde seule")));
    if (save == "there") chips.push_back(tr("Partie plus récente là-bas"));
    else if (save == "here") chips.push_back(tr("Partie plus récente ici"));
    else if (save == "same") chips.push_back(tr("Parties identiques"));
    else if (save == "conflict") chips.push_back(tr("Parties : à trancher dans Synchro"));
    int cx = px + 284;
    for (const auto& c : chips) {
        const int w = Surface::measure(c, small) + 20;
        if (cx + w > px + pw - 40) break;
        s.rect(cx, py + 134, w, 30, color::panel2, 8);
        s.text(font_, cx + 10, py + 137, c, small, color::text);
        cx += w + 8;
    }
    s.text(font_, px + 284, py + 184, rs_online_ ? trf("Les actions sont faites par %s ; elles apparaissent dans Tâches.", rs_name_.c_str())
                                                 : trf("%s est hors ligne : dernière liste connue, actions indisponibles.", rs_name_.c_str()),
           small, rs_online_ ? color::muted : color::warn, pw - 330);
    int y = py + 290;
    for (int i = 0; i < static_cast<int>(rs_acts_.size()); ++i) {
        const bool on = i == rsheet_focus_;
        s.rect(px + 48, y, pw - 96, 60, on ? color::panel2 : color::bg2, 16);
        if (on) s.outline(px + 44, y - 4, pw - 88, 68, color::accent, 20, 3);
        s.text(font_, px + 76, y + 14, rs_acts_[static_cast<std::size_t>(i)].first, body, on ? color::text : color::muted, pw - 160);
        y += 72;
    }
    draw_footer(s, {{0, tr("Choisir")}, {1, tr("Retour")}});
}

}  // namespace app
