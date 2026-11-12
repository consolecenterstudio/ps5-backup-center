// Synchro des PARTIES ACTIVES entre deux PS5 jumelées :
//   - pour chaque jeu choisi (liste propre à cette synchro, séparée des sauvegardes), on compare l'heure à laquelle le
//     jeu a écrit sa partie sur chaque console (même personne PSN des deux côtés) : la plus récente gagne ;
//   - AVANT d'écraser une partie, une vraie sauvegarde de celle-ci est faite (visible dans Sauvegardes) — c'est la
//     version gardée ; la restauration garde en plus sa copie de sécurité brute ;
//   - jamais pendant que le jeu tourne, sur l'une ou l'autre console ; toujours déclenché par l'utilisateur ;
//   - tout passe par les briques existantes : backup (sauvegarde), send_item (copie vérifiée + restauration là-bas),
//     /link/send-current-save (l'autre console envoie sa partie ici).
#include <chrono>
#include <cstdlib>
#include <set>
#include <thread>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "link/link.hpp"
#include "store/peer.hpp"
#include "store/remotes.hpp"
#include "tasks/tasks.hpp"
#include "sync_parties.hpp"

namespace pbc {

namespace {
std::string norm_id(std::string id) {  // « 0x14B0846B… » → « 14b0846b… »
    if (id.rfind("0x", 0) == 0 || id.rfind("0X", 0) == 0) id = id.substr(2);
    for (auto& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto nz = id.find_first_not_of('0');
    return nz == std::string::npos ? std::string() : id.substr(nz);
}
}  // namespace

// Règle de décision (testée seule, tests/test_main.cpp). Journal connu : on compare à ce qui a été noté à la fin de
// la dernière synchro (sinon une partie restaurée, datée du moment de la restauration, repartirait en boucle).
// Sinon : règle de la reprise d'une partie (takeover) — pas de partie d'un côté, plus de temps de jeu, plus récente.
PartiesDecision parties_decide(const PartiesSide& h, const PartiesSide& t, bool known, std::int64_t rec_here, std::int64_t rec_there, const std::string& resolve) {
    if (known && h.at && t.at) {
        const bool mh = std::llabs(h.at - rec_here) > 60, mt = std::llabs(t.at - rec_there) > 60;
        if (!mh && !mt) return {"same", "synced"};
        if (mh && !mt) return {"here_newer", "played_here"};
        if (mt && !mh) return {"there_newer", "played_there"};
        return {resolve == "here" ? "here_newer" : resolve == "there" ? "there_newer" : "conflict", "both_played"};
    }
    if (!h.at) return {"only_there", "no_save"};
    if (!t.at) return {"only_here", "no_save"};
    if (h.play >= 0 && t.play >= 0 && h.play != t.play) return {h.play > t.play ? "here_newer" : "there_newer", "more_play_time"};
    if (h.at - t.at > 60) return {"here_newer", "newer"};
    if (t.at - h.at > 60) return {"there_newer", "newer"};
    return {"same", ""};
}

// Journal des parties déjà synchronisées (« parties-sync.json ») : sans lui, la partie restaurée prend une nouvelle date
// sur la console qui la reçoit, paraît plus récente au tour suivant et repart dans l'autre sens — en boucle.
Json Engine::parties_journal(const std::string& peer) {
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "parties-sync.json")), text) || !Json::parse(text, all) || !all.is_object()) return Json::object();
    return all[peer].is_object() ? all[peer] : Json::object();
}

void Engine::parties_remember(const std::string& peer, const std::string& key, std::int64_t here_at, std::int64_t there_at) {
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "parties-sync.json")), text) || !Json::parse(text, all) || !all.is_object()) all = Json::object();
    Json p = all[peer].is_object() ? all[peer] : Json::object();
    Json r = Json::object();
    r.set("here_at", here_at); r.set("there_at", there_at); r.set("at", now_unix());
    p.set(key, r);
    all.set(peer, p);
    fs::write_atomic(fs::host(fs::join(dir_, "parties-sync.json")), all.dump(true));
}

void Engine::config_remember(const std::string& peer, const std::string& tid, const std::string& here_hash, const std::string& there_hash) {
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "parties-sync.json")), text) || !Json::parse(text, all) || !all.is_object()) all = Json::object();
    Json p = all[peer].is_object() ? all[peer] : Json::object();
    Json r = Json::object();
    r.set("here_hash", here_hash); r.set("there_hash", there_hash); r.set("at", now_unix());
    p.set("cfg|" + tid, r);
    all.set(peer, p);
    fs::write_atomic(fs::host(fs::join(dir_, "parties-sync.json")), all.dump(true));
}

