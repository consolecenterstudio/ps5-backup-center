// Sauvegardes › Gérer les backups : même écran que le web (engine/web/manage.js).
// Vue d'ensemble : une ligne par jeu (où sont ses copies, une phrase qui dit quoi faire, ce qu'on libère),
// vues Tous / Avec doublons / Pas encore comparés / Corbeille (Carré), « Tout comparer » (Triangle),
// jeux cochés (L2, Options : tout / rien) puis « Nettoyer la sélection… » (R2) → aperçu → corbeille.
// Un jeu (Croix) : ses copies (console d'abord), proposition du moteur cochée d'avance, protéger, comparer.
// Le résumé vient du moteur (GET /api/v2/backups/summary) : gardé en mémoire, relu à la demande et après
// chaque action ; tout passe par le chemin lent asynchrone (jamais d'attente dans le dessin).
#include <algorithm>
#include <map>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "ui.hpp"
#include "ui_util.hpp"

namespace app {

// Copie « à vérifier » (même règle que le web) : incomplète, ou vide (contenu de 0 octet) ; jamais celle d'un autre compte.
void draw_checkbox(Surface& s, int x, int cy, bool on, bool dim);  // plus bas (case à cocher commune)
bool copy_suspect(const pbc::Json& c) {
    // « 0 octet lu » n'est douteux que si la copie est vraiment presque vide : une grosse copie dont le contenu ne se lit
    // pas (ex. Gex, 100 Mo, identique à la console) n'est pas une copie vide.
    const bool empty = c.has("content_bytes") && c["content_bytes"].is_number() && c["content_bytes"].as_int() == 0 && c["bytes"].as_int() < (1 << 20);
    return !c["other_account"].is_string() && !c["on_peer"].as_bool() && (!c["complete"].as_bool(true) || empty);
}

using pbc::Json;
using namespace ui_util;

namespace {

const char* const views[4] = {N_("Tous"), N_("Avec doublons"), N_("Pas encore comparés"), N_("À vérifier")};
bool suspect(const Json& c) { return copy_suspect(c); }

std::string ckey(const Json& c) { return c["place"].as_string() + "|" + c["bundle"].as_string(); }
std::string gkey(const Json& g) {
    if (g["full"].as_bool()) return "full|" + (g["item"].as_string().empty() ? g["title"].as_string() : g["item"].as_string());
    return g["account"].as_string() + "|" + (g["title_id"].as_string().empty() ? g["title"].as_string() : g["title_id"].as_string());
}
// Taille d'une copie : contenu comparé (sauvegarde), sinon taille du backup complet.
std::uint64_t copy_bytes(const Json& c) {
    const std::int64_t b = c["content_bytes"].as_int() > 0 ? c["content_bytes"].as_int() : c["bytes"].as_int();
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, b));
}
// Type d'un backup complet, en court.
const char* kind_tag(const std::string& kind) {
    if (kind == "app") return tr("Appli");
    if (kind == "rom") return "ROM";
    return tr("Jeu");
}

bool has_index(const Json& a, std::size_t i) {
    for (const auto& x : a.items()) if (x.as_int(-1) == static_cast<std::int64_t>(i)) return true;
    return false;
}

std::string date_text(std::int64_t t, bool hour = true) {
    if (t <= 0) return "?";
    std::time_t tt = static_cast<std::time_t>(t);
    char b[32];
    std::strftime(b, sizeof b, hour ? "%d/%m/%Y %H:%M" : "%d/%m/%Y", std::localtime(&tt));
    return b;
}

// « 3 h 12 », « 18 min ».
std::string play_text(std::int64_t s) {
    if (s <= 0) return "";
    if (s < 3600) return std::to_string(std::max<std::int64_t>(1, s / 60)) + " min";
    char b[32];
    std::snprintf(b, sizeof b, "%lld h %02lld", static_cast<long long>(s / 3600), static_cast<long long>((s / 60) % 60));
    return b;
}

// Copies proposées à la corbeille pour un jeu : le choix fait à la main, sinon la proposition du moteur.
// Jamais une copie protégée ni une copie d'un autre compte.
std::set<std::string> chosen(const Json& g, const std::map<std::string, std::set<std::string>>& csel, const std::set<std::string>& prot) {
    std::set<std::string> out;
    const auto it = csel.find(gkey(g));
    const Json& copies = g["copies"];
    for (std::size_t i = 0; i < copies.size(); ++i) {
        const Json& c = copies[i];
        const std::string k = ckey(c);
        if (prot.count(k) || c["other_account"].is_string() || c["on_peer"].as_bool()) continue;  // jamais : autre compte, autre PS5
        if (it != csel.end() ? it->second.count(k) != 0 : has_index(g["remove"], i)) out.insert(k);
    }
    return out;
}

// Phrase simple tirée des nombres : « 30 copies, toutes identiques à la console : garder 3, 27 doublon(s). »
std::string summary_text(const Json& g) {
    const Json& copies = g["copies"];
    if (g["full"].as_bool()) {  // backup complet (jeu, appli, homebrew, ROM) : « 3 copies complètes (jeu + données) · 1,2 Go »
        std::vector<std::string> parts;
        std::uint64_t bytes = 0;
        for (const auto& c : copies.items()) {
            bytes += copy_bytes(c);
            for (const auto& pt : c["parts"].items()) {
                const std::string name = ui_util::part_name(pt.as_string());
                std::string low = name;
                if (!low.empty() && low[0] >= 'A' && low[0] <= 'Z') low[0] = static_cast<char>(low[0] - 'A' + 'a');
                if (std::find(parts.begin(), parts.end(), low) == parts.end()) parts.push_back(low);
            }
        }
        std::string what;
        for (const auto& pt : parts) what += (what.empty() ? "" : " + ") + pt;
        const int n = static_cast<int>(copies.size());
        if (what.empty()) return trf("%d copie(s) complète(s) · %s", n, size_text(bytes).c_str());
        return trf("%d copie(s) complète(s) (%s) · %s", n, what.c_str(), size_text(bytes).c_str());
    }
    int mine = 0;
    std::set<std::string> groups;
    bool all_console = g["console"]["group"].is_string();
    for (const auto& c : copies.items()) {
        if (c["other_account"].is_string() || c["on_peer"].as_bool() || !c["complete"].as_bool(true)) continue;
        ++mine;
        groups.insert(c["group"].as_string());
        if (c["group"].as_string() != g["console"]["group"].as_string()) all_console = false;
    }
    if (!mine) {  // seulement des copies rangées sur une autre PS5
        int np = 0;
        std::string pname;
        for (const auto& c : copies.items()) if (c["on_peer"].as_bool()) { ++np; pname = c["peer_name"].as_string(); }
        if (np) return trf("%d copie(s) rangée(s) sur %s : récupérables ici.", np, pname.c_str());
    }
    // Les copies d'un autre compte sont comptées à part (ligne suivante) : la phrase parle des tiennes.
    const int n = mine, rm = static_cast<int>(g["remove"].size()), keep = std::max(0, mine - rm);
    if (!g["compared"].as_bool()) return n == 1 ? std::string(tr("1 copie, pas encore comparée.")) : trf("%d copies, pas encore comparées.", n);
    if (rm) {
        if (groups.size() == 1 && all_console) return trf("%d copies, toutes identiques à la console : garder %d, %d doublon(s).", n, keep, rm);
        if (groups.size() == 1) return trf("%d copies, toutes identiques : garder %d, %d doublon(s).", n, keep, rm);
        return trf("%d copies, %d contenus différents : garder %d, %d doublon(s).", n, static_cast<int>(groups.size()), keep, rm);
    }
    if (n == 1) return tr("1 copie, rien à nettoyer.");
    if (static_cast<int>(groups.size()) == mine && mine > 1) return trf("%d copies, toutes différentes : rien à nettoyer.", n);
    return trf("%d copies à garder : rien à nettoyer.", n);
}

// Avancée : celle de la console, sinon celle de la copie la plus avancée ; temps de jeu ; copies d'un autre compte.
std::string progress_line(const Json& g) {
    std::string t = g["console"]["progress"]["text"].as_string();
    std::int64_t play = g["console"]["progress"]["play_time_seconds"].as_int();
    if (t.empty()) {
        const Json* best = nullptr;
        for (const auto& c : g["copies"].items()) {
            if (c["progress_text"].as_string().empty()) continue;
            if (!best || c["percent"].as_number(-1) > (*best)["percent"].as_number(-1) ||
                (c["percent"].as_number(-1) == (*best)["percent"].as_number(-1) && c["play_time_seconds"].as_int() > (*best)["play_time_seconds"].as_int()))
                best = &c;
        }
        if (best) { t = (*best)["progress_text"].as_string(); if (play <= 0) play = (*best)["play_time_seconds"].as_int(); }
    }
    if (play <= 0) play = g["play_time_tracked"].as_int();
    if (play >= 60 && t.find(':') == std::string::npos) t += (t.empty() ? "" : " · ") + trf("%s de jeu", play_text(play).c_str());
    int others = 0;
    for (const auto& c : g["copies"].items()) others += c["other_account"].is_string();
    if (others) t += (t.empty() ? "" : " · ") + trf("%d copie(s) d'un autre compte", others);
    return t;
}

std::string spot_name(const Json& c);  // plus bas
int place_count_of(const Json& g) {  // endroits différents qui gardent ce jeu (console comprise ; l'autre PS5 compte une fois)
    std::set<std::string> ps;
    if (g["console"].is_object()) ps.insert("console");
    for (const auto& c : g["copies"].items()) ps.insert(c["on_peer"].as_bool() ? "peer:" + c["peer_name"].as_string() : c["place"].as_string());
    return static_cast<int>(ps.size());
}
std::string places_text(const Json& g) {
    std::vector<std::string> seen;
    if (g["console"].is_object()) seen.push_back(tr("Console"));
    for (const auto& c : g["copies"].items()) {
        const std::string l = spot_name(c);  // l'autre PS5 une seule fois, sans redite du service
        if (std::find(seen.begin(), seen.end(), l) == seen.end()) seen.push_back(l);
    }
    std::string out;
    for (const auto& p : seen) out += (out.empty() ? "" : " · ") + p;
    return out;
}

void checkbox(Surface& s, int x, int cy, bool on, bool dim) {
    s.rect(x, cy - 14, 28, 28, on ? color::accent2 : dim ? rgb(255, 255, 255, 12) : rgb(34, 52, 77), 7);
    if (!on) return;
    for (int d = 0; d < 8; ++d) s.rect(x + 5 + d, cy + d / 2 - 2, 4, 4, color::text, 2);
    for (int d = 0; d < 11; ++d) s.rect(x + 11 + d, cy + 2 - d, 4, 4, color::text, 2);
}

// Nom court d'un endroit : « Google Drive (Google Drive) » → « Google Drive » ; l'autre PS5 par son nom.
std::string spot_name(const Json& c) {
    if (c["on_peer"].as_bool()) return c["peer_name"].as_string();
    std::string l = c["place_label"].as_string();
    const auto p = l.find(" (");
    if (p != std::string::npos && l.size() > p + 3 && l.substr(0, p).find(l.substr(p + 2, l.size() - p - 3)) != std::string::npos) l = l.substr(0, p);  // « Backblaze B2 (B2) »
    return l;
}
// Où se trouve chaque partie (lettre du contenu → endroits, une fois chacun ; la console compte comme un endroit).
std::map<std::string, std::vector<std::string>> spread_of(const Json& g) {
    std::map<std::string, std::vector<std::string>> by;
    auto add = [&](const std::string& L, const std::string& name) {
        if (L.empty()) return;
        auto& v = by[L];
        if (std::find(v.begin(), v.end(), name) == v.end()) v.push_back(name);
    };
    add(g["console"]["group"].as_string(), tr("Cette PS5"));
    for (const auto& c : g["copies"].items()) if (!c["other_account"].is_string()) add(c["group"].as_string(), spot_name(c));
    return by;
}
// La partie à mettre en avant : celle de la console, sinon la plus répandue.
std::pair<std::string, std::vector<std::string>> main_spread(const Json& g) {
    const auto by = spread_of(g);
    const std::string cg = g["console"]["group"].as_string();
    if (!cg.empty() && by.count(cg)) return {cg, by.at(cg)};
    std::pair<std::string, std::vector<std::string>> best;
    for (const auto& [L, v] : by) if (v.size() > best.second.size()) best = {L, v};
    return best;
}
std::string join_dot(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& x : v) out += (out.empty() ? "" : " · ") + x;
    return out;
}
// Une copie d'ici (ni d'un autre compte, ni sur l'autre PS5) dont la partie existe aussi à un autre endroit.
bool spread_copy(const Json& c, const std::map<std::string, std::vector<std::string>>& by) {
    if (c["other_account"].is_string() || c["on_peer"].as_bool() || !c["complete"].as_bool(true)) return false;
    const auto it = by.find(c["group"].as_string());
    return it != by.end() && it->second.size() >= 2;
}
}  // namespace

// Lignes d'un paragraphe coupé aux mots.
std::vector<std::string> wrap(const std::string& text, FontSize size, int max_w) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        const std::string tryl = line.empty() ? word : line + " " + word;
        if (!line.empty() && Surface::measure(tryl, size) > max_w) { lines.push_back(line); line = word; }
        else line = tryl;
        word.clear();
    };
    for (char c : text) { if (c == ' ') flush_word(); else word += c; }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// Compte affiché : le premier compte PS5 de cette console (vide : tous les comptes).
std::string Ui::mg_account() const {
    for (const auto& a : overview_["accounts"].items())
        if (a["on_console"].as_bool() && a["platform"].as_string() == "PS5") return a["key"].as_string();
    return "";
}

void Ui::mg_load_summary() {
    mg_sum_at_ = now_ ? now_ : 1;
    if (mg_sum_inflight_.exchange(true)) { mg_sum_dirty_ = true; return; }  // relu dès que la lecture en cours revient
    model_.command("GET", "/api/v2/backups/summary?account=" + url_encode(mg_sum_account_), "", [this](const std::string& e, const std::string& body) {
        Json j;
        const bool ok = e.empty() && Json::parse(body, j) && j.is_object();
        std::lock_guard<std::mutex> l(async_mu_);
        pending_mg_sum_ = ok ? std::move(j) : Json();
        pending_mg_sum_error_ = ok ? std::string() : e.empty() ? std::string(tr("réponse illisible")) : e;
        has_pending_mg_sum_ = true;
        mg_sum_inflight_ = false;
    });
}

void Ui::mg_request_trash(const std::string& place) {
    model_.command("GET", "/api/v2/trash?place=" + url_encode(place), "", [this, place](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j) || !j.is_array()) { j = Json::object(); j.set("error", e.empty() ? std::string(tr("réponse illisible")) : e); }
        std::lock_guard<std::mutex> l(async_mu_);
        pending_mg_trash_[place] = std::move(j);
    });
}

void Ui::mg_load_trash(const std::string& place) {
    if (!mg_trash_loading_.insert(place).second) return;
    mg_request_trash(place);
}

// Comparer le contenu d'un jeu : console + toutes ses copies complètes (lecture seule, sur des copies).
void Ui::mg_compare_game(const Json& g) {
    const std::string acc = g["account"].as_string(), tid = g["title_id"].as_string();
    if (tid.empty()) { toast(tr("Jeu sans identifiant : comparaison impossible")); return; }
    if (acc.find(':') == std::string::npos) { toast(tr("Aucun compte PS5 sur cette console")); return; }
    Json b = Json::object(), src = Json::array();
    b.set("type", "inspect_save");
    b.set("title_id", tid);
    b.set("user_id", acc.substr(acc.find(':') + 1));
    if (g["console"].is_object()) { Json c = Json::object(); c.set("kind", "console"); src.push(c); }
    for (const auto& c : g["copies"].items()) {
        if (!c["complete"].as_bool(true)) continue;
        if (c["on_peer"].as_bool()) continue;  // rangée sur l'autre PS5 : elle la compare elle-même (ici, illisible : toute la comparaison était refusée)
        Json s2 = Json::object();
        s2.set("place", c["place"]);
        s2.set("bundle", c["bundle"]);
        src.push(s2);
    }
    b.set("sources", src);
    mg_task_game_ = gkey(g);
    model_.command("POST", "/api/v2/tasks", b.dump(), [this](const std::string& e, const std::string& resp) {
        Json j;
        std::lock_guard<std::mutex> l(async_mu_);
        if (!e.empty() || !Json::parse(resp, j)) { pending_toast_ = e.empty() ? std::string(tr("Comparaison refusée")) : e; return; }
        mg_task_ = j["id"].as_string();
    });
    toast(tr("Comparaison du contenu… (lecture seule, sur des copies)"));
}

// Aperçu : ce qui part à la corbeille, par jeu et par emplacement.
void Ui::mg_open_preview(const std::set<std::string>& keys) {
    mg_pv_games_.clear();
    mg_pv_places_.clear();
    mg_pv_copies_.clear();
    std::map<std::string, std::size_t> place_at;
    for (const auto& g : mg_sum_["games"].items()) {
        const std::string gk = gkey(g);
        if (!keys.count(gk)) continue;
        const auto ch = chosen(g, mg_csel_, mg_protected_);
        if (ch.empty()) continue;
        MgPvGame pg;
        pg.title = g["title"].as_string();
        int mine = 0;
        for (const auto& c : g["copies"].items()) {
            if (c["other_account"].is_string() || c["on_peer"].as_bool()) continue;
            ++mine;
            if (!ch.count(ckey(c))) continue;
            const std::uint64_t bytes = copy_bytes(c);
            ++pg.remove;
            pg.bytes += bytes;
            mg_pv_copies_.push_back({c["place"].as_string(), c["bundle"].as_string()});
            auto it = place_at.find(c["place"].as_string());
            if (it == place_at.end()) { it = place_at.emplace(c["place"].as_string(), mg_pv_places_.size()).first; mg_pv_places_.push_back({c["place_label"].as_string(), 0, 0}); }
            mg_pv_places_[it->second].count++;
            mg_pv_places_[it->second].bytes += bytes;
        }
        pg.keep = mine - pg.remove;
        mg_pv_games_.push_back(std::move(pg));
    }
    if (mg_pv_copies_.empty()) { toast(tr("Rien à nettoyer dans la sélection")); return; }
    mg_preview_ = true;
    mg_pv_btn_ = 1;  // « Annuler » d'abord, comme les autres confirmations
}

// Mise à la corbeille : une copie après l'autre (le NAS répond en FTP), jamais en parallèle.
void Ui::mg_delete_next(std::size_t i) {
    if (i >= mg_pv_copies_.size()) { mg_del_finished_ = true; return; }
    Json b = Json::object();
    b.set("place", mg_pv_copies_[i].first);
    b.set("bundle", mg_pv_copies_[i].second);
    b.set("confirm", true);
    model_.command("POST", "/api/v2/backups/delete", b.dump(), [this, i](const std::string& e, const std::string&) {
        if (e.empty()) ++mg_del_done_;
        else if (e.find("protég") != std::string::npos) ++mg_del_skipped_;  // 409 : copie protégée, gardée
        else { ++mg_del_failed_; std::lock_guard<std::mutex> l(async_mu_); mg_del_error_ = e; }
        mg_delete_next(i + 1);
    });
}

void Ui::mg_finish_delete() {
    mg_deleting_ = false;
    mg_tidy_games_.clear();
    mg_preview_ = false;
    mg_del_finished_ = false;
    std::string msg = trf("%d copie(s) mise(s) à la corbeille (30 jours)", mg_del_done_.load());
    if (mg_del_skipped_) msg += " · " + trf("%d protégée(s), gardée(s)", mg_del_skipped_.load());
    if (mg_del_failed_) { std::lock_guard<std::mutex> l(async_mu_); msg += " · " + trf("%d refusée(s) : %s", mg_del_failed_.load(), mg_del_error_.c_str()); }
    toast(msg);
    mg_gsel_.clear();
    mg_csel_.clear();
    mg_trash_.clear();
    overview_loaded_ = 0;
    mg_load_summary();
}

void Ui::draw_manage(Surface& s, int x, int w) {
    if (!overview_.is_object()) {  // il faut d'abord savoir quel compte afficher
        s.text(font_, x + 60, 380, tr("Lecture des sauvegardes…"), large, color::muted);
        spinner(s, x + 20, 404, 18);
        draw_footer(s, {{1, tr("Retour")}});
        return;
    }
    const std::string acc = mg_account();
    if (acc != mg_sum_account_ || !mg_sum_at_) { if (acc != mg_sum_account_) mg_sum_ = Json(); mg_sum_account_ = acc; mg_load_summary(); }
    else if (now_ - mg_sum_at_ > (mg_sum_["updating"].as_bool() ? 3000 : 120000)) mg_load_summary();  // relu en silence toutes les 2 min ; 3 s si le moteur recompte encore
    if (mg_sum_dirty_ && !mg_sum_inflight_) { mg_sum_dirty_ = false; mg_load_summary(); }
    // Comparaisons en cours (Tout comparer) : le résumé est relu à chaque jeu terminé.
    int comparing = 0;
    for (const auto& t : snap_->tasks) comparing += t.type == "inspect_save" && (t.state == "queued" || t.state == "running");
    if (comparing < mg_comparing_) mg_load_summary();
    mg_comparing_ = comparing;
    if (!mg_sum_.is_object()) {
        if (!mg_sum_error_.empty()) {
            s.text(font_, x, 380, trf("Lecture impossible : %s", mg_sum_error_.c_str()), large, color::bad, w);
            s.text(font_, x, 440, tr("Il faut le moteur 2.33 ou plus récent pour cet écran."), body, color::muted, w);
            draw_footer(s, {{3, tr("Actualiser")}, {1, tr("Retour")}});
            return;
        }
        s.text(font_, x + 60, 380, tr("Lecture des sauvegardes…"), large, color::muted);
        s.text(font_, x + 60, 436, tr("Copies sur les disques et le NAS, et ce que dit leur contenu."), body, color::muted, w - 60);
        spinner(s, x + 20, 404, 18);
        draw_footer(s, {{1, tr("Retour")}});
        return;
    }
    if (mg_screen_ == 1) draw_mg_game(s, x, w);
    else if (mg_compare_) draw_mg_compare(s, x, w);
    else draw_mg_overview(s, x, w);
}

