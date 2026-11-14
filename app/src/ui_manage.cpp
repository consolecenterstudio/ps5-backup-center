// Sauvegardes › Gérer les backups : même écran que le web (engine/web/manage.js).
// Vue d'ensemble : une ligne par jeu (où sont ses copies, une phrase qui dit quoi faire, ce qu'on libère),
// vues Tous / Avec doublons / Pas encore comparés / Corbeille (Carré), « Tout comparer » (Triangle),
// jeux cochés (L2, Options : tout / rien) puis « Nettoyer la sélection… » (R2) → aperçu → corbeille.
// Un jeu (Croix) : ses copies (console d'abord), proposition du moteur cochée d'avance, protéger, comparer.
// Le résumé vient du moteur (GET /api/v2/backups/summary) : gardé en mémoire, relu à la demande et après
// chaque action ; tout passe par le chemin lent asynchrone (jamais d'attente dans le dessin).
#include <algorithm>
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

std::string places_text(const Json& g) {
    std::vector<std::string> seen;
    if (g["console"].is_object()) seen.push_back(tr("Console"));
    for (const auto& c : g["copies"].items()) {
        const std::string l = c["on_peer"].as_bool() ? c["peer_name"].as_string() : c["place_label"].as_string();  // l'autre PS5, une seule fois
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
    else if (now_ - mg_sum_at_ > 120000) mg_load_summary();  // relu en silence toutes les 2 minutes (l'ancien reste affiché)
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
        if (on) {
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
        s.text(font_, tx, yy + 40, summary_text(g), small, color::text, w - (tx - x) - 24);
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
    const std::string sq = mg_gsel_.empty() ? next : trf("Supprimer… (%d)", static_cast<int>(mg_gsel_.size()));
    draw_footer(s, {{0, tr("Ouvrir")}, {-1, tr("L2 : cocher")}, {-1, tr("R2 : doublons…")}, {-1, tr("Options : actions")}, {2, sq.c_str()}, {3, tr("Tout comparer")}});
}

// Corbeille de chaque disque USB et du NAS, par jour ; Croix remet la copie à sa place.
void Ui::draw_mg_trash(Surface& s, int x, int w, int y0) {
    struct Row { int kind; std::string text, place; const Json* item; };  // 0 emplacement, 1 jour, 2 copie
    std::vector<Row> rows;
    bool loading = false;
    for (const auto& p : snap_->places) {
        if ((p.type != "usb" && p.type != "ftp") || p.use == "sync") continue;  // NAS « Synchro seulement » : pas de corbeille ici
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
            std::string name;
            for (const auto& g : mg_sum_["games"].items()) {
                const std::string& tid = g["title_id"].as_string();
                if ((!tid.empty() && bundle.find(tid) != std::string::npos) || bundle.find(g["title"].as_string()) != std::string::npos) { name = g["title"].as_string(); break; }
            }
            const std::string file = bundle.substr(bundle.rfind('/') == std::string::npos ? 0 : bundle.rfind('/') + 1);
            s.text(font_, x + 24, yy + 8, name.empty() ? file : name, body, color::text, w - 300);
            s.text(font_, x + 24, yy + 42, bundle, small, color::muted, w - 300);
            const char* back = tr("Croix : remettre");
            s.text(font_, x + w - 24 - Surface::measure(back, small), yy + 24, back, small, f ? color::accent : color::muted);
        }
        yy += height(i);
    }
    if (rows.empty()) s.text(font_, x, y0 + 20, tr("Aucun disque USB ni NAS."), large, color::muted);
    s.text(font_, x, 946, tr("Les copies restent 30 jours dans « PBC Backup/Corbeille » de leur disque ou NAS."), small, color::muted, w);
    draw_footer(s, {{0, tr("Remettre")}, {3, tr("Actualiser")}, {-1, tr("Gauche / droite : endroits")}, {1, tr("Retour")}});
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
    s.text(font_, x + 92, 242, places_text(g), small, color::muted, w - 300);
    // Bandeau : la proposition (selon les cases cochées).
    int mine = 0;
    std::uint64_t bytes = 0;
    for (const auto& c : copies.items()) {
        if (c["other_account"].is_string()) continue;
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
            if (other) tag = tr("Autre compte");
            else if (c["needs_resign"].as_bool()) { tag = tr("Autre identifiant"); tone2 = color::accent; fill = rgb(124, 196, 255, 36); }
            else if (!c["complete"].as_bool(true)) { tag = tr("Incomplète"); tone2 = color::bad; fill = rgb(255, 123, 123, 36); }
            else if (g["full"].as_bool()) tag = nullptr;
            else if (!c["group"].is_string()) { tag = tr("À comparer"); tone2 = color::warn; fill = rgb(240, 184, 103, 36); }
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
            if (l2.empty()) l2 = c["save_mtime"].as_int() > 0 ? trf("partie enregistrée le %s", date_text(c["save_mtime"].as_int()).c_str()) : std::string(tr("contenu non comparé"));
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
    draw_footer(s, {{0, tr("Restaurer")}, {2, tr("Supprimer…")}, {-1, tr("L2 : cocher")}, {-1, tr("Options : actions")}, {1, tr("Retour")}});
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
        if ((k & key::circle) || ((k & key::cross) && mg_pv_btn_ == 1)) { mg_preview_ = false; toast(tr("Rien n'a été supprimé")); return; }
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
        if (k & key::square) {  // supprimer : les copies cochées, sinon celle sous le curseur → confirmation → corbeille
            const std::string gk = gkey(g);
            if (chosen(g, mg_csel_, mg_protected_).empty() || !mg_gsel_.count(gk)) {
                if (c["other_account"].is_string()) { toast(tr("Copie d'un autre compte : elle n'est jamais touchée")); return; }
                if (mg_protected_.count(ck)) { toast(tr("Copie protégée : retirez d'abord la protection")); return; }
                mg_csel_[gk] = {ck};
            }
            mg_gsel_.insert(gk);
            mg_pv_delete_ = true;
            mg_open_preview({gk});
            return;
        }
        if (k & key::r2) {
            const std::string gk = gkey(g);
            if (chosen(g, mg_csel_, mg_protected_).empty()) { toast(tr("Rien à mettre à la corbeille pour cet élément")); return; }
            mg_gsel_.insert(gk);
            mg_open_preview({gk});
            return;
        }
        if (k & key::cross) { mg_restore(g, c); return; }
        if (k & key::l2) {
            if (c["other_account"].is_string()) { toast(tr("Copie d'un autre compte : elle n'est jamais touchée")); return; }
            if (mg_protected_.count(ck)) { toast(tr("Copie protégée : retirez d'abord la protection")); return; }
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
            if ((p.type != "usb" && p.type != "ftp") || p.use == "sync") continue;  // NAS « Synchro seulement » : pas de corbeille ici
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
        return;
    }
    const auto rows = mg_rows();
    const int n = static_cast<int>(rows.size());
    if (k & key::circle) { mg_zone_ = 0; return; }
    if (k & key::up) { if (mg_row_ == 0) { mg_zone_ = 0; return; } mg_row_ = std::max(0, mg_row_ - 1); }
    if (k & key::down) mg_row_ = std::min(std::max(0, n - 1), mg_row_ + 1);
    if (k & key::square) {  // des éléments cochés : Carré = les supprimer (aperçu puis Carré maintenu 5 s) ; sinon, la vue suivante
        if (!mg_gsel_.empty()) { mg_pv_delete_ = true; mg_open_preview(mg_gsel_); return; }
        mg_view_ = (mg_view_ + 1) % 4; mg_row_ = 0; return;
    }
    if (k & key::triangle) {
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
        return;
    }
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
        if (p.type == "usb" || (p.type == "ftp" && p.use != "sync") || (p.type == "internal" && has_copies(p.id))) out.push_back(&p);  // NAS « Synchro seulement » : pas ici
    // Une carte par autre PS5 qui range des copies (ses disques et son NAS ensemble). Refaite seulement si elle change :
    // les pointeurs rendus restent valables pendant le dessin.
    std::vector<Place> chips;
    if (mg_sum_.is_object())
        for (const auto& g : mg_sum_["games"].items())
            for (const auto& c : g["copies"].items()) {
                if (!c["on_peer"].as_bool()) continue;
                const std::string id = "peer:" + c["peer"].as_string() + ":";
                auto it = std::find_if(chips.begin(), chips.end(), [&](const Place& x) { return x.id == id; });
                if (it == chips.end()) { Place p; p.id = id; p.type = "peerconsole"; p.label = c["peer_name"].as_string(); chips.push_back(p); it = chips.end() - 1; }
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
        acts.push_back({trf("Supprimer… (%d)", ng), [this] { mg_open_preview(mg_gsel_); }});
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
        acts.push_back({tr("Toutes les versions"), [this, gk] { mg_screen_ = 1; mg_game_ = gk; mg_crow_ = 0; mg_back_tab_ = -1; }});
    }
    acts.push_back({tr("Tout comparer"), [this] { const int z = mg_zone_; mg_zone_ = 1; input_manage(key::triangle); mg_zone_ = z; }});
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
    // Copie d'un autre identifiant (ancien compte, autre console) : la restauration la re-signe pour ce compte.
    acts.push_back({c["needs_resign"].as_bool() ? tr("Reprendre sur mon compte") : tr("Restaurer"), [this, gk, ck] {
        for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) for (const auto& y : x["copies"].items()) if (ckey(y) == ck) { mg_restore(x, y); return; }
    }});
    if (!ch.empty()) acts.push_back({trf("Supprimer… (%d)", static_cast<int>(ch.size())), [this, gk] { mg_pv_delete_ = true; mg_open_preview({gk}); }});
    else if (!other && !prot) acts.push_back({tr("Supprimer…"), [this, gk, ck] { mg_csel_[gk] = {ck}; mg_gsel_.insert(gk); mg_pv_delete_ = true; mg_open_preview({gk}); }});
    acts.push_back({prot ? tr("Retirer la protection") : tr("Protéger"), [this, gk, ck] {
        for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) for (const auto& y : x["copies"].items()) if (ckey(y) == ck) { mg_protect(y); return; }
    }});
    acts.push_back({g["full"].as_bool() ? tr("Ne plus sauvegarder…") : tr("Ignorer…"), [this, gk] { for (const auto& x : mg_sum_["games"].items()) if (gkey(x) == gk) { mg_open_ignore(x); break; } }});
    acts.push_back({ch.empty() ? tr("Tout cocher") : tr("Tout décocher"), [this, gk] {
        for (const auto& x : mg_sum_["games"].items()) {
            if (gkey(x) != gk) continue;
            std::set<std::string> set;
            if (chosen(x, mg_csel_, mg_protected_).empty())
                for (const auto& y : x["copies"].items()) if (!y["other_account"].is_string() && !mg_protected_.count(ckey(y))) set.insert(ckey(y));
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

void Ui::draw_backups_tab(Surface& s) {
    if (!overview_loaded_ || now_ - overview_loaded_ > 15000) load_overview();
    const int x = margin, w = 1920 - 2 * margin;
    if (mg_screen_ == 1) { s.text(font_, margin, 112, tr("Toutes les versions"), title, color::text); draw_manage(s, x, w); return; }
    {
        const int tw = s.text(font_, margin, 112, tr("Sauvegardes"), title, color::text);
        s.text(font_, margin + tw + 28, 140, tr("Des COPIES de vos parties : sur les disques de cette console, le NAS et les autres PS5 — pas la partie active."), small, color::muted, 1920 - 2 * margin - tw - 28 - 200);
    }
    // Cartes des endroits : Partout, chaque disque USB et NAS, Corbeille.
    const auto pl = mg_places();
    const int n = static_cast<int>(pl.size()) + 2;
    mg_place_ = std::clamp(mg_place_, 0, n - 1);
    // La Corbeille reste toujours visible, à droite ; les endroits défilent à sa gauche (› s'il y en a d'autres).
    const int cw = 300, ch = 112, gap = 18, cy = 196;
    const int places = n - 1, per = std::max(1, (w - cw) / (cw + gap));  // cartes d'endroit qui tiennent à gauche de la Corbeille
    const int sel_place = std::min(mg_place_, places - 1);
    const int first = std::clamp(sel_place - per + 1, 0, std::max(0, places - per));
    std::vector<int> shown;
    for (int i = first; i < places && i < first + per; ++i) shown.push_back(i);
    shown.push_back(n - 1);  // Corbeille
    for (std::size_t si = 0; si < shown.size(); ++si) {
        const int i = shown[si];
        const int cx = i == n - 1 ? x + w - cw : x + static_cast<int>(si) * (cw + gap);
        const bool sel = i == mg_place_, focus = sel && mg_zone_ == 0;
        if (focus) for (int g = 3; g >= 1; --g) s.rect(cx - g * 5, cy - g * 5, cw + g * 10, ch + g * 10, rgb(80, 150, 255, 20), 22 + g * 5);
        s.rect(cx, cy, cw, ch, sel ? color::panel2 : color::panel, 20);
        s.outline(cx, cy, cw, ch, focus ? color::accent : sel ? color::accent2 : color::line, 20, focus ? 4 : 2);
        std::string name, sub;
        int dev = -1;
        if (i == 0) { name = tr("Partout"); sub = tr("tous les disques et le NAS"); dev = dev_consoles; }
        else if (i == n - 1) { name = tr("Corbeille"); sub = tr("30 jours pour changer d'avis"); }
        else {
            const Place& p = *pl[static_cast<std::size_t>(i - 1)];
            name = p.type == "internal" ? std::string(tr("Interne")) : p.label;
            dev = p.type == "ftp" ? (p.protocol == "s3" || p.protocol == "gdrive" || p.protocol == "onedrive" || p.protocol == "dropbox" ? dev_cloud : dev_nas) : p.type == "peerconsole" || p.type == "internal" ? dev_internal : dev_usb;  // PS5 : image de la PS5
            sub = p.type == "ftp" ? std::string(tr("réseau")) : p.type == "peerconsole" ? trf("autre PS5 · %d copie(s)", static_cast<int>(p.total))
                                                                                         : trf("%s libres", size_text(p.free).c_str());
        }
        int tx = cx + 20;
        if (dev >= 0 && devices_[dev].valid()) { s.image(devices_[dev], cx + 12, cy + 12, 88, 88); tx = cx + 112; }
        else if (i == n - 1) {  // corbeille dessinée : un seau et son couvercle
            s.rect(cx + 34, cy + 30, 52, 8, color::muted, 4);
            s.rect(cx + 50, cy + 24, 20, 8, color::muted, 4);
            s.rect(cx + 38, cy + 42, 44, 48, color::muted, 8);
            for (int k2 = 0; k2 < 3; ++k2) s.rect(cx + 48 + k2 * 10, cy + 50, 4, 32, color::panel, 2);
            tx = cx + 112;
        }
        s.text(font_, tx, cy + 24, name, body, color::text, cx + cw - tx - 14);
        s.text(font_, tx, cy + 62, sub, small, color::muted, cx + cw - tx - 14);
    }
    if (first + per < places) s.text(font_, x + per * (cw + gap) - gap + 10, cy + ch / 2 - 18, "›", large, color::muted);  // d'autres endroits : juste après la dernière carte d'endroit
    if (mg_in_trash()) {
        draw_mg_trash(s, x, w, 334);
        return;
    }
    draw_manage(s, x, w);
}

void Ui::input_backups_tab(std::uint32_t k) {
    if (mg_screen_ == 1 || mg_zone_ == 1) { input_manage(k); return; }
    const int n = static_cast<int>(mg_places().size()) + 2;
    if (k & key::left) { mg_place_ = std::max(0, mg_place_ - 1); mg_row_ = mg_trow_ = 0; }
    if (k & key::right) { mg_place_ = std::min(n - 1, mg_place_ + 1); mg_row_ = mg_trow_ = 0; }
    if ((k & key::left) || (k & key::right)) { if (mg_in_trash()) mg_trash_.clear(); return; }
    if (k & (key::down | key::cross)) { mg_zone_ = 1; return; }
    if (mg_in_trash()) { if (k & key::triangle) mg_trash_.clear(); return; }
    if (k & key::square) { mg_view_ = (mg_view_ + 1) % 4; mg_row_ = 0; return; }
    if (k & key::options) {  // sur la carte d'une autre PS5 : synchroniser les copies avec elle
        const auto pl = mg_places();
        if (mg_place_ >= 1 && mg_place_ <= static_cast<int>(pl.size()) && pl[static_cast<std::size_t>(mg_place_ - 1)]->type == "peerconsole") {
            const Place& p = *pl[static_cast<std::size_t>(mg_place_ - 1)];
            sync_copies_dialog(p.id.substr(5, p.id.size() - 6), p.label, false);
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
                const auto devs = sy_devices();
                for (std::size_t i = 0; i < devs.size(); ++i) if (devs[i].id == peer) sn_card_ = static_cast<int>(i);
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

}  // namespace app