Json Engine::parties_plan(const std::string& peer) {
    if (peer.rfind("remote:", 0) == 0) return nas_parties_plan(peer);  // un NAS : le dépôt de parties
    Json out = Json::object();
    out.set("peer", peer);
    if (!link_) { out.set("error", "liaison indisponible"); return out; }
    Json there;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/saves/overview", Json(), there, err) || !there.is_object()) { out.set("error", "autre PS5 injoignable : " + err); return out; }
    const Json here = saves_overview();
    // Comptes de la même personne présents sur les deux consoles (numéro PSN) : ses parties se comparent.
    std::map<std::string, std::string> here_aid, there_aid;  // clé de compte → numéro PSN
    std::map<std::string, std::string> here_user, there_user; // clé de compte → user_id
    std::set<std::string> here_ids, there_ids;
    for (const auto& a : here["accounts"].items()) if (a["on_console"].as_bool() && !norm_id(a["account_id"].as_string()).empty()) {
        here_aid[a["key"].as_string()] = norm_id(a["account_id"].as_string()); here_user[a["key"].as_string()] = a["user_id"].as_string(); here_ids.insert(norm_id(a["account_id"].as_string())); }
    for (const auto& a : there["accounts"].items()) if (a["on_console"].as_bool() && !norm_id(a["account_id"].as_string()).empty()) {
        there_aid[a["key"].as_string()] = norm_id(a["account_id"].as_string()); there_user[a["key"].as_string()] = a["user_id"].as_string(); there_ids.insert(norm_id(a["account_id"].as_string())); }
    // Compte choisi de chaque côté (réglage de cette synchro) ; sinon, la même personne PSN des deux côtés.
    const Json prefs0 = sync_prefs(peer);
    const std::string pick_here = prefs0["parties_here_account"].as_string(), pick_there = prefs0["parties_there_account"].as_string();
    const bool picked = !pick_here.empty() && !pick_there.empty() && here_user.count(pick_here) && there_user.count(pick_there);
    {   // comptes proposés (pour le choix à l'écran)
        Json ha = Json::array(), ta = Json::array();
        for (const auto& a : here["accounts"].items()) if (a["on_console"].as_bool() && a["platform"].as_string() == "PS5") { Json x = Json::object(); x.set("key", a["key"]); x.set("name", a["name"]); x.set("user_id", a["user_id"]); ha.push(x); }
        for (const auto& a : there["accounts"].items()) if (a["on_console"].as_bool() && a["platform"].as_string() == "PS5") { Json x = Json::object(); x.set("key", a["key"]); x.set("name", a["name"]); x.set("user_id", a["user_id"]); ta.push(x); }
        out.set("here_accounts", ha); out.set("there_accounts", ta);
        out.set("here_account", picked ? pick_here : std::string()); out.set("there_account", picked ? pick_there : std::string());
        out.set("account_mode", picked ? "chosen" : "same_psn");
    }
    struct Side { std::int64_t at = 0, play = -1; std::string acc, user, item; };
    struct Row { std::string title, title_id; Side h, t; };
    std::map<std::string, Row> rows;  // numéro PSN|jeu
    auto take = [&](const Json& ov, const std::map<std::string, std::string>& aid, const std::map<std::string, std::string>& users, const std::set<std::string>& other, bool is_here) {
        for (const auto& e : ov["entries"].items()) {
            if (!e["console"].is_object() || e["status"].as_string() == "system") continue;  // sauvegardes système (LAPY, NPXS) : jamais
            std::string pair;
            if (picked) {  // compte choisi : seulement lui, de chaque côté
                if (e["account"].as_string() != (is_here ? pick_here : pick_there)) continue;
                pair = "choisi";
            } else {
                const auto a = aid.find(e["account"].as_string());
                if (a == aid.end() || !other.count(a->second)) continue;
                pair = a->second;
            }
            Row& r = rows[pair + "|" + e["title_id"].as_string()];
            r.title_id = e["title_id"].as_string();
            if (r.title.empty()) r.title = e["title"].as_string();
            Side& s = is_here ? r.h : r.t;
            s.at = e["console"]["mtime"].as_int();  // heure à laquelle le jeu a écrit sa partie
            s.play = e["console"]["progress"]["play_time_seconds"].as_int(-1);  // temps de jeu noté dans la partie (si le jeu le dit)
            s.acc = e["account"].as_string(); s.user = users.at(e["account"].as_string()); s.item = e["item"].as_string();
        }
    };
    take(here, here_aid, here_user, there_ids, true);
    take(there, there_aid, there_user, here_ids, false);
    const Json& prefs = prefs0;
    const Json journal = parties_journal(peer);
    std::set<std::string> followed;
    for (const auto& t : prefs["parties_games"].items()) followed.insert(t.as_string());
    // Jeu lancé (ici ou là-bas) : rien ne se fait pour lui.
    std::string run_here, run_there;
    { const Json fg = foreground_app(); if (fg["game"].as_bool()) run_here = fg["title_id"].as_string(); }
    { Json fg; std::string e2; if (link_->call(peer, "GET", "/api/v2/app/foreground", Json(), fg, e2) && fg["game"].as_bool()) run_there = fg["title_id"].as_string(); }
    Json titles = Json::array();
    int need = 0, chosen = 0;
    for (const auto& [k, r] : rows) {
        Json t = Json::object();
        t.set("title_id", r.title_id); t.set("title", r.title);
        t.set("here_at", r.h.at); t.set("there_at", r.t.at);
        t.set("here_account", r.h.acc); t.set("there_account", r.t.acc);
        t.set("here_user", r.h.user); t.set("there_user", r.t.user);
        t.set("here_item", r.h.item); t.set("there_item", r.t.item);
        // Même règle que la reprise d'une partie (takeover) : pas de partie d'un côté → l'autre ; plus de temps de jeu
        // → elle ; sinon la plus récente, à plus d'une minute près ; sinon c'est la même : on ne touche à rien.
        const Json& rec = journal[r.h.acc + ">" + r.t.acc + "|" + r.title_id];
        const PartiesDecision dec = parties_decide({r.h.at, r.h.play}, {r.t.at, r.t.play}, rec.is_object(), rec["here_at"].as_int(), rec["there_at"].as_int(),
                                                   prefs["parties_resolve"][r.title_id].as_string());
        const std::string st = dec.state, why = dec.reason;
        t.set("state", st);
        if (!why.empty()) t.set("reason", why);
        if (why == "both_played") t.set("conflict", true);
        t.set("key", r.h.acc + ">" + r.t.acc + "|" + r.title_id);
        if (r.h.play >= 0) t.set("here_play", r.h.play);
        if (r.t.play >= 0) t.set("there_play", r.t.play);
        const bool f = followed.count(r.title_id) > 0;
        t.set("followed", f);
        const bool running = (!run_here.empty() && run_here == r.title_id) || (!run_there.empty() && run_there == r.title_id);
        t.set("running", running);
        chosen += f;
        need += f && st != "same" && st != "conflict";
        titles.push(t);
    }
    out.set("titles", titles);
    out.set("chosen", chosen);
    out.set("need", need);
    out.set("running_here", run_here);
    out.set("running_there", run_there);
    return out;
}

Json Engine::set_parties_prefs(const Json& body, std::string& error) {
    const std::string peer = body["peer"].as_string();
    if (peer.empty()) { error = "PS5 inconnue"; return Json(); }
    Json p = sync_prefs(peer);
    const bool apps = body["section"].as_string() == "apps";
    const char* list_key = apps ? "apps_games" : "parties_games";  // une liste par section (parties, applis)
    std::set<std::string> games;
    for (const auto& t : p[list_key].items()) games.insert(t.as_string());
    for (const auto& [tid, on] : body["follow"].members()) { if (on.as_bool()) games.insert(tid); else games.erase(tid); }
    for (const char* k : {"apps_mode", "apps_resolve", "config_resolve"}) if (body[k].is_object()) {  // applis : mode, conflits tranchés
        Json m = p[k].is_object() ? p[k] : Json::object(), kept = Json::object();
        for (const auto& [tid, v] : body[k].members()) m.set(tid, v);
        for (const auto& [tid, v] : m.members()) if (!v.as_string().empty()) kept.set(tid, v);
        p.set(k, kept);
    }
    if (body["resolve"].is_object()) {  // conflit tranché par l'utilisateur : quelle partie garder
        Json rs = p["parties_resolve"].is_object() ? p["parties_resolve"] : Json::object(), kept = Json::object();
        for (const auto& [tid, v] : body["resolve"].members()) rs.set(tid, v);
        for (const auto& [tid, v] : rs.members()) if (!v.as_string().empty()) kept.set(tid, v);
        p.set("parties_resolve", kept);
    }
    if (body.has("here_account") || body.has("there_account")) {  // compte choisi de chaque côté ("" = même personne PSN)
        p.set("parties_here_account", body["here_account"].as_string());
        p.set("parties_there_account", body["there_account"].as_string());
    }
    if (body.has("follow_all")) {
        const Json plan = apps ? apps_plan(peer) : parties_plan(peer);
        games.clear();
        if (body["follow_all"].as_bool()) for (const auto& t : plan["titles"].items()) games.insert(t["title_id"].as_string());
    }
    Json list = Json::array();
    for (const auto& g : games) list.push(g);
    p.set(list_key, list);
    std::lock_guard<std::mutex> l(sync_mu_);
    std::string text;
    Json all;
    if (!fs::read(fs::host(fs::join(dir_, "sync-prefs.json")), text) || !Json::parse(text, all) || !all.is_object()) all = Json::object();
    all.set(peer, p);
    fs::write_atomic(fs::host(fs::join(dir_, "sync-prefs.json")), all.dump(true));
    return p;
}