void Ui::draw_mg_overview(Surface& s, int x, int w) {
    const Json& games = mg_sum_["games"];
    const bool focus_list = mg_zone_ == 1;
    // Compteurs : doublons sûrs, pas encore comparés (et comparaison en cours), à vérifier.
    {
        int check = 0;
        for (const auto& g : games.items()) { bool bad = false; for (const auto& c : g["copies"].items()) bad = bad || suspect(c); check += bad; }
        const std::string c1 = trf("Doublons sûrs : %s à libérer", size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, mg_sum_["freed_bytes"].as_int()))).c_str());
        const std::string c2 = mg_comparing_ ? trf("comparaison en cours : %d jeu(x)", mg_comparing_) : trf("Pas encore comparés : %lld jeu(x)", static_cast<long long>(mg_sum_["to_compare"].as_int()));
        const std::string c3 = trf("À vérifier : %d", check);
        int cx = x;
        const std::pair<const std::string*, std::uint32_t> chips[3] = {{&c1, color::ok}, {&c2, mg_sum_["to_compare"].as_int() ? color::warn : color::ok}, {&c3, check ? color::warn : color::ok}};
        for (const auto& [t, tone] : chips) {
            const int cw = Surface::measure(*t, small) + 32 + (t == &c2 && mg_comparing_ ? 28 : 0);
            s.rect(cx, 330, cw, 38, (tone & 0x00FFFFFFu) | 0x24000000u, 19);
            if (t == &c2 && mg_comparing_) spinner(s, cx + 24, 349, 9);
            s.text(font_, cx + 16 + (t == &c2 && mg_comparing_ ? 28 : 0), 335, *t, small, tone);
            cx += cw + 10;
        }
        if (mg_sum_inflight_) spinner(s, cx + 20, 349, 12);
    }
    // Vues (Carré).
    int px = x;
    for (int i = 0; i < 4; ++i) {
        const int pw = Surface::measure(tr(views[i]), small) + 32;
        if (i == mg_view_) s.rect(px, 384, pw, 38, color::text, 19);
        s.text(font_, px + 16, 390, tr(views[i]), small, i == mg_view_ ? color::bg : color::muted);
        px += pw + 6;
    }
    const std::string next = trf("Voir : %s", tr(views[(mg_view_ + 1) % 4]));
    const auto rows = mg_rows();
    const int n = static_cast<int>(rows.size());
    mg_row_ = std::min(mg_row_, std::max(0, n - 1));
    if (n > 5) { const std::string pos = std::to_string(mg_row_ + 1) + " / " + std::to_string(n); s.text(font_, x + w - Surface::measure(pos, small), 392, pos, small, color::muted); }
    const int per_page = 5, first = std::max(0, mg_row_ - per_page + 1), rh = 92;
    int yy = 438;
    // Bulle d'une autre PS5 : chaque ligne parle de SES copies (pas de tout le jeu) ; on les gère depuis elle.
    const std::string vplace = mg_place_id();
    const bool peer_view = vplace.rfind("peer:", 0) == 0;
    auto on_view = [&](const Json& c) { const std::string p = c["place"].as_string(); return p == vplace || (vplace.back() == ':' && p.rfind(vplace, 0) == 0); };
    for (int r = first; r < n && r < first + per_page; ++r) {
        const Json& g = games[rows[static_cast<std::size_t>(r)]];
        const std::string gk = gkey(g);
        const bool focus = focus_list && r == mg_row_, on = mg_gsel_.count(gk) != 0;
        s.rect(x, yy, w, rh, focus ? color::panel2 : color::panel, 16);
        if (focus) s.outline(x - 4, yy - 4, w + 8, rh + 8, color::accent, 20, 3);
        checkbox(s, x + 16, yy + rh / 2, on, false);
        {   // jaquette : l'élément de la bibliothèque, sinon l'icône officielle par son numéro
            Item stand_in;
            const Item* found = find_or_stand_in(g["item"].as_string(), g["title"].as_string(), g["title_id"].as_string(), stand_in, g["kind"].as_string());
            cover(s, *found, x + 58, yy + 10, 72, false);
        }
        const int tx = x + 146;
        // Pastille à droite : Libère X / À comparer / Rien à faire.
        std::string badge;
        std::uint32_t tone, fill;
        std::string peer_name;
        int peer_n = 0;
        std::int64_t peer_last = 0;
        // Copies de l'endroit choisi (vide sur Partout) : leur nombre, la dernière, les doublons proposés ici.
        int here_n = 0, here_dup = 0;
        std::int64_t here_last = 0;
        std::string here_name;
        if (!vplace.empty() && !peer_view) {
            const auto ch = chosen(g, mg_csel_, mg_protected_);
            for (const auto& c : g["copies"].items())
                if (on_view(c)) { ++here_n; here_last = std::max(here_last, c["created_unix"].as_int()); if (here_name.empty()) here_name = spot_name(c); if (ch.count(ckey(c))) ++here_dup; }
        }
        if (peer_view) for (const auto& c : g["copies"].items()) if (on_view(c)) { ++peer_n; peer_last = std::max(peer_last, c["created_unix"].as_int()); if (peer_name.empty()) peer_name = c["peer_name"].as_string(); }
        if (peer_view) { badge = trf("Sur %s", peer_name.c_str()); tone = color::muted; fill = rgb(255, 255, 255, 16); }
        else if (!vplace.empty() && !on && !g["full"].as_bool()) {  // un endroit : ce qu'il y a à faire ICI
            if (here_dup) { badge = trf("%d doublon(s) ici", here_dup); tone = color::ok; fill = rgb(95, 211, 154, 36); }
            else if (!g["compared"].as_bool()) { badge = tr("À comparer"); tone = color::warn; fill = rgb(240, 184, 103, 36); }
            else { badge = tr("Rien à faire ici"); tone = color::muted; fill = rgb(255, 255, 255, 16); }
        }
        else if (on) {
            badge = trf("Sélectionné · %d copie(s)", static_cast<int>(chosen(g, mg_csel_, mg_protected_).size()));
            tone = color::accent; fill = rgb(61, 139, 255, 50);
        } else if (g["full"].as_bool()) {
            std::uint64_t b = 0;
            for (const auto& c : g["copies"].items()) b += copy_bytes(c);
            badge = std::string(kind_tag(g["kind"].as_string())) + " · " + size_text(b);
            tone = color::text; fill = rgb(255, 255, 255, 20);
        } else if (g["remove"].size()) {
            badge = g["freed_bytes"].as_int() > 0 ? trf("Libère %s", size_text(static_cast<std::uint64_t>(g["freed_bytes"].as_int())).c_str()) : trf("%d doublon(s)", static_cast<int>(g["remove"].size()));
            tone = color::ok; fill = rgb(95, 211, 154, 36);
        } else if (!g["compared"].as_bool()) { badge = tr("À comparer"); tone = color::warn; fill = rgb(240, 184, 103, 36); }
        else { badge = tr("Rien à faire"); tone = color::muted; fill = rgb(255, 255, 255, 16); }
        const int bwid = Surface::measure(badge, small) + 30;
        s.rect(x + w - 20 - bwid, yy + 12, bwid, 34, fill, 17);
        s.text(font_, x + w - 20 - bwid + 15, yy + 17, badge, small, tone);
        const int tw = s.text(font_, tx, yy + 6, g["title"].as_string(), body, color::text, w - (tx - x) - bwid - 260);
        s.text(font_, tx + tw + 16, yy + 13, places_text(g), small, color::muted, std::max(0, w - (tx - x) - tw - bwid - 56));
        if (peer_view) s.text(font_, tx, yy + 40, trf("%d copie(s) sur %s · la dernière le %s · gérées depuis cette console-là", peer_n, peer_name.c_str(), date_text(peer_last, false).c_str()), small, color::text, w - (tx - x) - 24);
        else if (!vplace.empty() && here_n) {
            std::string line = trf("%d copie(s) sur %s · la dernière le %s", here_n, here_name.c_str(), date_text(here_last, false).c_str());
            const int elsewhere = place_count_of(g) - 1;
            if (elsewhere > 0) line += " · " + trf("aussi à %d autre(s) endroit(s)", elsewhere);
            s.text(font_, tx, yy + 40, line, small, color::text, w - (tx - x) - 24);
        }
        else s.text(font_, tx, yy + 40, summary_text(g), small, color::text, w - (tx - x) - 24);
        const std::string pl = progress_line(g);
        if (!pl.empty()) s.text(font_, tx, yy + 64, pl, small, color::accent, w - (tx - x) - 24);
        yy += rh + 8;
    }
    if (!n) s.text(font_, x, 460, mg_view_ == 1 ? tr("Aucun doublon sûr pour l'instant.") : mg_view_ == 2 ? tr("Tout est comparé.") : mg_view_ == 3 ? tr("Rien à vérifier.") : tr("Aucune sauvegarde trouvée."), large, color::muted, w);
    // Cochés : le compteur au-dessus de l'aide, et ce qui partirait à la corbeille.
    {
        int ng = 0, nc = 0;
        std::uint64_t bytes = 0;
        for (const auto& g : games.items()) {
            if (!mg_gsel_.count(gkey(g))) continue;
            const auto ch = chosen(g, mg_csel_, mg_protected_);
            ++ng;
            nc += static_cast<int>(ch.size());
            for (const auto& c : g["copies"].items()) if (ch.count(ckey(c))) bytes += copy_bytes(c);
        }
        if (ng) {
            draw_checked(s, ng);
            s.text(font_, margin + Surface::measure(trf("%d coché(s)", ng), small) + 48, 955, trf("%d copie(s) · libère %s", nc, size_text(bytes).c_str()), small, color::text);
        }
    }
    draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("L2 : cocher")}, {2, next.c_str()}, {3, tr("Tout comparer")}, {-1, tr("Options : actions")}, {-1, tr("R2 : doublons…")}, {1, tr("Retour")}});
}

// Corbeille de chaque disque USB et du NAS, par jour ; Croix remet la copie à sa place.
void Ui::draw_mg_trash(Surface& s, int x, int w, int y0) {
    struct Row { int kind; std::string text, place; const Json* item; };  // 0 emplacement, 1 jour, 2 copie
    std::vector<Row> rows;
    bool loading = false;
    for (const auto& p : snap_->places) {
        if ((p.type != "usb" && p.type != "ftp") || p.use == "sync" || !p.same_backups_as.empty() || p.readonly) continue;  // un même NAS par deux chemins : une fois  // NAS « Synchro seulement » : pas de corbeille ici
        const auto it = mg_trash_.find(p.id);
        if (it == mg_trash_.end()) { mg_load_trash(p.id); loading = true; rows.push_back({0, p.label + "  ·  " + tr("lecture…"), p.id, nullptr}); continue; }
        const Json& list = it->second;
        if (!list["error"].as_string().empty()) { rows.push_back({0, p.label + "  ·  " + list["error"].as_string(), p.id, nullptr}); continue; }
        if (list.size() == 0) { rows.push_back({0, p.label + "  ·  " + tr("corbeille vide"), p.id, nullptr}); continue; }
        rows.push_back({0, p.label + "  ·  " + trf("%d copie(s)", static_cast<int>(list.size())), p.id, nullptr});
        std::vector<const Json*> items;
        for (const auto& t : list.items()) items.push_back(&t);
        std::stable_sort(items.begin(), items.end(), [](const Json* a, const Json* b) { return (*a)["day"].as_string() > (*b)["day"].as_string(); });
        std::string day;
        for (const Json* t : items) {
            if ((*t)["day"].as_string() != day) {
                day = (*t)["day"].as_string();
                const std::string d = day.size() == 10 ? day.substr(8, 2) + "/" + day.substr(5, 2) + "/" + day.substr(0, 4) : day;
                rows.push_back({1, trf("Mise à la corbeille le %s", d.c_str()), p.id, nullptr});
            }
            rows.push_back({2, "", p.id, t});
        }
    }
    if (loading) spinner(s, x + w - 20, 402, 14);
    {   // Tous les endroits d'un coup d'œil, avant le détail (un disque peut en avoir des centaines)
        std::string all;
        int places_with = 0;
        for (const auto& p : snap_->places) {
            if ((p.type != "usb" && p.type != "ftp") || p.use == "sync" || !p.same_backups_as.empty() || p.readonly) continue;  // un même NAS par deux chemins : une fois
            const auto it = mg_trash_.find(p.id);
            if (it == mg_trash_.end() || !it->second.is_array() || it->second.size() == 0) continue;
            ++places_with;
            all += (all.empty() ? "" : " · ") + p.label + " " + std::to_string(it->second.size());
        }
        if (places_with > 1) rows.insert(rows.begin(), Row{1, trf("Dans la corbeille : %s", all.c_str()), "", nullptr});
    }
    std::vector<int> items;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (rows[static_cast<std::size_t>(i)].kind == 2) items.push_back(i);
    mg_trow_ = std::min(mg_trow_, std::max(0, static_cast<int>(items.size()) - 1));
    auto height = [&](int i) { return rows[static_cast<std::size_t>(i)].kind == 2 ? 76 : 46; };
    const int bottom = 936, focus = items.empty() ? 0 : items[static_cast<std::size_t>(mg_trow_)];
    int top = 0;
    {
        auto span = [&](int a, int b) { int h = 0; for (int i = a; i <= b; ++i) h += height(i); return h; };
        while (top < focus && span(top, focus) > bottom - y0) ++top;
    }
    int yy = y0;
    for (int i = top; i < static_cast<int>(rows.size()); ++i) {
        const Row& r = rows[static_cast<std::size_t>(i)];
        if (yy + height(i) - 6 > bottom) break;
        if (r.kind == 0) s.text(font_, x, yy + 10, r.text, body, color::text, w);
        else if (r.kind == 1) s.text(font_, x + 16, yy + 14, r.text, small, color::muted, w - 16);
        else {
            const bool f = mg_zone_ == 1 && i == focus;
            s.rect(x, yy, w, 70, f ? color::panel2 : color::panel, 14);
            if (f) s.outline(x - 4, yy - 4, w + 8, 78, color::accent, 18, 3);
            const std::string bundle = (*r.item)["bundle"].as_string();
            // Nom du jeu retrouvé par son identifiant (ou son titre) dans le nom de la copie.
            // (lettres et chiffres seulement : « Clair Obscur: Expedition 33 » s'écrit « Clair Obscur_ … » dans un nom de fichier)
            auto plain = [](const std::string& t) { std::string o; for (unsigned char ch : t) if (std::isalnum(ch) || ch >= 0x80) o += static_cast<char>(std::tolower(ch)); return o; };
            const std::string pb = plain(bundle);
            std::string name;
            for (const auto& g : mg_sum_["games"].items()) {
                const std::string& tid = g["title_id"].as_string();
                const std::string pt = plain(g["title"].as_string());
                if ((!tid.empty() && bundle.find(tid) != std::string::npos) || (!pt.empty() && pb.find(pt) != std::string::npos)) { name = g["title"].as_string(); break; }
            }
            const std::string file = bundle.substr(bundle.rfind('/') == std::string::npos ? 0 : bundle.rfind('/') + 1);
            s.text(font_, x + 24, yy + 8, name.empty() ? file : name, body, color::text, w - 300);
            s.text(font_, x + 24, yy + 42, bundle, small, color::muted, w - 300);
            const char* back = tr("Remettre");
            s.text(font_, x + w - 24 - Surface::measure(back, small), yy + 24, back, small, f ? color::accent : color::muted);
        }
        yy += height(i);
    }
    if (rows.empty()) s.text(font_, x, y0 + 20, tr("Aucun disque USB ni NAS."), large, color::muted);
    s.text(font_, x, 946, tr("Les copies restent 30 jours dans « PBC Backup/Corbeille » de leur disque ou NAS."), small, color::muted, w);
    if (mg_zone_ == 0) draw_footer(s, {{0, tr("Ouvrir la liste")}, {3, tr("Actualiser")}, {-1, tr("Gauche / droite : endroits")}, {1, tr("Retour")}});  // ← → seulement sur les bulles
    else draw_footer(s, {{0, tr("Remettre")}, {-1, tr("Options : supprimer, vider…")}, {3, tr("Actualiser")}, {1, tr("Retour")}});
}

