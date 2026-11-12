// Onglet Synchro : des mots qu'on comprend sans connaître l'app.
//   À gauche : les appareils (autres PS5 jumelées, Switch). À droite, pour l'appareil choisi :
//     en haut : où on en est, et UN bouton « Mettre à niveau » ;
//     dessous : des sections séparées — Sauvegardes (active), Parties, Applis, DLC, Réglages (bientôt).
//   « Choisir les jeux » : un interrupteur par jeu, « ajouter aussi les nouveaux jeux automatiquement », tout choisir / tout
//   retirer, et l'envoi du jeu entier UNE SEULE FOIS quand il manque de l'autre côté (ensuite, seule la sauvegarde suit).
//   Copier des jeux entiers, c'est l'onglet Transferts. Rien n'est jamais écrasé ni supprimé ; tout passe par le moteur.
#include <algorithm>
#include <set>
#include <cstdio>
#include <ctime>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

using pbc::Json;
using namespace ui_util;

void draw_checkbox(Surface& s, int x, int cy, bool on, bool dim);  // ui_manage.cpp

namespace {
std::string when_text(std::int64_t t) {
    if (t <= 0) return "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char b[32];
    std::snprintf(b, sizeof b, "%02d/%02d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    return b;
}
// « aujourd'hui 18:41 », « hier 21:40 », sinon « 08/11 21:40 »
std::string day_text(std::int64_t t) {
    if (t <= 0) return "";
    std::time_t tt = static_cast<std::time_t>(t), now = std::time(nullptr);
    std::tm a{}, b{};
    localtime_r(&tt, &a);
    localtime_r(&now, &b);
    char hm[16];
    std::snprintf(hm, sizeof hm, "%02d:%02d", a.tm_hour, a.tm_min);
    const long da = static_cast<long>(a.tm_year) * 400 + a.tm_yday, db = static_cast<long>(b.tm_year) * 400 + b.tm_yday;
    if (da == db) return trf("aujourd'hui %s", hm);
    if (da + 1 == db) return trf("hier %s", hm);
    return when_text(t);
}
std::string play_text(std::int64_t s) {
    if (s < 60) return "";
    return s >= 3600 ? trf("%d h de jeu", static_cast<int>(s / 3600)) : trf("%d min de jeu", static_cast<int>(s / 60));
}
// Petit badge rond sur une jaquette : flèche (↑ part, ↓ arrive) ou coche (au même niveau).
void badge(Surface& s, const Font& f, int x, int y, int st) {
    const std::uint32_t c = st == 0 ? color::ok : color::warn;
    s.rect(x, y, 32, 32, c, 16);
    if (st == 0) {
        for (int d = 0; d < 6; ++d) s.rect(x + 8 + d, y + 15 + d / 2, 3, 3, color::bg, 1);
        for (int d = 0; d < 10; ++d) s.rect(x + 13 + d, y + 18 - d, 3, 3, color::bg, 1);
    } else s.text(f, x + 8, y - 1, st == 1 ? "↑" : st == 2 ? "↓" : "↕", small, color::bg);
}
// Paragraphe coupé aux mots, au plus « max » lignes (la dernière finit par « … » si le texte est plus long).
int para(Surface& s, const Font& f, int x, int y, const std::string& text, FontSize sz, std::uint32_t c, int w, int max, int lh) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush = [&] {
        if (word.empty()) return;
        const std::string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && Surface::measure(t, sz) > w) { lines.push_back(cur); cur = word; } else cur = t;
        word.clear();
    };
    for (char ch : text) { if (ch == ' ') flush(); else word += ch; }
    flush();
    if (!cur.empty()) lines.push_back(cur);
    if (static_cast<int>(lines.size()) > max) { lines.resize(static_cast<std::size_t>(max)); lines.back() += "…"; }
    for (std::size_t i = 0; i < lines.size(); ++i) s.text(f, x, y + static_cast<int>(i) * lh, lines[i], sz, c, w);
    return static_cast<int>(lines.size()) * lh;
}
// Interrupteur (comme sur le web) : vert et rond à droite quand il est allumé.
void toggle(Surface& s, int x, int y, bool on) {
    s.rect(x, y, 88, 48, on ? color::ok : color::line, 24);
    s.rect(on ? x + 46 : x + 6, y + 6, 36, 36, on ? color::text : color::muted, 18);
}
}  // namespace

bool Ui::sy_needs(const Json& t) {
    if (!t["followed"].as_bool()) return false;
    if (t["pull"].as_int() || t["push"].as_int()) return true;
    return t["send_full"].as_bool() && (t["full_push_bytes"].as_int() || t["full_pull_bytes"].as_int());
}

std::vector<Ui::SyDev> Ui::sy_devices() const {
    std::vector<SyDev> out;
    for (const auto& p : peer_games_.items()) out.push_back({"ps5", p["instance"].as_string(), p["name"].as_string()});
    // PS5 jumelées dont les disques sont connus (« peer:<instance>:<disque> ») : présentes même avant la première lecture.
    for (const auto& pl : snap_->places) {
        if (pl.type != "peer" || pl.id.size() < 6) continue;
        const std::string rest = pl.id.substr(5), inst = rest.substr(0, rest.find(':'));
        if (std::any_of(out.begin(), out.end(), [&](const SyDev& d) { return d.id == inst; })) continue;
        std::string name = pl.label.substr(0, pl.label.find(" ("));
        name = name.substr(0, name.find(" · "));
        out.push_back({"ps5", inst, name});
    }
    for (const auto& [id, name] : switches_) out.push_back({"switch", id, name});
    for (const auto& pl : snap_->places) if (pl.type == "ftp") out.push_back({"nas", pl.id, pl.label});  // NAS : sauvegardes ↔ NAS
    return out;
}

const Json* Ui::sy_plan_for(const std::string& peer) const {
    for (const auto& [n, pl] : sync_plans_) if (pl["peer"].as_string() == peer) return &pl;
    return nullptr;
}

// Réglages des jeux choisis : enregistrés par le moteur, puis la liste et la page relues.
void Ui::sy_prefs(const Json& body, const std::string& ok) {
    model_.command("POST", "/api/v2/sync/copies/prefs", body.dump(), [this, ok](const std::string& e, const std::string&) {
        if (!e.empty() || !ok.empty()) post_toast(e.empty() ? ok : e);
        post_ui([this] {
            sync_plans_at_ = 0;
            if (sy_pick_) sy_open_pick(sy_pick_peer_, sy_pick_name_);
        });
    });
}

void Ui::sy_open_pick(const std::string& peer, const std::string& name) {
    const bool again = sy_pick_ && !sy_parties_ && sy_pick_peer_ == peer;
    sy_pick_ = true;
    sy_parties_ = false;
    sy_pick_peer_ = peer;
    sy_pick_name_ = name;
    if (!again) { sn_row_ = 0; sy_pick_plan_ = Json(); sn_all_versions_ = false; }
    model_.command("GET", "/api/v2/sync/copies/plan?peer=" + peer + "&full=1", "", [this, peer](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("réponse illisible")) : e); return; }
        post_ui([this, j, peer] {
            if (sy_pick_peer_ == peer) sy_pick_plan_ = j;
            for (auto& [n, pl] : sync_plans_) if (pl["peer"].as_string() == peer) pl = j;  // la page et la bulle suivent
        });
    });
}

// « Mettre à niveau » : où ranger les sauvegardes qui arrivent ? Une seule possibilité → directement ; plusieurs disques
// (sur l'autre PS5 ou ici) → on demande, le disque proposé par le moteur en premier.
void Ui::sy_start(const std::string& peer, const std::string& name) {
    const Json* pl = sy_plan_for(peer);
    bool to_peer = false, to_here = false;
    if (pl) for (const auto& t : (*pl)["titles"].items()) {
        if (!sy_needs(t)) continue;
        to_peer = to_peer || t["push"].as_int() || (t["send_full"].as_bool() && t["full_push_bytes"].as_int());
        to_here = to_here || t["pull"].as_int() || (t["send_full"].as_bool() && t["full_pull_bytes"].as_int());
    }
    struct Dest { std::string id, label; std::uint64_t free; };
    std::vector<Dest> there, here;
    const std::string pre = "peer:" + peer + ":";
    for (const auto& p : snap_->places) {
        if (p.type == "peer" && p.id.rfind(pre, 0) == 0) {
            std::string lb = p.label.substr(p.label.find(" · ") == std::string::npos ? 0 : p.label.find(" · ") + 3);
            there.push_back({p.id, lb, p.free});
        } else if (p.type == "internal" || p.type == "usb") here.push_back({p.id, p.type == "internal" ? std::string(tr("Interne")) : p.label, p.free});
    }
    auto first = [](std::vector<Dest>& v, const std::string& id) {
        std::stable_partition(v.begin(), v.end(), [&](const Dest& d) { return d.id == id; });
    };
    if (pl) { first(there, (*pl)["peer_place"].as_string()); first(here, (*pl)["here_place"].as_string()); }
    auto go = [this, peer, name](const std::string& here_id, const std::string& there_id) {
        Json b = Json::object();
        b.set("peer", peer); b.set("include_full", true); b.set("followed", true);
        if (!here_id.empty()) b.set("here_place", here_id);
        if (!there_id.empty()) b.set("peer_place", there_id);
        model_.command("POST", "/api/v2/sync/copies/start", b.dump(), [this, name](const std::string& e, const std::string&) {
            post_toast(e.empty() ? trf("Mise à niveau avec %s lancée (suivie sur l'accueil)", name.c_str()) : e);
        });
        sync_copies_at_ = 0;
    };
    // Deuxième question (ici), puis départ
    auto ask_here = [this, here, to_here, go, name](const std::string& there_id) {
        if (!to_here || here.size() <= 1) { go(here.empty() ? std::string() : here[0].id, there_id); return; }
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        for (const auto& d : here) acts.push_back({trf("%s · %s libres", d.label.c_str(), size_text(d.free).c_str()), [go, id = d.id, there_id] { go(id, there_id); }});
        acts.push_back({tr("Annuler"), [] {}});
        open_actions(tr("Où ranger ici les sauvegardes qui arrivent ?"), std::move(acts));
    };
    if (!to_peer || there.size() <= 1) { ask_here(there.empty() ? std::string() : there[0].id); return; }
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    for (const auto& d : there) acts.push_back({trf("%s · %s libres", d.label.c_str(), size_text(d.free).c_str()), [this, ask_here, id = d.id] { post_ui([ask_here, id] { ask_here(id); }); }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Où ranger les sauvegardes sur %s ?", name.c_str()), std::move(acts));
}

// Actions de la page (Accueil › bulle, et la page elle-même).
void Ui::sy_run(const SyDev& d, int action) {
    if (d.kind == "switch") {
        if (action == 0) {  // synchro Switch existante : onglet Parties, sur cette Switch
            int idx = 0;
            for (std::size_t i = 0; i < switches_.size(); ++i) if (switches_[i].first == d.id) idx = static_cast<int>(i);
            tab_ = parties;
            pt_player_ = static_cast<int>(overview_["accounts"].size()) + idx;
            pt_zone_ = 1;
            saves_row_ = 0;
        } else toast(tr("Bientôt : Switch ↔ Switch (les parties passent par Eden sur cette PS5, la plus récente gagne)."));
        return;
    }
    if (action == 0) {  // « Mettre à niveau » (ou « Choisir les jeux » tant qu'aucun n'est choisi)
        const Json* pl = sy_plan_for(d.id);
        int need = 0, chosen = 0;
        if (pl) for (const auto& t : (*pl)["titles"].items()) { need += sy_needs(t); chosen += t["followed"].as_bool(); }
        if (!pl) { toast(tr("Lecture de l'autre PS5…")); return; }
        if (!chosen) { sy_open_pick(d.id, d.name); return; }
        if (!need) { toast(tr("Tout est déjà au même niveau.")); return; }
        if (sync_copies_status_["state"].as_string() == "running") { toast(tr("Une mise à niveau est déjà en cours.")); return; }
        sy_start(d.id, d.name);
        return;
    }
    if (action == 1) { sy_open_pick(d.id, d.name); return; }
    if (action == 2) { sy_open_parties(d.id, d.name, 0); return; }
    if (d.kind == "nas" && action >= 4) { toast(tr("Cette section se synchronise avec une autre PS5, pas avec un NAS.")); return; }
    if (action == 3) { sy_open_parties(d.id, d.name, 1); return; }
    if (action == 4) { sy_open_parties(d.id, d.name, 2); return; }
    if (action == 5) { sy_open_parties(d.id, d.name, 1); sp_filter_ = 6; return; }
    static const char* const soon[] = {N_("Bientôt : reprenez votre partie sur l'autre PS5 ; la plus récente arrive, l'ancienne est gardée."),
                                       N_("Bientôt : une appli mise à jour ici l'est aussi là-bas, jamais de retour en arrière."),
                                       N_("Bientôt : seulement les extensions qui manquent de l'autre côté."),
                                       N_("Bientôt : la configuration de certaines applis, au cas par cas.")};
    if (action >= 2 && action <= 5) toast(tr(soon[action - 2]));
}

// Ancienne liste « copie par copie » (R2 dans « Choisir les jeux ») : pour envoyer aussi d'anciennes versions.
std::vector<std::pair<const Json*, bool>> Ui::sy_pick_rows() const {
    std::vector<std::pair<const Json*, bool>> rows;
    for (const auto& c : sy_pick_plan_["pull"].items()) rows.push_back({&c, true});
    for (const auto& c : sy_pick_plan_["push"].items()) rows.push_back({&c, false});
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const auto& r) { return (*r.first)["full"].as_bool(); }), rows.end());
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        const Json &x = *a.first, &y = *b.first;
        if (x["title"].as_string() != y["title"].as_string()) return x["title"].as_string() < y["title"].as_string();
        return x["created_unix"].as_int() > y["created_unix"].as_int();
    });
    return rows;
}