Json Engine::parties_status() { std::lock_guard<std::mutex> l(sync_mu_); return parties_status_; }

Json Engine::parties_start(const Json& body, std::string& error) {
    if (body["peer"].as_string().rfind("remote:", 0) == 0) return nas_parties_start(body, error);
    if (!link_) { error = "liaison indisponible"; return Json(); }
    const std::string peer = body["peer"].as_string();
    const Json plan = parties_plan(peer);
    if (plan.has("error")) { error = plan["error"].as_string(); return Json(); }
    std::set<std::string> only;
    for (const auto& t : body["title_ids"].items()) only.insert(t.as_string());
    Json jobs = Json::array();
    for (const auto& t : plan["titles"].items()) {
        // « one » : un seul jeu demandé depuis sa fiche (Bibliothèque de l'autre PS5), suivi ou non dans Synchro.
        const bool asked = body["one"].as_bool() && only.count(t["title_id"].as_string());
        if ((!t["followed"].as_bool() && !asked) || t["state"].as_string() == "same" || t["state"].as_string() == "conflict") continue;
        if (!only.empty() && !only.count(t["title_id"].as_string())) continue;
        if (t["running"].as_bool()) { error = "« " + t["title"].as_string() + " » est lancé : fermez le jeu d'abord (rien n'a été modifié)"; return Json(); }
        jobs.push(t);
    }
    if (jobs.size() == 0) { error = "rien à mettre à niveau"; return Json(); }
    if (parties_running_.exchange(true)) { error = "une mise à niveau des parties est déjà en cours"; return Json(); }
    // Disque de l'autre PS5 pour la partie qui part (le plus libre), et d'ici pour celle qui arrive.
    std::string peer_place, here_place;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        std::int64_t best = -1;
        for (const auto& p : peer_places_.items()) if (p["peer"].as_string() == peer && p["free"].as_int() > best) { best = p["free"].as_int(); peer_place = p["id"].as_string(); }
    }
    { Place hp; std::string e2; if (resolve_place("auto", hp, e2)) here_place = hp.id; }
    if (!body["peer_place"].as_string().empty()) peer_place = body["peer_place"].as_string();
    if (!body["here_place"].as_string().empty()) here_place = body["here_place"].as_string();
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", peer);
        parties_status_.set("total", static_cast<std::int64_t>(jobs.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, peer, jobs, peer_place, here_place] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        // Attentes (30 min au plus) : tâche d'ici, tâche de l'autre PS5, envoi.
        auto wait_local = [this](const std::string& id, std::string& err) {
            for (int i = 0; i < 900; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (tasks::Task t; queue_->get(id, t)) {
                    if (t.state == tasks::State::done) return true;
                    if (t.state == tasks::State::failed || t.state == tasks::State::cancelled) { err = t.error.empty() ? "tâche interrompue" : t.error; return false; }
                }
            }
            err = "trop long";
            return false;
        };
        auto wait_peer = [this, peer](const std::string& id, std::string& err) {
            for (int i = 0; i < 900; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                Json ts; std::string e2;
                if (!link_->call(peer, "GET", "/api/v2/tasks?brief=1", Json(), ts, e2)) continue;
                for (const auto& t : ts.items()) if (t["id"].as_string() == id) {
                    const std::string st = t["state"].as_string();
                    if (st == "done") return true;
                    if (st == "failed" || st == "cancelled") { err = t["error"].as_string().empty() ? "tâche interrompue là-bas" : t["error"].as_string(); return false; }
                }
            }
            err = "trop long";
            return false;
        };
        int done = 0;
        for (const auto& j : jobs.items()) {
            const std::string title = j["title"].as_string(), st = j["state"].as_string();
            const bool up = st == "here_newer" || st == "only_here";  // la partie d'ici part là-bas
            set("current", title);
            std::string err;
            bool ok = true;
            if (up) {
                if (j["there_at"].as_int()) {  // 1. version gardée : la partie de là-bas est sauvegardée là-bas avant d'être remplacée
                    set("step", "version");
                    Json b = Json::object(), o;
                    b.set("type", "backup"); b.set("item", j["there_item"]); b.set("account", j["there_account"]);
                    Json parts = Json::array(); parts.push("save"); b.set("parts", parts);
                    ok = link_->call(peer, "POST", "/api/v2/tasks", b, o, err) && wait_peer(o["id"].as_string(), err);
                    if (!ok) err = "sauvegarde de la partie remplacée impossible, rien n'a été modifié : " + err;
                }
                if (ok) {  // 2. la partie d'ici : copie vérifiée sur un disque de là-bas, puis restaurée sur le compte de là-bas
                    set("step", "copy");
                    std::string sid;
                    ok = !peer_place.empty() && send_item("peer:" + peer + ":" + peer_place, j["here_item"].as_string(), true, "", j["here_account"].as_string(), j["there_user"].as_string(), sid, err);
                    if (peer_place.empty()) err = "aucun disque de l'autre PS5";
                    std::string remote;
                    for (int i = 0; ok && i < 900; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        const Json s = link_sends()[sid];
                        if (s["state"].as_string() == "failed") { ok = false; err = s["error"].as_string(); }
                        if (s["state"].as_string() == "done") { remote = s["remote_task"].as_string(); break; }
                    }
                    if (ok) { set("step", "restore"); ok = !remote.empty() && wait_peer(remote, err); }
                }
            } else {
                if (j["here_at"].as_int()) {  // 1. version gardée ici
                    set("step", "version");
                    std::string bid;
                    ok = backup(j["here_item"].as_string(), {Part::save}, "", bid, err, j["here_account"].as_string()) && wait_local(bid, err);
                    if (!ok) err = "sauvegarde de la partie remplacée impossible, rien n'a été modifié : " + err;
                }
                if (ok) {  // 2. l'autre PS5 envoie sa partie ici (copie vérifiée sur un disque d'ici) et la fait restaurer ici
                    set("step", "copy");
                    Json b = Json::object(), o;
                    b.set("place", "peer:" + link_id_ + ":" + here_place); b.set("item", j["there_item"]);
                    b.set("account", j["there_account"]); b.set("user_id", j["here_user"]);
                    ok = !here_place.empty() && link_->call(peer, "POST", "/api/v2/link/send-current-save", b, o, err);
                    const std::string sid = o["send"].as_string();
                    std::string local;
                    for (int i = 0; ok && i < 900; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(2));
                        Json sends; std::string e2;
                        if (!link_->call(peer, "GET", "/api/v2/link/sends", Json(), sends, e2)) continue;
                        const Json s = sends[sid];
                        if (s["state"].as_string() == "failed") { ok = false; err = s["error"].as_string(); }
                        if (s["state"].as_string() == "done") { local = s["remote_task"].as_string(); break; }
                    }
                    if (ok) { set("step", "restore"); ok = !local.empty() && wait_local(local, err); }
                }
            }
            if (ok) {
                // La partie restaurée a une nouvelle date : on la relit (jusqu'à 2 min), puis on note les deux côtés.
                set("step", "journal");
                const std::string tid = j["title_id"].as_string();
                const std::int64_t before = up ? j["there_at"].as_int() : j["here_at"].as_int();
                std::int64_t here_now = j["here_at"].as_int(), there_now = j["there_at"].as_int();
                for (int i = 0; i < 40; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    Json ov;
                    std::string e2;
                    if (up) { if (!link_->call(peer, "GET", "/api/v2/saves/overview", Json(), ov, e2)) continue; }
                    else ov = saves_overview();
                    std::int64_t now_at = 0;
                    const std::string acc = up ? j["there_account"].as_string() : j["here_account"].as_string();
                    for (const auto& e : ov["entries"].items())
                        if (e["title_id"].as_string() == tid && (acc.empty() || e["account"].as_string() == acc) && e["console"].is_object()) now_at = e["console"]["mtime"].as_int();
                    if (now_at && now_at != before) { (up ? there_now : here_now) = now_at; break; }
                }
                parties_remember(peer, j["key"].as_string(), here_now, there_now);
                {   // tranché une fois : le choix ne sert plus
                    Json pr = sync_prefs(peer);
                    if (pr["parties_resolve"].has(tid)) { Json b = Json::object(), rs = Json::object(); rs.set(tid, ""); b.set("peer", peer); b.set("resolve", rs); std::string e3; set_parties_prefs(b, e3); }
                }
            }
            Json r = Json::object();
            r.set("title", title); r.set("direction", up ? "push" : "pull"); r.set("ok", ok);
            if (!ok) { r.set("error", err); Json e = Json::object(); e.set("title", title); e.set("error", err); push("errors", e); }
            push("results", r);
            set("done", static_cast<std::int64_t>(++done));
            log("parties: %s %s %s", title.c_str(), up ? "→" : "←", ok ? "ok" : err.c_str());
        }
        {
            std::lock_guard<std::mutex> l(sync_mu_);
            parties_status_.set("state", parties_status_["errors"].size() ? "failed" : "done");
            parties_status_.set("finished", now_unix());
            parties_status_.set("current", "");
        }
        parties_running_ = false;
        ++revision_;
    }).detach();
    return parties_status();
}

}  // namespace pbc