void Ui::draw_mg_game(Surface& s, int x, int w) {
    const Json* gp = nullptr;
    for (const auto& g : mg_sum_["games"].items()) if (gkey(g) == mg_game_) { gp = &g; break; }
    if (!gp) { mg_screen_ = 0; draw_mg_overview(s, x, w); return; }
    const Json& g = *gp;
    const Json& copies = g["copies"];
    const auto ch = chosen(g, mg_csel_, mg_protected_);
    {
        Item stand_in;
        const Item* found = find_or_stand_in(g["item"].as_string(), g["title"].as_string(), g["title_id"].as_string(), stand_in, g["kind"].as_string());
        cover(s, *found, x, 196, 72, false);
    }
    s.text(font_, x + 92, 194, g["title"].as_string(), large, color::text, w - 92);
    {
        const auto sp = g["full"].as_bool() ? std::pair<std::string, std::vector<std::string>>() : main_spread(g);
        if (sp.second.size() >= 2) s.text(font_, x + 92, 242, trf("Même partie à %d endroits : %s", static_cast<int>(sp.second.size()), join_dot(sp.second).c_str()), small, color::ok, w - 300);
        else s.text(font_, x + 92, 242, places_text(g), small, color::muted, w - 300);
    }
    // Bandeau : la proposition (selon les cases cochées).
    int mine = 0;
    std::uint64_t bytes = 0;
    for (const auto& c : copies.items()) {
        if (c["other_account"].is_string() || c["on_peer"].as_bool()) continue;  // comme la liste : les copies d'ici
        ++mine;
        if (ch.count(ckey(c))) bytes += copy_bytes(c);
    }
    const bool comparing = mg_task_game_ == mg_game_;
    std::string banner;
    std::uint32_t tone;
    if (comparing) { banner = tr("Comparaison du contenu… (lecture seule, sur des copies)"); tone = color::accent; }
    else if (!ch.empty() && g["full"].as_bool()) { banner = trf("%d copie(s) cochée(s) sur %d — libère %s", static_cast<int>(ch.size()), mine, size_text(bytes).c_str()); tone = color::ok; }
    else if (!ch.empty()) { banner = trf("Proposition : garder %d copie(s), en supprimer %d — libère %s", mine - static_cast<int>(ch.size()), static_cast<int>(ch.size()), size_text(bytes).c_str()); tone = color::ok; }
    else if (g["full"].as_bool()) { banner = tr("Sauvegardes complètes : cochez celles à mettre à la corbeille"); tone = color::muted; }
    else if (!g["compared"].as_bool()) { banner = tr("Pas encore comparées : lancez la comparaison"); tone = color::warn; }
    else { banner = tr("Rien à nettoyer"); tone = color::muted; }
    s.rect(x, 284, w, 60, color::panel2, 16);
    s.rect(x, 284, 6, 60, tone, 3);
    s.text(font_, x + 26, 299, banner, body, tone == color::muted ? color::text : tone, w - 80);
    if (comparing) spinner(s, x + w - 34, 314, 14);

    // Lignes : console, mes copies (plus récentes d'abord), puis celles d'un autre compte.
    struct Row { int kind; std::size_t idx; };  // 0 console, 1 copie, 2 titre « autre compte »
    std::vector<Row> rows;
    if (g["console"].is_object()) rows.push_back({0, 0});
    std::vector<std::size_t> mineidx, others;
    for (std::size_t i = 0; i < copies.size(); ++i) (copies[i]["other_account"].is_string() ? others : mineidx).push_back(i);
    auto newest = [&](std::size_t a, std::size_t b) { return copies[a]["created_unix"].as_int() > copies[b]["created_unix"].as_int(); };
    std::stable_sort(mineidx.begin(), mineidx.end(), newest);
    std::stable_sort(others.begin(), others.end(), newest);
    for (auto i : mineidx) rows.push_back({1, i});
    if (!others.empty()) { rows.push_back({2, 0}); for (auto i : others) rows.push_back({1, i}); }
    std::vector<int> focusable;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (rows[static_cast<std::size_t>(i)].kind == 1) focusable.push_back(i);
    mg_crow_ = std::min(mg_crow_, std::max(0, static_cast<int>(focusable.size()) - 1));
    auto height = [&](int i) { const int k = rows[static_cast<std::size_t>(i)].kind; return k == 1 ? 76 : k == 0 ? 70 : 48; };
    const int y0 = 362, bottom = 992, focus = focusable.empty() ? -1 : focusable[static_cast<std::size_t>(mg_crow_)];
    int top = 0;
    {
        auto span = [&](int a, int b) { int h = 0; for (int i = a; i <= b; ++i) h += height(i); return h; };
        while (focus >= 0 && top < focus && span(top, focus) > bottom - y0) ++top;
    }
    const std::string console_group = g["console"]["group"].as_string();
    int yy = y0;
    for (int i = top; i < static_cast<int>(rows.size()); ++i) {
        const Row& r = rows[static_cast<std::size_t>(i)];
        if (yy + height(i) - 6 > bottom) break;
        if (r.kind == 2) {
            const std::string id = copies[others.front()]["other_account"].as_string();
            s.text(font_, x, yy + 12, trf("Un autre compte (%s…) : gardées telles quelles", id.substr(0, 8).c_str()), body, color::muted, w);
        } else if (r.kind == 0) {
            const Json& con = g["console"];
            s.rect(x, yy, w, 64, rgb(255, 255, 255, 10), 14);
            std::string l1 = std::string(tr("Console")) + "  ·  " + size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, con["bytes"].as_int())));
            if (con["mtime"].as_int() > 0) l1 += "  ·  " + date_text(con["mtime"].as_int());
            const int lw = s.text(font_, x + 62, yy + 6, l1, body, color::text, w - 300);
            std::string pt = con["progress"]["text"].as_string();
            s.text(font_, x + 62, yy + 38, pt.empty() ? std::string(tr("La sauvegarde du jeu sur la PS5 (jamais touchée)")) : pt, small, color::muted, w - 260);
            (void)lw;
            if (!console_group.empty()) { const std::string L = trf("contenu %s", console_group.c_str()); s.text(font_, x + w - 24 - Surface::measure(L, small), yy + 22, L, small, color::muted); }
        } else {
            const Json& c = copies[r.idx];
            const std::string k = ckey(c);
            const bool f = mg_zone_ == 1 && i == focus, on = ch.count(k) != 0, other = c["other_account"].is_string();
            const bool prot = mg_protected_.count(k) != 0;
            s.rect(x, yy, w, 70, f ? color::panel2 : color::panel, 14);
            if (f) s.outline(x - 4, yy - 4, w + 8, 78, color::accent, 18, 3);
            else if (on) s.outline(x, yy, w, 70, color::accent2, 14, 1);
            checkbox(s, x + 16, yy + 35, on, other || prot);
            // Étiquette : Autre compte / Incomplète / À comparer / Gardée / Doublon.
            const char* tag = nullptr;
            std::uint32_t tone2 = color::muted, fill = rgb(255, 255, 255, 16);
            const std::string peer_tag = c["on_peer"].as_bool() ? trf("Sur %s", c["peer_name"].as_string().c_str()) : std::string();
            if (!peer_tag.empty()) tag = peer_tag.c_str();  // rangée sur l'autre PS5 : elle la compare elle-même, rien à faire d'ici
            else if (other) tag = tr("Autre compte");
            else if (c["needs_resign"].as_bool()) { tag = tr("Autre identifiant"); tone2 = color::accent; fill = rgb(124, 196, 255, 36); }
            else if (!c["complete"].as_bool(true)) { tag = tr("Incomplète"); tone2 = color::bad; fill = rgb(255, 123, 123, 36); }
            else if (g["full"].as_bool()) tag = nullptr;
            else if (!c["group"].is_string() || c["same_bytes"].as_bool()) { tag = tr("À comparer"); tone2 = color::warn; fill = rgb(240, 184, 103, 36); }
            else if (has_index(g["keep"], r.idx)) { tag = tr("Gardée"); tone2 = color::ok; fill = rgb(95, 211, 154, 36); }
            else if (has_index(g["remove"], r.idx)) { tag = tr("Doublon"); tone2 = color::warn; fill = rgb(240, 184, 103, 36); }
            int right = x + w - 20;
            if (tag) {
                const int tw = Surface::measure(tag, small) + 30;
                right -= tw;
                s.rect(right, yy + 18, tw, 34, fill, 17);
                s.text(font_, right + 15, yy + 23, tag, small, tone2);
                right -= 14;
            }
            if (c["group"].is_string()) {  // lettre du contenu, discrète
                const std::string L = c["group"].as_string() == console_group ? trf("contenu %s (= console)", console_group.c_str()) : trf("contenu %s", c["group"].as_string().c_str());
                right -= Surface::measure(L, small);
                s.text(font_, right, yy + 25, L, small, color::muted);
                right -= 16;
            }
            s.text(font_, x + 62, yy + 6, c["place_label"].as_string() + "  ·  " + date_text(c["created_unix"].as_int()), body, color::text, right - x - 62);
            std::string l2 = c["progress_text"].as_string();
            if (g["full"].as_bool()) {
                for (const auto& pt : c["parts"].items()) l2 += (l2.empty() ? "" : ", ") + std::string(ui_util::part_name(pt.as_string()));
                if (copy_bytes(c)) l2 += (l2.empty() ? "" : " · ") + size_text(copy_bytes(c));
            }
            const std::string pt = play_text(c["play_time_seconds"].as_int());
            if (!pt.empty() && l2.find(':') == std::string::npos) l2 += (l2.empty() ? "" : " · ") + trf("%s de jeu", pt.c_str());
            if (c["progress_auto"].as_bool() && !l2.empty()) l2 += " · " + std::string(tr("détecté automatiquement"));
            if (c["settings_only"].as_bool()) l2 = std::string(tr("réglages seulement, pas de partie")) + (l2.empty() ? "" : " · " + l2);
            if (prot) l2 += (l2.empty() ? "" : " · ") + std::string(tr("protégée"));
            if (c["needs_resign"].as_bool()) {  // à qui elle est : nom connu, sinon le début de l'identifiant
                std::string id6 = c["owner"].as_string().substr(0, 6);
                for (auto& ch2 : id6) ch2 = static_cast<char>(std::toupper(static_cast<unsigned char>(ch2)));
                const std::string who = c["owner_name"].as_string().empty() ? id6 + "…" : c["owner_name"].as_string();
                l2 += (l2.empty() ? "" : " · ") + trf("identifiant %s, absent de cette console", who.c_str());
            }
            if (c["legacy"].as_bool()) l2 += (l2.empty() ? "" : " · ") + std::string(tr("ancienne (script)"));
            if (c["on_peer"].as_bool()) l2 = trf("rangée sur %s : Croix pour la récupérer ici", c["peer_name"].as_string().c_str()) + (l2.empty() ? "" : " · " + l2);
            if (l2.empty()) {
                const std::int64_t saved = c["game_saved_at"].as_int() ? c["game_saved_at"].as_int() : c["save_mtime"].as_int();
                l2 = saved > 0 ? trf("partie enregistrée le %s", date_text(saved).c_str())
                   : c["on_peer"].as_bool() && !c["group"].is_string() ? trf("pas encore lue par %s", c["peer_name"].as_string().c_str())
                   : c["same_bytes"].as_bool() ? std::string(tr("identique octet pour octet à une autre copie ; avancée pas encore lue"))
                   : c["group"].is_string() ? std::string(tr("contenu lu, le jeu n'écrit pas son avancée")) : std::string(tr("contenu non comparé"));
            }
            s.text(font_, x + 62, yy + 40, l2, small, prot ? color::warn : color::muted, right - x - 62);
        }
        yy += height(i);
    }
    if (focusable.size() > 6) {
        const std::string pos = std::to_string(mg_crow_ + 1) + " / " + std::to_string(focusable.size());
        s.text(font_, x + w - Surface::measure(pos, small), 252, pos, small, color::muted);
    }
    {   // version focalisée : sauvegarde (restaurable sur le compte) ou backup complet
        if (!ch.empty()) draw_checked(s, static_cast<int>(ch.size()));
    }
    const bool focus_peer = focus >= 0 && copies[rows[static_cast<std::size_t>(focus)].idx]["on_peer"].as_bool();  // copie rangée sur l'autre PS5
    if (focus_peer) draw_footer(s, {{0, tr("Récupérer ici…")}, {3, tr("Comparer ce jeu")}, {1, tr("Retour")}});
    else if (!ch.empty()) draw_footer(s, {{0, tr("Restaurer")}, {-1, tr("L2 : cocher")}, {3, tr("Comparer ce jeu")}, {-1, tr("R2 : cochées à la corbeille…")}, {-1, tr("Options : actions")}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr("Restaurer")}, {-1, tr("L2 : cocher")}, {3, tr("Comparer ce jeu")}, {-1, tr("Options : actions")}, {1, tr("Retour")}});  // supprimer : dans Options  // △ et R2 marchaient sans être annoncés
}

void Ui::draw_mg_preview(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int w = 1160, x = (1920 - w) / 2;
    const int shown = std::min(6, static_cast<int>(mg_pv_games_.size())), more = static_cast<int>(mg_pv_games_.size()) - shown;
    const std::string note = tr("Les copies vont dans « PBC Backup/Corbeille » du même disque ou NAS et peuvent être remises pendant 30 jours. Les copies protégées et celles d'un autre compte ne sont jamais touchées.");
    const auto lines = wrap(note, small, w - 96);
    const int h = 112 + shown * 46 + (more ? 40 : 0) + 24 + 40 + static_cast<int>(mg_pv_places_.size()) * 42 + 20 + static_cast<int>(lines.size()) * 32 + 30 + 60 + 36;
    const int y = std::max(40, (1000 - h) / 2);
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    std::uint64_t total = 0;
    for (const auto& p : mg_pv_places_) total += p.bytes;
    const std::string title = mg_pv_delete_ ? (mg_pv_games_.size() == 1 ? trf("Supprimer de « %s »", mg_pv_games_[0].title.c_str()) : trf("Supprimer de %d jeux", static_cast<int>(mg_pv_games_.size())))
                            : mg_pv_games_.size() == 1 ? trf("Nettoyer « %s »", mg_pv_games_[0].title.c_str()) : trf("Nettoyer %d jeux", static_cast<int>(mg_pv_games_.size()));
    s.text(font_, x + 48, y + 32, title, large, color::text, w - 96);
    int yy = y + 112;
    for (int i = 0; i < shown; ++i) {
        const auto& pg = mg_pv_games_[static_cast<std::size_t>(i)];
        const std::string r = trf("garde %d, supprime %d — libère %s", pg.keep, pg.remove, size_text(pg.bytes).c_str());
        const int rw = Surface::measure(r, small);
        s.text(font_, x + 48, yy, pg.title, body, color::text, w - 140 - rw);
        s.text(font_, x + w - 48 - rw, yy + 5, r, small, color::muted);
        yy += 46;
    }
    if (more) { s.text(font_, x + 48, yy, trf("et %d autre(s) jeu(x)", more), small, color::muted); yy += 40; }
    yy += 24;
    s.text(font_, x + 48, yy, tr("Par emplacement"), small, color::muted);
    yy += 40;
    for (const auto& p : mg_pv_places_) {
        const std::string r = trf("%d copie(s) · %s", p.count, size_text(p.bytes).c_str());
        s.text(font_, x + 48, yy, p.label, body, color::text, w - 400);
        s.text(font_, x + w - 48 - Surface::measure(r, body), yy, r, body, color::text);
        yy += 42;
    }
    yy += 20;
    for (const auto& l : lines) { s.text(font_, x + 48, yy, l, small, color::muted); yy += 32; }
    yy += 30;
    if (mg_deleting_) {
        const int done = mg_del_done_ + mg_del_skipped_ + mg_del_failed_;
        spinner(s, x + 72, yy + 28, 18);
        s.text(font_, x + 110, yy + 12, trf("Mise à la corbeille… %d / %d", done, static_cast<int>(mg_pv_copies_.size())), body, color::text);
    } else {
        const std::string b0 = trf("Mettre à la corbeille (%s)", size_text(total).c_str());
        const char* labels[2] = {b0.c_str(), tr("Annuler")};
        int bx = x + 48;
        for (int i = 0; i < 2; ++i) {
            const int bw = Surface::measure(labels[i], body) + 56;
            const bool on = mg_pv_btn_ == i;
            s.rect(bx, yy, bw, 60, on ? color::text : i == 0 ? rgb(255, 123, 123, 60) : rgb(255, 255, 255, 22), 30);
            s.text(font_, bx + 28, yy + 14, labels[i], body, on ? color::bg : color::text);
            bx += bw + 16;
        }
    }
    s.rect(0, 990, 1920, 90, color::bg);
    if (mg_deleting_) draw_footer(s, {{-1, tr("Une copie après l'autre…")}});
    else draw_footer(s, {{0, tr("Valider")}, {-1, tr("Gauche / droite")}, {1, tr("Annuler")}});
}

void Ui::input_manage(std::uint32_t k) {
    if (mg_ign_open_) {  // ignorer : Haut / Bas, Croix choisit, Rond annule (rien pendant l'attente)
        if (mg_ign_busy_) return;
        if (k & key::up) mg_ign_btn_ = std::max(0, mg_ign_btn_ - 1);
        if (k & key::down) mg_ign_btn_ = std::min(2, mg_ign_btn_ + 1);
        if ((k & key::circle) || ((k & key::cross) && mg_ign_btn_ == 2)) { mg_ign_open_ = false; toast(tr("Rien n'a été modifié")); return; }
        if (!(k & key::cross)) return;
        mg_ign_busy_ = true;
        mg_ign_done_ = mg_ign_trashed_ = mg_ign_refused_ = mg_ign_failed_ = 0;
        ignore_next(0, mg_ign_btn_ == 0);
        return;
    }
    if (mg_preview_) {
        if (mg_deleting_) return;
        if (k & (key::left | key::right)) mg_pv_btn_ = 1 - mg_pv_btn_;
        if ((k & key::circle) || ((k & key::cross) && mg_pv_btn_ == 1)) {
            mg_preview_ = false;
            for (const auto& gk : mg_tidy_games_) mg_csel_.erase(gk);  // le tri annulé ne laisse rien de coché
            mg_tidy_games_.clear();
            toast(tr("Rien n'a été supprimé"));
            return;
        }
        if (k & key::cross) {
            // Dernière étape : maintenir Carré 5 s (comme pour la sauvegarde de la console). Les copies vont à la corbeille
            // de leur disque ou du NAS (30 jours) ; on le rappelle.
            std::uint64_t total = 0;
            std::set<std::string> nas;
            for (const auto& c : mg_pv_copies_) if (c.first.rfind("remote:", 0) == 0) nas.insert(c.first);
            for (const auto& p : mg_pv_places_) total += p.bytes;
            std::string note = trf("%d copie(s), %s.", static_cast<int>(mg_pv_copies_.size()), size_text(total).c_str()) + " " +
                               tr("Ce sont des sauvegardes (des copies), pas la partie active sur la console. Elles vont à la corbeille de leur disque pendant 30 jours.");
            if (!nas.empty()) note += " " + std::string(tr("Une partie est sur le NAS : elle va dans la corbeille du NAS."));
            open_hold_confirm(tr("Mettre ces copies à la corbeille ?"), note, [this] {
                mg_deleting_ = true;
                mg_del_done_ = mg_del_skipped_ = mg_del_failed_ = 0;
                mg_del_finished_ = false;
                mg_delete_next(0);
            });
        }
        return;
    }
    if (!mg_sum_.is_object()) {
        if ((k & key::triangle) && !mg_sum_error_.empty()) { mg_sum_error_.clear(); mg_load_summary(); }
        if (k & (key::up | key::circle)) mg_zone_ = 0;
        return;
    }
    const Json& games = mg_sum_["games"];
    if (mg_screen_ == 1) {  // Toutes les versions d'un élément
        const Json* gp = nullptr;
        for (const auto& g : games.items()) if (gkey(g) == mg_game_) { gp = &g; break; }
        if (!gp || (k & key::circle)) {
            mg_screen_ = 0;
            if (mg_back_tab_ >= 0) { tab_ = static_cast<Tab>(mg_back_tab_); mg_back_tab_ = -1; }
            return;
        }
        const Json& g = *gp;
        const Json& copies = g["copies"];
        std::vector<std::size_t> order, others;  // même ordre que l'écran
        for (std::size_t i = 0; i < copies.size(); ++i) (copies[i]["other_account"].is_string() ? others : order).push_back(i);
        auto newest = [&](std::size_t a, std::size_t b) { return copies[a]["created_unix"].as_int() > copies[b]["created_unix"].as_int(); };
        std::stable_sort(order.begin(), order.end(), newest);
        std::stable_sort(others.begin(), others.end(), newest);
        order.insert(order.end(), others.begin(), others.end());
        const int n = static_cast<int>(order.size());
        if (k & key::up) mg_crow_ = std::max(0, mg_crow_ - 1);
        if (k & key::down) mg_crow_ = std::min(std::max(0, n - 1), mg_crow_ + 1);
        if (k & key::triangle) { if (g["full"].as_bool()) toast(tr("Pas de comparaison pour une sauvegarde complète")); else mg_compare_game(g); return; }
        if (mg_crow_ >= n) return;
        const Json& c = copies[order[static_cast<std::size_t>(mg_crow_)]];
        const std::string ck = ckey(c);
        if (k & key::options) { mg_version_actions(g, c); return; }
        if (c["on_peer"].as_bool() && (k & (key::square | key::l2 | key::cross))) {  // copie rangée sur l'autre PS5
            if (k & key::cross) mg_version_actions(g, c);  // « Récupérer ici… »
            else toast(trf("Cette copie est rangée sur %s : pour la supprimer ou la protéger, faites-le depuis cette console.", c["peer_name"].as_string().c_str()));
            return;
        }
        if (k & key::square) { toast(tr("Supprimer : Options › Supprimer…")); return; }  // Carré ne supprime plus jamais (règle des touches)
        if (k & key::r2) {
            const std::string gk = gkey(g);
            if (chosen(g, mg_csel_, mg_protected_).empty()) { toast(tr("Rien à mettre à la corbeille pour cet élément")); return; }
            mg_gsel_.insert(gk);
            mg_open_preview({gk});
            return;
        }
        if (k & key::cross) {  // ✕ propose, ne lance pas (charte §1)
            if (g["full"].as_bool()) {
                open_actions(tr("Restaurer cette sauvegarde complète ?"), {{tr("Restaurer"), [this, g, c] { mg_restore(g, c); }}, {tr("Annuler"), [] {}}});
                popup_note_ = tr("Les fichiers sont remis dans « PBC Restaurations », sur le même disque. Rien n'est écrasé.");
            } else mg_restore(g, c);
            return;
        }
        if (k & key::l2) {
            if (c["other_account"].is_string()) { toast(tr("Copie d'un autre compte : elle n'est jamais touchée")); return; }
            if (mg_protected_.count(ck)) { toast(tr("Copie protégée : retirez d'abord la protection")); return; }
            if (ro_place(c["place"].as_string())) { toast(tr("Copie d'un partage reçu : lecture seule, elle n'est jamais touchée")); return; }
            auto set = chosen(g, mg_csel_, mg_protected_);
            if (!set.erase(ck)) set.insert(ck);
            const std::string gk = gkey(g);
            if (set.empty()) mg_gsel_.erase(gk); else mg_gsel_.insert(gk);
            mg_csel_[gk] = std::move(set);
        }
        return;
    }
    if (mg_in_trash()) {  // corbeille
        if (k & (key::circle | (mg_trow_ == 0 ? key::up : 0u))) { mg_zone_ = 0; return; }
        if (k & key::triangle) { mg_trash_.clear(); return; }
        std::vector<std::pair<std::string, const Json*>> items;  // même ordre que l'écran
        for (const auto& p : snap_->places) {
            if ((p.type != "usb" && p.type != "ftp") || p.use == "sync" || !p.same_backups_as.empty() || p.readonly) continue;  // un même NAS par deux chemins : une fois  // NAS « Synchro seulement » : pas de corbeille ici
            const auto it = mg_trash_.find(p.id);
            if (it == mg_trash_.end() || !it->second.is_array()) continue;
            std::vector<const Json*> v;
            for (const auto& t : it->second.items()) v.push_back(&t);
            std::stable_sort(v.begin(), v.end(), [](const Json* a, const Json* b) { return (*a)["day"].as_string() > (*b)["day"].as_string(); });
            for (const Json* t : v) items.push_back({p.id, t});
        }
        const int n = static_cast<int>(items.size());
        if (k & key::up) mg_trow_ = std::max(0, mg_trow_ - 1);
        if (k & key::down) mg_trow_ = std::min(std::max(0, n - 1), mg_trow_ + 1);
        if ((k & key::cross) && mg_trow_ < n) {
            const std::string place = items[static_cast<std::size_t>(mg_trow_)].first;
            Json b = Json::object();
            b.set("place", place);
            b.set("path", (*items[static_cast<std::size_t>(mg_trow_)].second)["path"]);
            mg_trash_loading_.insert(place);  // relue après la remise en place
            model_.command("POST", "/api/v2/trash/restore", b.dump(), [this, place](const std::string& e, const std::string&) {
                post_toast(e.empty() ? std::string(tr("Copie remise à sa place")) : e);
                mg_sum_dirty_ = true;
                mg_request_trash(place);
            });
            overview_loaded_ = 0;
            toast(tr("Remise en place…"));
        }
        // Options : remettre, supprimer pour de bon, vider la corbeille de cet endroit (Carré maintenu pour confirmer).
        if ((k & key::options) && mg_trow_ < n) {
            const std::string place = items[static_cast<std::size_t>(mg_trow_)].first;
            const std::string path = (*items[static_cast<std::size_t>(mg_trow_)].second)["path"].as_string();
            const std::string bundle = (*items[static_cast<std::size_t>(mg_trow_)].second)["bundle"].as_string();
            std::string label = place;
            for (const auto& p : snap_->places) if (p.id == place) label = p.label;
            int count = 0;
            if (const auto it = mg_trash_.find(place); it != mg_trash_.end() && it->second.is_array()) count = static_cast<int>(it->second.size());
            auto del = [this, place](Json b, std::string done) {
                b.set("place", place);
                b.set("confirm", true);
                mg_trash_loading_.insert(place);
                model_.command("POST", "/api/v2/trash/delete", b.dump(), [this, place, done](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? done : e);
                    mg_request_trash(place);
                });
            };
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            acts.push_back({tr("Remettre à sa place"), [this] { input_manage(key::cross); }});
            acts.push_back({tr("Supprimer définitivement…"), [this, del, path, bundle] {
                open_hold_confirm(tr("Supprimer définitivement ?"), trf("« %s » sera effacée pour de bon : elle ne pourra plus être remise.", bundle.c_str()), [del, path] {
                    Json b = Json::object();
                    b.set("path", path);
                    del(b, tr("Copie supprimée pour de bon"));
                });
            }});
            acts.push_back({trf("Vider la corbeille de %s… (%d)", label.c_str(), count), [this, del, label, count] {
                open_hold_confirm(trf("Vider la corbeille de %s ?", label.c_str()), trf("%d copie(s) seront effacées pour de bon : elles ne pourront plus être remises.", count), [del] {
                    Json b = Json::object();
                    b.set("all", true);
                    del(b, tr("Corbeille vidée"));
                });
            }});
            open_actions(tr("Corbeille"), std::move(acts));
        }
        return;
    }
    const auto rows = mg_rows();
    const int n = static_cast<int>(rows.size());
    if (k & key::circle) { mg_zone_ = 0; return; }
    if (k & key::up) { if (mg_row_ == 0) { mg_zone_ = 0; return; } mg_row_ = std::max(0, mg_row_ - 1); }
    if (k & key::down) mg_row_ = std::min(std::max(0, n - 1), mg_row_ + 1);
    if (k & key::square) { mg_view_ = (mg_view_ + 1) % 4; mg_row_ = 0; return; }  // Carré : toujours la vue suivante (supprimer : Options)
    if (k & key::triangle) { mg_compare_all(); return; }
    const Json* g = mg_row_ < n ? &games[rows[static_cast<std::size_t>(mg_row_)]] : nullptr;
    if (k & key::options) { mg_actions(g); return; }
    if (k & key::r2) { mg_clean_duplicates(); return; }
    if (!g) return;
    if (k & key::l2) {
        const std::string gk = gkey(*g);
        if (mg_gsel_.erase(gk)) return;
        if (chosen(*g, mg_csel_, mg_protected_).empty()) mg_select_all(*g);
        if (chosen(*g, mg_csel_, mg_protected_).empty()) { toast(tr("Rien à mettre à la corbeille pour cet élément")); return; }
        mg_gsel_.insert(gk);
        return;
    }
    if (k & key::cross) { mg_screen_ = 1; mg_game_ = gkey(*g); mg_crow_ = 0; mg_back_tab_ = -1; }
}