void Ui::sy_set_follow(const std::vector<std::string>& title_ids, bool on) {
    if (title_ids.empty()) return;
    Json f = Json::object();
    for (const auto& t : title_ids) f.set(t, on);
    Json b = Json::object();
    b.set("peer", sy_pick_peer_);
    b.set("follow", f);
    // Tout de suite à l'écran ; le moteur confirme ensuite.
    Json titles = Json::array();
    for (const auto& t : sy_pick_plan_["titles"].items()) {
        Json x = t;
        if (std::find(title_ids.begin(), title_ids.end(), t["title_id"].as_string()) != title_ids.end()) x.set("followed", on);
        titles.push(x);
    }
    sy_pick_plan_.set("titles", titles);
    sy_prefs(b, "");
}

// Lignes de « Choisir les jeux » : filtre (tous, choisis, à mettre à jour, pas choisis), tri (nom, date).
std::vector<const Json*> Ui::sy_title_rows() const {
    std::vector<const Json*> rows;
    for (const auto& t : sy_pick_plan_["titles"].items()) {
        const bool on = t["followed"].as_bool();
        const bool up = t["push"].as_int() || t["full_push_bytes"].as_int(), down = t["pull"].as_int() || t["full_pull_bytes"].as_int();
        if ((sn_filter_ == 1 && !on) || (sn_filter_ == 2 && !sy_needs(t)) || (sn_filter_ == 3 && on) || (sn_filter_ == 4 && !up) || (sn_filter_ == 5 && !down)) continue;
        rows.push_back(&t);
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const Json* a, const Json* b) {
        if (sn_sort_ == 1) return std::max((*a)["here_at"].as_int(), (*a)["there_at"].as_int()) > std::max((*b)["here_at"].as_int(), (*b)["there_at"].as_int());
        return (*a)["title"].as_string() < (*b)["title"].as_string();
    });
    return rows;
}

void Ui::input_synchro(std::uint32_t k) {
    const auto devs = sy_devices();
    if (devs.empty()) return;
    sn_card_ = std::clamp(sn_card_, 0, static_cast<int>(devs.size()) - 1);
    const SyDev& d = devs[static_cast<std::size_t>(sn_card_)];
    if (sy_pick_ && sy_parties_) { input_synchro_parties(k); return; }
    if (sy_pick_ && sn_all_versions_) {  // anciennes versions : copie par copie, ✕ pour choisir, △ pour envoyer
        const auto rows = sy_pick_rows();
        const int n = static_cast<int>(rows.size());
        if (k & (key::circle | key::r2)) { sn_all_versions_ = false; sn_row_ = 0; sy_pick_versions_.clear(); return; }
        sn_row_ = std::clamp(sn_row_, 0, std::max(0, n - 1));
        if (n && (k & key::up)) sn_row_ = (sn_row_ + n - 1) % n;
        if (n && (k & key::down)) sn_row_ = (sn_row_ + 1) % n;
        if ((k & key::cross) && sn_row_ < n) {
            const std::string b = (*rows[static_cast<std::size_t>(sn_row_)].first)["bundle"].as_string();
            if (!sy_pick_versions_.erase(b)) sy_pick_versions_.insert(b);
        }
        if ((k & key::triangle) && !sy_pick_versions_.empty()) {
            Json b = Json::object(), list = Json::array();
            for (const auto& x : sy_pick_versions_) list.push(x);
            b.set("peer", sy_pick_peer_); b.set("include_full", true); b.set("bundles", list);
            const std::string name = sy_pick_name_;
            model_.command("POST", "/api/v2/sync/copies/start", b.dump(), [this, name](const std::string& e, const std::string&) {
                post_toast(e.empty() ? trf("Mise à niveau avec %s lancée (suivie sur l'accueil)", name.c_str()) : e);
            });
            sync_copies_at_ = 0;
            sy_pick_versions_.clear();
            sn_all_versions_ = false;
            sy_pick_ = false;
        }
        return;
    }
    if (sy_pick_) {  // « Choisir les jeux » : ligne 0 = nouveaux jeux automatiquement, puis un jeu par ligne
        const auto rows = sy_title_rows();
        const int n = static_cast<int>(rows.size()) + 1;
        if (k & key::circle) { sy_pick_ = false; return; }
        if (k & key::square) { sn_filter_ = (sn_filter_ + 1) % 6; sn_row_ = 0; return; }
        if (k & key::l2) { sn_sort_ = sn_sort_ == 1 ? 0 : 1; sn_row_ = 0; return; }
        if (k & key::r2) { sn_all_versions_ = true; sn_row_ = 0; return; }
        sn_row_ = std::clamp(sn_row_, 0, n - 1);
        if (k & key::up) sn_row_ = (sn_row_ + n - 1) % n;  // la liste boucle
        if (k & key::down) sn_row_ = (sn_row_ + 1) % n;
        if (k & key::triangle) {  // tout choisir / tout retirer (tous les jeux)
            bool all = !sy_pick_plan_["titles"].items().empty();
            for (const auto& t : sy_pick_plan_["titles"].items()) all = all && t["followed"].as_bool();
            Json b = Json::object();
            b.set("peer", sy_pick_peer_); b.set("follow_all", !all);
            Json titles = Json::array();
            for (const auto& t : sy_pick_plan_["titles"].items()) { Json x = t; x.set("followed", !all); titles.push(x); }
            sy_pick_plan_.set("titles", titles);
            sy_prefs(b, all ? std::string(tr("Plus aucun jeu choisi : rien ne se synchronise")) : std::string(tr("Tous les jeux sont choisis")));
            return;
        }
        if (k & key::cross) {
            if (sn_row_ == 0) {  // nouveaux jeux automatiquement (les choix actuels sont gardés)
                const bool on = !sy_pick_plan_["auto_new"].as_bool();
                sy_pick_plan_.set("auto_new", on);
                Json b = Json::object();
                b.set("peer", sy_pick_peer_); b.set("auto_new", on);
                sy_prefs(b, on ? std::string(tr("Les nouveaux jeux seront choisis tout seuls")) : std::string(tr("Les nouveaux jeux seront à choisir")));
            } else {
                const Json& t = *rows[static_cast<std::size_t>(sn_row_ - 1)];
                sy_set_follow({t["title_id"].as_string()}, !t["followed"].as_bool());
            }
        }
        if ((k & key::options) && sn_row_ > 0) {  // le jeu entier aussi, une seule fois (s'il manque de l'autre côté)
            const Json& t = *rows[static_cast<std::size_t>(sn_row_ - 1)];
            if (t["followed"].as_bool() && (t["full_push_bytes"].as_int() || t["full_pull_bytes"].as_int())) {
                Json f = Json::object(), b = Json::object();
                f.set(t["title_id"].as_string(), !t["send_full"].as_bool());
                b.set("peer", sy_pick_peer_); b.set("send_full", f);
                sy_prefs(b, "");
                Json titles = Json::array();
                for (const auto& x : sy_pick_plan_["titles"].items()) {
                    Json y = x;
                    if (x["title_id"].as_string() == t["title_id"].as_string()) y.set("send_full", !t["send_full"].as_bool());
                    titles.push(y);
                }
                sy_pick_plan_.set("titles", titles);
            }
        }
        return;
    }
    if (sn_zone_ == 0) {  // appareils, puis la carte « Jeux entiers » (la liste boucle)
        const int nd = static_cast<int>(devs.size());
        if (k & key::up) { if (sn_games_) { sn_games_ = false; sn_card_ = nd - 1; } else if (sn_card_ == 0) sn_games_ = true; else --sn_card_; }
        if (k & key::down) { if (sn_games_) { sn_games_ = false; sn_card_ = 0; } else if (sn_card_ == nd - 1) sn_games_ = true; else ++sn_card_; }
        if (sn_games_ && (k & key::cross)) { tab_ = transfers; return; }
        if (!sn_games_ && (k & (key::right | key::cross))) { sn_zone_ = 1; sn_target_ = 0; }
        return;
    }
    if (k & key::circle) { sn_zone_ = 0; return; }
    if (d.kind == "switch") {  // deux cibles : Eden (0), Switch ↔ Switch (1)
        if (k & key::left) { if (sn_target_ == 0) sn_zone_ = 0; else sn_target_ = 0; }
        if (k & (key::right | key::down)) sn_target_ = 1;
        if (k & key::up) sn_target_ = 0;
        if (k & key::cross) sy_run(d, sn_target_);
        return;
    }
    // Grille : 0 = Mettre à niveau ; 1 = Sauvegardes (grande carte) ; 2 Parties, 3 Applis (ligne 1) ; 4 DLC, 5 Réglages (ligne 2)
    static const int up[6] = {0, 0, 0, 0, 2, 3}, down[6] = {1, 1, 4, 5, 4, 5}, left[6] = {-1, -1, 1, 2, 1, 4}, right[6] = {0, 2, 3, 3, 5, 5};
    sn_target_ = std::clamp(sn_target_, 0, 5);
    if (k & key::up) sn_target_ = up[sn_target_];
    else if (k & key::down) sn_target_ = down[sn_target_];
    else if (k & key::left) { if (left[sn_target_] < 0) sn_zone_ = 0; else sn_target_ = left[sn_target_]; }
    else if (k & key::right) sn_target_ = right[sn_target_];
    if (k & key::cross) sy_run(d, sn_target_);
}