namespace pbc {

// ---- Synchro des APPLIS (mêmes applis, même version sur les deux PS5) ----------------------------------------------
// Même règle que les mises à jour de l'app : même version → rien ; plus récente d'un côté → elle remplace l'ancienne de
// l'autre côté (jamais en double, jamais de retour en arrière) ; absente d'un côté → installée si l'appli est choisie.
// Transport et installation : les briques des jeux (send-game, install_backup), droits d'exécution relus.
Json Engine::apps_plan(const std::string& peer) {
    if (peer.rfind("remote:", 0) == 0) return nas_apps_plan(peer);  // un NAS : le dépôt d'applis
    Json out = Json::object();
    out.set("peer", peer);
    if (!link_) { out.set("error", "liaison indisponible"); return out; }
    Json there;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/apps/versions", Json(), there, err, 180) || !there.is_array()) { out.set("error", "autre PS5 injoignable : " + err); return out; }
    const Json here = apps_versions();
    std::map<std::string, std::pair<Json, Json>> by;
    for (const auto& a : here.items()) by[a["title_id"].as_string()].first = a;
    for (const auto& a : there.items()) by[a["title_id"].as_string()].second = a;
    const Json prefs = sync_prefs(peer);
    const Json journal = parties_journal(peer);  // réglages déjà synchronisés (empreintes des deux côtés)
    std::set<std::string> followed;
    for (const auto& t : prefs["apps_games"].items()) followed.insert(t.as_string());
    Json titles = Json::array();
    int need = 0, chosen = 0;
    for (const auto& [tid, p] : by) {
        const Json &h = p.first, &t = p.second;
        Json x = Json::object();
        x.set("title_id", tid);
        x.set("title", h.is_object() ? h["title"] : t["title"]);
        x.set("here_version", h["version"]); x.set("there_version", t["version"]);
        x.set("here_key", h["key"]); x.set("there_key", t["key"]);
        x.set("here_place", h["place"]); x.set("there_place", t["place"]);
        std::string st;
        if (!h.is_object()) st = "only_there";
        else if (!t.is_object()) st = "only_here";
        else if (h["version"].as_string().empty() || t["version"].as_string().empty()) st = "unknown";
        else { const int c = compare_versions(h["version"].as_string(), t["version"].as_string()); st = c > 0 ? "here_newer" : c < 0 ? "there_newer" : "same"; }
        // Même version mais fichiers différents (homebrew recompilé sans changer de numéro) : on ne choisit pas, à trancher.
        if (st == "same" && !h["hash"].as_string().empty() && !t["hash"].as_string().empty() && h["hash"].as_string() != t["hash"].as_string()) {
            const std::string pick = prefs["apps_resolve"][tid].as_string();
            st = pick == "here" ? "here_newer" : pick == "there" ? "there_newer" : "conflict";
            x.set("conflict", true);
            x.set("reason", "same_version_diff_files");
        }
        // La synchro installe des applis en dossier ; une appli en image (.ffpkg, .exfat) se met à jour par Transferts.
        const bool movable = (st == "here_newer" || st == "only_here") ? h["folder"].as_bool() : (st == "there_newer" || st == "only_there") ? t["folder"].as_bool() : true;
        if (!movable) st = "manual";
        x.set("state", st);
        const bool f = followed.count(tid) > 0;
        x.set("followed", f);
        // Mode : « app » (appli seule, par défaut), « app_config » (appli + réglages), « config » (réglages seuls).
        const std::string mode = prefs["apps_mode"][tid].as_string().empty() ? std::string("app") : prefs["apps_mode"][tid].as_string();
        x.set("mode", mode);
        // Réglages : identiques, d'un côté plus récents, ou à trancher — avec le journal (jamais en boucle).
        std::string cst;
        const Json &hc = h["config"], &tc = t["config"];
        if (!h.is_object() || !t.is_object()) cst = "";
        else if (!hc.is_object() || !tc.is_object()) cst = "undefined";  // pas de dossier de réglages connu d'un côté
        else if (hc["hash"].as_string() == tc["hash"].as_string()) cst = "same";
        else {
            const Json& rec = journal["cfg|" + tid];
            if (rec.is_object()) {
                const bool mh = hc["hash"].as_string() != rec["here_hash"].as_string(), mt = tc["hash"].as_string() != rec["there_hash"].as_string();
                cst = mh && !mt ? "here_newer" : mt && !mh ? "there_newer" : "conflict";
            } else {
                const std::int64_t d = hc["mtime"].as_int() - tc["mtime"].as_int();
                cst = d > 60 ? "here_newer" : d < -60 ? "there_newer" : "conflict";
            }
            if (cst == "conflict") { const std::string pick = prefs["config_resolve"][tid].as_string(); if (!pick.empty()) cst = pick == "here" ? "here_newer" : "there_newer"; }
            if (cst != "conflict" && st != "same" && mode == "config") cst = "version_differs";  // réglages seuls : seulement entre la même version
        }
        if (!cst.empty()) { x.set("config_state", cst); x.set("here_config", hc); x.set("there_config", tc); }
        const bool app_moves = mode != "config" && st != "same" && st != "unknown" && st != "manual" && st != "conflict";
        const bool cfg_moves = mode != "app" && (cst == "here_newer" || cst == "there_newer");
        x.set("app_moves", app_moves); x.set("config_moves", cfg_moves);
        chosen += f;
        need += f && (app_moves || cfg_moves);
        titles.push(x);
    }
    out.set("titles", titles);
    out.set("chosen", chosen);
    out.set("need", need);
    return out;
}

Json Engine::apps_start(const Json& body, std::string& error) {
    if (body["peer"].as_string().rfind("remote:", 0) == 0) return nas_apps_start(body, error);
    if (!link_) { error = "liaison indisponible"; return Json(); }
    const std::string peer = body["peer"].as_string();
    const Json plan = apps_plan(peer);
    if (plan.has("error")) { error = plan["error"].as_string(); return Json(); }
    Json jobs = Json::array();
    for (const auto& t : plan["titles"].items())
        if (t["followed"].as_bool() && (t["app_moves"].as_bool() || t["config_moves"].as_bool())) jobs.push(t);
    if (jobs.size() == 0) { error = "rien à mettre à niveau"; return Json(); }
    if (parties_running_.exchange(true)) { error = "une mise à niveau est déjà en cours"; return Json(); }
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", peer); parties_status_.set("section", "apps");
        parties_status_.set("total", static_cast<std::int64_t>(jobs.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, peer, jobs] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        auto wait_local = [this](const std::string& id, std::string& err) {
            for (int i = 0; i < 1800; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (tasks::Task t; queue_->get(id, t)) {
                    if (t.state == tasks::State::done) return true;
                    if (t.state == tasks::State::failed || t.state == tasks::State::cancelled) { err = t.error.empty() ? "installation interrompue" : t.error; return false; }
                }
            }
            err = "trop long";
            return false;
        };
        auto wait_peer = [this, peer](const std::string& id, std::string& err) {
            for (int i = 0; i < 1800; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                Json ts; std::string e2;
                if (!link_->call(peer, "GET", "/api/v2/tasks?brief=1", Json(), ts, e2)) continue;
                for (const auto& t : ts.items()) if (t["id"].as_string() == id) {
                    const std::string st = t["state"].as_string();
                    if (st == "done") return true;
                    if (st == "failed" || st == "cancelled") { err = t["error"].as_string().empty() ? "installation interrompue là-bas" : t["error"].as_string(); return false; }
                }
            }
            err = "trop long";
            return false;
        };
        int done = 0;
        for (const auto& j : jobs.items()) {
            const std::string title = j["title"].as_string(), st = j["state"].as_string();
            const bool up = st == "here_newer" || st == "only_here";
            set("current", title);
            std::string err;
            bool ok = true;
            if (!j["app_moves"].as_bool()) {}  // réglages seuls : plus bas
            else if (up) {  // l'appli d'ici part là-bas et remplace l'ancienne version (même disque que l'ancienne)
                set("step", "copy");
                const std::string place = j["there_place"].as_string().empty() ? std::string("internal") : j["there_place"].as_string();
                std::string sid;
                ok = send_item("peer:" + peer + ":" + place, j["here_key"].as_string(), false, "keep", "", "", sid, err, false, true);
                Json remote;
                for (int i = 0; ok && i < 3600; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    const Json s = link_sends()[sid];
                    if (s["state"].as_string() == "failed") { ok = false; err = s["error"].as_string(); }
                    if (s["state"].as_string() == "done") { remote = s["remote_tasks"]; break; }
                }
                set("step", "install");
                for (const auto& tk : remote.items()) if (ok) ok = wait_peer(tk.as_string(), err);
            } else {  // l'autre PS5 envoie son appli ici, qui remplace l'ancienne version d'ici
                set("step", "copy");
                const std::string place = j["here_place"].as_string().empty() ? std::string("internal") : j["here_place"].as_string();
                Json b = Json::object(), o;
                b.set("place", "peer:" + link_id_ + ":" + place); b.set("item", j["there_key"]); b.set("format", "keep"); b.set("replace", true);
                ok = link_->call(peer, "POST", "/api/v2/link/send-game", b, o, err);
                const std::string sid = o["send"].as_string();
                Json local;
                for (int i = 0; ok && i < 3600; ++i) {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    Json sends; std::string e2;
                    if (!link_->call(peer, "GET", "/api/v2/link/sends", Json(), sends, e2)) continue;
                    const Json s = sends[sid];
                    if (s["state"].as_string() == "failed") { ok = false; err = s["error"].as_string(); }
                    if (s["state"].as_string() == "done") { local = s["remote_tasks"]; break; }
                }
                set("step", "install");
                for (const auto& tk : local.items()) if (ok) ok = wait_local(tk.as_string(), err);
            }
            if (ok && j["config_moves"].as_bool()) {  // réglages : la console qui reçoit écrit selon son profil, copie de sécurité d'abord
                set("step", "config");
                const std::string tid = j["title_id"].as_string();
                const bool cup = j["config_state"].as_string() == "here_newer";
                if (cup) {
                    Json b = Json::object(), o;
                    b.set("title_id", tid);
                    b.set("files", app_config(tid, title, true)["files"]);
                    ok = link_->call(peer, "POST", "/api/v2/apps/config/receive", b, o, err);
                } else {
                    Json theirs;
                    std::string enc;
                    for (unsigned char ch : title) { if (std::isalnum(ch)) enc += static_cast<char>(ch); else { char hx[4]; std::snprintf(hx, sizeof hx, "%%%02X", ch); enc += hx; } }
                    ok = link_->call(peer, "GET", "/api/v2/apps/config?title_id=" + tid + "&title=" + enc, Json(), theirs, err);
                    if (ok) { Json b = Json::object(); b.set("title_id", tid); b.set("files", theirs["files"]); ok = app_config_receive(b, err); }
                }
                if (ok) {  // journal : empreintes des deux côtés après la synchro
                    Json theirs;
                    std::string e2, enc;
                    for (unsigned char ch : title) { if (std::isalnum(ch)) enc += static_cast<char>(ch); else { char hx[4]; std::snprintf(hx, sizeof hx, "%%%02X", ch); enc += hx; } }
                    link_->call(peer, "GET", "/api/v2/apps/config?data=0&title_id=" + tid + "&title=" + enc, Json(), theirs, e2);
                    config_remember(peer, tid, app_config(tid, title, false)["hash"].as_string(), theirs["hash"].as_string());
                } else err = "réglages : " + err;
            }
            Json r = Json::object();
            r.set("title", title); r.set("direction", up ? "push" : "pull"); r.set("ok", ok);
            if (!ok) { r.set("error", err); Json e = Json::object(); e.set("title", title); e.set("error", err); push("errors", e); }
            push("results", r);
            set("done", static_cast<std::int64_t>(++done));
            log("applis: %s %s %s", title.c_str(), up ? "→" : "←", ok ? "ok" : err.c_str());
        }
        {
            std::lock_guard<std::mutex> l(sync_mu_);
            parties_status_.set("state", parties_status_["errors"].size() ? "failed" : "done");
            parties_status_.set("finished", now_unix());
            parties_status_.set("current", "");
        }
        parties_running_ = false;
        request_scan();
        ++revision_;
    }).detach();
    return parties_status();
}