void Ui::mg_open_ignore(const Json& g) {
    if (!g["title_id"].as_string().empty()) open_ignore({{"title_id", g["title_id"].as_string(), g["title"].as_string()}}, static_cast<int>(g["copies"].size()));
    else if (!g["item"].as_string().empty()) open_ignore({{"key", g["item"].as_string(), g["title"].as_string()}}, static_cast<int>(g["copies"].size()));
    else toast(tr("Jeu sans identifiant : impossible de l'ignorer"));
}

void Ui::open_ignore(std::vector<IgnTarget> targets, int copies) {
    if (targets.empty()) return;
    mg_ign_targets_ = std::move(targets);
    mg_ign_count_ = copies;
    mg_ign_btn_ = 2;  // « Annuler » d'abord
    mg_ign_busy_ = false;
    mg_ign_open_ = true;
}

// Une demande après l'autre (le NAS répond en FTP) ; à la fin, un seul message.
void Ui::ignore_next(std::size_t i, bool trash) {
    if (i >= mg_ign_targets_.size()) {
        std::string msg;
        const int n = static_cast<int>(mg_ign_targets_.size()), done = mg_ign_done_;
        if (n == 1 && done == 1) msg = trash ? trf("« %s » ignoré · %d copie(s) mise(s) à la corbeille", mg_ign_targets_[0].title.c_str(), mg_ign_trashed_.load()) : trf("« %s » ignoré", mg_ign_targets_[0].title.c_str());
        else if (done) msg = trash ? trf("%d élément(s) ignoré(s) · %d copie(s) mise(s) à la corbeille", done, mg_ign_trashed_.load()) : trf("%d élément(s) ignoré(s)", done);
        if (mg_ign_refused_ > 0) msg += (msg.empty() ? "" : " · ") + trf("%d copie(s) gardée(s) (protégée(s) ou refusée(s))", mg_ign_refused_.load());
        std::lock_guard<std::mutex> l(async_mu_);
        if (mg_ign_failed_) msg += (msg.empty() ? "" : " · ") + mg_ign_error_;
        pending_mg_ign_msg_ = std::move(msg);
        has_pending_mg_ign_ = true;
        return;
    }
    const IgnTarget& t = mg_ign_targets_[i];
    Json b = Json::object();
    b.set(t.field, t.value);
    b.set("on", true);
    b.set("trash", trash);
    model_.command("POST", "/api/v2/ignore", b.dump(), [this, i, trash](const std::string& e, const std::string& body) {
        if (!e.empty()) { ++mg_ign_failed_; std::lock_guard<std::mutex> l(async_mu_); mg_ign_error_ = e; }
        else {
            Json j;
            Json::parse(body, j);
            auto count = [](const Json& v) { return static_cast<int>(v.is_array() ? v.size() : v.as_int()); };
            ++mg_ign_done_;
            mg_ign_trashed_ += count(j["trashed"]);
            mg_ign_refused_ += count(j["refused"]);
        }
        ignore_next(i + 1, trash);
    });
}

void Ui::set_ignored(const Item& it, bool on) {
    Json b = Json::object();
    if (!it.title_id.empty()) b.set("title_id", it.title_id); else b.set("key", it.key);
    b.set("on", on);
    model_.command("POST", "/api/v2/ignore", b.dump(), [this, title = it.title](const std::string& e, const std::string&) {
        post_toast(e.empty() ? trf("« %s » n'est plus ignoré", title.c_str()) : e);
    });
    overview_loaded_ = 0;
    mg_sum_dirty_ = true;
}

void Ui::draw_mg_ignore(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int w = 1100, x = (1920 - w) / 2;
    const int n = static_cast<int>(mg_ign_targets_.size());
    const std::string text = n > 1 ? trf("Ces %d éléments ne seront plus sauvegardés ni proposés. Leurs sauvegardes (parties et copies complètes) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS.", n)
                           : mg_ign_count_ >= 0 ? trf("Il ne sera plus sauvegardé ni affiché. Ses %d copie(s) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS.", mg_ign_count_)
                           : std::string(tr("Il ne sera plus sauvegardé ni proposé. Ses sauvegardes (parties et copies complètes) peuvent aller à la corbeille (30 jours) sur tous les disques et le NAS."));
    const auto lines = wrap(text, body, w - 96);
    const int h = 112 + static_cast<int>(lines.size()) * 40 + 30 + 3 * 72 + 30;
    const int y = (1000 - h) / 2;
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    s.text(font_, x + 48, y + 32, (n > 1 ? trf("Ignorer %d éléments ?", n) : trf("Ignorer « %s » ?", n ? mg_ign_targets_[0].title.c_str() : "")), large, color::text, w - 96);
    int yy = y + 112;
    for (const auto& l : lines) { s.text(font_, x + 48, yy, l, body, color::muted); yy += 40; }
    yy += 30;
    const char* labels[3] = {tr("Ignorer et mettre ses copies à la corbeille"), tr("Ignorer seulement"), tr("Annuler")};
    for (int i = 0; i < 3; ++i) {
        const bool on = mg_ign_btn_ == i;
        s.rect(x + 48, yy, w - 96, 60, on ? color::text : i == 0 ? rgb(255, 123, 123, 60) : rgb(255, 255, 255, 22), 30);
        s.text(font_, x + 80, yy + 14, labels[i], body, on ? color::bg : color::text, w - 200);
        if (on && mg_ign_busy_) {
            spinner(s, x + w - 90, yy + 30, 16);
            if (n > 1) { const std::string c = std::to_string(mg_ign_done_ + mg_ign_failed_) + " / " + std::to_string(n); s.text(font_, x + w - 130 - Surface::measure(c, body), yy + 14, c, body, color::bg); }
        }
        yy += 72;
    }
    s.rect(0, 990, 1920, 90, color::bg);
    if (mg_ign_busy_) draw_footer(s, {{-1, tr("Un moment : le NAS peut être long…")}});
    else draw_footer(s, {{0, tr("Choisir")}, {1, tr("Annuler")}});
}

// ---- Onglet Backups : cartes des endroits (Partout, l'interne s'il garde des copies, chaque disque USB et NAS, Corbeille), puis la gestion ----

std::vector<const Place*> Ui::mg_places() const {
    std::vector<const Place*> out;
    // L'interne n'a sa carte que s'il garde des copies (comme sur la page web).
    auto has_copies = [&](const std::string& id) {
        if (mg_sum_.is_object())
            for (const auto& g : mg_sum_["games"].items())
                for (const auto& c : g["copies"].items()) if (!c["on_peer"].as_bool() && c["place"].as_string() == id) return true;
        return false;
    };
    for (const auto& p : snap_->places)
        if (p.type == "usb" || (p.type == "ftp" && p.use != "sync" && p.same_backups_as.empty()) || (p.type == "internal" && has_copies(p.id))) out.push_back(&p);  // NAS « Synchro seulement » : pas ici ; même NAS par deux chemins (FTP et WebDAV) : une seule carte
    // Une carte par autre PS5 qui range des copies (ses disques et son NAS ensemble). Refaite seulement si elle change :
    // les pointeurs rendus restent valables pendant le dessin.
    std::vector<Place> chips;
    // D'abord tous les disques que l'autre PS5 annonce (Interne, USB 0, USB 1…), même vides : on voit toute la console.
    for (const auto& sp : snap_->places) {
        if (sp.type != "peer" || sp.id.rfind("peer:", 0) != 0) continue;
        Place p; p.id = sp.id; p.type = "peerconsole"; p.free = sp.free;
        const auto dot = sp.label.find(" · ");
        std::string pname = dot == std::string::npos ? sp.label : sp.label.substr(0, dot);
        const auto par = pname.find(" (");
        if (par != std::string::npos) pname = pname.substr(0, par);  // « example (12.40) » → « example »
        p.service = pname;
        p.label = dot == std::string::npos ? sp.label : sp.label.substr(dot + 3);
        chips.push_back(p);
    }
    if (mg_sum_.is_object())
        for (const auto& g : mg_sum_["games"].items())
            for (const auto& c : g["copies"].items()) {
                if (!c["on_peer"].as_bool()) continue;
                // Une bulle par disque de l'autre PS5 (« peer:<instance>:disk-… ») ; ses disques forment une famille, comme
                // ceux de cette PS5 (Croix l'ouvre). label : le disque (« USB 0 ») ; service : le nom de la PS5.
                const std::string id = c["place"].as_string(), pname = c["peer_name"].as_string();
                auto it = std::find_if(chips.begin(), chips.end(), [&](const Place& x) { return x.id == id; });
                if (it == chips.end()) {
                    Place p; p.id = id; p.type = "peerconsole"; p.service = pname;
                    std::string disk = c["place_label"].as_string();
                    if (disk.rfind(pname + " · ", 0) == 0) disk = disk.substr(pname.size() + 4);
                    p.label = disk.empty() ? pname : disk;
                    chips.push_back(p); it = chips.end() - 1;
                }
                ++it->total;  // nombre de copies
            }
    bool same = chips.size() == mg_peer_chips_.size();
    for (std::size_t i = 0; same && i < chips.size(); ++i) same = chips[i].id == mg_peer_chips_[i].id && chips[i].total == mg_peer_chips_[i].total;
    if (!same) mg_peer_chips_ = std::move(chips);
    for (const auto& p : mg_peer_chips_) out.push_back(&p);
    return out;
}

bool Ui::mg_in_trash() const { return mg_place_ == static_cast<int>(mg_places().size()) + 1; }

std::string Ui::mg_place_id() const {
    const auto pl = mg_places();
    return mg_place_ >= 1 && mg_place_ <= static_cast<int>(pl.size()) ? pl[static_cast<std::size_t>(mg_place_ - 1)]->id : std::string();
}

std::vector<std::size_t> Ui::mg_rows() const {
    std::vector<std::size_t> rows;
    const Json& games = mg_sum_["games"];
    const std::string place = mg_place_id();
    for (std::size_t i = 0; i < games.size(); ++i) {
        const Json& g = games[i];
        if (mg_view_ == 1 && g["remove"].size() == 0) continue;
        if (mg_view_ == 2 && (g["compared"].as_bool() || g["full"].as_bool())) continue;
        if (mg_view_ == 3) { bool bad = false; for (const auto& c : g["copies"].items()) bad = bad || suspect(c); if (!bad) continue; }
        if (!place.empty()) {
            const bool console = place.rfind("peer:", 0) == 0 && place.back() == ':';  // toute une autre PS5
            bool here = false;
            for (const auto& c : g["copies"].items()) here = here || c["place"].as_string() == place || (console && c["place"].as_string().rfind(place, 0) == 0);
            if (!here) continue;
        }
        rows.push_back(i);
    }
    auto rank = [&](const Json& g) { return g["remove"].size() ? 0 : !g["compared"].as_bool() && !g["full"].as_bool() ? 1 : 2; };
    std::stable_sort(rows.begin(), rows.end(), [&](std::size_t a, std::size_t b) {
        const Json &ga = games[a], &gb = games[b];
        if (rank(ga) != rank(gb)) return rank(ga) < rank(gb);
        if (ga["freed_bytes"].as_int() != gb["freed_bytes"].as_int()) return ga["freed_bytes"].as_int() > gb["freed_bytes"].as_int();
        return ga["title"].as_string() < gb["title"].as_string();
    });
    return rows;
}

void Ui::mg_select_all(const Json& g) {
    const std::string place = mg_place_id();
    std::set<std::string> all;
    for (const auto& c : g["copies"].items())
        if (!c["other_account"].is_string() && !c["on_peer"].as_bool() && !mg_protected_.count(ckey(c)) && (place.empty() || c["place"].as_string() == place)) all.insert(ckey(c));
    mg_csel_[gkey(g)] = std::move(all);
}

// Options dans la liste : sur les éléments cochés, sinon sur celui sous le curseur.
// R2 / Options › « Nettoyer les doublons sûrs… » : les jeux cochés, sinon tous ceux qui ont des doublons proposés
// (copies identiques, la plus récente de chaque contenu et de chaque disque est gardée) → aperçu → corbeille.
void Ui::mg_clean_duplicates() {
    if (mg_gsel_.empty()) {
        const Json& games = mg_sum_["games"];
        for (auto i : mg_rows()) {
            const Json& g = games[i];
            if (g["remove"].size() && !chosen(g, mg_csel_, mg_protected_).empty()) mg_gsel_.insert(gkey(g));
        }
    }
    if (mg_gsel_.empty()) { toast(tr("Aucun doublon sûr pour l'instant.")); return; }
    mg_pv_delete_ = false;
    mg_open_preview(mg_gsel_);
}

void Ui::mg_actions(const Json* focus) {
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    const int ng = static_cast<int>(mg_gsel_.size());
    if (!ng && mg_sum_["to_remove"].as_int() > 0)
        acts.push_back({trf("Nettoyer les doublons sûrs… (%s)", size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, mg_sum_["freed_bytes"].as_int()))).c_str()), [this] { mg_clean_duplicates(); }});
    if (ng) {
        acts.push_back({trf("Mettre la sélection à la corbeille… (%d)", ng), [this] { mg_pv_delete_ = true; mg_open_preview(mg_gsel_); }});
        acts.push_back({trf("Ignorer… (%d)", ng), [this] {
            std::vector<IgnTarget> t;
            for (const auto& g : mg_sum_["games"].items()) {
                if (!mg_gsel_.count(gkey(g))) continue;
                if (!g["title_id"].as_string().empty()) t.push_back({"title_id", g["title_id"].as_string(), g["title"].as_string()});
                else if (!g["item"].as_string().empty()) t.push_back({"key", g["item"].as_string(), g["title"].as_string()});
            }
            open_ignore(std::move(t), -1);
        }});
    } else if (focus) {
        const std::string gk = gkey(*focus), title = (*focus)["title"].as_string();
        if (!(*focus)["on_peer_only"].as_bool())  // rangé seulement sur l'autre PS5 : rien à supprimer d'ici
            acts.push_back({tr("Supprimer…"), [this, gk] {
                for (const auto& g : mg_sum_["games"].items()) if (gkey(g) == gk && chosen(g, mg_csel_, mg_protected_).empty()) mg_select_all(g);
                mg_open_preview({gk});
            }});
        acts.push_back({tr("Ignorer…"), [this, gk] { for (const auto& g : mg_sum_["games"].items()) if (gkey(g) == gk) { mg_open_ignore(g); break; } }});
        if (const std::string tid = (*focus)["title_id"].as_string(); has_console_save(tid))  // les copies restent ; seule la sauvegarde EN PLACE part
            acts.push_back({tr("Supprimer la partie de la console…"), [this, tid, title] { delete_console_save(tid, title); }});
        acts.push_back({tr("Détails"), [this, gk] { mg_screen_ = 1; mg_game_ = gk; mg_crow_ = 0; mg_back_tab_ = -1; }});
    }
    acts.push_back({tr("Tout comparer"), [this] { mg_compare_all(); }});
    acts.push_back({ng ? tr("Tout décocher") : tr("Tout cocher"), [this] {
        if (!mg_gsel_.empty()) { mg_gsel_.clear(); return; }
        const Json& games = mg_sum_["games"];
        for (auto i : mg_rows()) { const Json& g = games[i]; if (!chosen(g, mg_csel_, mg_protected_).empty()) mg_gsel_.insert(gkey(g)); }  // les doublons proposés
        if (mg_gsel_.empty()) toast(tr("Rien à nettoyer pour l'instant"));
    }});
    open_actions(ng ? trf("%d coché(s)", ng) : focus ? (*focus)["title"].as_string() : std::string(tr("Gérer les sauvegardes")), std::move(acts));
}

void Ui::mg_protect(const Json& c) {
    const std::string ck = ckey(c);
    const bool on = !mg_protected_.count(ck);
    Json b = Json::object();
    b.set("place", c["place"]);
    b.set("bundle", c["bundle"]);
    b.set("on", on);
    if (on) mg_protected_.insert(ck); else mg_protected_.erase(ck);  // affiché tout de suite, corrigé si refusé
    model_.command("POST", "/api/v2/backups/protect", b.dump(), [this, ck, on](const std::string& e, const std::string&) {
        std::lock_guard<std::mutex> l(async_mu_);
        if (!e.empty()) { pending_toast_ = e; if (on) pending_mg_unprotected_.insert(ck); else pending_mg_protected_.insert(ck); }
    });
    toast(on ? tr("Protégée : la rotation et la suppression la laisseront") : tr("Protection retirée"));
}

// Restaurer une version : sauvegarde → vérification puis confirmation (flux existant) ; backup complet → dossier neuf.
void Ui::mg_restore(const Json& g, const Json& c) {
    if (!c["complete"].as_bool(true)) { toast(tr("Sauvegarde incomplète")); return; }
    if (g["full"].as_bool()) {
        std::string mount;
        for (const auto& p : snap_->places) if (p.id == c["place"].as_string()) mount = p.mount;
        std::string target = mount + "/PBC Restaurations/" + c["bundle"].as_string();
        if (target.size() > 5 && target.substr(target.size() - 5) == ".pbck") target.resize(target.size() - 5);
        model_.command("POST", "/api/v2/tasks", "{\"type\":\"restore\",\"place\":" + json_str(c["place"].as_string()) + ",\"bundle\":" + json_str(c["bundle"].as_string()) +
                       ",\"target\":" + json_str(target) + "}", [this](const std::string& err, const std::string&) { post_toast(err.empty() ? std::string(tr("Restauration ajoutée aux tâches")) : err); });
        return;
    }
    // Compte de la console qui reçoit la sauvegarde : celui du jeu s'il est sur la console, sinon le premier compte PS5 d'ici.
    const auto& accs = overview_["accounts"];
    int target = -1;
    for (std::size_t i = 0; i < accs.size(); ++i) if (accs[i]["key"].as_string() == g["account"].as_string() && accs[i]["on_console"].as_bool()) target = static_cast<int>(i);
    for (std::size_t i = 0; i < accs.size() && target < 0; ++i) if (accs[i]["on_console"].as_bool() && accs[i]["platform"].as_string() == "PS5") target = static_cast<int>(i);
    if (target < 0) { toast(tr("Aucun compte PS5 sur cette console")); return; }
    // Plusieurs comptes PS5 sur la console : on demande lequel reçoit la sauvegarde (la vérification dira s'il faut la
    // re-signer pour ce compte).
    std::vector<int> ps5;
    for (std::size_t i = 0; i < accs.size(); ++i) if (accs[i]["on_console"].as_bool() && accs[i]["platform"].as_string() == "PS5") ps5.push_back(static_cast<int>(i));
    if (ps5.size() > 1 && !mg_rs_target_chosen_) {
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        const Json gg = g, cc = c;
        for (int i : ps5)
            acts.push_back({accs[static_cast<std::size_t>(i)]["name"].as_string(), [this, gg, cc, i] { mg_rs_target_chosen_ = true; mg_rs_target_ = i; mg_restore(gg, cc); mg_rs_target_chosen_ = false; }});
        open_actions(tr("Restaurer sur quel compte ?"), std::move(acts));
        return;
    }
    if (mg_rs_target_chosen_) target = mg_rs_target_;
    Json e = Json::object();
    e.set("title_id", g["title_id"]);
    e.set("title", g["title"]);
    rs_entry_ = e;
    rs_title_ = g["title"].as_string();
    Json v = Json::object();
    v.set("place", c["place"]);
    v.set("bundle", c["bundle"]);
    const int keep = saves_acc_;
    saves_acc_ = target;
    rs_start(v);
    saves_acc_ = keep;
}