void Ui::draw_synchro(Surface& s) {
    const int x = margin, w = 1920 - 2 * margin;
    load_sync_plans();  // relu ici aussi : un changement de jeux choisis se voit tout de suite
    if (sy_pick_) { draw_synchro_pick(s, x, w); return; }
    {
        const int tw = s.text(font_, margin, 112, tr("Synchro"), title, color::text);
        s.text(font_, margin + tw + 40, 146, tr("Vos appareils au même niveau, sans jamais rien recopier deux fois."), small, color::muted, w - tw - 40);
    }
    const auto devs = sy_devices();
    if (devs.empty()) {
        s.text(font_, x, 240, tr("Aucun appareil avec qui se synchroniser : jumelez une autre PS5 (Appareils) ou ajoutez une Switch."), body, color::muted, w);
        draw_footer(s, {{-1, tr("L1 / R1  onglets")}});
        return;
    }
    sn_card_ = std::clamp(sn_card_, 0, static_cast<int>(devs.size()) - 1);
    const SyDev& d = devs[static_cast<std::size_t>(sn_card_)];
    const Json* pl = d.kind != "switch" ? sy_plan_for(d.id) : nullptr;
    // ---- Appareils (colonne de gauche) ----
    const int lw = 400, ch = 112, top = 230;
    const int fit = std::max(1, (1080 - 120 - 166 - top) / (ch + 16));  // ce qui tient au-dessus de la carte « Jeux entiers »
    const int nd = static_cast<int>(devs.size());
    const int dfirst = nd <= fit ? 0 : std::clamp(sn_card_ - fit / 2, 0, nd - fit);
    int y = top;
    if (dfirst > 0) s.text(font_, x + lw / 2 - 10, top - 34, "↑", small, color::muted);
    for (std::size_t i = static_cast<std::size_t>(dfirst); i < devs.size() && static_cast<int>(i) < dfirst + fit; ++i, y += ch + 16) {
        const bool sel = static_cast<int>(i) == sn_card_, focus = sel && sn_zone_ == 0 && !sn_games_;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(x - g * 5, y - g * 5, lw + g * 10, ch + g * 10, rgb(80, 150, 255, 20), 24 + g * 5);
        s.rect(x, y, lw, ch, sel ? color::panel2 : color::panel, 22);
        s.outline(x, y, lw, ch, focus ? color::accent : sel ? color::accent2 : color::line, 22, focus ? 4 : 2);
        const auto& dv = devs[i];
        const int dev = dv.kind == "switch" ? dev_switch : dv.kind == "nas" ? dev_nas : dev_internal;
        int tx = x + 22;
        if (devices_[dev].valid()) { s.image(devices_[dev], x + 14, y + 14, 84, 84); tx = x + 112; }
        s.text(font_, tx, y + 16, dv.name, body, color::text, x + lw - tx - 14);
        std::string sub;
        std::uint32_t tone = color::muted;
        if (dv.kind == "switch") sub = tr("Switch · avec Eden");
        else if (dv.kind == "nas" && !sy_plan_for(dv.id)) sub = tr("NAS · lecture…");
        else if (const Json* p = sy_plan_for(dv.id)) {
            int need = 0, chosen = 0;
            for (const auto& t : (*p)["titles"].items()) { need += sy_needs(t); chosen += t["followed"].as_bool(); }
            sub = !chosen ? std::string(tr("aucun jeu choisi")) : need ? trf("%d à mettre à jour", need) : std::string(tr("au même niveau"));
            tone = !chosen ? color::muted : need ? color::warn : color::ok;
        } else sub = tr("lecture…");
        s.text(font_, tx, y + 62, sub, small, tone, x + lw - tx - 14);
    }
    {   // Jeux entiers : ce n'est pas de la synchro, c'est Transferts (copier, compresser, envoyer) — la carte y mène
        if (dfirst + fit < nd) s.text(font_, x + lw / 2 - 10, y - 14, "↓", small, color::muted);
        const int gyy = std::max(y + 8, top + 3 * (ch + 16) + 8), gh2 = 150;
        const bool f = sn_zone_ == 0 && sn_games_;
        if (f) for (int g = 3; g >= 1; --g) s.rect(x - g * 5, gyy - g * 5, lw + g * 10, gh2 + g * 10, rgb(80, 150, 255, 20), 24 + g * 5);
        s.rect(x, gyy, lw, gh2, color::panel, 22);
        s.outline(x, gyy, lw, gh2, f ? color::accent : color::line, 22, f ? 4 : 1);
        s.text(font_, x + 22, gyy + 16, tr("Jeux entiers ›"), body, color::text, lw - 44);
        para(s, font_, x + 22, gyy + 58, tr("Copier, compresser, envoyer un jeu : c'est l'onglet Transferts."), small, color::muted, lw - 44, 3, 28);
    }
    // ---- À droite : où on en est, et un bouton ----
    const int rx = x + lw + 34, rw = 1920 - margin - rx, hy = top, hh = 244;
    const bool zone = sn_zone_ == 1;
    s.rect(rx, hy, rw, hh, color::panel, 28);                 // couleurs du thème (clair comme sombre)
    s.rect(rx, hy, rw, hh, rgb(95, 178, 255, 26), 28);         // légère teinte bleue, jamais grise
    s.outline(rx, hy, rw, hh, color::accent2, 28, 2);
    {
        const int iw = 104;
        if (devices_[dev_internal].valid()) s.image(devices_[dev_internal], rx + 30, hy + 50, iw, iw);
        s.text(font_, rx + 54, hy + 52 + iw + 6, tr("Cette PS5"), small, color::muted);
        s.text(font_, rx + 30 + iw + 12, hy + 82, "↔", large, color::accent);
        const int dev2 = d.kind == "switch" ? dev_switch : d.kind == "nas" ? dev_nas : dev_internal;
        if (devices_[dev2].valid()) s.image(devices_[dev2], rx + 30 + iw + 70, hy + 50, iw, iw);
        const std::string nm = d.name.size() > 14 ? d.name.substr(0, 13) + "…" : d.name;
        s.text(font_, rx + 30 + iw + 70 + iw / 2 - Surface::measure(nm, small) / 2, hy + 52 + iw + 6, nm, small, color::muted);
    }
    const int tx = rx + 30 + 104 * 2 + 130, bw_max = 360;
    std::string eyebrow = trf("AVEC %s", d.name.c_str()), head, sub, btn = tr("Mettre à niveau"), last;
    std::uint32_t head_c = color::text;
    const Json& st = sync_copies_status_;
    const bool running = d.kind != "switch" && st["peer"].as_string() == d.id && st["state"].as_string() == "running";
    if (d.kind == "switch") {
        eyebrow = tr("AVEC EDEN SUR CETTE PS5");
        head = tr("Les parties de la Switch et d'Eden");
        sub = tr("La plus récente gagne, jeu par jeu : la synchro Switch, ouverte d'ici.");
        btn = tr("Ouvrir");
    } else if (!pl) {
        head = d.kind == "nas" ? tr("Lecture du NAS…") : tr("Lecture de l'autre PS5…");
        sub = d.kind == "nas" ? tr("Il doit être allumé et joignable.") : tr("Elle doit être allumée, sur le même réseau.");
    } else {
        int need = 0, chosen = 0;
        std::int64_t bytes = 0;
        std::vector<std::string> names;
        for (const auto& t : (*pl)["titles"].items()) {
            chosen += t["followed"].as_bool();
            if (!sy_needs(t)) continue;
            ++need;
            bytes += t["bytes"].as_int();
            if (t["send_full"].as_bool()) bytes += t["full_push_bytes"].as_int() + t["full_pull_bytes"].as_int();
            if (names.size() < 3) names.push_back(t["title"].as_string());
        }
        if (running) {
            head = trf("Mise à niveau en cours : %lld / %lld", static_cast<long long>(st["done"].as_int()), static_cast<long long>(st["total"].as_int()));
            sub = st["current"].as_string();
            btn = "";
        } else if (!chosen) {
            head = tr("Aucun jeu choisi");
            sub = d.kind == "nas" ? tr("Choisissez les jeux dont les sauvegardes sont aussi gardées sur le NAS (et inversement).") : tr("Choisissez les jeux dont les sauvegardes sont copiées sur les deux PS5.");
            btn = tr("Choisir les jeux");
            head_c = color::muted;
        } else if (!need) {
            head = tr("Tout est au même niveau");
            sub = trf("%d jeu(x) choisi(s) : les mêmes sauvegardes des deux côtés.", chosen);
            head_c = color::ok;
        } else {
            head = trf("%d jeu(x) à mettre à jour", need);
            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) list += (i ? ", " : "") + names[i];
            if (need > static_cast<int>(names.size())) list += "…";
            sub = list + " · " + size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, bytes)));
        }
        if (st["peer"].as_string() == d.id && !running) {
            const std::string state = st["state"].as_string();
            if (state == "done") last = trf("Dernière fois : %s · tout s'est bien passé", day_text(st["finished"].as_int()).c_str());
            else if (!state.empty()) last = trf("Dernière fois : %s · %lld erreur(s)", day_text(st["finished"].as_int()).c_str(), static_cast<long long>(st["errors"].size()));
        }
    }
    const int bw = btn.empty() ? 0 : std::min(bw_max, Surface::measure(btn, body) + 70);
    const int textw = rx + rw - tx - (bw ? bw + 50 : 30);
    s.text(font_, tx, hy + 44, eyebrow, small, color::accent, textw);
    s.text(font_, tx, hy + 80, head, large, head_c, textw);
    para(s, font_, tx, hy + 136, sub, small, color::muted, textw, 2, 30);
    if (running && st["total"].as_int() > 0) {
        s.rect(tx, hy + 190, textw, 10, color::line, 5);
        s.rect(tx, hy + 190, static_cast<int>(textw * st["done"].as_int() / st["total"].as_int()), 10, color::accent, 5);
    }
    if (bw) {
        const int bx = rx + rw - 36 - bw, by = hy + 70;
        const bool f = zone && sn_target_ == 0;
        if (f) s.outline(bx - 6, by - 6, bw + 12, 84, color::accent, 42, 4);
        s.rect(bx, by, bw, 72, f ? color::text : color::panel2, 36);
        s.text(font_, bx + (bw - Surface::measure(btn, body)) / 2, by + 18, btn, body, f ? color::bg : color::text);
        if (!last.empty()) s.text(font_, rx + rw - 36 - Surface::measure(last, small), hy + 166, last, small, color::muted);
    }
    // ---- Sections ----
    const int gy = hy + hh + 26, gh = 1080 - 120 - gy, gap = 18;
    if (d.kind == "switch") {
        const int cw = (rw - gap) / 2;
        const bool f = zone && sn_target_ == 1;
        s.rect(rx, gy, cw, 236, color::panel, 24);
        s.outline(rx, gy, cw, 236, f ? color::accent : color::line, 24, f ? 4 : 1);
        s.text(font_, rx + 26, gy + 24, tr("Switch ↔ Switch"), large, color::muted);
        para(s, font_, rx + 26, gy + 88, tr("Deux Switch au même niveau, en passant par Eden sur cette PS5."), small, color::muted, cw - 52, 3, 30);
        const std::string b = tr("Bientôt");
        s.rect(rx + cw - 40 - Surface::measure(b, small), gy + 28, Surface::measure(b, small) + 24, 38, color::line, 19);
        s.text(font_, rx + cw - 28 - Surface::measure(b, small), gy + 31, b, small, color::muted);
        draw_footer(s, {{0, tr("Valider")}, {-1, tr("Haut / bas : appareils")}, {1, tr("Retour")}});
        return;
    }
    const int bigw = (rw - 2 * gap) * 155 / 355, smw = (rw - 2 * gap - bigw) / 2, smh = (gh - gap) / 2;
    {   // Sauvegardes : la grande carte, la seule active pour l'instant
        const bool f = zone && sn_target_ == 1;
        if (f) for (int g = 3; g >= 1; --g) s.rect(rx - g * 5, gy - g * 5, bigw + g * 10, gh + g * 10, rgb(80, 150, 255, 18), 24 + g * 5);
        s.rect(rx, gy, bigw, gh, color::panel, 24);
        s.rect(rx, gy, bigw, gh, rgb(79, 209, 139, 18), 24);  // légère teinte verte
        s.outline(rx, gy, bigw, gh, f ? color::accent : rgb(79, 209, 139, 90), 24, f ? 4 : 2);
        s.rect(rx + 26, gy + 26, 10, 40, color::ok, 5);
        s.text(font_, rx + 50, gy + 22, tr("Sauvegardes"), large, color::text);
        int need = 0, chosen = 0;
        std::vector<const Json*> shown;  // jaquettes : d'abord ce qui bouge, puis le reste des jeux choisis
        if (pl) {
            for (const auto& t : (*pl)["titles"].items()) if (sy_needs(t)) { ++need; shown.push_back(&t); }
            for (const auto& t : (*pl)["titles"].items()) if (t["followed"].as_bool()) { ++chosen; if (!sy_needs(t)) shown.push_back(&t); }
        }
        if (pl) {
            const std::string chip = !chosen ? std::string(tr("aucun jeu choisi")) : need ? trf("%d à mettre à jour", need) : std::string(tr("au même niveau"));
            const std::uint32_t cc = !chosen ? color::muted : need ? color::warn : color::ok;
            const int cw = Surface::measure(chip, small) + 32;
            s.rect(rx + bigw - 26 - cw, gy + 28, cw, 40, rgb(255, 255, 255, 14), 20);
            s.text(font_, rx + bigw - 26 - cw + 16, gy + 32, chip, small, cc);
        }
        para(s, font_, rx + 26, gy + 84, (d.kind == "nas" ? tr("Des copies de vos parties (onglet Sauvegardes), gardées sur cette PS5 et sur le NAS. Pas jouables telles quelles : on en restaure une pour jouer.") : tr("Des copies de vos parties (onglet Sauvegardes), rangées sur les deux PS5. Pas jouables telles quelles : on en restaure une pour jouer. Ne touche pas aux parties actives.")),
             small, color::text, bigw - 52, 4, 28);
        const int cy = gy + 208, cs = 90;
        const int fit = std::max(1, (bigw - 52 + 12) / (cs + 12));
        const int nshow = static_cast<int>(shown.size()) > fit ? fit - 1 : static_cast<int>(shown.size());
        for (int i = 0; i < nshow; ++i) {
            const Json& t = *shown[static_cast<std::size_t>(i)];
            Item tmp;
            const Item* it = find_or_stand_in("title:" + t["title_id"].as_string(), t["title"].as_string(), t["title_id"].as_string(), tmp, t["kind"].as_string());
            const int cx = rx + 26 + i * (cs + 12);
            cover(s, *it, cx, cy, cs, false);
            const std::string stt = t["state"].as_string();
            badge(s, font_, cx + cs - 24, cy - 8, !sy_needs(t) ? 0 : stt == "push" ? 1 : stt == "pull" ? 2 : 3);
        }
        if (static_cast<int>(shown.size()) > nshow && nshow > 0) {
            const int cx = rx + 26 + nshow * (cs + 12);
            s.rect(cx, cy, cs, cs, rgb(255, 255, 255, 14), 14);
            const std::string more = "+" + std::to_string(shown.size() - static_cast<std::size_t>(nshow));
            s.text(font_, cx + (cs - Surface::measure(more, large)) / 2, cy + 26, more, large, color::muted);
        }
        if (shown.empty() && pl) s.text(font_, rx + 26, cy + 30, tr("Aucun jeu choisi pour l'instant."), body, color::muted);
        const int ly = cy + cs + 20;
        const std::string cnt = trf("%d jeu(x) choisi(s)", chosen);
        s.rect(rx + 26, ly, Surface::measure(cnt, small) + 32, 42, rgb(95, 178, 255, 36), 21);
        s.text(font_, rx + 42, ly + 5, cnt, small, color::accent);
        const std::string link = tr("Choisir les jeux ›");
        s.text(font_, rx + bigw - 26 - Surface::measure(link, body), ly + 2, link, body, f ? color::text : color::accent);
        para(s, font_, rx + 26, ly + 54, (d.kind == "nas" ? tr("↑ part vers le NAS · ↓ arrive ici · ✓ déjà au même niveau") : tr("↑ part vers l'autre PS5 · ↓ arrive ici · ✓ déjà au même niveau")), small, color::muted, bigw - 52, 2, 28);
    }
    struct Soon { const char* name; const char* text; std::uint32_t c; };
    static const Soon soon[4] = {{N_("Parties actives"), N_("Reprenez votre partie sur l'autre PS5 : la plus récente arrive, l'ancienne est gardée."), rgb(95, 178, 255)},
                                 {N_("Applis"), N_("Une appli mise à jour ici l'est aussi là-bas. Jamais de retour en arrière."), rgb(155, 124, 255)},
                                 {N_("DLC"), N_("Seulement les extensions qui manquent de l'autre côté, rien d'autre."), rgb(255, 180, 77)},
                                 {N_("Réglages"), N_("La configuration de certaines applis, au cas par cas."), rgb(170, 185, 205)}};
    for (int i = 0; i < 4; ++i) {
        const int cx = rx + bigw + gap + (i % 2) * (smw + gap), cy = gy + (i / 2) * (smh + gap);
        const bool f = zone && sn_target_ == 2 + i;
        if (d.kind == "nas" && i >= 2) {  // DLC et Réglages : entre deux PS5 seulement
            s.rect(cx, cy, smw, smh, color::panel, 24);
            s.outline(cx, cy, smw, smh, f ? color::accent : color::line, 24, f ? 4 : 1);
            s.rect(cx + 24, cy + 26, 10, 36, soon[i].c, 5);
            s.text(font_, cx + 48, cy + 18, tr(soon[i].name), large, color::text, smw - 72);
            s.text(font_, cx + 48, cy + 64, tr("Entre deux PS5"), small, color::muted);
            para(s, font_, cx + 24, cy + 102, tr("Cette section se synchronise avec une autre PS5, pas avec un NAS."), small, color::muted, smw - 48, 4, 28);
            continue;
        }
        if (i == 2) {  // DLC : ce qui manque d'un côté (lecture seule pour l'instant)
            const Json* dp = nullptr;
            for (const auto& [n2, pl2] : dlc_plans_) if (pl2["peer"].as_string() == d.id) dp = &pl2;
            const std::int64_t nh = dp ? (*dp)["summary"]["to_complete_here"].as_int() : 0, nt = dp ? (*dp)["summary"]["to_complete_there"].as_int() : 0;
            s.rect(cx, cy, smw, smh, color::panel, 24);
            s.outline(cx, cy, smw, smh, f ? color::accent : rgb(255, 178, 60, 90), 24, f ? 4 : 2);
            s.rect(cx + 24, cy + 26, 10, 36, soon[i].c, 5);
            s.text(font_, cx + 48, cy + 18, tr(soon[i].name), large, color::text, smw - 72);
            std::string line;
            std::uint32_t lc = color::muted;
            const Json& ps2 = parties_status_;
            if (ps2["state"].as_string() == "running" && ps2["peer"].as_string() == d.id && ps2["section"].as_string() == "dlc") { line = trf("En cours : %lld / %lld", static_cast<long long>(ps2["done"].as_int()), static_cast<long long>(ps2["total"].as_int())); lc = color::accent; }
            else if (!dp) line = tr("lecture…");
            else if (dp->has("error")) { line = tr("autre PS5 injoignable"); lc = color::warn; }
            else if (nh || nt) { line = trf("%lld jeu(x) à compléter", static_cast<long long>(nh + nt)); lc = color::warn; }
            else { line = tr("rien ne manque"); lc = color::ok; }
            s.text(font_, cx + 48, cy + 64, line, small, lc, smw - 72);
            para(s, font_, cx + 24, cy + 102, tr("Les extensions et mises à jour qui manquent d'un côté, sans jamais renvoyer le jeu entier."), small, color::muted, smw - 48, 4, 28);
            continue;
        }
        if (i == 3) {  // Réglages : les applis qui ont des réglages (même écran que Applis, filtre « Avec réglages »)
            const Json* ap = nullptr;
            for (const auto& [n2, pl2] : apps_plans_) if (pl2["peer"].as_string() == d.id) ap = &pl2;
            int with = 0, moving = 0;
            if (ap) for (const auto& t : (*ap)["titles"].items()) {
                const std::string cs = t["config_state"].as_string();
                if (!cs.empty() && cs != "undefined") { ++with; moving += t["followed"].as_bool() && t["config_moves"].as_bool(); }
            }
            s.rect(cx, cy, smw, smh, color::panel, 24);
            s.outline(cx, cy, smw, smh, f ? color::accent : rgb(170, 185, 205, 90), 24, f ? 4 : 2);
            s.rect(cx + 24, cy + 26, 10, 36, soon[i].c, 5);
            s.text(font_, cx + 48, cy + 18, tr(soon[i].name), large, color::text, smw - 72);
            const bool aerr = ap && ap->has("error");
            s.text(font_, cx + 48, cy + 64, !ap ? std::string(tr("lecture…")) : aerr ? std::string(tr("autre PS5 injoignable")) : moving ? trf("%d à mettre à niveau", moving) : trf("%d appli(s) avec réglages", with), small, aerr || moving ? color::warn : color::muted);
            para(s, font_, cx + 24, cy + 102, tr("Les réglages des applis : appli + réglages ou réglages seuls, selon un profil par appli."), small, color::muted, smw - 48, 4, 28);
            continue;
        }
        if (i <= 1) {  // Parties actives, Applis : actives — combien choisis, combien à mettre à niveau, ou l'avancement
            const Json* pp = nullptr;
            if (i == 0) pp = parties_plan_for(d.id);
            else for (const auto& [n2, pl2] : apps_plans_) if (pl2["peer"].as_string() == d.id) pp = &pl2;
            const Json& ps = parties_status_;
            s.rect(cx, cy, smw, smh, color::panel, 24);
            s.rect(cx, cy, smw, smh, rgb(95, 178, 255, 16), 24);
            s.outline(cx, cy, smw, smh, f ? color::accent : rgb(95, 178, 255, 90), 24, f ? 4 : 2);
            s.rect(cx + 24, cy + 26, 10, 36, soon[i].c, 5);
            s.text(font_, cx + 48, cy + 18, tr(soon[i].name), large, color::text, smw - 72);
            std::string line;
            std::uint32_t lc = color::muted;
            if (ps["state"].as_string() == "running" && ps["peer"].as_string() == d.id && (ps["section"].as_string() == "apps") == (i == 1)) { line = trf("En cours : %lld / %lld", static_cast<long long>(ps["done"].as_int()), static_cast<long long>(ps["total"].as_int())); lc = color::accent; }
            else if (!pp) line = tr("lecture…");
            else if (pp->has("error")) { line = d.kind == "nas" ? tr("NAS injoignable") : tr("autre PS5 injoignable"); lc = color::warn; }
            else if (!(*pp)["chosen"].as_int()) line = i == 1 ? tr("aucune appli choisie") : tr("aucun jeu choisi");
            else if ((*pp)["need"].as_int()) { line = trf("%lld à mettre à niveau", static_cast<long long>((*pp)["need"].as_int())); lc = color::warn; }
            else { line = tr("au même niveau"); lc = color::ok; }
            s.text(font_, cx + 48, cy + 64, line, small, lc);
            para(s, font_, cx + 24, cy + 102, d.kind == "nas" ? (i == 1 ? tr("Le NAS garde la dernière version de chaque appli choisie ; une PS5 en retard l'installe depuis le NAS.")
                                                                        : tr("Le NAS garde la partie la plus récente ; une PS5 en retard la récupère, la sienne d'abord sauvegardée."))
                                               : (i == 1 ? tr("La version la plus récente d'une appli remplace l'ancienne de l'autre côté. Même version : rien.")
                                                         : tr("La partie la plus récente arrive sur l'autre PS5 ; celle qu'elle remplace est d'abord sauvegardée.")), small, color::muted, smw - 48, 4, 28);
            continue;
        }
        s.rect(cx, cy, smw, smh, color::panel, 24);
        s.outline(cx, cy, smw, smh, f ? color::accent : color::line, 24, f ? 4 : 1);
        s.rect(cx + 24, cy + 26, 10, 36, soon[i].c, 5);
        s.text(font_, cx + 48, cy + 18, tr(soon[i].name), large, color::text, smw - 72);  // le nom entier (« Parties actives »)
        s.text(font_, cx + 48, cy + 64, tr("Bientôt"), small, soon[i].c);                  // « Bientôt » dessous, à la couleur de la section
        para(s, font_, cx + 24, cy + 102, tr(soon[i].text), small, color::muted, smw - 48, 4, 28);
    }
    if (sn_zone_ == 0) draw_footer(s, {{0, sn_games_ ? tr("Ouvrir Transferts") : tr("Choisir")}, {-1, tr("Haut / bas : appareils")}});
    else if (sn_target_ == 0) draw_footer(s, {{0, btn.empty() ? tr("Valider") : btn.c_str()}, {-1, tr("Bas : les sections")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, sn_target_ == 1 ? tr("Choisir les jeux") : tr("Valider")}, {-1, tr("Flèches : sections")}, {1, tr("Retour")}});
}