// =====================================================================================================================
// NAS = DÉPÔT. Même règle qu'entre deux PS5 (la plus récente gagne, jamais de retour en arrière, jamais en boucle),
// mais l'autre côté n'est pas une console : c'est la dernière copie rangée sur le NAS.
// =====================================================================================================================
namespace {
// Attente d'une tâche d'ici (30 min au plus).
bool wait_task(tasks::Queue& q, const std::string& id, std::string& err) {
    for (int i = 0; i < 900; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        for (const auto& t : q.list()) if (t.id == id) {
            if (t.state == tasks::State::done) return true;
            if (t.state == tasks::State::failed || t.state == tasks::State::cancelled) { err = t.error.empty() ? "tâche interrompue" : t.error; return false; }
        }
    }
    err = "trop long";
    return false;
}
}  // namespace

// ---- Parties actives ↔ NAS ----------------------------------------------------------------------------------------
Json Engine::nas_parties_plan(const std::string& nas) {
    Json out = Json::object();
    out.set("peer", nas);
    out.set("nas", true);
    std::string label;
    {
        const Json rem = remotes_json();
        for (const auto& r : rem.items()) if (r["id"].as_string() == nas) label = r["name"].as_string();
    }
    if (label.empty()) { out.set("error", "NAS inconnu"); return out; }
    out.set("name", label);
    out.set("here_accounts", Json::array()); out.set("there_accounts", Json::array());
    out.set("here_account", ""); out.set("there_account", ""); out.set("account_mode", "same_psn");
    // Ici : les parties sur la console, par compte (numéro PSN).
    const Json here = saves_overview();
    std::map<std::string, std::string> acc_psn, acc_user;   // compte → numéro PSN, user_id
    std::map<std::string, std::string> psn_acc;             // numéro PSN → compte d'ici (pour restaurer une partie absente ici)
    for (const auto& a : here["accounts"].items()) {
        if (!a["on_console"].as_bool() || a["platform"].as_string() != "PS5") continue;
        const std::string id = norm_id(a["account_id"].as_string());
        if (id.empty()) continue;
        acc_psn[a["key"].as_string()] = id; acc_user[a["key"].as_string()] = a["user_id"].as_string();
        if (!psn_acc.count(id)) psn_acc[id] = a["key"].as_string();
    }
    // Là-bas : la copie la plus récente de chaque jeu, pour chaque numéro PSN (y compris les dossiers « PS5 backup »
    // faits hors de l'app : le résumé lit leur propriétaire et l'heure écrite par le jeu).
    struct Copy { std::int64_t at = 0, play = -1; std::string bundle; };
    std::map<std::string, Copy> nas_best;  // numéro PSN|jeu
    const Json sum = backups_summary("", false);
    for (const auto& g : sum["games"].items())
        for (const auto& c : g["copies"].items()) {
            if (c["place"].as_string() != nas || !c["complete"].as_bool(true)) continue;
            const std::string owner = norm_id(c["owner"].as_string());
            const std::int64_t at = std::max(c["game_saved_at"].as_int(), c["save_mtime"].as_int());
            if (owner.empty() || !at) continue;
            Copy& b = nas_best[owner + "|" + g["title_id"].as_string()];
            if (at > b.at) { b.at = at; b.play = c["play_time_seconds"].as_int(-1); b.bundle = c["bundle"].as_string(); }
        }
    struct Row { std::string title, tid, acc, user, item; std::int64_t at = 0, play = -1; };
    std::map<std::string, Row> rows;  // numéro PSN|jeu
    for (const auto& e : here["entries"].items()) {
        if (!e["console"].is_object() || e["status"].as_string() == "system") continue;
        const auto a = acc_psn.find(e["account"].as_string());
        if (a == acc_psn.end()) continue;
        Row& r = rows[a->second + "|" + e["title_id"].as_string()];
        r.title = e["title"].as_string(); r.tid = e["title_id"].as_string(); r.acc = e["account"].as_string();
        r.user = acc_user[r.acc]; r.item = e["item"].as_string();
        r.at = e["console"]["mtime"].as_int(); r.play = e["console"]["progress"]["play_time_seconds"].as_int(-1);
    }
    // Une partie gardée sur le NAS pour un jeu installé ici mais jamais joué sur ce compte : elle peut venir.
    std::map<std::string, std::pair<std::string, std::string>> installed;  // jeu → (élément, titre)
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if ((it.kind == Kind::game_ps5 || it.kind == Kind::game_ps4) && !it.title_id.empty()) installed[it.title_id] = {it.key, it.title};
    }
    std::map<std::string, std::string> names;
    for (const auto& g : sum["games"].items()) names[g["title_id"].as_string()] = g["title"].as_string();
    for (const auto& [k, c] : nas_best) {
        if (rows.count(k)) continue;
        const std::string psn = k.substr(0, k.find('|')), tid = k.substr(k.find('|') + 1);
        const auto acc = psn_acc.find(psn);
        const auto inst = installed.find(tid);
        if (acc == psn_acc.end() || inst == installed.end()) continue;
        Row& r = rows[k];
        r.tid = tid; r.title = inst->second.second.empty() ? names[tid] : inst->second.second; r.acc = acc->second; r.user = acc_user[acc->second]; r.item = inst->second.first;
    }
    const Json prefs = sync_prefs(nas);
    const Json journal = parties_journal(nas);
    std::set<std::string> followed;
    for (const auto& t : prefs["parties_games"].items()) followed.insert(t.as_string());
    std::string run_here;
    { const Json fg = foreground_app(); if (fg["game"].as_bool()) run_here = fg["title_id"].as_string(); }
    Json titles = Json::array();
    int need = 0, chosen = 0;
    for (const auto& [k, r] : rows) {
        const Copy c = nas_best.count(k) ? nas_best.at(k) : Copy{};
        Json t = Json::object();
        t.set("title_id", r.tid); t.set("title", r.title);
        t.set("here_at", r.at); t.set("there_at", c.at);
        t.set("here_account", r.acc); t.set("here_user", r.user); t.set("here_item", r.item);
        t.set("there_bundle", c.bundle);
        const std::string key = r.acc + ">nas|" + r.tid;
        const Json& rec = journal[key];
        const PartiesDecision dec = parties_decide({r.at, r.play}, {c.at, c.play}, rec.is_object(), rec["here_at"].as_int(), rec["there_at"].as_int(),
                                                   prefs["parties_resolve"][r.tid].as_string());
        t.set("state", dec.state);
        if (!dec.reason.empty()) t.set("reason", dec.reason);
        if (dec.reason == "both_played") t.set("conflict", true);
        t.set("key", key);
        if (r.play >= 0) t.set("here_play", r.play);
        if (c.play >= 0) t.set("there_play", c.play);
        const bool f = followed.count(r.tid) > 0;
        t.set("followed", f);
        t.set("running", !run_here.empty() && run_here == r.tid);
        chosen += f;
        need += f && dec.state != "same" && dec.state != "conflict";
        titles.push(t);
    }
    out.set("titles", titles);
    out.set("chosen", chosen);
    out.set("need", need);
    out.set("running_here", run_here);
    return out;
}