// Options sur une version : restaurer, cocher, protéger, supprimer, ignorer l'élément, tout cocher.
void Ui::mg_version_actions(const Json& g, const Json& c) {
    load_link_peers();  // consoles jumelées à jour pour la prochaine ouverture
    const std::string gk = gkey(g), ck = ckey(c);
    const bool other = c["other_account"].is_string(), prot = mg_protected_.count(ck) != 0;
    const auto ch = chosen(g, mg_csel_, mg_protected_);
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    if (c["on_peer"].as_bool()) {  // copie rangée sur l'autre PS5 : la récupérer ici ; rien ne s'efface d'ici
        const std::string peer = c["peer"].as_string(), pname = c["peer_name"].as_string(), place = c["place"].as_string(), bundle = c["bundle"].as_string();
        const std::string tid = g["title_id"].as_string(), title = g["title"].as_string();
        if (!g["full"].as_bool() && c["complete"].as_bool(true))
            acts.push_back({tr("Récupérer ici (restaurer sur un compte de cette console)…"), [this, peer, pname, place, bundle, tid, title] {
                pick_account("", tr("cette console"), [this, peer, pname, place, bundle, tid, title](const std::string& uid, const std::string& who) {
                    Json b = Json::object();
                    b.set("peer", peer); b.set("place", place); b.set("bundle", bundle); b.set("title_id", tid); b.set("user_id", uid);
                    model_.command("POST", "/api/v2/link/pull-save", b.dump(), [this, title, pname, who](const std::string& e, const std::string&) {
                        post_toast(e.empty() ? trf("« %s » arrive de %s : restaurée sur %s", title.c_str(), pname.c_str(), who.c_str()) : e);
                    });
                });
            }});
        acts.push_back({tr("Fermer"), [] {}});
        open_actions(c["place_label"].as_string() + "  ·  " + date_text(c["created_unix"].as_int()), std::move(acts));
        popup_note_ = place.find(":remote:") != std::string::npos
            ? trf("Cette copie est sur un NAS que cette console n'a pas : ajoutez-le dans Appareils pour la gérer d'ici, ou faites-le depuis %s.", pname.c_str())
            : trf("Cette copie est rangée sur %s : pour la supprimer ou la protéger, faites-le depuis cette console.", pname.c_str());
        return;
    }
    // Envoyer cette copie à une autre PS5 jumelée (liaison) : restaurée là-bas, re-signée pour son compte.
    if (!g["full"].as_bool() && c["complete"].as_bool(true))
        for (const auto& p : link_peers_.items()) {
            if (!p["paired"].as_bool()) continue;
            const std::string inst = p["instance"].as_string(), name = p["name"].as_string().empty() ? p["host"].as_string() : p["name"].as_string();
            const std::string place = c["place"].as_string(), bundle = c["bundle"].as_string(), tid = g["title_id"].as_string();
            acts.push_back({trf("Envoyer à %s…", name.c_str()), [this, inst, name, place, bundle, tid] {
                Json b = Json::object();
                b.set("instance", inst); b.set("place", place); b.set("bundle", bundle); b.set("title_id", tid);
                model_.command("POST", "/api/v2/link/send-save", b.dump(), [this, name](const std::string& e, const std::string&) {
                    post_toast(e.empty() ? trf("Envoi vers %s en cours : la sauvegarde y sera restaurée", name.c_str()) : e);
                });
            }});
        }
    // Installer le jeu sur cette console depuis la copie : image sur le disque choisi, ShadowMountPlus, sauvegarde re-signée.
    if (g["full"].as_bool() && c["complete"].as_bool(true)) {
        bool has_app = false;
        for (const auto& p : c["parts"].items()) if (p.as_string() == "app") has_app = true;
        if (has_app)
            for (const auto& p : snap_->places) {
                if (p.type != "internal" && p.type != "usb") continue;
                const std::string place = c["place"].as_string(), bundle = c["bundle"].as_string(), target = p.id, title = g["title"].as_string();
                const std::string disk = p.type == "internal" ? std::string(tr("Interne")) : p.label;
                acts.push_back({trf("Installer sur %s · %s libres", disk.c_str(), size_text(p.free).c_str()), [this, place, bundle, target, title, disk] {
                    Json ck = Json::object();
                    ck.set("place", place);
                    ck.set("bundle", bundle);
                    system_check_then("/api/v2/backups/install-check", ck, title, [this, place, bundle, target, title, disk] { choose_format(trf("Format de « %s » sur %s", title.c_str(), disk.c_str()), [this, place, bundle, target, title](const std::string& fmt) {
                        pick_account("", tr("cette console"), [this, place, bundle, target, title, fmt](const std::string& uid, const std::string&) {
                            Json b = Json::object();
                            b.set("place", place); b.set("bundle", bundle); b.set("target", target); b.set("format", fmt); b.set("user_id", uid);
                            model_.command("POST", "/api/v2/backups/install", b.dump(), [this, title](const std::string& e, const std::string&) {
                                post_toast(e.empty() ? trf("Installation de « %s » en cours (suivie dans Tâches) ; le jeu apparaît sur l'accueil une fois l'application fermée", title.c_str()) : e);
                            });
                        });
                    }); });
                }});
            }
    }
    if (c["complete"].as_bool(true)) acts.push_back({tr("Partager cette copie…"), [this, gk, ck] {
        for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) for (const auto& y : x["copies"].items()) if (ckey(y) == ck) { open_share_copy(x, y); return; }
    }});
    // Copie d'un autre identifiant (ancien compte, autre console) : la restauration la re-signe pour ce compte.
    acts.push_back({c["needs_resign"].as_bool() ? tr("Reprendre sur mon compte") : tr("Restaurer"), [this, gk, ck] {
        for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) for (const auto& y : x["copies"].items()) if (ckey(y) == ck) { mg_restore(x, y); return; }
    }});
    if (!ch.empty()) acts.push_back({trf("Supprimer… (%d)", static_cast<int>(ch.size())), [this, gk] { mg_pv_delete_ = true; mg_open_preview({gk}); }});
    else if (!other && !prot && !ro_place(c["place"].as_string())) acts.push_back({tr("Supprimer…"), [this, gk, ck] { mg_csel_[gk] = {ck}; mg_gsel_.insert(gk); mg_pv_delete_ = true; mg_open_preview({gk}); }});
    if (!ro_place(c["place"].as_string()))  // partage reçu : lecture seule
        acts.push_back({prot ? tr("Retirer la protection") : tr("Protéger"), [this, gk, ck] {
            for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) for (const auto& y : x["copies"].items()) if (ckey(y) == ck) { mg_protect(y); return; }
        }});
    acts.push_back({g["full"].as_bool() ? tr("Ne plus sauvegarder…") : tr("Ignorer…"), [this, gk] { for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) { mg_open_ignore(x); break; } }});
    acts.push_back({ch.empty() ? tr("Tout cocher") : tr("Tout décocher"), [this, gk] {
        for (const auto& x : mg_sum_["games"].items()) {
            if (gkey(x) != gk) continue;
            std::set<std::string> set;
            if (chosen(x, mg_csel_, mg_protected_).empty())
                for (const auto& y : x["copies"].items()) if (!y["other_account"].is_string() && !mg_protected_.count(ckey(y)) && !ro_place(y["place"].as_string())) set.insert(ckey(y));
            if (set.empty()) mg_gsel_.erase(gk); else mg_gsel_.insert(gk);
            mg_csel_[gk] = std::move(set);
        }
    }});
    open_actions(c["place_label"].as_string() + "  ·  " + date_text(c["created_unix"].as_int()), std::move(acts));
}

// Toutes les versions d'un élément, depuis la fiche, Parties ou Backups.
void Ui::open_versions(const std::string& account, const std::string& title_id, const std::string& title, const std::string& item) {
    std::string key;
    const std::string acc = account.empty() && !title_id.empty() ? mg_account() : account;  // depuis la fiche : le compte PS5 de la console
    if (!acc.empty()) key = acc + "|" + (title_id.empty() ? title : title_id);
    if (mg_sum_.is_object()) {
        bool found = false;
        for (const auto& g : mg_sum_["games"].items()) if (gkey(g) == key) found = true;
        for (const auto& g : mg_sum_["games"].items()) {  // sinon : la même sauvegarde sous un autre compte, ou un backup complet
            if (found) break;
            const bool same = (!title_id.empty() && g["title_id"].as_string() == title_id) || (!item.empty() && g["item"].as_string() == item);
            if (same) { key = gkey(g); found = true; }
        }
        if (!found) { toast(tr("Aucune copie de cet élément pour l'instant")); return; }
    } else if (key.empty()) key = "full|" + (item.empty() ? title : item);
    mg_back_tab_ = tab_;
    tab_ = backups;
    sheet_ = false;
    mg_screen_ = 1;
    mg_game_ = key;
    mg_crow_ = 0;
    mg_zone_ = 1;
}

std::string Ui::mg_fam(int old) const {
    const auto pl = mg_places();
    const int n = static_cast<int>(pl.size()) + 2;
    if (old <= 0) return "all";
    if (old >= n - 1) return "trash";
    const Place& p = *pl[static_cast<std::size_t>(old - 1)];
    if (p.type == "internal" || p.type == "usb") return "ps5";
    if (p.type == "peerconsole") return "peer:" + p.id.substr(5, p.id.find(':', 5) - 5);  // la famille : l'autre PS5 (tous ses disques)
    const bool cloud = cloud_proto(p.protocol);
    return cloud ? "cloud" : "nas";
}

std::string Ui::peer_name(const std::string& fam) const {
    for (const Place* p : mg_places()) if (p->type == "peerconsole" && "peer:" + p->id.substr(5, p->id.find(':', 5) - 5) == fam) return p->service;
    return tr("Autre PS5");
}

std::vector<Ui::MgChip> Ui::mg_chips() const {
    const int n = static_cast<int>(mg_places().size()) + 2;
    std::map<std::string, int> size;
    for (int i = 0; i < n; ++i) ++size[mg_fam(i)];
    std::vector<MgChip> out;
    std::set<std::string> shown;
    for (int i = 0; i < n; ++i) {
        const std::string f = mg_fam(i);
        if (size[f] > 1 && f != mg_open_) { if (shown.insert(f).second) out.push_back({i, f, true, size[f]}); continue; }
        out.push_back({i, f, false, size[f]});
    }
    out.push_back({-1, "compare", false, 0});  // Comparer et trier
    // Ordre : Partout, Cette PS5, les autres PS5, le NAS, le nuage, Comparer et trier ; la Corbeille reste la dernière (à droite).
    auto rank = [](const std::string& f) { return f == "all" ? 0 : f == "ps5" ? 1 : f.rfind("peer:", 0) == 0 ? 2 : f == "nas" ? 3 : f == "cloud" ? 4 : f == "compare" ? 8 : f == "trash" ? 9 : 5; };
    std::stable_sort(out.begin(), out.end(), [&](const MgChip& a, const MgChip& b) { return rank(a.fam) < rank(b.fam); });
    return out;
}

void Ui::draw_backups_tab(Surface& s) {
    if (!overview_loaded_ || now_ - overview_loaded_ > 15000) load_overview();
    const int x = margin, w = 1920 - 2 * margin;
    if (mg_screen_ == 1) { s.text(font_, margin, 112, tr("Détails"), title, color::text); draw_manage(s, x, w); return; }
    {
        draw_page_head(s, tr("Sauvegardes"), tr("Vos copies : sur la console, le NAS, le nuage et l'autre PS5."));
    }
    // Cartes des endroits : Partout, chaque disque USB et NAS, Corbeille.
    const auto pl = mg_places();
    const int n = static_cast<int>(pl.size()) + 2;
    mg_place_ = std::clamp(mg_place_, 0, n - 1);
    // Bulles groupées : une par famille ; la Corbeille reste toujours visible, à droite ; les autres défilent à sa gauche.
    const auto chips = mg_chips();
    const int nc = static_cast<int>(chips.size());
    mg_chip_ = std::clamp(mg_chip_, 0, std::max(0, nc - 1));
    if (mg_zone_ != 0) for (int c = 0; c < nc; ++c) {
        const MgChip& cc = chips[static_cast<std::size_t>(c)];
        if (mg_compare_ ? cc.fam == "compare" : (!cc.group && cc.fam != "compare" && cc.old == mg_place_)) mg_chip_ = c;
    }
    const int gap = 18, cw = (w - 4 * gap) / 5, ch = 112, cy = 196;  // cinq bulles de front : endroits, puis Comparer et Corbeille
    // Comparer et trier et la Corbeille : toujours visibles à droite (les endroits défilent à gauche).
    const bool has_cmp = nc >= 2 && chips[static_cast<std::size_t>(nc - 2)].fam == "compare";
    // Comparer et trier et la Corbeille : deux bulles entières, côte à côte, toujours visibles à droite (propriétaire, 16/11).
    const int tcw = cw;  // les deux outils : même largeur (le texte tient, pas de trou dans la rangée)
    const int tools = has_cmp ? 2 : 1, tools_w = tools * tcw + (tools - 1) * gap;
    const int places = nc - tools, slots = std::max(1, (w - tools_w) / (cw + gap));  // bulles qui tiennent à gauche
    // Trop de bulles : la dernière place montre ce qui est caché (« + 3 de plus · NAS · Nuage… »), pour inviter à aller voir.
    const bool overflow = places > slots;
    const int per = overflow ? std::max(1, slots - 1) : slots;
    const int sel_place = std::min(mg_chip_, places - 1);
    const int first = std::clamp(sel_place - per + 1, 0, std::max(0, places - per));
    std::vector<int> shown;
    for (int c = first; c < places && c < first + per; ++c) shown.push_back(c);
    if (has_cmp) shown.push_back(nc - 2);  // Comparer et trier (en haut à droite)
    shown.push_back(nc - 1);  // Corbeille (en bas à droite, ou pleine hauteur sans Comparer)
    auto chip_name = [&](const MgChip& c) -> std::string {
        if (c.group) return c.fam == "ps5" ? std::string(tr("Cette PS5")) : c.fam == "cloud" ? std::string(tr("Nuage")) : c.fam.rfind("peer:", 0) == 0 ? peer_name(c.fam) : std::string("NAS");
        if (c.fam == "compare") return tr("Comparer");
        if (c.old <= 0) return tr("Partout");
        if (c.old >= n - 1) return tr("Corbeille");
        const Place& p = *pl[static_cast<std::size_t>(c.old - 1)];
        return p.type == "internal" ? std::string(tr("Interne")) : p.type == "peerconsole" ? p.service : p.label;
    };
    auto chip_dev = [&](const MgChip& c) -> int {
        if (c.group) return c.fam == "ps5" || c.fam.rfind("peer:", 0) == 0 ? dev_internal : c.fam == "cloud" ? dev_cloud : dev_nas;
        if (c.fam == "compare" || c.old <= 0 || c.old >= n - 1) return -1;
        const Place& p = *pl[static_cast<std::size_t>(c.old - 1)];
        return p.type == "ftp" ? (cloud_proto(p.protocol) ? cloud_dev(p.protocol, p.service) : dev_nas) : p.type == "usb" ? dev_usb : dev_internal;
    };
    if (overflow) {  // carte d'aperçu : combien, lesquels (noms et petites images)
        // Ce qui reste à droite d'abord ; arrivé au bout, ce qui est passé à gauche (« ‹ 2 avant »).
        std::vector<int> hidden;
        for (int c = first + per; c < places; ++c) hidden.push_back(c);
        const bool ahead = !hidden.empty();
        if (!ahead) for (int c = 0; c < first; ++c) hidden.push_back(c);
        const int px = x + per * (cw + gap);
        const int pw = std::min(cw, x + w - tools_w - gap - px);
        std::string names;
        DevCard more;
        for (int c : hidden) {  // mots courts : « NAS », « Nuage », le nom d'un disque ou d'une PS5
            const MgChip& hc = chips[static_cast<std::size_t>(c)];
            std::string nm = chip_name(hc);
            if (!hc.group && hc.fam == "nas") nm = "NAS";
            else if (!hc.group && hc.fam == "cloud") nm = tr("Nuage");
            if (names.find(nm) == std::string::npos) names += (names.empty() ? "" : " · ") + nm;
            more.minis.push_back(chip_dev(hc));
        }
        // La carte commune, légère : combien, lesquels (noms et petites images).
        more.light = true;
        more.name = ahead ? trf("+ %d de plus ›", static_cast<int>(hidden.size())) : trf("‹ %d avant", static_cast<int>(hidden.size()));
        more.line = names;
        device_card(s, more, px, cy, pw, ch, false);
    }
    for (std::size_t si = 0; si < shown.size(); ++si) {
        const MgChip& chip = chips[static_cast<std::size_t>(shown[si])];
        const int i = chip.old;
        const bool tool = shown[si] >= places;  // Comparer ou Corbeille : à droite
        const bool half = false;  // plus de demi-cartes empilées
        const int bw = tool ? tcw : cw;
        const int cx = !tool ? x + static_cast<int>(si) * (cw + gap) : shown[si] == nc - 1 ? x + w - tcw : x + w - tools_w;
        const int hh = (ch - 8) / 2;
        const int ccy = !half ? cy : shown[si] == nc - 2 ? cy : cy + hh + 8, cch = half ? hh : ch;
        bool sel = !chip.group && chip.fam != "compare" && i == mg_place_ && !mg_compare_;
        if (chip.fam == "compare") sel = mg_compare_;
        if (chip.group) for (int j = 0; j < n; ++j) if (j == mg_place_ && mg_fam(j) == chip.fam) sel = true;  // l'endroit choisi est dans cette famille
        const bool focus = shown[si] == mg_chip_ && mg_zone_ == 0;
        std::string name, sub, sub2;
        std::uint32_t sub_col = color::text;
        int dev = -1;
        // Chaque bulle dit la même chose : combien de copies elle contient (celles de ce compte), puis une précision.
        auto copies_at = [&](const Place& p) -> int {
            if (p.type == "peerconsole") return static_cast<int>(p.total);
            int k2 = 0;
            for (const auto& g : mg_sum_["games"].items())
                for (const auto& c : g["copies"].items()) { const std::string cp = c["place"].as_string(); if (cp == p.id || cp == "remote:" + p.id || "remote:" + cp == p.id) ++k2; }
            return k2;
        };
        if (chip.group) {  // bulle fermée d'une famille : Croix l'ouvre
            name = chip.fam == "ps5" ? tr("Cette PS5") : chip.fam == "cloud" ? tr("Nuage") : chip.fam.rfind("peer:", 0) == 0 ? peer_name(chip.fam) : std::string("NAS");
            int copies = 0, where = 0;
            for (int j = 1; j < n - 1; ++j) if (mg_fam(j) == chip.fam) { copies += copies_at(*pl[static_cast<std::size_t>(j - 1)]); ++where; }
            sub = trf("%d copie(s)", copies); sub2 = trf("%d endroits ›", where);
            dev = chip.fam == "ps5" || chip.fam.rfind("peer:", 0) == 0 ? dev_internal : chip.fam == "cloud" ? dev_cloud : dev_nas;
        }
        else if (chip.fam == "compare") {  // l'outil dit où on en est : combien de jeux restent à comparer
            const int left = static_cast<int>(mg_sum_["to_compare"].as_int());
            name = tr("Comparer · trier");
            sub = left ? trf("%d à comparer", left) : std::string(tr("Tout est comparé"));
            sub_col = left ? color::warn : color::ok;
        }
        else if (i == 0) {
            int all = 0;
            for (const auto& g : mg_sum_["games"].items()) for (const auto& c : g["copies"].items()) if (!c["other_account"].is_string()) ++all;
            name = tr("Partout"); sub = trf("%d copie(s)", all); sub2 = trf("%d jeu(x)", static_cast<int>(mg_sum_["games"].size())); dev = dev_consoles;
        }
        else if (i == n - 1) { name = tr("Corbeille"); sub = tr("gardée 30 jours"); sub_col = color::muted; }
        else {
            const Place& p = *pl[static_cast<std::size_t>(i - 1)];
            name = p.type == "internal" ? std::string(tr("Interne")) : p.label;
            dev = p.type == "ftp" ? (cloud_proto(p.protocol) ? cloud_dev(p.protocol, p.service) : dev_nas) : p.type == "peerconsole" || p.type == "internal" ? dev_internal : dev_usb;  // PS5 : image de la PS5
            const int k2 = copies_at(p);
            sub = trf("%d copie(s)", k2);
            if (p.type == "peerconsole") {  // famille ouverte : le disque en nom, la PS5 dessous ; sinon l'inverse
                const bool opened = mg_open_ == mg_fam(i);
                name = opened ? p.label : p.service;
            }
            sub2 = p.type == "ftp" ? std::string(cloud_proto(p.protocol) ? tr("nuage") : tr("réseau")) : p.type == "peerconsole" ? (mg_open_ == mg_fam(i) ? p.service : p.label)
                                   : trf("%s libres", size_text(p.free).c_str());
        }
        // La carte commune (même dessin que l'onglet Appareils) : cadre, image, étiquette ; le nom et la ligne sont écrits
        // ici (un nom long passe sur deux lignes plutôt que « … »).
        DevCard card;
        card.dev = half ? -1 : dev;
        card.reserve = !half && (chip.fam == "compare" || i == n - 1);
        card.img = 66;  // six bulles de front : une image un peu plus petite laisse le nom entier
        int tx = device_card(s, card, cx, ccy, bw, cch, focus, sel);
        if (half) {  // demi-carte : le nom seul, jamais coupé (plus petit, sinon le nom court « Comparer »)
            std::string nm = name;
            FontSize f = body;
            if (Surface::measure(nm, body) > cw - 48) f = small;
            if (Surface::measure(nm, small) > cw - 48 && chip.fam == "compare") { nm = tr("Comparer"); f = Surface::measure(nm, body) <= cw - 48 ? body : small; }
            s.text(font_, cx + 24, ccy + cch / 2 - (f == body ? 21 : 16), nm, f, color::text, cw - 48);
            continue;
        }
        if (chip.fam == "compare") {  // deux copies (feuilles lignées) l'une sur l'autre, et une pastille verte « c'est trié »
            const int ix = cx + 14, iy = ccy + 22;
            s.rect(ix, iy, 40, 52, rgb(255, 255, 255, 40), 8);
            for (int l = 0; l < 3; ++l) s.rect(ix + 8, iy + 12 + l * 11, 24, 4, rgb(255, 255, 255, 70), 2);
            s.rect(ix + 22, iy + 12, 40, 52, color::accent, 8);
            for (int l = 0; l < 3; ++l) s.rect(ix + 30, iy + 24 + l * 11, 24, 4, rgb(255, 255, 255, 170), 2);
            s.rect(ix + 46, iy + 46, 24, 24, color::ok, 12);
            s.rect(ix + 52, iy + 58, 5, 5, color::text, 2); s.rect(ix + 56, iy + 61, 4, 4, color::text, 2); s.rect(ix + 59, iy + 55, 4, 8, color::text, 2);
            tx = cx + 96;
        }
        else if (i == n - 1) {  // corbeille : couvercle avec sa poignée, cuve arrondie et ses côtes
            const int ix = cx + 18, iy = ccy + 22;
            const std::uint32_t tc = rgb(255, 140, 140);
            s.rect(ix + 18, iy, 20, 8, tc, 4);
            s.rect(ix, iy + 8, 56, 9, tc, 4);
            s.rect(ix + 5, iy + 20, 46, 50, rgb(255, 140, 140, 70), 10);
            s.outline(ix + 5, iy + 20, 46, 50, tc, 10, 3);
            for (int k2 = 0; k2 < 3; ++k2) s.rect(ix + 16 + k2 * 11, iy + 30, 4, 32, tc, 2);
            tx = cx + 96;
        }
        const int room = cx + bw - tx - 14 - (card.tag.empty() ? 0 : Surface::measure(card.tag, small) + 18);  // place de l'étiquette « 2 › »
        // Trois lignes : le nom, combien de copies (la même mesure sur chaque bulle), puis la précision (place libre, réseau…).
        // Nom trop long même en petit (« example (12.40) ») : il passe sur deux lignes, la précision s'efface.
        if (!sub2.empty() && Surface::measure(name, small) > room && chip.fam != "compare") sub2.clear();
        const bool two = !sub2.empty();
        const int ny = two ? ccy + 6 : ccy + 24;
        if (Surface::measure(name, body) <= room - 30) s.text(font_, tx, ny, name, body, color::text, room);
        else if (Surface::measure(name, small) <= room) s.text(font_, tx, ny + 4, name, small, color::text, room);  // nom long : plus petit
        else if (!two) {  // encore trop long (« Comparer et trier ») : sur deux lignes, jamais « … »
            auto lines = wrap(name, small, room);
            if (!lines.empty() && Surface::measure(lines[0], small) > room) {  // un seul mot trop long (« example ») : coupé après un tiret
                const std::string w0 = lines[0];
                std::size_t cut = 0;
                for (std::size_t q = 0; q < w0.size(); ++q) if (w0[q] == '-' && Surface::measure(w0.substr(0, q + 1), small) <= room) cut = q + 1;
                if (cut) { std::string rest = w0.substr(cut); for (std::size_t q = 1; q < lines.size(); ++q) rest += " " + lines[q]; lines = {w0.substr(0, cut), rest}; }
            }
            s.text(font_, tx, ccy + 8, lines[0], small, color::text, room);
            if (lines.size() > 1) s.text(font_, tx, ccy + 38, lines[1], small, color::text, room);
        } else s.text(font_, tx, ny + 4, name, small, color::text, room);
        s.text(font_, tx, two ? ccy + 46 : ccy + 72, sub, small, sub_col, cx + bw - tx - 14);
        if (two) s.text(font_, tx, ccy + 76, sub2, small, chip.group ? color::accent : color::muted, cx + bw - tx - 14);
    }
    // (d'autres endroits : la carte d'aperçu ci-dessus les montre)
    if (mg_in_trash()) {
        draw_mg_trash(s, x, w, 334);
        return;
    }
    draw_manage(s, x, w);
}