const Json* Ui::parties_plan_for(const std::string& peer) const {
    for (const auto& [n, pl] : parties_plans_) if (pl["peer"].as_string() == peer) return &pl;
    return nullptr;
}

void Ui::sy_open_parties(const std::string& peer, const std::string& name, int section) {
    if (section < 0) section = sy_section_;
    const bool again = sy_pick_ && sy_parties_ && sy_pick_peer_ == peer && sy_section_ == section;
    sy_pick_ = true;
    sy_parties_ = true;
    sy_section_ = section;
    sy_pick_peer_ = peer;
    sy_pick_name_ = name;
    if (!again) { sn_row_ = 0; sy_pick_plan_ = Json(); sp_filter_ = 0; }
    const std::string path = section == 2 ? "/api/v2/sync/dlc/plan?peer=" : section == 1 ? "/api/v2/sync/apps/plan?peer=" : "/api/v2/sync/parties/plan?peer=";
    model_.command("GET", path + peer, "", [this, peer, section](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("réponse illisible")) : e); return; }
        post_ui([this, j, peer, section] {
            if (sy_pick_peer_ == peer && sy_parties_ && sy_section_ == section) sy_pick_plan_ = j;
            for (auto& [n, pl] : section == 2 ? dlc_plans_ : section == 1 ? apps_plans_ : parties_plans_) if (pl["peer"].as_string() == peer) pl = j;
        });
    });
}