Json Engine::nas_parties_start(const Json& body, std::string& error) {
    const std::string nas = body["peer"].as_string();
    const Json plan = nas_parties_plan(nas);
    if (plan.has("error")) { error = plan["error"].as_string(); return Json(); }
    std::set<std::string> only;
    for (const auto& t : body["title_ids"].items()) only.insert(t.as_string());
    Json jobs = Json::array();
    for (const auto& t : plan["titles"].items()) {
        // « one » : un seul jeu demandé depuis sa fiche (Bibliothèque de l'autre PS5), suivi ou non dans Synchro.
        const bool asked = body["one"].as_bool() && only.count(t["title_id"].as_string());
        if ((!t["followed"].as_bool() && !asked) || t["state"].as_string() == "same" || t["state"].as_string() == "conflict") continue;
        if (!only.empty() && !only.count(t["title_id"].as_string())) continue;
        if (t["running"].as_bool()) { error = "« " + t["title"].as_string() + " » est lancé : fermez le jeu d'abord (rien n'a été modifié)"; return Json(); }
        jobs.push(t);
    }
    if (jobs.size() == 0) { error = "rien à mettre à niveau"; return Json(); }
    if (parties_running_.exchange(true)) { error = "une mise à niveau est déjà en cours"; return Json(); }
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", nas);
        parties_status_.set("total", static_cast<std::int64_t>(jobs.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, nas, jobs] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        int done = 0;
        for (const auto& j : jobs.items()) {
            const std::string title = j["title"].as_string(), st = j["state"].as_string(), tid = j["title_id"].as_string();
            const bool up = st == "here_newer" || st == "only_here";  // la partie d'ici va au dépôt
            set("current", title);
            std::string err, bid;
            bool ok = true;
            std::int64_t here_now = j["here_at"].as_int(), there_now = j["there_at"].as_int();
            if (up) {  // une nouvelle copie sur le NAS (les précédentes restent : les sauvegardes gardent leurs versions)
                set("step", "copy");
                ok = backup(j["here_item"].as_string(), {Part::save}, nas, bid, err, j["here_account"].as_string()) && wait_task(*queue_, bid, err);
                if (ok) there_now = here_now;  // la copie porte l'heure écrite par le jeu
            } else {
                if (j["here_at"].as_int()) {  // 1. la partie d'ici est d'abord gardée (sur un disque de la console)
                    set("step", "version");
                    ok = backup(j["here_item"].as_string(), {Part::save}, "", bid, err, j["here_account"].as_string()) && wait_task(*queue_, bid, err);
                    if (!ok) err = "sauvegarde de la partie remplacée impossible, rien n'a été modifié : " + err;
                }
                if (ok) {  // 2. la copie du NAS est restaurée sur le compte d'ici (vérifiée, re-signée si besoin)
                    set("step", "restore");
                    std::string rid;
                    ok = !j["there_bundle"].as_string().empty() && restore_save(nas, j["there_bundle"].as_string(), j["here_user"].as_string(), tid, true, rid, err, false) && wait_task(*queue_, rid, err);
                    if (j["there_bundle"].as_string().empty()) err = "copie du NAS introuvable";
                }
                if (ok) {  // la partie restaurée a une nouvelle heure : on la relit avant de noter le journal
                    set("step", "journal");
                    for (int i = 0; i < 40; ++i) {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                        const Json ov = saves_overview();
                        std::int64_t now_at = 0;
                        for (const auto& e : ov["entries"].items())
                            if (e["title_id"].as_string() == tid && e["account"].as_string() == j["here_account"].as_string() && e["console"].is_object()) now_at = e["console"]["mtime"].as_int();
                        if (now_at && now_at != j["here_at"].as_int()) { here_now = now_at; break; }
                    }
                }
            }
            if (ok) {
                parties_remember(nas, j["key"].as_string(), here_now, there_now);
                Json pr = sync_prefs(nas);
                if (pr["parties_resolve"].has(tid)) { Json b = Json::object(), rs = Json::object(); rs.set(tid, ""); b.set("peer", nas); b.set("resolve", rs); std::string e3; set_parties_prefs(b, e3); }
            }
            Json r = Json::object();
            r.set("title", title); r.set("direction", up ? "push" : "pull"); r.set("ok", ok);
            if (!ok) { r.set("error", err); Json e = Json::object(); e.set("title", title); e.set("error", err); push("errors", e); }
            push("results", r);
            set("done", static_cast<std::int64_t>(++done));
            log("parties NAS: %s %s %s", title.c_str(), up ? "→" : "←", ok ? "ok" : err.c_str());
        }
        {
            std::lock_guard<std::mutex> l(sync_mu_);
            parties_status_.set("state", parties_status_["errors"].size() ? "failed" : "done");
            parties_status_.set("finished", now_unix());
            parties_status_.set("current", "");
        }
        { std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.erase(nas); }  // la liste du NAS a changé
        parties_running_ = false;
        plans_invalidate();
        ++revision_;
    }).detach();
    return parties_status();
}