namespace {
const char* const cmp_filters[8] = {N_("Tout"), N_("Sauvegardes"), N_("Jeux et images"), N_("Doublons"), N_("Pas encore comparé"), N_("Différents"), N_("À vérifier"), N_("Plusieurs endroits")};
std::string day_month(std::int64_t t) {
    if (t <= 0) return "";
    char b[16];
    const std::time_t tt = static_cast<std::time_t>(t);
    std::strftime(b, sizeof b, "%d/%m", std::localtime(&tt));
    return b;
}
int distinct_groups(const Json& g) {
    std::set<std::string> gs;
    if (g["console"]["group"].is_string()) gs.insert(g["console"]["group"].as_string());
    for (const auto& c : g["copies"].items()) if (c["group"].is_string() && !c["group"].as_string().empty()) gs.insert(c["group"].as_string());
    return static_cast<int>(gs.size());
}
int place_count(const Json& g) {
    std::set<std::string> ps;
    if (g["console"].is_object()) ps.insert("console");
    for (const auto& c : g["copies"].items()) ps.insert(c["on_peer"].as_bool() ? "peer:" + c["peer_name"].as_string() : c["place"].as_string());
    return static_cast<int>(ps.size());
}
}  // namespace

std::vector<std::size_t> Ui::mg_cmp_rows() const {
    std::vector<std::size_t> rows;
    const Json& games = mg_sum_["games"];
    for (std::size_t i = 0; i < games.size(); ++i) {
        const Json& g = games[i];
        const bool full = g["full"].as_bool();
        bool ok = true;
        switch (mg_cfilter_) {
            case 1: ok = !full; break;
            case 2: ok = full; break;
            case 3: ok = g["remove"].size() > 0; break;
            case 4: ok = !full && !g["compared"].as_bool(); break;
            case 5: ok = !full && g["compared"].as_bool() && distinct_groups(g) > 1; break;
            case 6: ok = false; for (const auto& c : g["copies"].items()) ok = ok || suspect(c); break;  // copie vide ou incomplète
            case 7: {  // même partie à plusieurs endroits, avec au moins une copie d'ici qu'on pourrait retirer
                ok = false;
                if (full) break;
                const auto by = spread_of(g);
                for (const auto& c : g["copies"].items()) ok = ok || (spread_copy(c, by) && !mg_protected_.count(ckey(c)));
                break;
            }
            default: break;
        }
        if (ok && (place_count(g) > 1 || mg_cfilter_ != 0)) rows.push_back(i);  // « Tout » : les jeux qui ont des copies à plusieurs endroits
    }
    return rows;
}

void Ui::draw_mg_compare(Surface& s, int x, int w) {
    const Json& games = mg_sum_["games"];
    // Chiffres en tête : à plusieurs endroits, doublons, plus récentes ailleurs, place récupérable.
    int multi = 0, dups = 0, newer = 0;
    for (const auto& g : games.items()) {
        multi += place_count(g) > 1;
        dups += static_cast<int>(g["remove"].size());
        newer += g["status"].as_string() == "backup_newer";
    }
    const std::string v1 = std::to_string(multi), v2 = std::to_string(dups), v3 = std::to_string(newer),
                      v4 = size_text(static_cast<std::uint64_t>(std::max<std::int64_t>(0, mg_sum_["freed_bytes"].as_int())));
    const std::pair<const std::string*, const char*> tiles[4] = {{&v1, "jeux avec des copies à plusieurs endroits"}, {&v2, "copies identiques (doublons)"},
                                                                 {&v3, "copie(s) plus récente(s) ailleurs"}, {&v4, "récupérables en gardant une copie par contenu"}};
    const int tw = (w - 3 * 14) / 4;
    for (int i = 0; i < 4; ++i) {
        const int tx = x + i * (tw + 14);
        s.rect(tx, 326, tw, 84, color::panel, 16);
        s.text(font_, tx + 18, 332, *tiles[i].first, large, i == 2 && newer ? color::warn : color::text, tw - 36);
        s.text(font_, tx + 18, 374, tr(tiles[i].second), small, color::muted, tw - 36);
    }
    if (mg_sum_inflight_ || mg_comparing_) spinner(s, x + w - 20, 304, 10);
    // Filtres (Carré).
    int px = x;
    for (int i = 0; i < 8; ++i) {
        const int pw = Surface::measure(tr(cmp_filters[i]), small) + 32;
        if (i == mg_cfilter_) s.rect(px, 424, pw, 38, color::text, 19);
        else s.outline(px, 424, pw, 38, color::line, 19, 2);
        s.text(font_, px + 16, 430, tr(cmp_filters[i]), small, i == mg_cfilter_ ? color::bg : color::muted);
        px += pw + 8;
    }
    const auto rows = mg_cmp_rows();
    const int n = static_cast<int>(rows.size());
    mg_cmp_row_ = std::clamp(mg_cmp_row_, 0, std::max(0, n - 1));
    if (n > 3) { const std::string pos = std::to_string(mg_cmp_row_ + 1) + " / " + std::to_string(n); s.text(font_, x + w - Surface::measure(pos, small), 432, pos, small, color::muted); }
    const int ch = 152, per = 3, first = std::max(0, mg_cmp_row_ - per + 1);
    int yy = 478;
    const bool focus_list = mg_zone_ == 1;
    for (int r = first; r < n && r < first + per; ++r) {
        const Json& g = games[rows[static_cast<std::size_t>(r)]];
        const bool focus = focus_list && r == mg_cmp_row_;
        s.rect(x, yy, w, ch, focus ? color::panel2 : color::panel, 18);
        if (focus) s.outline(x - 4, yy - 4, w + 8, ch + 8, color::accent, 22, 3);
        {
            Item stand_in;
            const Item* found = find_or_stand_in(g["item"].as_string(), g["title"].as_string(), g["title_id"].as_string(), stand_in, g["kind"].as_string());
            cover(s, *found, x + 16, yy + 14, 64, false);
        }
        // Verdict, d'après le contenu (jamais d'après la date de la copie).
        const bool full = g["full"].as_bool();
        const int groups = distinct_groups(g);
        std::string verdict;
        std::uint32_t tone, fill;
        if (full) { verdict = trf("%d copie(s) complète(s)", static_cast<int>(g["copies"].size())); tone = color::text; fill = rgb(255, 255, 255, 20); }
        else if (!g["compared"].as_bool()) { verdict = tr("À comparer"); tone = color::warn; fill = rgb(240, 184, 103, 36); }
        else if (g["status"].as_string() == "backup_newer") { verdict = tr("Une copie plus récente"); tone = color::warn; fill = rgb(240, 184, 103, 36); }
        else if (groups <= 1) { verdict = tr("Même contenu partout"); tone = color::ok; fill = rgb(95, 211, 154, 36); }
        else if (g["remove"].size()) { verdict = trf("%d doublon(s)", static_cast<int>(g["remove"].size())); tone = color::ok; fill = rgb(95, 211, 154, 36); }
        else { verdict = tr("Différentes"); tone = color::bad; fill = rgb(255, 123, 123, 36); }
        const int vw = Surface::measure(verdict, small) + 30;
        s.rect(x + w - 20 - vw, yy + 14, vw, 34, fill, 17);
        s.text(font_, x + w - 20 - vw + 15, yy + 19, verdict, small, tone);
        const int tx = x + 96;
        s.text(font_, tx, yy + 10, g["title"].as_string(), body, color::text, w - (tx - x) - vw - 40);
        std::string note = full ? std::string(kind_tag(g["kind"].as_string())) : std::string(tr("Sauvegarde"));
        if (g["console"].is_object()) note += " · " + std::string(tr("partie active sur cette console"));
        if (!full) {  // la même partie rangée à plusieurs endroits : des copies voulues, pas un problème
            const auto sp = main_spread(g);
            if (sp.second.size() >= 2) note = trf("Même partie à %d endroits : %s", static_cast<int>(sp.second.size()), join_dot(sp.second).c_str());
        }
        s.text(font_, tx, yy + 46, note, small, color::muted, w - (tx - x) - vw - 40);
        // Copies côte à côte : la partie active d'abord, puis chaque copie avec son étiquette.
        struct Tile { std::string tag, where, line; std::uint32_t tone; };
        std::vector<Tile> tiles2;
        const std::string cg = g["console"]["group"].as_string();
        const std::int64_t cm = g["console"]["mtime"].as_int();
        const std::int64_t cplayed = g["console"]["progress"]["mtime"].as_int() ? g["console"]["progress"]["mtime"].as_int() : cm;  // heure écrite par le jeu
        if (g["console"].is_object())
            tiles2.push_back({tr("Partie active"), tr("Console"), trf("partie du %s", day_month(cplayed).c_str()) + (cg.empty() ? std::string() : trf(" · contenu %s", cg.c_str())), color::ok});
        std::set<std::int64_t> removing;
        for (const auto& i : g["remove"].items()) removing.insert(i.as_int());
        const std::int64_t adv = g["most_advanced"]["index"].as_int(-1);
        const Json& copies = g["copies"];
        for (std::size_t i = 0; i < copies.size(); ++i) {
            const Json& c = copies[i];
            const std::string grp = c["group"].as_string();
            const std::int64_t saved = c["game_saved_at"].as_int();
            Tile t;
            t.where = c["on_peer"].as_bool() ? c["peer_name"].as_string() + " · " + c["place_label"].as_string() : c["place_label"].as_string();
            t.line = (saved ? trf("partie du %s", day_month(saved).c_str()) : trf("copiée le %s", day_month(c["created_unix"].as_int()).c_str())) +
                     (grp.empty() ? std::string() : trf(" · contenu %s", grp.c_str()));
            if (c["on_peer"].as_bool() && (grp.empty() || full)) { t.tag = trf("Sur %s", c["peer_name"].as_string().c_str()); t.tone = color::muted; t.where = c["place_label"].as_string(); }
            else if (full) { t.tag = tr("Copie complète"); t.tone = color::text; }
            else if (suspect(c)) { t.tag = tr("À vérifier"); t.tone = color::bad; }
            else if (c["restored_copy"].as_bool() && grp.empty()) { t.tag = tr("Partie restaurée"); t.tone = color::ok; }
            else if (grp.empty() || c["same_bytes"].as_bool()) { t.tag = tr("Pas comparée"); t.tone = color::warn; }  // même contenu qu'une autre, avancée pas lue
            else if (removing.count(static_cast<std::int64_t>(i))) { t.tag = tr("Doublon"); t.tone = color::ok; }
            else if (!cg.empty() && grp == cg) { t.tag = tr("Identique"); t.tone = color::ok; }
            else if (adv == static_cast<std::int64_t>(i)) { t.tag = tr("Plus avancée"); t.tone = color::accent; }
            else if (cm && saved > cm + 60) { t.tag = tr("Plus récente"); t.tone = color::warn; }
            else { t.tag = tr("Plus ancienne"); t.tone = color::muted; }
            tiles2.push_back(t);
        }
        const int tlw = 262, tlh = 60, ty = yy + 80;
        const int fit = std::max(1, (w - 32) / (tlw + 10));
        for (int i = 0; i < static_cast<int>(tiles2.size()) && i < fit; ++i) {
            const Tile& t = tiles2[static_cast<std::size_t>(i)];
            const int bx = x + 16 + i * (tlw + 10);
            if (i == fit - 1 && static_cast<int>(tiles2.size()) > fit) {  // la place manque : combien d'autres
                s.rect(bx, ty, tlw, tlh, rgb(255, 255, 255, 10), 12);
                s.text(font_, bx + 16, ty + 20, trf("+ %d autre(s)", static_cast<int>(tiles2.size()) - fit + 1), body, color::muted, tlw - 32);
                break;
            }
            s.rect(bx, ty, tlw, tlh, color::bg, 12);
            if (t.tone == color::ok && (t.tag == tr("Identique") || t.tag == tr("Partie active"))) s.outline(bx, ty, tlw, tlh, color::ok, 12, 2);
            const int tgw = s.text(font_, bx + 12, ty + 4, t.tag, small, t.tone, tlw - 24);  // étiquette · endroit, puis la date et le contenu
            s.text(font_, bx + 12 + tgw, ty + 4, " · " + t.where, small, color::text, std::max(0, tlw - 24 - tgw));
            s.text(font_, bx + 12, ty + 30, t.line, small, color::muted, tlw - 24);
        }
        yy += ch + 12;
    }
    if (!n) s.text(font_, x, 500, mg_cfilter_ == 0 ? tr("Aucun jeu n'a de copies à plusieurs endroits.") : tr("Rien dans cette vue."), large, color::muted, w);
    if (focus_list && mg_cfilter_ == 7) draw_footer(s, {{0, tr("Faire le tri")}, {-1, tr("Options : garder seulement sur…")}, {2, trf("Voir : %s", tr(cmp_filters[(mg_cfilter_ + 1) % 8])).c_str()}, {1, tr("Retour")}});
    else if (focus_list) draw_footer(s, {{0, tr("Ouvrir")}, {3, tr("Comparer ce jeu")}, {-1, tr("Options : actions")}, {2, trf("Voir : %s", tr(cmp_filters[(mg_cfilter_ + 1) % 8])).c_str()}, {1, tr("Retour")}});
    else draw_footer(s, {{0, tr("Choisir un jeu")}, {3, mg_zone_ == 0 ? tr("Tout comparer") : tr("Comparer ce jeu")}, {2, trf("Voir : %s", tr(cmp_filters[(mg_cfilter_ + 1) % 8])).c_str()}, {-1, tr("Options : actions")}, {-1, tr("Gauche / droite : endroits")}, {1, tr("Retour")}});
}

// « Tout comparer » : toutes les copies de tous les jeux, sur des copies (lecture seule) ; le résumé se relit à mesure.
void Ui::mg_compare_all() {
    Json b = Json::object();
    b.set("account", mg_sum_account_);
    model_.command("POST", "/api/v2/backups/compare-all", b.dump(), [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty()) { post_toast(e); return; }
        Json::parse(body, j);
        const int q = static_cast<int>(j["queued"].as_int());
        std::string msg = q ? trf("Comparaison lancée : %d jeu(x)", q) : std::string(tr("Tout est déjà comparé"));
        if (j["skipped_running"].as_int() > 0) msg += " · " + std::string(tr("le jeu en cours est laissé de côté"));
        post_toast(msg);
    });
    toast(tr("Comparaison de toutes les copies…"));
}

void Ui::input_mg_compare(std::uint32_t k) {
    const auto rows = mg_cmp_rows();
    const int n = static_cast<int>(rows.size());
    if (k & key::square) { mg_cfilter_ = (mg_cfilter_ + 1) % 8; mg_cmp_row_ = 0; return; }
    if (k & key::options) {  // ≡ : toutes les actions de l'écran, dont « Tout comparer »
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        if (mg_cmp_row_ < n && mg_sum_.is_object()) {
            const Json g = mg_sum_["games"][rows[static_cast<std::size_t>(mg_cmp_row_)]];
            if (!g["full"].as_bool()) acts.push_back({trf("Comparer « %s »", g["title"].as_string().c_str()), [this, g] { mg_compare_game(g); }});
        }
        acts.push_back({tr("Tout comparer"), [this] { mg_compare_all(); }});
        open_actions(tr("Comparer · trier"), std::move(acts));
        return;
    }
    if ((k & key::circle) || ((k & key::up) && mg_cmp_row_ == 0)) { mg_zone_ = 0; return; }
    if (k & key::up) { mg_cmp_row_ = std::max(0, mg_cmp_row_ - 1); return; }
    if (k & key::down) { mg_cmp_row_ = std::min(std::max(0, n - 1), mg_cmp_row_ + 1); return; }
    if (mg_cmp_row_ >= n || !mg_sum_.is_object()) return;
    const Json& g = mg_sum_["games"][rows[static_cast<std::size_t>(mg_cmp_row_)]];
    if (mg_cfilter_ == 7 && (k & key::cross)) { mg_tidy_start(&g); return; }   // faire le tri pour ce jeu
    if (mg_cfilter_ == 7 && (k & key::options)) { mg_tidy_start(nullptr); return; }  // garder seulement sur… (tous les jeux)
    if (k & key::cross) { mg_game_ = gkey(g); mg_screen_ = 1; mg_crow_ = 0; return; }  // toutes ses copies, avec leurs actions
    if (k & key::triangle) { if (g["full"].as_bool()) toast(tr("Pas de comparaison pour une sauvegarde complète")); else mg_compare_game(g); return; }
    if (k & key::options) { mg_actions(&g); return; }
}

void Ui::open_compare(const std::string& title_id, const std::string& item) {
    tab_ = backups;
    mg_compare_ = true;
    mg_cfilter_ = 0;
    mg_place_ = 0;
    mg_zone_ = 1;
    mg_screen_ = 0;
    const auto chips = mg_chips();
    for (int c = 0; c < static_cast<int>(chips.size()); ++c) if (chips[static_cast<std::size_t>(c)].fam == "compare") mg_chip_ = c;
    mg_cmp_row_ = 0;
    if (mg_sum_.is_object()) {
        const auto rows = mg_cmp_rows();
        bool found = false;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const Json& g = mg_sum_["games"][rows[r]];
            if ((!title_id.empty() && g["title_id"].as_string() == title_id) || (!item.empty() && g["item"].as_string() == item)) { mg_cmp_row_ = static_cast<int>(r); found = true; break; }
        }
        if (!found) toast(tr("Ce jeu n'a pas de copies à plusieurs endroits."));
    }
}

void Ui::input_backups_tab(std::uint32_t k) {
    if (mg_compare_ && mg_screen_ == 0 && mg_zone_ == 1 && !mg_preview_ && !mg_ign_open_) { input_mg_compare(k); return; }
    if (mg_screen_ == 1 || mg_zone_ == 1) { input_manage(k); return; }
    const auto chips = mg_chips();
    const int nc = static_cast<int>(chips.size());
    mg_chip_ = std::clamp(mg_chip_, 0, std::max(0, nc - 1));
    if ((k & key::left) || (k & key::right)) {
        mg_chip_ = (k & key::left) ? std::max(0, mg_chip_ - 1) : std::min(nc - 1, mg_chip_ + 1);
        const MgChip& c = chips[static_cast<std::size_t>(mg_chip_)];
        mg_compare_ = c.fam == "compare";
        if (mg_compare_) { mg_place_ = 0; mg_cmp_row_ = 0; }  // toutes les copies, partout
        else if (!c.group) { mg_place_ = c.old; mg_row_ = mg_trow_ = 0; }  // une bulle de famille ne change rien tant qu'on ne l'ouvre pas
        if (mg_in_trash()) mg_trash_.clear();
        return;
    }
    if ((k & key::circle) && !mg_open_.empty()) { mg_open_.clear(); mg_chip_ = 0; mg_place_ = 0; return; }  // refermer la famille
    // Comparer et trier ou Corbeille choisie : Rond revient aux endroits (Partout).
    if ((k & key::circle) && (chips[static_cast<std::size_t>(mg_chip_)].fam == "compare" || mg_chip_ == nc - 1)) {
        mg_chip_ = 0; mg_compare_ = false; mg_place_ = 0; mg_row_ = 0;
        return;
    }
    if (mg_chip_ < nc && chips[static_cast<std::size_t>(mg_chip_)].group) {
        if (k & (key::cross | key::down)) {  // ouvrir la famille : le curseur va sur son premier endroit
            mg_open_ = chips[static_cast<std::size_t>(mg_chip_)].fam;
            const auto after = mg_chips();
            for (int c = 0; c < static_cast<int>(after.size()); ++c)
                if (after[static_cast<std::size_t>(c)].fam == mg_open_) { mg_chip_ = c; mg_place_ = after[static_cast<std::size_t>(c)].old; mg_row_ = mg_trow_ = 0; break; }
        }
        return;
    }
    if (mg_compare_) {  // bulle entière comme les autres : Croix ou Bas descend dans la liste
        if (k & (key::cross | key::down)) { mg_zone_ = 1; return; }
        if (k & key::square) { mg_cfilter_ = (mg_cfilter_ + 1) % 8; mg_cmp_row_ = 0; }
        if (k & key::triangle) mg_compare_all();
        return;
    }
    if (k & (key::down | key::cross)) { mg_zone_ = 1; return; }
    if (mg_in_trash()) { if (k & key::triangle) mg_trash_.clear(); return; }
    if (k & key::square) { mg_view_ = (mg_view_ + 1) % 4; mg_row_ = 0; return; }
    if (k & key::options) {  // sur la carte d'une autre PS5 : synchroniser les copies avec elle
        const auto pl = mg_places();
        if (mg_place_ >= 1 && mg_place_ <= static_cast<int>(pl.size()) && pl[static_cast<std::size_t>(mg_place_ - 1)]->type == "peerconsole") {
            const Place& p = *pl[static_cast<std::size_t>(mg_place_ - 1)];
            sync_copies_dialog(p.id.substr(5, p.id.find(':', 5) - 5), p.service, false);
            return;
        }
    }
    if (k & (key::triangle | key::options)) { mg_zone_ = 1; input_manage(k); mg_zone_ = k & key::options ? 1 : 0; }
}