// « Mettre à niveau » des parties : ce qui va se passer, jeu par jeu, puis confirmation (on écrit dans les consoles).
void Ui::sy_parties_start(const std::string& peer, const std::string& name) {
    const Json& pl = sy_pick_plan_;
    std::string note;
    int n = 0;
    for (const auto& t : pl["titles"].items()) {
        if (!t["followed"].as_bool()) continue;
        if (sy_section_ == 0 && (t["state"].as_string() == "same" || t["state"].as_string() == "conflict")) continue;  // conflit : à trancher d'abord
        if (t["running"].as_bool()) { toast(trf("« %s » est lancé : fermez le jeu d'abord.", t["title"].as_string().c_str())); return; }
        const std::string st0 = t["state"].as_string();
        if (sy_section_ == 1 && !(t["app_moves"].as_bool() || t["config_moves"].as_bool())) continue;  // applis : rien à faire pour elle
        const bool up = st0 == "here_newer" || st0 == "only_here";
        if (n < 4) {
            std::string what;
            if (sy_section_ == 1 && !t["app_moves"].as_bool()) what = t["config_state"].as_string() == "here_newer" ? trf("réglages d'ici → %s", name.c_str()) : trf("réglages de %s → ici", name.c_str());
            else if (sy_section_ == 1) {
                const std::string vh = t["here_version"].as_string(), vt = t["there_version"].as_string();
                what = up ? (vt.empty() ? trf("installée sur %s (v%s)", name.c_str(), vh.c_str()) : trf("v%s d'ici remplace v%s sur %s", vh.c_str(), vt.c_str(), name.c_str()))
                          : (vh.empty() ? trf("installée ici (v%s)", vt.c_str()) : trf("v%s de %s remplace v%s d'ici", vt.c_str(), name.c_str(), vh.c_str()));
            } else what = up ? trf("la partie d'ici remplace celle de %s", name.c_str()) : trf("la partie de %s remplace celle d'ici", name.c_str());
            note += (note.empty() ? "" : " · ") + t["title"].as_string() + " : " + what;
        }
        ++n;
    }
    if (!n) { toast(tr("Tout est déjà au même niveau.")); return; }
    if (n > 4) note += " · " + trf("… et %d autre(s)", n - 4);
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    const bool apps = sy_section_ == 1;
    acts.push_back({apps ? trf("Mettre à niveau %d appli(s)", n) : trf("Mettre à niveau %d partie(s)", n), [this, peer, name, apps] {
        Json b = Json::object();
        b.set("peer", peer);
        model_.command("POST", apps ? "/api/v2/sync/apps/start" : "/api/v2/sync/parties/start", b.dump(), [this, name, apps](const std::string& e, const std::string&) {
            post_toast(e.empty() ? (apps ? trf("Applis : mise à niveau avec %s lancée", name.c_str()) : trf("Parties : mise à niveau avec %s lancée", name.c_str())) : e);
            post_ui([this] { parties_status_at_ = 0; });
        });
    }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(apps ? tr("Mettre à niveau les applis ?") : tr("Mettre à niveau les parties actives ?"), std::move(acts));
    popup_note_ = note + ". " + (apps ? tr("L'ancienne version n'est retirée qu'une fois la nouvelle installée, vérifiée et ses droits relus ; sinon elle reste en place.")
                                      : tr("Avant de remplacer une partie, elle est sauvegardée (onglet Sauvegardes). Jamais pendant que le jeu tourne."));
}

// Lignes de l'écran des parties : filtre (tous, choisis, à mettre à niveau, à trancher, vers là-bas, vers ici).
std::vector<const Json*> Ui::sy_parties_rows() const {
    std::vector<const Json*> rows;
    if (sy_section_ == 2) {  // DLC : tous, à compléter ici, à compléter là-bas, jeu absent, pareil
        for (const auto& t : sy_pick_plan_["titles"].items()) {
            const std::string st = t["state"].as_string();
            const bool h = st == "missing_here" || st == "both", th = st == "missing_there" || st == "both", g = st.rfind("game_missing", 0) == 0;
            if ((sp_filter_ == 1 && !h) || (sp_filter_ == 2 && !th) || (sp_filter_ == 3 && !g) || (sp_filter_ == 4 && st != "same")) continue;
            rows.push_back(&t);
        }
        return rows;
    }
    for (const auto& t : sy_pick_plan_["titles"].items()) {
        const std::string st = t["state"].as_string();
        const bool on = t["followed"].as_bool(), up = st == "here_newer" || st == "only_here", down = st == "there_newer" || st == "only_there";
        const bool odd = sy_section_ == 1 ? (st == "unknown" || st == "manual" || st == "conflict" || t["config_state"].as_string() == "conflict") : t["conflict"].as_bool();
        if (sy_section_ == 1 && sp_filter_ == 2) { if (!on || !(t["app_moves"].as_bool() || t["config_moves"].as_bool())) continue; rows.push_back(&t); continue; }
        if (sy_section_ == 1 && sp_filter_ == 6) { const std::string cs = t["config_state"].as_string(); if (cs.empty() || cs == "undefined") continue; rows.push_back(&t); continue; }
        if ((sp_filter_ == 1 && !on) || (sp_filter_ == 2 && (!on || st == "same" || st == "conflict" || st == "unknown" || st == "manual")) || (sp_filter_ == 3 && !odd) ||
            (sp_filter_ == 4 && !up) || (sp_filter_ == 5 && !down)) continue;
        rows.push_back(&t);
    }
    return rows;
}

void Ui::input_synchro_parties(std::uint32_t k) {
    const std::vector<const Json*> rows = sy_parties_rows();
    const int n = static_cast<int>(rows.size());
    if (k & key::circle) { sy_pick_ = false; sy_parties_ = false; return; }
    if (k & key::square) { sp_filter_ = (sp_filter_ + 1) % (sy_section_ == 2 ? 5 : sy_section_ == 1 ? 7 : 6); sn_row_ = 0; return; }
    sn_row_ = std::clamp(sn_row_, 0, std::max(0, n - 1));
    if (n && (k & key::up)) sn_row_ = (sn_row_ + n - 1) % n;
    if (n && (k & key::down)) sn_row_ = (sn_row_ + 1) % n;
    if (sy_section_ == 2) {  // DLC : lecture seule pour l'instant (étape A)
        if ((k & key::cross) && n) sy_dlc_details(*rows[static_cast<std::size_t>(sn_row_)]);
        if (k & key::triangle) sy_dlc_start();
        return;
    }
    auto prefs = [this](Json b) {
        if (sy_section_ == 1) b.set("section", "apps");
        model_.command("POST", "/api/v2/sync/parties/prefs", b.dump(), [this](const std::string& e, const std::string&) {
            if (!e.empty()) post_toast(e);
            post_ui([this] { sy_open_parties(sy_pick_peer_, sy_pick_name_); sync_plans_at_ = 0; });
        });
    };
    if ((k & key::cross) && n) {
        const Json& t = *rows[static_cast<std::size_t>(sn_row_)];
        Json f = Json::object(), b = Json::object();
        f.set(t["title_id"].as_string(), !t["followed"].as_bool());
        b.set("peer", sy_pick_peer_); b.set("follow", f);
        Json titles = Json::array();  // tout de suite à l'écran
        for (const auto& x : sy_pick_plan_["titles"].items()) { Json y = x; if (x["title_id"].as_string() == t["title_id"].as_string()) y.set("followed", !t["followed"].as_bool()); titles.push(y); }
        sy_pick_plan_.set("titles", titles);
        prefs(b);
    }
    if ((k & key::options) && n) {  // tout choisir / tout retirer
        bool all = true;
        for (const auto* t : rows) all = all && (*t)["followed"].as_bool();
        Json b = Json::object();
        b.set("peer", sy_pick_peer_); b.set("follow_all", !all);
        prefs(b);
    }
    if (k & key::triangle) sy_parties_start(sy_pick_peer_, sy_pick_name_);
    if (sy_section_ == 1 && n && (k & (key::l2 | key::r2))) {
        const Json& t = *rows[static_cast<std::size_t>(sn_row_)];
        const std::string tid = t["title_id"].as_string();
        Json m = Json::object(), b = Json::object();
        b.set("peer", sy_pick_peer_);
        if (k & key::l2) {  // appli seule → appli + réglages → réglages seuls
            const std::string cur = t["mode"].as_string();
            m.set(tid, cur == "app" ? "app_config" : cur == "app_config" ? "config" : "app");
            b.set("apps_mode", m);
            prefs(b);
        } else if (t["state"].as_string() == "conflict" || t["conflict"].as_bool()) {  // même version, fichiers différents
            const std::string st = t["state"].as_string();
            m.set(tid, st == "conflict" ? "here" : st == "here_newer" ? "there" : "");
            b.set("apps_resolve", m);
            prefs(b);
        } else if (t["config_state"].as_string() == "conflict" || !t["config_state"].is_null()) {  // réglages modifiés des deux côtés
            const std::string cs = t["config_state"].as_string();
            m.set(tid, cs == "conflict" ? "here" : cs == "here_newer" ? "there" : "");
            b.set("config_resolve", m);
            prefs(b);
        }
        return;
    }
    if ((k & key::r2) && n && (*rows[static_cast<std::size_t>(sn_row_)])["conflict"].as_bool()) {  // conflit : garder celle d'ici / de là-bas / à trancher
        const Json& t = *rows[static_cast<std::size_t>(sn_row_)];
        const std::string st = t["state"].as_string(), next = st == "conflict" ? "here" : st == "here_newer" ? "there" : "";
        Json rs = Json::object(), b = Json::object();
        rs.set(t["title_id"].as_string(), next);
        b.set("peer", sy_pick_peer_); b.set("resolve", rs);
        prefs(b);
    }
    if ((k & key::l2) && sy_section_ == 0) {  // changer le compte de chaque côté (parties seulement)
        std::vector<std::pair<std::string, std::string>> pairs = {{"", ""}};  // d'abord : même personne PSN (automatique)
        for (const auto& a : sy_pick_plan_["here_accounts"].items())
            for (const auto& b : sy_pick_plan_["there_accounts"].items()) pairs.push_back({a["key"].as_string(), b["key"].as_string()});
        std::size_t cur = 0;
        for (std::size_t i = 0; i < pairs.size(); ++i)
            if (pairs[i].first == sy_pick_plan_["here_account"].as_string() && pairs[i].second == sy_pick_plan_["there_account"].as_string()) cur = i;
        const auto& nx = pairs[(cur + 1) % pairs.size()];
        Json b = Json::object();
        b.set("peer", sy_pick_peer_); b.set("here_account", nx.first); b.set("there_account", nx.second);
        sy_pick_plan_ = Json();
        prefs(b);
    }
}

// DLC : ce qui manque d'un côté, en détail (Croix sur une ligne).
void Ui::sy_dlc_details(const Json& t) {
    const std::string st = t["state"].as_string(), name = sy_pick_name_;
    std::string note;
    auto list = [&](const Json& a) {
        std::string r;
        int i = 0;
        for (const auto& d : a.items()) {
            if (i++ == 5) { r += " · " + trf("… et %d autre(s)", static_cast<int>(a.size()) - 5); break; }
            std::string l = d["file"].as_string();
            const auto b = l.find(" [DLC ");
            if (b != std::string::npos) { const auto e = l.find(']', b + 6); if (e != std::string::npos) l = l.substr(b + 6, e - b - 6); }
            r += (r.empty() ? "" : " · ") + l;
        }
        return r;
    };
    if (st == "game_missing_there") note = trf("Le jeu n'est pas sur %s : envoyez d'abord le jeu entier (Transferts).", name.c_str());
    else if (st == "game_missing_here") note = trf("Le jeu n'est pas ici : faites-le venir d'abord de %s (Transferts).", name.c_str());
    else {
        if (t["missing_here"].as_int()) note += trf("Manque ici : %s", list(t["missing_here_list"]).c_str()) + ". ";
        if (t["missing_there"].as_int()) note += trf("Manque sur %s : %s", name.c_str(), list(t["missing_there_list"]).c_str()) + ". ";
        const std::string u = t["update"].as_string();
        if (u == "there_newer") note += trf("Mise à jour plus récente sur %s (%s ; ici %s).", name.c_str(), t["there_version"].as_string().c_str(), t["here_version"].as_string().empty() ? "—" : t["here_version"].as_string().c_str()) + " ";
        if (u == "here_newer") note += trf("Mise à jour plus récente ici (%s ; sur %s %s).", t["here_version"].as_string().c_str(), name.c_str(), t["there_version"].as_string().empty() ? "—" : t["there_version"].as_string().c_str()) + " ";
        if (note.empty()) note = tr("Les mêmes DLC et la même mise à jour des deux côtés.");
    }
    if (t["library"].size()) {
        std::string l;
        int i = 0;
        for (const auto& p : t["library"].items()) {
            if (i++ == 4) { l += " · " + trf("… et %d autre(s)", static_cast<int>(t["library"].size()) - 4); break; }
            const std::string k = p["kind"].as_string();
            l += (l.empty() ? "" : " · ") + p["file"].as_string() + " (" + p["place"].as_string() + ", " + size_text(static_cast<std::uint64_t>(p["bytes"].as_int())) + ", " +
                 (k == "dlc" ? tr("DLC") : k == "game_or_update" ? tr("jeu ou mise à jour") : tr("type inconnu")) + ")";
        }
        note += " " + trf("Paquets rangés ici : %s.", l.c_str());
    }
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({tr("OK"), [] {}});
    open_actions(t["title"].as_string(), std::move(acts));
    popup_note_ = note + " " + (t["kind"].as_string() == "switch" ? tr("Triangle : compléter.") : tr("Jeux PS5 / PS4 : leurs DLC s'installeront par l'installeur (prochaine étape)."));
}

// Triangle : compléter ici ou là-bas (Switch : simples fichiers ; PS5 : par l'installeur, plus tard).
void Ui::sy_dlc_start() {
    if (parties_status_["state"].as_string() == "running") { toast(tr("Une mise à niveau est déjà en cours.")); return; }
    std::int64_t nh = 0, nt = 0, bh = 0, bt = 0;
    int ps = 0;
    for (const auto& t : sy_pick_plan_["titles"].items()) {
        const std::string st = t["state"].as_string();
        const bool h = st == "missing_here" || st == "both", th = st == "missing_there" || st == "both";
        if (!h && !th) continue;
        if (t["kind"].as_string() != "switch") { ++ps; continue; }
        if (h) { ++nh; bh += t["bytes_missing_here"].as_int(); }
        if (th) { ++nt; bt += t["bytes_missing_there"].as_int(); }
    }
    if (!nh && !nt) { toast(ps ? tr("Jeux PS5 / PS4 : leurs DLC s'installeront par l'installeur (prochaine étape).") : tr("Rien à compléter.")); return; }
    const std::string peer = sy_pick_peer_, name = sy_pick_name_;
    auto go = [this, peer, name](const char* dir) {
        Json b = Json::object();
        b.set("peer", peer); b.set("direction", dir);
        model_.command("POST", "/api/v2/sync/dlc/start", b.dump(), [this, name, dir](const std::string& e, const std::string&) {
            post_toast(e.empty() ? (std::string(dir) == "here" ? std::string(tr("DLC : copie vers cette PS5 lancée")) : trf("DLC : copie vers %s lancée", name.c_str())) : e);
            post_ui([this] { parties_status_at_ = 0; });
        });
    };
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (nh) acts.push_back({trf("Compléter ici · %lld jeu(x) · %s", static_cast<long long>(nh), size_text(static_cast<std::uint64_t>(bh)).c_str()), [go] { go("here"); }});
    if (nt) acts.push_back({trf("Compléter sur %s · %lld jeu(x) · %s", name.c_str(), static_cast<long long>(nt), size_text(static_cast<std::uint64_t>(bt)).c_str()), [go] { go("there"); }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(tr("Compléter les DLC et mises à jour ?"), std::move(acts));
    popup_note_ = std::string(tr("Jeux Switch : seuls les fichiers qui manquent sont copiés dans le dossier du jeu. Rien n'est écrasé ni supprimé ; jamais pendant qu'Eden est ouvert.")) +
                  (ps ? std::string(" ") + tr("Jeux PS5 / PS4 : leurs DLC s'installeront par l'installeur (prochaine étape).") : std::string());
}

void Ui::draw_synchro_dlc(Surface& s, int x, int w) {
    const Json& pl = sy_pick_plan_;
    const Json& sm = pl["summary"];
    const int total = static_cast<int>(pl["titles"].size());
    {
        const int tw = s.text(font_, x, 124, tr("DLC et mises à jour"), large, color::text);
        s.text(font_, x + tw + 30, 138, trf("avec %s", sy_pick_name_.c_str()), small, color::muted);
        const std::string r = trf("%lld à compléter ici · %lld sur %s", static_cast<long long>(sm["to_complete_here"].as_int()), static_cast<long long>(sm["to_complete_there"].as_int()), sy_pick_name_.c_str());
        if (pl.is_object() && !pl.has("error")) s.text(font_, x + w - Surface::measure(r, small), 138, r, small, rgb(200, 215, 234));
    }
    para(s, font_, x, 184, tr("Les extensions et mises à jour de chaque jeu, des deux côtés. Seul ce qui manque est copié, jamais le jeu entier (lui passe par Transferts). Switch : copie directe ; PS5 / PS4 : par l'installeur (prochaine étape)."), small, color::muted, w, 2, 30);
    {   // où ranger les paquets (étape suivante)
        std::string st;
        for (const auto& p : pl["stores"].items()) st += (st.empty() ? "" : ", ") + p["label"].as_string();
        s.text(font_, x, 250, st.empty() ? std::string(tr("Pour installer plus tard, il faudra un point de stockage : une clé USB sur la console ou un NAS."))
                                         : trf("Points de stockage pour les paquets : %s", st.c_str()), small, st.empty() ? color::warn : color::accent, w);
    }
    {
        int nh = 0, nt = 0, ng = 0, ns = 0;
        for (const auto& t : pl["titles"].items()) {
            const std::string st = t["state"].as_string();
            nh += st == "missing_here" || st == "both"; nt += st == "missing_there" || st == "both"; ng += st.rfind("game_missing", 0) == 0; ns += st == "same";
        }
        const std::string fl[5] = {trf("Tous · %d", total), trf("↓ À compléter ici · %d", nh), trf("↑ À compléter sur %s · %d", sy_pick_name_.c_str(), nt), trf("Jeu absent · %d", ng), trf("Pareil · %d", ns)};
        int cx = x;
        for (int i = 0; i < 5; ++i) {
            const int cw = Surface::measure(fl[i], small) + 32;
            s.rect(cx, 292, cw, 44, i == sp_filter_ ? color::text : color::panel2, 22);
            s.text(font_, cx + 16, 298, fl[i], small, i == sp_filter_ ? color::bg : color::muted);
            cx += cw + 8;
        }
    }
    const int ly = 352;
    if (!pl.is_object()) { spinner(s, x + 20, ly + 20, 16); s.text(font_, x + 50, ly + 4, tr("Lecture…"), body, color::muted); }
    else if (pl.has("error")) s.text(font_, x, ly, pl["error"].as_string(), body, color::bad, w);
    else if (!total) para(s, font_, x, ly, tr("Aucun DLC ni aucune mise à jour différente entre les deux consoles."), body, color::muted, w, 3, 40);
    const std::vector<const Json*> rows = sy_parties_rows();
    if (pl.is_object() && total && rows.empty()) s.text(font_, x, ly, tr("Rien à afficher avec ce filtre."), body, color::muted);
    const int n = static_cast<int>(rows.size()), rh = 118, per = (1080 - 110 - ly) / (rh + 12);
    const int first = std::clamp(sn_row_ - per / 2, 0, std::max(0, n - per));
    for (int r = first; r < n && r < first + per; ++r) {
        const Json& t = *rows[static_cast<std::size_t>(r)];
        const int yy = ly + (r - first) * (rh + 12);
        const bool f = r == sn_row_;
        s.rect(x, yy, w, rh, f ? rgb(21, 41, 74) : color::panel, 20);
        s.outline(x, yy, w, rh, f ? color::accent : color::line, 20, f ? 3 : 1);
        Item tmp;
        const Item* it = find_or_stand_in("title:" + t["title_id"].as_string(), t["title"].as_string(), t["title_id"].as_string(), tmp, "");
        cover(s, *it, x + 16, yy + 16, 86, false);
        const int tx = x + 128, tw = w - 128 - 520;
        s.text(font_, tx, yy + 14, t["title"].as_string(), body, color::text, tw);
        const std::string kind = t["kind"].as_string();
        const std::string vh = t["here_version"].as_string(), vt = t["there_version"].as_string();
        const std::string det = trf("Ici : %d DLC", static_cast<int>(t["here"].as_int())) + (vh.empty() ? std::string() : " · " + vh) + "      " +
                                trf("Sur %s : %d DLC", sy_pick_name_.c_str(), static_cast<int>(t["there"].as_int())) + (vt.empty() ? std::string() : " · " + vt) +
                                "      " + (kind == "switch" ? "Switch" : kind == "ps4" ? "PS4" : "PS5");
        std::string libtxt;
        if (t["library"].size()) {
            std::set<std::string> where;
            for (const auto& p : t["library"].items()) where.insert(p["place"].as_string());
            std::string w;
            for (const auto& p : where) w += (w.empty() ? "" : ", ") + p;
            libtxt = trf("Paquets rangés ici : %d (%s)", static_cast<int>(t["library"].size()), w.c_str());
        }
        s.text(font_, tx, libtxt.empty() ? yy + 62 : yy + 52, det, small, color::muted, tw + 340);
        if (!libtxt.empty()) s.text(font_, tx, yy + 80, libtxt, small, color::accent, tw + 340);
        const std::string st = t["state"].as_string(), u = t["update"].as_string();
        std::string chip;
        std::uint32_t cc = color::warn;
        auto what = [&](int dlc, bool upd, std::int64_t bytes) {
            std::string r = dlc ? trf("%d DLC", dlc) : std::string();
            if (upd) r += std::string(r.empty() ? "" : " + ") + tr("mise à jour");
            if (bytes > 0) r += " · " + size_text(static_cast<std::uint64_t>(bytes));
            return r;
        };
        if (st == "game_missing_there") { chip = trf("jeu absent sur %s : Transferts", sy_pick_name_.c_str()); cc = color::muted; }
        else if (st == "game_missing_here") { chip = tr("jeu absent ici : Transferts"); cc = color::muted; }
        else if (st == "same" && t["library"].size()) { chip = tr("paquet rangé ici : pour l'installeur"); cc = color::accent; }
        else if (st == "same") { chip = tr("✓ pareil des deux côtés"); cc = color::ok; }
        else {
            const std::string down = what(static_cast<int>(t["missing_here"].as_int()), u == "there_newer", t["bytes_missing_here"].as_int());
            const std::string up = what(static_cast<int>(t["missing_there"].as_int()), u == "here_newer", t["bytes_missing_there"].as_int());
            if (!down.empty()) chip = "↓ " + down;
            if (!up.empty()) chip += (chip.empty() ? "" : "   ") + std::string("↑ ") + up;
        }
        const int cwid = std::min(500, Surface::measure(chip, small) + 32);
        s.rect(x + w - 24 - cwid, yy + 34, cwid, 42, rgb(255, 255, 255, 12), 21);
        s.text(font_, x + w - 24 - cwid + 16, yy + 38, chip, small, cc, cwid - 32);
    }
    {
        const Json& ps = parties_status_;
        if (ps["section"].as_string() == "dlc" && ps["peer"].as_string() == sy_pick_peer_ && ps["state"].as_string() == "running")
            s.text(font_, x + w - 640, 250, trf("Copie en cours : %lld / %lld · %s", static_cast<long long>(ps["done"].as_int()), static_cast<long long>(ps["total"].as_int()), ps["current"].as_string().c_str()), small, color::accent, 640);
    }
    draw_footer(s, {{0, tr("Détails")}, {1, tr("Retour à Synchro")}, {2, tr("Filtre")}, {3, tr("Compléter…")}});
}

void Ui::draw_synchro_parties(Surface& s, int x, int w) {
    if (sy_section_ == 2) { draw_synchro_dlc(s, x, w); return; }
    const Json& pl = sy_pick_plan_;
    int chosen = 0, need = 0, total = 0;
    for (const auto& t : pl["titles"].items()) {
        ++total; chosen += t["followed"].as_bool();
        need += t["followed"].as_bool() && (sy_section_ == 1 ? (t["app_moves"].as_bool() || t["config_moves"].as_bool()) : t["state"].as_string() != "same" && t["state"].as_string() != "conflict");
    }
    {
        const bool apps = sy_section_ == 1;
        const int tw = s.text(font_, x, 124, apps ? tr("Applis à garder au même niveau") : tr("Parties actives à garder au même niveau"), large, color::text);
        s.text(font_, x + tw + 30, 138, trf("avec %s", sy_pick_name_.c_str()), small, color::muted);
        const std::string r = (apps ? trf("%d appli(s) choisie(s) sur %d", chosen, total) : trf("%d jeu(x) choisi(s) sur %d", chosen, total)) + (need ? " · " + trf("%lld à mettre à niveau", static_cast<long long>(need)) : std::string());
        s.text(font_, x + w - Surface::measure(r, small), 138, r, small, rgb(200, 215, 234));
    }
    const bool apps = sy_section_ == 1;
    para(s, font_, x, 184, pl["nas"].as_bool() ? (apps ? tr("Le NAS sert de dépôt : il garde la dernière version de chaque appli choisie. Plus récente ici : elle y est déposée ; plus récente sur le NAS : elle est installée ici, à la place de l'ancienne. Jamais de retour en arrière.")
                                                       : tr("Le NAS sert de dépôt : il garde la partie la plus récente de chaque jeu choisi. Plus récente ici : une copie y part ; plus récente sur le NAS : elle est restaurée ici, celle d'ici d'abord sauvegardée. Jamais pendant que le jeu tourne."))
                         : apps ? tr("Les applis installées sur chaque PS5. Même version : rien. Plus récente d'un côté : elle remplace l'ancienne de l'autre côté — jamais deux versions, jamais de retour en arrière. Une appli en image (.ffpkg, .exfat) se met à jour par Transferts.")
                                : tr("Les parties qui sont SUR les consoles, celles que vous reprenez en lançant le jeu. La plus récente gagne ; celle qu'elle remplace est d'abord sauvegardée. Jamais pendant que le jeu tourne."), small, color::muted, w, 2, 30);
    const bool nas = pl["nas"].as_bool();
    if (!apps && nas) s.text(font_, x, 250, tr("Compte : la partie de chaque compte de cette PS5 est comparée à sa dernière copie sur le NAS."), small, color::accent, w);
    if (!apps && !nas) {   // le compte de chaque côté (la partie est remise sur l'identifiant de la console qui la reçoit)
        auto name_of = [&](const char* list, const std::string& key) {
            for (const auto& a : pl[list].items()) if (a["key"].as_string() == key) return a["name"].as_string() + " (" + a["user_id"].as_string() + ")";
            return key;
        };
        const std::string line = pl["account_mode"].as_string() == "chosen"
            ? trf("Compte : %s ici ↔ %s sur %s", name_of("here_accounts", pl["here_account"].as_string()).c_str(), name_of("there_accounts", pl["there_account"].as_string()).c_str(), sy_pick_name_.c_str())
            : std::string(tr("Compte : le même compte PSN des deux côtés (automatique)"));
        const int lw = s.text(font_, x, 250, line, small, color::accent);
        s.text(font_, x + lw + 24, 250, tr("L2 : changer de compte"), small, color::muted);
    }
    {   // filtres (Carré)
        int nch = 0, nneed = 0, nconf = 0, nup = 0, ndown = 0;
        for (const auto& t : pl["titles"].items()) {
            const std::string st = t["state"].as_string();
            const bool on = t["followed"].as_bool();
            nch += on; nneed += on && st != "same" && st != "conflict" && st != "unknown" && st != "manual";
            nconf += apps ? (st == "unknown" || st == "manual") : t["conflict"].as_bool();
            nup += st == "here_newer" || st == "only_here"; ndown += st == "there_newer" || st == "only_there";
        }
        int ncfg = 0;
        for (const auto& t : pl["titles"].items()) { const std::string cs = t["config_state"].as_string(); ncfg += !cs.empty() && cs != "undefined"; }
        const std::string fl[7] = {trf("Tous · %d", total), trf("Choisis · %d", nch), trf("À mettre à jour · %d", nneed), apps ? trf("Par Transferts · %d", nconf) : trf("À trancher · %d", nconf),
                                   trf("↑ Vers %s · %d", sy_pick_name_.c_str(), nup), trf("↓ Vers ici · %d", ndown), trf("Avec réglages · %d", ncfg)};
        int cx = x;
        for (int i = 0; i < (apps ? 7 : 6); ++i) {
            const int cw = Surface::measure(fl[i], small) + 32;
            s.rect(cx, 292, cw, 44, i == sp_filter_ ? color::text : color::panel2, 22);
            s.text(font_, cx + 16, 298, fl[i], small, i == sp_filter_ ? color::bg : color::muted);
            cx += cw + 8;
        }
    }
    const int ly = 352;
    if (!pl.is_object()) { spinner(s, x + 20, ly + 20, 16); s.text(font_, x + 50, ly + 4, tr("Lecture…"), body, color::muted); }
    else if (pl.has("error")) s.text(font_, x, ly, pl["error"].as_string(), body, color::bad, w);
    else if (!total) para(s, font_, x, ly, apps ? tr("Aucune appli à comparer.") : nas ? tr("Aucune partie à comparer : il faut des parties sur cette PS5, ou des copies sur le NAS d'un compte présent ici.")
                                                                          : tr("Aucune partie à comparer : il faut le même compte PSN sur les deux PS5, et des parties sur au moins l'une d'elles."), body, color::muted, w, 3, 40);
    const std::vector<const Json*> rows = sy_parties_rows();
    if (pl.is_object() && total && rows.empty()) s.text(font_, x, ly, tr("Rien à afficher avec ce filtre."), body, color::muted);
    const int n = static_cast<int>(rows.size()), rh = 118, per = (1080 - 110 - ly) / (rh + 12);
    const int first = std::clamp(sn_row_ - per / 2, 0, std::max(0, n - per));
    for (int r = first; r < n && r < first + per; ++r) {
        const Json& t = *rows[static_cast<std::size_t>(r)];
        const int yy = ly + (r - first) * (rh + 12);
        const bool f = r == sn_row_, on = t["followed"].as_bool();
        s.rect(x, yy, w, rh, f ? rgb(21, 41, 74) : color::panel, 20);
        s.outline(x, yy, w, rh, f ? color::accent : color::line, 20, f ? 3 : 1);
        Item tmp;
        const Item* it = find_or_stand_in("title:" + t["title_id"].as_string(), t["title"].as_string(), t["title_id"].as_string(), tmp, apps ? "app" : "");
        cover(s, *it, x + 16, yy + 16, 86, false);
        const int tx = x + 128, tw = w - 128 - 470;
        s.text(font_, tx, yy + 14, t["title"].as_string(), body, on ? color::text : color::muted, tw);
        std::string det;
        if (apps) {  // versions installées
            const std::string vh = t["here_version"].as_string(), vt = t["there_version"].as_string();
            const bool hi = t["here_key"].is_string(), th = t["there_key"].is_string();
            det = (hi ? trf("Ici : v%s", vh.empty() ? "?" : vh.c_str()) : std::string(tr("Pas installée ici"))) + "      " +
                  (th ? trf("Sur %s", sy_pick_name_.c_str()) + " : v" + (vt.empty() ? std::string("?") : vt) : pl["nas"].as_bool() ? std::string(tr("Pas encore dans le dépôt")) : trf("Pas installée sur %s", sy_pick_name_.c_str()));
            const std::string mode = t["mode"].as_string(), cs = t["config_state"].as_string();
            det += "      " + std::string(mode == "app_config" ? tr("Appli + réglages") : mode == "config" ? tr("Réglages seuls") : tr("Appli seule"));
            if (mode != "app" && !cs.empty())
                det += " · " + std::string(cs == "same" ? tr("réglages identiques") : cs == "here_newer" ? tr("réglages : ↑ à envoyer") : cs == "there_newer" ? tr("réglages : ↓ à recevoir")
                                         : cs == "conflict" ? tr("réglages modifiés des deux côtés : R2") : cs == "version_differs" ? tr("réglages : versions différentes") : tr("réglages : dossier inconnu"));
        } else {
            const std::int64_t ha = t["here_at"].as_int(), ta = t["there_at"].as_int();
            det = (ha ? trf("Ici : %s", day_text(ha).c_str()) : std::string(tr("Pas de partie ici"))) + "      " +
                  (ta ? trf("Sur %s", sy_pick_name_.c_str()) + " : " + day_text(ta) : trf("Pas de partie sur %s", sy_pick_name_.c_str()));
        }
        s.text(font_, tx, yy + 62, det, small, color::muted, tw + 300);
        const std::string st = t["state"].as_string();
        std::string chip;
        std::uint32_t cc = color::muted;
        if (t["running"].as_bool()) { chip = tr("en cours de jeu"); cc = color::bad; }
        else if (st == "conflict") { chip = apps ? tr("même version, fichiers différents : R2") : tr("jouée des deux côtés : R2 pour choisir"); cc = color::bad; }
        else if (st == "manual") { chip = tr("en image : par Transferts"); cc = color::muted; }
        else if (st == "unknown") { chip = tr("version inconnue"); cc = color::muted; }
        else if (!on) chip = tr("pas choisi");
        else if (st == "same") { chip = tr("✓ au même niveau"); cc = color::ok; }
        else if (apps && st == "only_here") { chip = trf("↑ à installer sur %s", sy_pick_name_.c_str()); cc = color::warn; }
        else if (apps && st == "only_there") { chip = tr("↓ à installer ici"); cc = color::warn; }
        else if (st == "here_newer" || st == "only_here") { chip = trf("↑ la plus récente est ici → %s", sy_pick_name_.c_str()); cc = color::warn; }
        else { chip = trf("↓ la plus récente est sur %s", sy_pick_name_.c_str()); cc = color::warn; }
        const std::string why = t["reason"].as_string();
        if (on && st != "conflict" && st != "same") {
            if (why == "more_play_time") chip += std::string(" · ") + tr("plus de temps de jeu");
            else if (why == "played_here") chip += std::string(" · ") + tr("rejouée ici");
            else if (why == "played_there") chip += std::string(" · ") + tr("rejouée là-bas");
            else if (why == "both_played") chip += std::string(" · ") + tr("votre choix");
        }
        const int cwid = std::min(420, Surface::measure(chip, small) + 32);
        s.rect(x + w - 150 - cwid, yy + 34, cwid, 42, rgb(255, 255, 255, 12), 21);
        s.text(font_, x + w - 150 - cwid + 16, yy + 38, chip, small, cc, cwid - 32);
        toggle(s, x + w - 26 - 88, yy + 30, on);
    }
    if (apps) draw_footer(s, {{0, tr("Choisir / retirer")}, {1, tr("Retour à Synchro")}, {3, need ? tr("Mettre à niveau…") : tr("Rien à mettre à niveau")}, {2, tr("Filtre")}, {-1, tr("L2 : mode")}});
    else draw_footer(s, {{0, tr("Choisir / retirer")}, {1, tr("Retour à Synchro")}, {3, need ? tr("Mettre à niveau…") : tr("Rien à mettre à niveau")}, {2, tr("Filtre")}, {-1, tr("Options : tout / rien")}});
}

// « Choisir les jeux » : un interrupteur par jeu, l'état des deux côtés, et le jeu entier une seule fois s'il manque.
void Ui::draw_synchro_pick(Surface& s, int x, int w) {
    if (sy_parties_) { draw_synchro_parties(s, x, w); return; }
    if (sn_all_versions_) { draw_synchro_versions(s, x, w); return; }
    const Json& pl = sy_pick_plan_;
    const auto rows = sy_title_rows();
    int chosen = 0, total = 0, need = 0;
    std::int64_t bytes = 0;
    for (const auto& t : pl["titles"].items()) {
        ++total;
        chosen += t["followed"].as_bool();
        if (sy_needs(t)) { ++need; bytes += t["bytes"].as_int() + (t["send_full"].as_bool() ? t["full_push_bytes"].as_int() + t["full_pull_bytes"].as_int() : 0); }
    }
    {
        const int tw = s.text(font_, x, 124, tr("Sauvegardes à garder au même niveau"), large, color::text);
        s.text(font_, x + tw + 30, 138, trf("avec %s", sy_pick_name_.c_str()), small, color::muted);
        const std::string r = trf("%d jeu(x) choisi(s) sur %d", chosen, total) + (need ? " · " + trf("%s à échanger", size_text(static_cast<std::uint64_t>(bytes)).c_str()) : std::string());
        s.text(font_, x + w - Surface::measure(r, small), 138, r, small, rgb(200, 215, 234));
    }
    // Nouveaux jeux automatiquement (ligne 0)
    {
        const int y = 200, h = 96;
        const bool f = sn_row_ == 0;
        s.rect(x, y, w, h, f ? color::panel2 : color::panel, 22);
        s.outline(x, y, w, h, f ? color::accent : color::line, 22, f ? 3 : 1);
        s.text(font_, x + 30, y + 14, tr("Ajouter aussi les nouveaux jeux automatiquement"), body, color::text, w - 200);
        s.text(font_, x + 30, y + 54, tr("Un jeu sauvegardé pour la première fois est choisi tout seul. Vous pouvez toujours le retirer."), small, color::muted, w - 200);
        toggle(s, x + w - 30 - 88, y + 24, pl["auto_new"].as_bool());
    }
    // Filtres
    int fy = 316;
    {
        int nch = 0, nneed = 0, nup = 0, ndown = 0;
        for (const auto& t : pl["titles"].items()) {
            nch += t["followed"].as_bool(); nneed += sy_needs(t);
            nup += t["push"].as_int() || t["full_push_bytes"].as_int(); ndown += t["pull"].as_int() || t["full_pull_bytes"].as_int();
        }
        const std::string f[6] = {trf("Tous · %d", total), trf("Choisis · %d", nch), trf("À mettre à jour · %d", nneed), trf("Pas choisis · %d", total - nch),
                                  trf("↑ Vers %s · %d", sy_pick_name_.c_str(), nup), trf("↓ Vers ici · %d", ndown)};
        int cx = x;
        for (int i = 0; i < 6; ++i) {
            const int cw = Surface::measure(f[i], small) + 32;
            s.rect(cx, fy, cw, 44, i == sn_filter_ ? color::text : color::panel2, 22);
            s.text(font_, cx + 16, fy + 6, f[i], small, i == sn_filter_ ? color::bg : color::muted);
            cx += cw + 8;
        }
        const std::string hints = std::string(sn_sort_ == 1 ? tr("L2 : trier par nom") : tr("L2 : trier par date")) + "   ·   " + tr("R2 : anciennes versions");
        if (cx + 20 + Surface::measure(hints, small) < x + w) s.text(font_, cx + 20, fy + 6, hints, small, color::muted);
        else s.text(font_, x + w - Surface::measure(hints, small), 176, hints, small, color::muted);
    }
    const int ly = fy + 64;
    if (!pl.is_object()) { spinner(s, x + 20, ly + 20, 16); s.text(font_, x + 50, ly + 4, tr("Lecture…"), body, color::muted); }
    else if (pl.has("error")) s.text(font_, x, ly, pl["error"].as_string(), body, color::bad, w);
    else if (rows.empty()) s.text(font_, x, ly, tr("Rien à afficher avec ce filtre."), body, color::muted);
    const int n = static_cast<int>(rows.size()), rh = 118, big = 190, avail = 1080 - 110 - ly;
    const int focus = sn_row_ - 1;
    auto extra_of = [&](const Json& t) { return t["followed"].as_bool() && (t["full_push_bytes"].as_int() || t["full_pull_bytes"].as_int()); };
    // Première ligne affichée : la ligne choisie reste visible
    int first = 0;
    {
        auto height = [&](int i) { return (i == focus && extra_of(*rows[static_cast<std::size_t>(i)])) ? big : rh; };
        while (first < focus) {
            int used = 0;
            for (int i = first; i <= focus; ++i) used += height(i) + 12;
            if (used <= avail) break;
            ++first;
        }
    }
    int yy = ly;
    bool show_opt = false;
    for (int r = first; r < n; ++r) {
        const Json& t = *rows[static_cast<std::size_t>(r)];
        const bool f = r == focus, on = t["followed"].as_bool(), ex = f && extra_of(t);
        const int h = ex ? big : rh;
        if (yy + h > ly + avail) break;
        s.rect(x, yy, w, h, f ? rgb(21, 41, 74) : color::panel, 20);
        s.outline(x, yy, w, h, f ? color::accent : color::line, 20, f ? 3 : 1);
        Item tmp;
        const Item* it = find_or_stand_in("title:" + t["title_id"].as_string(), t["title"].as_string(), t["title_id"].as_string(), tmp, t["kind"].as_string());
        cover(s, *it, x + 16, yy + 16, 86, false);
        const unsigned dim = on ? 255 : 150;
        const int tx = x + 128, tw = w - 128 - 470;
        s.text(font_, tx, yy + 14, t["title"].as_string(), body, on ? color::text : color::muted, tw);
        // Les deux côtés : date de la sauvegarde la plus récente et temps de jeu
        const std::int64_t ha = t["here_at"].as_int(), ta = t["there_at"].as_int();
        std::string det;
        const std::string other = trf("Sur %s", sy_pick_name_.c_str());
        if (ha && ta && ha == ta) det = trf("Ici et %s : %s", sy_pick_name_.c_str(), day_text(ha).c_str());
        else if (ha && ta) det = trf("Ici : %s", day_text(ha).c_str()) + "      " + other + " : " + day_text(ta);
        else if (ha) det = trf("Seulement ici : %s", day_text(ha).c_str());
        else if (ta) det = trf("Seulement sur %s : %s", sy_pick_name_.c_str(), day_text(ta).c_str());
        const std::string pt = play_text(std::max(t["here_play"].as_int(), t["there_play"].as_int()));
        if (!pt.empty()) det += "  ·  " + pt;
        s.text(font_, tx, yy + 62, det, small, color::muted, tw + 300);
        (void)dim;
        // État (à droite), puis l'interrupteur
        std::string chip;
        std::uint32_t cc = color::muted;
        const std::string stt = t["state"].as_string();
        if (!on) chip = tr("pas choisi");
        else if (!sy_needs(t)) { chip = tr("✓ au même niveau"); cc = color::ok; }
        else if (stt == "push") { chip = trf("↑ part vers %s", sy_pick_name_.c_str()); cc = color::warn; }
        else if (stt == "pull") { chip = tr("↓ arrive ici"); cc = color::warn; }
        else if (stt == "both") { chip = tr("↕ des deux côtés"); cc = color::warn; }
        else { chip = t["full_push_bytes"].as_int() ? trf("↑ le jeu part vers %s", sy_pick_name_.c_str()) : std::string(tr("↓ le jeu arrive ici")); cc = color::warn; }
        const int cwid = std::min(380, Surface::measure(chip, small) + 32);
        s.rect(x + w - 150 - cwid, yy + 34, cwid, 42, rgb(255, 255, 255, 12), 21);
        s.text(font_, x + w - 150 - cwid + 16, yy + 38, chip, small, cc, cwid - 32);
        toggle(s, x + w - 26 - 88, yy + 30, on);
        if (ex) {  // le jeu entier, une seule fois
            const bool push = t["full_push_bytes"].as_int() > 0;
            const std::string sz = size_text(static_cast<std::uint64_t>(push ? t["full_push_bytes"].as_int() : t["full_pull_bytes"].as_int()));
            const std::string msg = push ? trf("Le jeu n'est pas sur %s : l'envoyer aussi, une seule fois (%s). Ensuite, seule la sauvegarde suit.", sy_pick_name_.c_str(), sz.c_str())
                                         : trf("Le jeu n'est pas ici : le recevoir aussi, une seule fois (%s). Ensuite, seule la sauvegarde suit.", sz.c_str());
            const int bw2 = std::min(w - 160, Surface::measure(msg, small) + 80);
            s.rect(tx, yy + 110, bw2, 56, rgb(95, 178, 255, 26), 14);
            s.outline(tx, yy + 110, bw2, 56, rgb(95, 178, 255, 110), 14, 1);
            draw_checkbox(s, tx + 14, yy + 138, t["send_full"].as_bool(), false);
            s.text(font_, tx + 56, yy + 122, msg, small, rgb(207, 226, 250), bw2 - 70);
            show_opt = true;
        }
        yy += h + 12;
    }
    const char* first_hint = sn_row_ == 0 ? tr("Activer / désactiver") : tr("Choisir / retirer");
    // Le rond ramène à la page Synchro : toujours affiché (le reste passe après).
    if (show_opt) draw_footer(s, {{0, first_hint}, {1, tr("Retour à Synchro")}, {3, tr("Tout choisir / tout retirer")}, {-1, tr("Options : le jeu aussi")}});
    else draw_footer(s, {{0, first_hint}, {1, tr("Retour à Synchro")}, {3, tr("Tout choisir / tout retirer")}, {2, tr("Filtre")}});
}

// Anciennes versions : chaque copie de sauvegarde qui manque d'un côté, pour en envoyer aussi de plus vieilles.
void Ui::draw_synchro_versions(Surface& s, int x, int w) {
    s.text(font_, x, 124, tr("Anciennes versions"), large, color::text);
    s.text(font_, x, 186, trf("Chaque sauvegarde qui manque d'un côté, avec %s : cochez celles à envoyer, une par une, sans choisir tout le jeu.", sy_pick_name_.c_str()), small, color::muted, w);
    const auto rows = sy_pick_rows();
    const int y = 250, rh = 92, per = 7, n = static_cast<int>(rows.size());
    if (rows.empty()) s.text(font_, x, y, tr("Rien à afficher : aucune ancienne version ne manque."), body, color::muted);
    const int first = std::clamp(sn_row_ - per / 2, 0, std::max(0, n - per));
    for (int r = first; r < n && r < first + per; ++r) {
        const auto& [c, in] = rows[static_cast<std::size_t>(r)];
        const int yy = y + (r - first) * rh;
        const bool focus = r == sn_row_, on = sy_pick_versions_.count((*c)["bundle"].as_string()) > 0;
        s.rect(x, yy, w, rh - 10, focus ? color::panel2 : color::panel, 14);
        if (focus) s.outline(x - 3, yy - 3, w + 6, rh - 4, color::accent, 16, 3);
        draw_checkbox(s, x + 16, yy + (rh - 10) / 2, on, false);
        Item tmp;
        const Item* it = find_or_stand_in("title:" + (*c)["title_id"].as_string(), (*c)["title"].as_string(), (*c)["title_id"].as_string(), tmp, (*c)["kind"].as_string());
        cover(s, *it, x + 56, yy + 6, rh - 22, false);
        const int tx = x + 56 + rh;
        s.text(font_, tx, yy + 4, (*c)["title"].as_string(), body, color::text, w - 600);
        std::string det = in ? std::string(tr("↓ arrive ici")) : trf("↑ part vers %s", sy_pick_name_.c_str());
        det += "  ·  " + trf("copie du %s", day_text((*c)["created_unix"].as_int()).c_str());
        if (const std::string pt = play_text((*c)["play_time"].as_int()); !pt.empty()) det += "  ·  " + pt;
        s.text(font_, tx, yy + 44, det, small, color::muted, w - (tx - x) - 180);
        const std::string sz = size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, (*c)["bytes"].as_int())));
        s.text(font_, x + w - 24 - Surface::measure(sz, body), yy + 24, sz, body, color::muted);
    }
    const std::string send = trf("Envoyer (%d)", static_cast<int>(sy_pick_versions_.size()));
    draw_footer(s, {{0, tr("Cocher")}, {3, send.c_str()}, {1, tr("Retour")}});
}

}  // namespace app