// ---- Applis ↔ NAS (dépôt) -----------------------------------------------------------------------------------------
Json Engine::nas_apps_depot(const std::string& nas, std::string& error) {
    store::Remote r;
    if (!remotes_->find(nas, r)) { error = "NAS inconnu"; return Json(); }
    auto st = remotes_->open(nas, error);
    if (!st) return Json();
    std::string text;
    Json d;
    if (!st->read_small(fs::join(r.folder(), "pbc-apps.json"), text) || !Json::parse(text, d) || !d.is_object()) return Json::object();  // pas encore de dépôt
    return d["apps"].is_object() ? d["apps"] : Json::object();
}

bool Engine::nas_apps_depot_set(const std::string& nas, const std::string& tid, const Json& entry, std::string& error) {
    store::Remote r;
    if (!remotes_->find(nas, r)) { error = "NAS inconnu"; return false; }
    auto st = remotes_->open(nas, error);
    if (!st) return false;
    std::string text;
    Json d;
    if (!st->read_small(fs::join(r.folder(), "pbc-apps.json"), text) || !Json::parse(text, d) || !d.is_object()) d = Json::object();
    Json apps = d["apps"].is_object() ? d["apps"] : Json::object();
    apps.set(tid, entry);
    d.set("format", "pbc-apps-depot"); d.set("version", 1); d.set("updated", now_unix()); d.set("apps", apps);
    if (!st->write_small(fs::join(r.folder(), "pbc-apps.json"), d.dump(true) + "\n")) { error = "dépôt du NAS : écriture impossible"; return false; }
    return true;
}