void Ui::sync_copies_dialog(const std::string& peer, const std::string& name, bool full) {
    toast(tr("Calcul de ce qui manque de chaque côté…"));
    model_.command("GET", "/api/v2/sync/copies/plan?peer=" + peer + (full ? "&full=1" : ""), "", [this, peer, name, full](const std::string& e, const std::string& body) {
        Json plan;
        if (!e.empty() || !Json::parse(body, plan)) { post_toast(e.empty() ? std::string(tr("réponse illisible")) : e); return; }
        post_ui([this, peer, name, full, plan] {
            if (plan.has("error")) { toast(plan["error"].as_string()); return; }
            const int np = static_cast<int>(plan["pull"].size()), ns = static_cast<int>(plan["push"].size());
            const auto pb = static_cast<std::uint64_t>(std::max<std::int64_t>(0, plan["pull_bytes"].as_int()));
            const auto sb = static_cast<std::uint64_t>(std::max<std::int64_t>(0, plan["push_bytes"].as_int()));
            const int jf = static_cast<int>(plan["pull_full"].as_int() + plan["push_full"].as_int());
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            if (np + ns > 0)
                acts.push_back({trf("Synchroniser : %d ici, %d là-bas", np, ns), [this, peer, name, full, plan] {
                    Json b = Json::object();
                    b.set("peer", peer);
                    b.set("include_full", full);
                    model_.command("POST", "/api/v2/sync/copies/start", b.dump(), [this, name](const std::string& e2, const std::string&) {
                        post_toast(e2.empty() ? trf("Synchro des copies avec %s lancée (suivie sur l'accueil)", name.c_str()) : e2);
                    });
                    sync_copies_at_ = 0;  // relire l'avancement tout de suite
                }});
            if (!full && jf > 0) acts.push_back({trf("Inclure aussi les jeux complets (%d)…", jf), [this, peer, name] { sync_copies_dialog(peer, name, true); }});
            acts.push_back({tr("Annuler"), [] {}});
            open_actions(trf("Synchroniser les copies avec %s", name.c_str()), std::move(acts));
            std::string note = np + ns == 0 ? std::string(tr("Rien ne manque : les deux consoles ont les mêmes copies.")) + " "
                                            : trf("Ici ← %d copie(s) (%s) depuis %s, rangées sur %s. %s ← %d copie(s) (%s), rangées sur %s.", np, size_text(pb).c_str(), name.c_str(),
                                                  plan["here_label"].as_string().c_str(), name.c_str(), ns, size_text(sb).c_str(), plan["peer_label"].as_string().c_str()) + " ";
            note += tr("On ajoute seulement ce qui manque : rien n'est écrasé ni supprimé, et rien ne change dans les parties des consoles.");
            if (!full && jf > 0) note += " " + trf("%d jeu(x) complet(s) laissé(s) de côté (gros) : à inclure si vous voulez.", jf);
            popup_note_ = note;
        });
    });
}

// Synchroniser les jeux suivis : seulement eux (jeux complets compris s'ils sont suivis). Aucun jeu suivi : on le dit et
// on propose de les choisir.
void Ui::sync_followed_dialog(const std::string& peer, const std::string& name) {
    model_.command("GET", "/api/v2/sync/copies/plan?peer=" + peer + "&full=1", "", [this, peer, name](const std::string& e, const std::string& body) {
        Json plan;
        if (!e.empty() || !Json::parse(body, plan)) { post_toast(e.empty() ? std::string(tr("réponse illisible")) : e); return; }
        post_ui([this, peer, name, plan] {
            if (plan.has("error")) { toast(plan["error"].as_string()); return; }
            const int np = static_cast<int>(plan["followed_pull"].as_int()), ns = static_cast<int>(plan["followed_push"].as_int());
            const bool none = plan["mode"].as_string() != "all" && plan["followed_games"].as_int() == 0;
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            if (!none && np + ns > 0)
                acts.push_back({trf("Synchroniser : %d ici, %d là-bas", np, ns), [this, peer, name] {
                    Json b = Json::object();
                    b.set("peer", peer); b.set("include_full", true); b.set("followed", true);
                    model_.command("POST", "/api/v2/sync/copies/start", b.dump(), [this, name](const std::string& e2, const std::string&) {
                        post_toast(e2.empty() ? trf("Synchro des copies avec %s lancée (suivie sur l'accueil)", name.c_str()) : e2);
                    });
                    sync_copies_at_ = 0;
                }});
            acts.push_back({tr("Choisir les jeux suivis…"), [this, peer, name] {
                tab_ = synchro;
                sy_select(peer);
                sy_open_pick(peer, name);
            }});
            acts.push_back({tr("Annuler"), [] {}});
            open_actions(trf("Synchroniser les jeux suivis avec %s", name.c_str()), std::move(acts));
            popup_note_ = none ? std::string(tr("Aucun jeu suivi pour l'instant : choisissez ceux à garder au même niveau sur les deux consoles."))
                       : np + ns == 0 ? std::string(tr("Vos jeux suivis ont les mêmes copies des deux côtés."))
                                      : std::string(tr("Seulement vos jeux suivis. On ajoute ce qui manque : rien n'est écrasé ni supprimé."));
        });
    });
}

void draw_checkbox(Surface& s, int x, int cy, bool on, bool dim) { checkbox(s, x, cy, on, dim); }


// ---- Même partie à plusieurs endroits : tri à la demande ----
// Un jeu : chaque copie d'ici dont la partie existe ailleurs, cochée = gardée. Tous les jeux : chaque endroit.
// Jamais la dernière copie d'une partie (la console et l'autre PS5 comptent), jamais une partie différente.
// Copie rangée dans un partage reçu (lien, lecture seule) : on la lit et la restaure, on ne la supprime ni ne la protège.
bool Ui::ro_place(const std::string& id) const {
    for (const auto& p : snap_->places) if (p.id == id) return p.readonly;
    return false;
}

void Ui::mg_tidy_start(const Json* g) {
    mg_tidy_rows_.clear();
    mg_tidy_row_ = 0;
    mg_tidy_all_ = g == nullptr;
    if (g) {
        const auto by = spread_of(*g);
        mg_tidy_title_ = (*g)["title"].as_string();
        for (const auto& c : (*g)["copies"].items()) {
            if (!spread_copy(c, by) || mg_protected_.count(ckey(c)) || ro_place(c["place"].as_string())) continue;
            MgTidyRow r;
            r.gk = gkey(*g);
            r.key = ckey(c);
            r.label = spot_name(c);
            r.letter = c["group"].as_string();
            r.sub = trf("copiée le %s", date_text(c["created_unix"].as_int(), false).c_str()) + " · " + trf("même partie à %d endroits", static_cast<int>(by.at(r.letter).size()));
            mg_tidy_rows_.push_back(r);
        }
    } else {
        mg_tidy_title_.clear();
        std::map<std::string, std::size_t> at;
        for (auto i : mg_cmp_rows()) {
            const Json& gg = mg_sum_["games"][i];
            const auto by = spread_of(gg);
            for (const auto& c : gg["copies"].items()) {
                if (!spread_copy(c, by) || mg_protected_.count(ckey(c)) || ro_place(c["place"].as_string())) continue;
                const std::string place = c["place"].as_string();
                auto it = at.find(place);
                if (it == at.end()) { it = at.emplace(place, mg_tidy_rows_.size()).first; MgTidyRow r; r.key = place; r.label = spot_name(c); mg_tidy_rows_.push_back(r); }
                ++mg_tidy_rows_[it->second].n;
            }
        }
        for (auto& r : mg_tidy_rows_) r.sub = trf("%d copie(s) d'une partie rangée aussi ailleurs", r.n);
    }
    if (mg_tidy_rows_.empty()) { toast(tr("Rien à trier ici")); return; }
    mg_tidy_locks();
    mg_tidy_open_ = true;
}

// Un jeu : une copie ne se décoche pas si c'est la dernière gardée de sa partie (console et autre PS5 comprises).
void Ui::mg_tidy_locks() {
    if (mg_tidy_all_) return;
    const Json* g = nullptr;
    for (const auto& x : mg_sum_["games"].items()) if (!mg_tidy_rows_.empty() && gkey(x) == mg_tidy_rows_[0].gk) { g = &x; break; }
    for (auto& r : mg_tidy_rows_) {
        int holders = 0;
        if (g && (*g)["console"]["group"].as_string() == r.letter) ++holders;
        if (g) for (const auto& c : (*g)["copies"].items()) if (c["on_peer"].as_bool() && c["group"].as_string() == r.letter) ++holders;
        for (const auto& o : mg_tidy_rows_) if (&o != &r && o.keep && o.letter == r.letter) ++holders;
        r.lock = r.keep && holders == 0;
    }
}

// Applique le choix : les copies décochées passent par l'aperçu habituel (rien ne part sans Carré maintenu).
void Ui::mg_tidy_apply() {
    std::set<std::string> games;
    if (!mg_tidy_all_) {
        std::set<std::string> drop;
        for (const auto& r : mg_tidy_rows_) if (!r.keep) drop.insert(r.key);
        if (drop.empty()) { toast(tr("Tout est gardé : rien n'a changé")); mg_tidy_open_ = false; return; }
        mg_csel_[mg_tidy_rows_[0].gk] = drop;
        games.insert(mg_tidy_rows_[0].gk);
    } else {
        std::set<std::string> off;
        for (const auto& r : mg_tidy_rows_) if (!r.keep) off.insert(r.key);
        if (off.empty()) { toast(tr("Tout est gardé : rien n'a changé")); mg_tidy_open_ = false; return; }
        for (auto i : mg_cmp_rows()) {
            const Json& g = mg_sum_["games"][i];
            const auto by = spread_of(g);
            // Ce qui reste de chaque partie : la console, l'autre PS5, et les copies sur les endroits gardés.
            std::set<std::string> stays;
            stays.insert(g["console"]["group"].as_string());
            for (const auto& c : g["copies"].items()) {
                if (c["other_account"].is_string()) continue;
                if (c["on_peer"].as_bool() || !off.count(c["place"].as_string()) || mg_protected_.count(ckey(c))) stays.insert(c["group"].as_string());
            }
            std::set<std::string> drop;
            for (const auto& c : g["copies"].items())
                if (spread_copy(c, by) && off.count(c["place"].as_string()) && !mg_protected_.count(ckey(c)) && stays.count(c["group"].as_string())) drop.insert(ckey(c));
            if (drop.empty()) continue;
            mg_csel_[gkey(g)] = drop;
            games.insert(gkey(g));
        }
        if (games.empty()) { toast(tr("Rien à retirer : ces parties n'existent pas ailleurs")); mg_tidy_open_ = false; return; }
    }
    mg_tidy_open_ = false;
    mg_pv_delete_ = false;
    mg_tidy_games_ = games;
    mg_open_preview(games);
}

void Ui::draw_mg_tidy(Surface& s) {
    s.rect(0, 0, 1920, 1080, color::shadow);
    const int w = 1200, x = (1920 - w) / 2, per = 8;
    const int n = static_cast<int>(mg_tidy_rows_.size());
    const int shown = std::min(n, per), first = std::clamp(mg_tidy_row_ - per + 1, 0, std::max(0, n - per));
    const auto help = wrap(mg_tidy_all_ ? tr("Décochez un endroit : les copies d'une partie rangée aussi ailleurs y seront retirées. La dernière copie d'une partie reste toujours.")
                                        : tr("Cochée = gardée. Ce sont des copies de la même partie ; la dernière copie d'une partie ne peut pas être décochée."), small, w - 96);
    const int top = 92 + static_cast<int>(help.size()) * 34 + 18;
    const int h = top + shown * 78 + 24;
    const int y = std::max(40, (990 - h) / 2);
    s.rect(x, y, w, h, color::panel, 28);
    s.outline(x, y, w, h, color::line, 28, 2);
    s.text(font_, x + 48, y + 30, mg_tidy_all_ ? std::string(tr("Garder seulement sur…")) : trf("Faire le tri : %s", mg_tidy_title_.c_str()), large, color::text, w - 96);
    for (std::size_t i = 0; i < help.size(); ++i) s.text(font_, x + 48, y + 92 + static_cast<int>(i) * 34, help[i], small, color::muted);
    int yy = y + top;
    for (int i = first; i < n && i < first + per; ++i) {
        const MgTidyRow& r = mg_tidy_rows_[static_cast<std::size_t>(i)];
        const bool f = i == mg_tidy_row_;
        s.rect(x + 32, yy, w - 64, 68, f ? color::panel2 : rgb(255, 255, 255, 8), 14);
        if (f) s.outline(x + 28, yy - 4, w - 56, 76, color::accent, 18, 3);
        checkbox(s, x + 56, yy + 34, r.keep, r.lock);
        s.text(font_, x + 96, yy + 6, r.label, body, color::text, w - 420);
        s.text(font_, x + 96, yy + 38, r.sub, small, color::muted, w - 420);
        const std::string tag = r.lock ? std::string(tr("dernière copie")) : r.keep ? std::string(tr("gardée")) : std::string(tr("à la corbeille"));
        const int tw = Surface::measure(tag, small) + 30;
        s.rect(x + w - 56 - tw, yy + 17, tw, 34, r.keep ? rgb(95, 211, 154, 36) : rgb(255, 255, 255, 18), 17);
        s.text(font_, x + w - 56 - tw + 15, yy + 22, tag, small, r.keep ? color::ok : color::muted);
        yy += 78;
    }
    s.rect(0, 990, 1920, 90, color::bg);
    draw_footer(s, {{0, tr("Cocher / décocher")}, {2, tr("Appliquer (aperçu avant)")}, {1, tr("Annuler")}});
}

void Ui::input_mg_tidy(std::uint32_t k) {
    const int n = static_cast<int>(mg_tidy_rows_.size());
    if (k & key::circle) { mg_tidy_open_ = false; toast(tr("Rien n'a été modifié")); return; }
    if (k & key::up) { mg_tidy_row_ = std::max(0, mg_tidy_row_ - 1); return; }
    if (k & key::down) { mg_tidy_row_ = std::min(n - 1, mg_tidy_row_ + 1); return; }
    if ((k & key::cross) && mg_tidy_row_ < n) {
        MgTidyRow& r = mg_tidy_rows_[static_cast<std::size_t>(mg_tidy_row_)];
        if (r.lock) { toast(tr("La dernière copie de cette partie reste gardée")); return; }
        r.keep = !r.keep;
        mg_tidy_locks();
        return;
    }
    if (k & key::square) mg_tidy_apply();
}