Json Engine::nas_apps_plan(const std::string& nas) {
    Json out = Json::object();
    out.set("peer", nas);
    out.set("nas", true);
    std::string err;
    const Json depot = nas_apps_depot(nas, err);
    if (!err.empty()) { out.set("error", "NAS injoignable : " + err); return out; }
    const Json here = apps_versions();
    std::map<std::string, std::pair<Json, Json>> by;
    for (const auto& a : here.items()) by[a["title_id"].as_string()].first = a;
    for (const auto& [tid, e] : depot.members()) by[tid].second = e;
    const Json prefs = sync_prefs(nas);
    std::set<std::string> followed;
    for (const auto& t : prefs["apps_games"].items()) followed.insert(t.as_string());
    Json titles = Json::array();
    int need = 0, chosen = 0;
    for (const auto& [tid, p] : by) {
        const Json &h = p.first, &t = p.second;
        Json x = Json::object();
        x.set("title_id", tid);
        x.set("title", h.is_object() ? h["title"] : t["title"]);
        x.set("here_version", h["version"]); x.set("there_version", t["version"]);
        x.set("here_key", h["key"]); x.set("there_key", t.is_object() ? Json("depot:" + tid) : Json());
        x.set("here_place", h["place"]); x.set("there_bundle", t["bundle"]);
        std::string st;
        if (!h.is_object()) st = "only_there";
        else if (!t.is_object()) st = "only_here";
        else if (h["version"].as_string().empty() && t["version"].as_string().empty() && !h["hash"].as_string().empty() && h["hash"].as_string() == t["hash"].as_string()) st = "same";  // sans numéro, mêmes fichiers
        else if (h["version"].as_string().empty() || t["version"].as_string().empty()) st = "unknown";
        else { const int c = compare_versions(h["version"].as_string(), t["version"].as_string()); st = c > 0 ? "here_newer" : c < 0 ? "there_newer" : "same"; }
        if (st == "same" && !h["hash"].as_string().empty() && !t["hash"].as_string().empty() && h["hash"].as_string() != t["hash"].as_string()) {
            const std::string pick = prefs["apps_resolve"][tid].as_string();
            st = pick == "here" ? "here_newer" : pick == "there" ? "there_newer" : "conflict";
            x.set("conflict", true); x.set("reason", "same_version_diff_files");
        }
        // Le dépôt ne garde que des applis en dossier ; une appli en image se met à jour par Transferts.
        if ((st == "here_newer" || st == "only_here") && !h["folder"].as_bool()) st = "manual";
        x.set("state", st);
        const bool f = followed.count(tid) > 0;
        x.set("followed", f);
        x.set("mode", "app");  // NAS : l'appli seule (les réglages se synchronisent entre deux PS5)
        const bool app_moves = st != "same" && st != "unknown" && st != "manual" && st != "conflict";
        x.set("app_moves", app_moves); x.set("config_moves", false);
        chosen += f;
        need += f && app_moves;
        titles.push(x);
    }
    out.set("titles", titles);
    out.set("chosen", chosen);
    out.set("need", need);
    return out;
}

Json Engine::nas_apps_start(const Json& body, std::string& error) {
    const std::string nas = body["peer"].as_string();
    const Json plan = nas_apps_plan(nas);
    if (plan.has("error")) { error = plan["error"].as_string(); return Json(); }
    Json jobs = Json::array();
    for (const auto& t : plan["titles"].items()) if (t["followed"].as_bool() && t["app_moves"].as_bool()) jobs.push(t);
    if (jobs.size() == 0) { error = "rien à mettre à niveau"; return Json(); }
    if (parties_running_.exchange(true)) { error = "une mise à niveau est déjà en cours"; return Json(); }
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", nas); parties_status_.set("section", "apps");
        parties_status_.set("total", static_cast<std::int64_t>(jobs.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, nas, jobs] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        int done = 0;
        for (const auto& j : jobs.items()) {
            const std::string title = j["title"].as_string(), st = j["state"].as_string(), tid = j["title_id"].as_string();
            const bool up = st == "here_newer" || st == "only_here";
            set("current", title);
            std::string err;
            bool ok = true;
            if (up) {  // l'appli d'ici entre au dépôt : une copie de l'appli (fichiers + droits notés), puis la fiche du dépôt
                set("step", "copy");
                std::string bid;
                const std::int64_t t0 = now_unix();
                ok = backup(j["here_key"].as_string(), {Part::app}, nas, bid, err) && wait_task(*queue_, bid, err);
                std::string bundle;
                if (ok) {
                    { std::lock_guard<std::mutex> l(remote_index_mu_); remote_index_.erase(nas); }
                    const Json idx = remote_index(nas, true);
                    std::int64_t best = 0;
                    for (const auto& b : idx["backups"].items()) {
                        bool app = false;
                        for (const auto& pp : b["parts"].items()) app = app || pp.as_string() == "app";
                        if (app && b["title_id"].as_string() == tid && b["complete"].as_bool() && b["created_unix"].as_int() >= t0 - 120 && b["created_unix"].as_int() >= best) { best = b["created_unix"].as_int(); bundle = b["bundle"].as_string(); }
                    }
                    if (bundle.empty()) { ok = false; err = "copie de l'appli introuvable sur le NAS après l'envoi"; }
                }
                if (ok) {
                    Json hv;
                    const Json av = apps_versions();  // gardée en vie pendant la boucle
                    for (const auto& a : av.items()) if (a["title_id"].as_string() == tid) hv = a;
                    Json e = Json::object();
                    e.set("title", title); e.set("version", j["here_version"]); e.set("hash", hv["hash"]); e.set("bundle", bundle);
                    e.set("at", now_unix()); e.set("from", link_id_);
                    ok = nas_apps_depot_set(nas, tid, e, err);
                }
            } else {  // le dépôt a une version plus récente : copiée sur un disque d'ici, puis installée à la place de l'ancienne
                set("step", "copy");
                const std::string bundle = j["there_bundle"].as_string(), name(fs::basename(bundle));
                Place hp;
                std::string e2;
                const std::string here_place = resolve_place("auto", hp, e2) ? hp.id : std::string("internal");
                std::string src_path, dst_path;
                auto src = version_store(nas, bundle, src_path, err);
                auto dst = src ? version_store(here_place, name, dst_path, err) : nullptr;
                std::atomic<std::int64_t> bytes{0};
                std::atomic<bool> stop{false};
                ok = src && dst && copy_store_tree(*src, src_path, *dst, dst_path, bytes, stop, err);
                if (ok) {
                    set("step", "install");
                    Json tk;
                    const std::string target = j["here_place"].as_string().empty() ? std::string("internal") : j["here_place"].as_string();
                    ok = install_backup(here_place, name, target, "keep", "", tk, err, true, true);
                    for (const auto& t : tk.items()) if (ok) ok = wait_task(*queue_, t.as_string(), err);
                }
            }
            Json r = Json::object();
            r.set("title", title); r.set("direction", up ? "push" : "pull"); r.set("ok", ok);
            if (!ok) { r.set("error", err); Json e = Json::object(); e.set("title", title); e.set("error", err); push("errors", e); }
            push("results", r);
            set("done", static_cast<std::int64_t>(++done));
            log("applis NAS: %s %s %s", title.c_str(), up ? "→" : "←", ok ? "ok" : err.c_str());
        }
        {
            std::lock_guard<std::mutex> l(sync_mu_);
            parties_status_.set("state", parties_status_["errors"].size() ? "failed" : "done");
            parties_status_.set("finished", now_unix());
            parties_status_.set("current", "");
        }
        parties_running_ = false;
        plans_invalidate();
        request_scan();
        ++revision_;
    }).detach();
    return parties_status();
}

}  // namespace pbc