// ---- Partager ma copie (3.1.43) : un lien vers une copie (version d'une sauvegarde), où qu'elle soit rangée ----
// Lien temporaire de la console (.zip, réseau de la maison), lien BitTorrent (copie sur un disque de la console) ou
// lien MEGA (copie sur MEGA). Le lien et son code QR s'affichent dans une fenêtre ; « Mes liens de partage » les liste.
void Ui::open_share_copy(const Json& g, const Json& c) {
    const std::string place = c["place"].as_string(), bundle = c["bundle"].as_string(), title = g["title"].as_string();
    const std::string where = copy_kind_text(g, c) + "  ·  " + c["place_label"].as_string() + "  ·  " + date_text(c["created_unix"].as_int());
    std::string proto;
    for (const auto& p : snap_->places) if (p.id == place) proto = p.protocol;
    const bool local = place.rfind("remote:", 0) != 0 && place.rfind("peer", 0) != 0;
    auto ask = [this, place, bundle, title, where](const std::string& kind, int hours) {
        Json b = Json::object();
        b.set("place", place); b.set("bundle", bundle); b.set("kind", kind); b.set("hours", hours); b.set("title", title);
        model_.command("POST", "/api/v2/shares/out", b.dump(), [this, title, where, kind](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Lien impossible")) : e); return; }
            if (kind == "bt") { const std::string job = j["job"].as_string(); post_ui([this, job, title] { show_new_bt_link(job, title); }); return; }
            j.set("title", title); j.set("where", where);
            post_ui([this, j] { sh_ = j; sh_pub_ = false; sh_open_ = true; });
        });
    };
    std::vector<std::pair<std::string, std::function<void()>>> acts;
    acts.push_back({tr("Lien de la console (.zip) · maison ou Internet…"), [this, ask, title] {
        std::vector<std::pair<std::string, std::function<void()>>> d;
        d.push_back({tr("Pendant 1 heure"), [ask] { ask("local", 1); }});
        d.push_back({tr("Pendant 24 heures"), [ask] { ask("local", 24); }});
        d.push_back({tr("Pendant 7 jours"), [ask] { ask("local", 24 * 7); }});
        d.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Lien vers « %s »", title.c_str()), std::move(d));
        popup_note_ = tr("Le lien s'arrête seul à la fin ; vous pouvez l'arrêter avant dans Téléchargements › Mes liens.");
    }});
    if (local && !c["on_peer"].as_bool()) acts.push_back({tr("Lien BitTorrent (magnet) · sans copie"), [ask] { ask("bt", 0); }});
    if (proto == "mega") acts.push_back({trf("Lien %s · du service", "MEGA"), [ask] { ask("mega", 0); }});
    // Le lien du service lui-même : OneDrive et Dropbox (droit « sharing.write » accordé le 18/11 ; une connexion plus
    // ancienne le dit clairement : reconnecter Dropbox). Google Drive refuse de partager un dossier avec l'accès limité de
    // l'appli (drive.file) : pour lui, le lien temporaire de la console.
    if (proto == "onedrive") acts.push_back({trf("Lien %s · du service", "OneDrive"), [ask] { ask("service", 0); }});
    if (proto == "dropbox") acts.push_back({trf("Lien %s · du service", "Dropbox"), [ask] { ask("service", 0); }});
    acts.push_back({tr("Mes liens…"), [this] { open_my_shares(); }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Partager « %s »", title.c_str()), std::move(acts));
    popup_note_ = where + "  ·  " + std::string(tr("Le lien donne cette copie seulement, en lecture : rien n'est effacé ni modifié."));
}

// Taille d'une copie (« · 74 Mo ») : le contenu comparé, sinon la taille du backup ; rien si elle n'est pas notée
// (anciennes copies) plutôt que « 0 o ».
static std::string copy_size_text(const Json& c) {
    const std::int64_t b = c["content_bytes"].as_int() > 0 ? c["content_bytes"].as_int() : c["bytes"].as_int();
    return b > 0 ? "  ·  " + size_text(static_cast<std::uint64_t>(b)) : std::string();
}

// Ce qu'est une copie, en clair : la sauvegarde de partie de quel compte, ou une copie du jeu lui-même.
std::string Ui::copy_kind_text(const Json& g, const Json& c) const {
    bool app = g["full"].as_bool();
    for (const auto& p : c["parts"].items()) if (p.as_string() == "app") app = true;
    if (app) return tr("Copie du jeu");
    std::string who = c["owner_name"].as_string();
    if (who.empty()) for (const auto& a : overview_["accounts"].items()) if (a["key"].as_string() == g["account"].as_string()) who = a["name"].as_string();
    return who.empty() ? std::string(tr("Sauvegarde de partie")) : trf("Sauvegarde de partie · %s", who.c_str());
}

// Depuis la fiche : « Que partager ? » — le jeu lui-même (là où il est : interne, USB…, lien BitTorrent, sans copie),
// une sauvegarde de partie, ou une copie du jeu (le jeu et sa partie ensemble). Puis la liste, la plus récente d'abord.
void Ui::share_from_sheet(const Item& it) {
    if (!mg_sum_.is_object()) { mg_load_summary(); toast(tr("Un instant : lecture des copies…")); return; }
    using Acts = std::vector<std::pair<std::string, std::function<void()>>>;
    std::vector<std::pair<Json, Json>> saves, games;
    for (const auto& g : mg_sum_["games"].items()) {
        const bool same = (!it.title_id.empty() && g["title_id"].as_string() == it.title_id) || g["item"].as_string() == it.key;
        if (!same) continue;
        for (const auto& c : g["copies"].items()) {
            if (!c["complete"].as_bool(true)) continue;
            (copy_kind_text(g, c) == tr("Copie du jeu") ? games : saves).push_back({g, c});
        }
    }
    auto newest = [](const auto& a, const auto& b) { return a.second["created_unix"].as_int() > b.second["created_unix"].as_int(); };
    std::sort(saves.begin(), saves.end(), newest);
    std::sort(games.begin(), games.end(), newest);
    const std::string title = it.title, key = it.key;
    auto pick = [this, title](const std::vector<std::pair<Json, Json>>& list, const std::string& what) {
        Acts a;
        for (const auto& [g, c] : list) {
            if (a.size() >= 12) break;
            a.push_back({copy_kind_text(g, c) + "  ·  " + c["place_label"].as_string() + "  ·  " + date_text(c["created_unix"].as_int(), false) + copy_size_text(c),
                         [this, g = g, c = c] { open_share_copy(g, c); }});
        }
        a.push_back({tr("Annuler"), [] {}});
        open_actions(trf("Partager « %s » : %s", title.c_str(), what.c_str()), std::move(a));
    };
    // Le jeu lui-même : la partie « app » (jeu PS5 / PS4, appli), sinon « game » (ROM).
    std::uint64_t app_bytes = 0;
    bool has_app = false;
    for (const auto& c : it.components) if (c.part == "app") { has_app = true; app_bytes += c.bytes; }
    if (!has_app) for (const auto& c : it.components) if (c.part == "game") { has_app = true; app_bytes += c.bytes; }
    Acts acts;
    if (has_app) {
        const std::string where = item_disk_label(it);
        acts.push_back({trf("Le jeu · %s · %s · lien BitTorrent (magnet), sans copie", where.c_str(), size_text(app_bytes).c_str()), [this, key, title] {
            open_actions(trf("Créer un lien pour « %s » ?", title.c_str()), {
                {tr("Oui, créer le lien"), [this, key, title] {
                    Json b = Json::object();
                    b.set("kind", "game");
                    b.set("item", key);
                    model_.command("POST", "/api/v2/shares/out", b.dump(), [this, title](const std::string& e, const std::string& body) {
                        Json j;
                        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Lien impossible")) : e); return; }
                        const std::string job = j["job"].as_string();
                        post_ui([this, job, title] { show_new_bt_link(job, title); });
                        std::lock_guard<std::mutex> l(async_mu_);
                        tor_loaded_ = 0;
                    });
                }},
                {tr("Annuler"), [] {}},
            });
            popup_note_ = tr("La console calcule les empreintes (quelques minutes pour un gros jeu), puis le partage depuis là où il est : rien n'est copié. Le lien et son code QR restent dans Téléchargements › Mes liens.");
        }});
    }
    if (!saves.empty()) acts.push_back({trf("Une sauvegarde de partie… (%d)", static_cast<int>(saves.size())), [pick, saves] { pick(saves, tr("quelle sauvegarde ?")); }});
    if (!games.empty()) acts.push_back({trf("Une copie du jeu, avec sa partie… (%d)", static_cast<int>(games.size())), [pick, games] { pick(games, tr("quelle copie ?")); }});
    if (acts.empty()) { toast(tr("Aucune copie de cet élément pour l'instant : sauvegardez-le d'abord")); return; }
    acts.push_back({tr("Mes liens…"), [this] { open_my_shares(); }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(trf("Que partager de « %s » ?", title.c_str()), std::move(acts));
}

// Mes liens : tous les liens donnés depuis la console — BitTorrent (en préparation ou partagé), .zip de la console, MEGA,
// lien du service (OneDrive…). Chacun s'ouvre dans la fenêtre « Partager » : code QR, adresse, arrêt.
void Ui::load_links() {
    links_at_ = now_;
    model_.command("GET", "/api/v2/links", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) return;
        post_ui([this, j] { links_ = j["links"].is_array() ? j["links"] : Json::array(); });
    });
}

std::string Ui::link_line(const Json& l) const {
    const std::string kind = l["kind"].as_string(), st = l["state"].as_string();
    std::string what = kind == "bt" ? std::string("BitTorrent") : kind == "local" ? std::string(tr("Lien de la console (.zip)"))
                     : kind == "mega" ? std::string("MEGA") : l["place_label"].as_string().empty() ? std::string(tr("Lien du service")) : l["place_label"].as_string();
    std::string state;
    if (st == "preparing") {
        const std::int64_t t = l["total"].as_int(), d = l["done"].as_int();
        state = t > 0 ? trf("en préparation · %d %%", static_cast<int>(d * 100 / t)) : std::string(tr("en préparation"));
    } else if (st == "error") state = tr("erreur");
    else if (st == "paused") state = tr("en pause");
    else if (st == "checking") state = tr("vérification des fichiers");
    else if (st == "missing") state = tr("fichiers introuvables");
    else if (kind == "bt") state = trf("partagé · %d pair(s)", static_cast<int>(l["peers"].as_int()));
    else if (kind == "local") state = trf("jusqu'au %s · %d téléchargement(s)", date_text(l["expires"].as_int()).c_str(), static_cast<int>(l["downloads"].as_int()));
    else state = tr("ouvert");
    return l["title"].as_string() + "  ·  " + what + "  ·  " + state;
}

void Ui::open_link(const Json& l) {
    Json x = l;
    const std::string kind = l["kind"].as_string();
    if (kind == "local") x.set("where", trf("jusqu'au %s", date_text(l["expires"].as_int()).c_str()));
    else if (kind == "bt") x.set("where", std::string("BitTorrent (magnet)") + (l["total"].as_int() > 0 ? "  ·  " + size_text(static_cast<std::uint64_t>(l["total"].as_int())) : std::string()));
    else x.set("where", kind == "mega" ? std::string("MEGA") : l["place_label"].as_string());
    if (kind == "bt") x.set("match", l["file"].is_string() ? l["file"] : l["title"]);  // fichier partagé : retrouve le lien quand la préparation finit
    sh_ = x; sh_pub_ = false; sh_open_ = true; sh_poll_ = now_;
}

void Ui::show_new_bt_link(const std::string& job, const std::string& title) {
    Json x = Json::object();
    x.set("kind", "bt");
    x.set("id", job);
    x.set("title", title);
    x.set("state", "preparing");
    x.set("where", "BitTorrent (magnet)");
    sh_ = x; sh_pub_ = false; sh_open_ = true; sh_poll_ = 0;
    links_at_ = 0;
}

void Ui::open_my_shares() {
    model_.command("GET", "/api/v2/links", "", [this](const std::string& e, const std::string& body) {
        Json j;
        if (!e.empty() || !Json::parse(body, j)) { post_toast(e.empty() ? std::string(tr("Liste impossible")) : e); return; }
        post_ui([this, j] {
            links_ = j["links"].is_array() ? j["links"] : Json::array();
            links_at_ = now_;
            std::vector<std::pair<std::string, std::function<void()>>> acts;
            for (const auto& l : links_.items()) { const Json x = l; acts.push_back({link_line(x), [this, x] { open_link(x); }}); }
            if (acts.empty()) { toast(tr("Aucun lien pour l'instant : Partager… crée un lien pour un jeu, une ROM ou une sauvegarde")); return; }
            acts.push_back({tr("Fermer"), [] {}});
            open_actions(tr("Mes liens"), std::move(acts));
            popup_note_ = tr("Tous les liens donnés depuis cette console : choisissez-en un pour son code QR, son adresse, ou pour l'arrêter.");
        });
    });
}

void Ui::input_share(std::uint32_t k) {
    if (k & key::circle) { sh_open_ = false; return; }
    if ((k & key::square) && sh_["url_public"].is_string()) { sh_pub_ = !sh_pub_; return; }
    if ((k & key::triangle) && sh_["kind"].as_string() == "local") {  // redemander l'adresse publique, puis relire ce lien
        const std::string tok = sh_["token"].as_string();
        model_.command("POST", "/api/v2/public-address", "{\"refresh\":true}", [this, tok](const std::string&, const std::string&) {
            model_.command("GET", "/api/v2/shares/out", "", [this, tok](const std::string& e, const std::string& body) {
                Json j;
                if (!e.empty() || !Json::parse(body, j)) return;
                for (const auto& x : j["shares"].items())
                    if (x["token"].as_string() == tok)
                        post_ui([this, x] {
                            if (!sh_open_) return;
                            sh_.set("url_public", x["url_public"]);
                            sh_.set("qr_public", x["qr_public"]);
                            toast(x["url_public"].is_string() ? std::string(tr("Adresse publique à jour")) : std::string(tr("Adresse publique inconnue : la console n'a pas pu la demander")));
                        });
            });
        });
        return;
    }
    const std::string kind = sh_["kind"].as_string();
    if ((k & key::cross) && kind == "bt" && !sh_["id"].as_string().empty()) {
        const Json cur = sh_;
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        acts.push_back({tr("Arrêter ce partage"), [this, cur] {
            Json b = Json::object();
            b.set("kind", "bt");
            b.set("id", cur["id"]);
            model_.command("POST", "/api/v2/shares/out/stop", b.dump(), [this](const std::string& e, const std::string&) {
                post_toast(e.empty() ? std::string(tr("Partage arrêté : le jeu reste sur la console")) : e);
            });
            links_at_ = 0;
            sh_open_ = false;
        }});
        acts.push_back({tr("Annuler"), [] {}});
        open_actions(tr("Arrêter ce partage ?"), std::move(acts));
        popup_note_ = tr("Le lien ne marchera plus. Rien n'est effacé : le jeu et ses fichiers restent où ils sont.");
        return;
    }
    if ((k & key::cross) && (kind == "local" || kind == "mega" || kind == "service")) {
        const Json cur = sh_;
        std::vector<std::pair<std::string, std::function<void()>>> acts;
        acts.push_back({tr("Arrêter ce lien"), [this, cur, kind] {
            Json b = Json::object();
            std::string path = "/api/v2/shares/out/stop";
            if (kind == "mega" || kind == "service") { path = "/api/v2/shares/out"; b.set("kind", kind); b.set("off", true); b.set("place", cur["place"]); b.set("bundle", cur["bundle"]); }
            else b.set("token", cur["token"]);
            model_.command("POST", path, b.dump(), [this](const std::string& e, const std::string&) { post_toast(e.empty() ? std::string(tr("Lien arrêté : il ne s'ouvre plus")) : e); });
            sh_open_ = false;
        }});
        acts.push_back({tr("Annuler"), [] {}});
        open_actions(tr("Arrêter ce lien ?"), std::move(acts));
    }
}

void Ui::draw_share(Surface& s) {
    s.rect(0, 0, 1920, 1080, rgb(2, 8, 20, 170));
    const int w = 1300, h = 640, x = (1920 - w) / 2, y = (1080 - h) / 2 - 20;
    window_frame(s, x, y, w, h, 112);
    help_in(x, y, w, h);
    const std::string kind = sh_["kind"].as_string();
    if (kind == "bt" && sh_["state"].as_string() != "ready" && now_ - sh_poll_ > 2000) {  // la préparation avance : relire
        sh_poll_ = now_;
        model_.command("GET", "/api/v2/links", "", [this](const std::string& e, const std::string& body) {
            Json j;
            if (!e.empty() || !Json::parse(body, j)) return;
            post_ui([this, j] {
                if (!sh_open_ || sh_["kind"].as_string() != "bt") return;
                links_ = j["links"];
                const std::string id = sh_["id"].as_string(), match = sh_["match"].as_string();
                for (const auto& l : j["links"].items()) {
                    if (l["kind"].as_string() != "bt") continue;
                    const std::string file = l["file"].is_string() ? l["file"].as_string() : l["title"].as_string();
                    const bool same = l["id"].as_string() == id || (!match.empty() && file == match && l["state"].as_string() != "preparing");
                    if (!same) continue;
                    for (const char* f : {"id", "state", "done", "total", "error", "url", "qr", "url_public", "qr_public", "peers", "local", "public"}) sh_.set(f, l[f]);
                    sh_.set("match", file);
                    if (l["total"].as_int() > 0) sh_.set("where", "BitTorrent (magnet)  ·  " + size_text(static_cast<std::uint64_t>(l["total"].as_int())));
                    return;
                }
            });
        });
    }
    s.text(font_, x + 40, y + 30, trf("Partager « %s »", sh_["title"].as_string().c_str()), large, color::text, w - 80);
    s.text(font_, x + 40, y + 78, sh_["where"].as_string(), small, color::muted, w - 80);
    const int lw = w - 80 - 420;
    int ty = y + 150;
    const std::string main = kind == "mega" ? tr("Lien MEGA : il s'ouvre partout, dans un navigateur ou l'appli MEGA. Il contient la clé : ne le donnez qu'aux personnes voulues.")
                           : kind == "service" ? tr("Lien du service : il s'ouvre partout, pour toute personne qui a le lien, en lecture. La console peut être éteinte. Arrêtez-le quand il ne sert plus.")
                           : kind == "bt" ? (sh_["state"].as_string() == "preparing" ? tr("La console calcule les empreintes du jeu, là où il est rangé : rien n'est copié. Le code QR apparaît ici à la fin ; vous pouvez fermer, le lien reste dans Téléchargements › Mes liens.")
                                            : sh_["state"].as_string() == "error" ? trf("Le lien n'a pas pu être fait : %s", sh_["error"].as_string().c_str())
                                            : sh_["state"].as_string() == "missing" ? tr("Les fichiers de ce lien ne sont plus là où ils étaient (déplacés ou retirés) : il ne partage rien. Arrêtez-le, puis refaites un lien avec Partager…")
                                            : tr("Lien magnet : scannez-le avec le téléphone ou ouvrez-le dans un logiciel BitTorrent (une autre PS5, qBittorrent…). La console partage le jeu tant qu'elle est allumée et que le lien reste dans Mes liens."))
                                            : tr("Scannez avec le téléphone, ou ouvrez l'adresse sur un ordinateur : la copie se télécharge en .zip, lue directement là où elle est rangée.");
    for (const auto& l : wrap(main, body, lw)) { s.text(font_, x + 40, ty, l, body, color::text); ty += 42; }
    ty += 10;
    if (kind == "bt" && sh_["state"].as_string() == "ready") {  // le lien public : la même adresse que pour la PS5 à distance
        // Deux liens, comme le .zip : Carré passe du lien de la maison au lien Internet (le code QR change avec).
        const bool has_pub = sh_["url_public"].is_string();
        const std::string cap = has_pub && sh_pub_ ? trf("Lien Internet, pour quelqu'un de loin (%s) : la console doit rester allumée et son port BitTorrent ouvert sur la box (UPnP le fait seul s'il est activé dans Paramètres › Réseau).", sh_["public"].as_string().c_str())
                              : has_pub ? std::string(tr("Lien de la maison : pour une autre PS5 ou un PC sur le même réseau."))
                                        : std::string(tr("Marche sur le réseau de la maison ; depuis Internet, dès que l'adresse publique est connue"));
        for (const auto& l : wrap(cap, small, lw)) { s.text(font_, x + 40, ty, l, small, color::muted); ty += 32; }
        ty += 6;
    }
    if (kind == "local") {
        std::string note = trf("Le lien s'arrête seul le %s.", date_text(sh_["expires"].as_int()).c_str()) + " " +
                           std::string(sh_pub_ ? tr("Lien Internet, pour quelqu'un de loin : la console doit rester allumée et son port 52370 ouvert sur la box (UPnP le fait seul s'il est activé dans Paramètres › Réseau).")
                                               : tr("Il marche sur le réseau de la maison ; par Internet seulement si le port 52370 de la console est ouvert sur la box. La console doit rester allumée."));
        for (const auto& l : wrap(note, small, lw)) { s.text(font_, x + 40, ty, l, small, color::muted); ty += 32; }
    }
    if (kind == "bt" && sh_["state"].as_string() == "preparing") {
        const std::int64_t t = sh_["total"].as_int(), d = sh_["done"].as_int();
        const int bx = x + 40, by = ty + 30, bw = lw;
        s.rect(bx, by, bw, 14, rgb(255, 255, 255, 40), 7);
        if (t > 0) s.rect(bx, by, std::max(14, static_cast<int>(bw * d / t)), 14, color::accent, 7);
        s.text(font_, bx, by + 34, t > 0 ? trf("Préparation : %d %%  ·  %s sur %s", static_cast<int>(d * 100 / t), size_text(static_cast<std::uint64_t>(d)).c_str(), size_text(static_cast<std::uint64_t>(t)).c_str())
                                         : std::string(tr("Préparation…")), small, color::muted, bw);
        draw_footer(s, {{0, tr("Arrêter…")}, {1, tr("Fermer")}});
        return;
    }
    const bool pub = sh_pub_ && sh_["url_public"].is_string();
    // Un lien BitTorrent sans ses fichiers ne partage rien : ni code QR ni adresse, pour qu'on ne le donne pas pour rien.
    const bool dead = kind == "bt" && sh_["state"].as_string() == "missing";
    const auto& rows = sh_[pub ? "qr_public" : "qr"].items();
    const int m = static_cast<int>(rows.size()), qs = 380, qx = x + w - 40 - qs, qy = y + 150;
    if (!dead) {
    s.exact_colors(true);
    s.rect(qx, qy, qs, qs, rgb(255, 255, 255), 12);
    if (m) {
        const double cell = qs / (m + 4.0);
        for (int r = 0; r < m; ++r) {
            const std::string& line = rows[static_cast<std::size_t>(r)].as_string();
            for (int c = 0; c < m && c < static_cast<int>(line.size()); ++c)
                if (line[static_cast<std::size_t>(c)] == '1') s.rect(qx + static_cast<int>((c + 2) * cell), qy + static_cast<int>((r + 2) * cell), static_cast<int>((c + 3) * cell) - static_cast<int>((c + 2) * cell), static_cast<int>((r + 3) * cell) - static_cast<int>((r + 2) * cell), rgb(0, 0, 0), 0);
        }
    }
    s.exact_colors(false);
    if (sh_["url_public"].is_string()) s.text(font_, qx, qy + qs + 14, pub ? tr("Lien Internet") : tr("Lien de la maison"), small, color::muted, qs);
    {  // l'adresse en entier, à gauche sous le texte, coupée au caractère (pas d'espaces dans une adresse)
        const std::string u = sh_[pub ? "url_public" : "url"].as_string();
        std::size_t at = 0;
        ty += 16;
        while (at < u.size() && ty <= y + h - 130) {
            std::size_t n = 1;
            while (at + n < u.size() && Surface::measure(u.substr(at, n + 1), small) <= lw) ++n;
            s.text(font_, x + 40, ty, u.substr(at, n), small, color::accent, lw);
            at += n; ty += 32;
        }
    }
    }  // !dead
    if (kind == "local" && sh_["url_public"].is_string()) draw_footer(s, {{0, tr("Arrêter le lien…")}, {2, pub ? tr("Lien de la maison") : tr("Lien Internet")}, {3, tr("Actualiser l'adresse")}, {1, tr("Fermer")}});
    else if (kind == "local") draw_footer(s, {{0, tr("Arrêter le lien…")}, {3, tr("Actualiser l'adresse")}, {1, tr("Fermer")}});
    else if (kind == "local" || kind == "mega" || kind == "service") draw_footer(s, {{0, tr("Arrêter le lien…")}, {1, tr("Fermer")}});
    else if (kind == "bt" && !sh_["id"].as_string().empty() && sh_["url_public"].is_string() && sh_["state"].as_string() == "ready")
        draw_footer(s, {{0, tr("Arrêter le partage…")}, {2, pub ? tr("Lien de la maison") : tr("Lien Internet")}, {1, tr("Fermer")}});
    else if (kind == "bt" && !sh_["id"].as_string().empty()) draw_footer(s, {{0, tr("Arrêter le partage…")}, {1, tr("Fermer")}});
    else draw_footer(s, {{1, tr("Fermer")}});
}


// Le vrai disque d'un élément, comme la fiche : celui des données d'une image (une image sur une clé USB…), sinon son endroit.
std::string Ui::item_disk_label(const Item& it) const {
    std::string where = where_label(it.place);
    for (const auto& p : snap_->places) if (p.id == it.place && !p.label.empty()) where = p.label;
    const auto img = tr_images_.find(it.key);
    if (img != tr_images_.end() && !img->second["disk"]["label"].as_string().empty()) where = tr(img->second["disk"]["label"].as_string().c_str());
    return where;
}

// Téléchargements › Partager… : d'abord quoi (jeux PS5, PS4, ROM par console, applis, sauvegardes, un fichier), puis où
// (chaque disque, avec le nombre), puis la liste de A à Z ; un élément ouvre « Que partager ? » (le jeu, une sauvegarde…).
void Ui::open_share_hub() {
    using Acts = std::vector<std::pair<std::string, std::function<void()>>>;
    std::vector<Item> ps5, ps4, apps, roms, saves;
    for (const auto& it : snap_->items) {
        if (it.kind == "game_ps5") ps5.push_back(it);
        else if (it.kind == "game_ps4") ps4.push_back(it);
        else if (it.kind == "app") apps.push_back(it);
        else if (it.kind == "rom") roms.push_back(it);
        for (const auto& c : it.components) if (c.part == "save") { saves.push_back(it); break; }
    }
    Acts acts;
    auto add = [&](const char* name, std::vector<Item> list) {
        if (list.empty()) return;
        const std::string what = tr(name);
        const int n = static_cast<int>(list.size());
        acts.push_back({what + "  ·  " + std::to_string(n), [this, list, what] { share_hub_where(list, what); }});
    };
    add(N_("Jeux PS5"), ps5);
    add(N_("Jeux PS4"), ps4);
    if (!roms.empty()) {  // ROM : d'abord la console
        const int n = static_cast<int>(roms.size());
        acts.push_back({std::string(tr("ROM (par console)")) + "  ·  " + std::to_string(n), [this, roms] {
            std::map<std::string, std::vector<Item>> by;
            for (const auto& r : roms) by[r.platform.empty() ? std::string(tr("Autre")) : r.platform].push_back(r);
            Acts a;
            for (const auto& [plat, list] : by)
                a.push_back({plat + "  ·  " + std::to_string(list.size()), [this, list = list, plat = plat] { share_hub_where(list, "ROM " + plat); }});
            a.push_back({tr("Annuler"), [] {}});
            open_actions(tr("ROM : quelle console ?"), std::move(a));
        }});
    }
    add(N_("Applis"), apps);
    add(N_("Sauvegardes de partie"), saves);
    acts.push_back({tr("Un fichier ou un dossier…"), [this] {
        open_browser(tr("Créer un lien : quel fichier ou dossier partager ?"), "/data", true, [this](const std::string& path) { create_link(path); });
        br_any_ = true;
    }});
    acts.push_back({tr("Mes liens…"), [this] { open_my_shares(); }});
    acts.push_back({tr("Annuler"), [] {}});
    open_actions(tr("Que partager ?"), std::move(acts));
}

void Ui::share_hub_where(std::vector<Item> list, const std::string& what) {
    std::map<std::string, std::vector<Item>> by;
    for (const auto& it : list) by[item_disk_label(it)].push_back(it);
    if (by.size() <= 1) { share_hub_list(std::move(list), what); return; }  // un seul disque : pas de question
    std::vector<std::pair<std::string, std::function<void()>>> a;
    a.push_back({std::string(tr("Partout")) + "  ·  " + std::to_string(list.size()), [this, list, what] { share_hub_list(list, what); }});
    for (const auto& [disk, l] : by) a.push_back({disk + "  ·  " + std::to_string(l.size()), [this, l = l, what, disk = disk] { share_hub_list(l, what + " · " + disk); }});
    a.push_back({tr("Annuler"), [] {}});
    open_actions(trf("%s : où ?", what.c_str()), std::move(a));
}

void Ui::share_hub_list(std::vector<Item> list, const std::string& what) {
    std::sort(list.begin(), list.end(), [](const Item& x, const Item& y) { return x.title < y.title; });
    std::vector<std::pair<std::string, std::function<void()>>> a;
    for (const auto& it : list) {
        std::uint64_t b = 0;
        for (const auto& c : it.components) b += c.bytes;
        a.push_back({it.title + "  ·  " + item_disk_label(it) + (b ? "  ·  " + size_text(b) : std::string()), [this, it] { share_from_sheet(it); }});
    }
    a.push_back({tr("Annuler"), [] {}});
    open_actions(trf("%s · %d", what.c_str(), static_cast<int>(list.size())), std::move(a));
}

}  // namespace app
