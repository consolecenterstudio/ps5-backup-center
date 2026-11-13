// SPDX-License-Identifier: GPL-3.0-or-later
// Même progression des émulateurs entre deux PS5 (ProsperoEden / Eden pour la Switch, Églantine pour la Switch 2).
//
// Une sauvegarde d'émulateur = un dossier « <données>/user/nand/user/save/0000000000000000/<profil>/<TITLEID> ».
// Les profils n'ont pas le même identifiant d'une console à l'autre : on les associe par leur NOM (ou « device » pour le
// profil commun). Clé d'un jeu : « <famille>|<profil>|<TITLEID> », famille = switch ou switch2.
//
// Règles (les mêmes que pour les parties PS5, validées par le propriétaire) :
// - la plus récente gagne, d'après l'heure écrite par l'émulateur (fichier le plus récent du dossier) ;
// - journal par appareil (parties-sync.json, clé « emu|… ») : jamais d'aller-retour en boucle, conflit si les deux ont
//   avancé depuis la dernière synchro, tranché par l'utilisateur ;
// - chaque console n'écrit que dans SES dossiers : elle va chercher la version la plus récente chez l'autre ; quand c'est
//   l'autre qui doit recevoir, on le lui demande ;
// - jamais quand l'émulateur est ouvert là où on écrit ; copie de sécurité du dossier remplacé d'abord ; remplacement
//   d'un bloc (dossier neuf puis renommages), retour arrière en cas d'échec ; les heures des fichiers sont gardées.
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <thread>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "saves/switch_import.hpp"
#include "sync_parties.hpp"

namespace pbc {

namespace {

constexpr const char* kSaveBase = "user/nand/user/save/0000000000000000";
constexpr const char* kDeviceProfile = "00000000000000000000000000000000";

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool hex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Un chemin relatif sans « .. », sans « / » au début, sans élément vide.
bool safe_rel_path(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find('\\') != std::string::npos) return false;
    std::size_t s = 0;
    while (s <= p.size()) {
        const auto e = p.find('/', s);
        const std::string part = p.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (part.empty() || part == "." || part == ".." || part.find(".pbc-") != std::string::npos) return false;
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return true;
}

void walk(const std::string& root, const std::string& rel, Json& files, std::int64_t& newest, std::int64_t& bytes) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(rel.empty() ? root : fs::join(root, rel)), es)) return;
    for (const auto& e : es) {
        if (e.name.empty() || e.name.find(".pbc-") != std::string::npos) continue;
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
        if (e.dir) { walk(root, r, files, newest, bytes); continue; }
        Json f = Json::object();
        f.set("path", r); f.set("size", static_cast<std::int64_t>(e.size)); f.set("mtime", e.mtime);
        files.push(f);
        newest = std::max(newest, e.mtime);
        bytes += static_cast<std::int64_t>(e.size);
    }
}

bool copy_dir(const std::string& from, const std::string& to) {
    if (!fs::mkdirs(fs::host(to))) return false;
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(from), es)) return false;
    for (const auto& e : es) {
        const std::string a = fs::join(from, e.name), b = fs::join(to, e.name);
        if (e.dir) { if (!copy_dir(a, b)) return false; continue; }
        std::string data;
        if (!fs::read(fs::host(a), data, 1ull << 31) || !fs::write_atomic(fs::host(b), data)) return false;
    }
    return true;
}

void set_mtime(const std::string& host_path, std::int64_t t) {
    timeval tv[2];
    tv[0].tv_sec = tv[1].tv_sec = static_cast<time_t>(t);
    tv[0].tv_usec = tv[1].tv_usec = 0;
    ::utimes(host_path.c_str(), tv);
}

}  // namespace

// Les sauvegardes d'émulateurs de cette console : [{key, family, emulator, root, profile, profile_name, title_id, title,
// at, bytes, files}]. Lue aussi par l'autre PS5 (GET /api/v2/emu/saves).
Json Engine::emu_saves() {
    struct Root { std::string family, emulator, data; std::vector<std::string> app_ids; };
    std::vector<Root> roots;
    {
        std::lock_guard<std::mutex> l(catalog_mu_);
        for (const auto& e : catalog_.all()) {
            if (e.switch_saves.empty() || e.data.empty()) continue;
            const std::string fam = e.platform == "Switch 2" ? "switch2" : "switch";
            roots.push_back({fam, e.name, e.data, e.title_ids});
        }
    }
    std::map<std::string, std::string> titles;  // TITLEID → nom du jeu (bibliothèque)
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) if (it.kind == Kind::rom && !it.title_id.empty() && !titles.count(it.title_id)) titles[it.title_id] = it.title;
    }
    Json out = Json::array();
    std::set<std::string> seen;  // une même clé dans /data/prosperoeden et /data/eden : la première (ProsperoEden) gagne
    for (const auto& r : roots) {
        const std::string base = fs::join(r.data, kSaveBase);
        if (!fs::is_dir(fs::host(base))) continue;
        std::map<std::string, std::string> names;  // dossier du profil → nom
        for (const auto& p : saves::eden_profiles({r.data})) names[lower(p.folder)] = p.name;
        std::vector<fs::Entry> profiles;
        fs::list(fs::host(base), profiles);
        for (const auto& pe : profiles) {
            if (!pe.dir || pe.name.size() != 32) continue;
            const bool device = pe.name == kDeviceProfile;
            const std::string pname = device ? std::string("device") : names.count(lower(pe.name)) ? names[lower(pe.name)] : std::string();
            if (pname.empty()) continue;  // profil sans nom : impossible de l'associer à celui de l'autre PS5
            std::vector<fs::Entry> games;
            fs::list(fs::host(fs::join(base, pe.name)), games);
            for (const auto& g : games) {
                if (!g.dir || !hex16(g.name)) continue;
                const std::string tid = [&] { std::string t = g.name; for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return t; }();
                const std::string key = r.family + "|" + lower(pname) + "|" + tid;
                if (!seen.insert(key).second) continue;
                Json files = Json::array();
                std::int64_t at = 0, bytes = 0;
                walk(fs::join(fs::join(base, pe.name), g.name), "", files, at, bytes);
                if (files.items().empty()) continue;
                Json s = Json::object();
                s.set("key", key); s.set("family", r.family); s.set("emulator", r.emulator); s.set("root", r.data);
                s.set("profile", pe.name); s.set("profile_name", device ? std::string("") : pname);
                s.set("title_id", tid); s.set("title", titles.count(tid) ? titles[tid] : tid);
                s.set("at", at); s.set("bytes", bytes); s.set("files", static_cast<std::int64_t>(files.items().size()));
                Json ids = Json::array();
                for (const auto& id : r.app_ids) ids.push(id);
                s.set("app_ids", ids);
                out.push(s);
            }
        }
    }
    Json j = Json::object();
    j.set("saves", out);
    // Profils connus ici (pour recevoir un jeu absent de cette console : il faut un profil du même nom)
    Json prof = Json::array();
    for (const auto& r : roots) {
        if (!fs::is_dir(fs::host(fs::join(r.data, kSaveBase)))) continue;
        Json p = Json::object(); p.set("family", r.family); p.set("root", r.data); p.set("profile", kDeviceProfile); p.set("name", "device"); prof.push(p);
        for (const auto& ep : saves::eden_profiles({r.data})) {
            if (ep.folder == kDeviceProfile) continue;
            Json q = Json::object(); q.set("family", r.family); q.set("root", r.data); q.set("profile", ep.folder); q.set("name", lower(ep.name)); prof.push(q);
        }
    }
    j.set("profiles", prof);
    return j;
}

namespace {
// Le dossier d'une sauvegarde de cette console, d'après sa clé : vide si inconnue.
std::string emu_dir(const Json& saves, const std::string& key, Json* entry = nullptr) {
    for (const auto& s : saves["saves"].items()) if (s["key"].as_string() == key) {
        if (entry) *entry = s;
        return fs::join(fs::join(fs::join(s["root"].as_string(), kSaveBase), s["profile"].as_string()), s["title_id"].as_string());
    }
    return "";
}
// Un émulateur de cette famille est-il au premier plan ? (son appli lit et écrit ses sauvegardes)
bool emu_open(const std::string& fg, const std::string& family, const Json& saves) {
    if (fg.empty()) return false;
    if (family == "switch" && (fg == "PPSA99008" || fg == "PPSA99186")) return true;
    for (const auto& s : saves["saves"].items()) if (s["family"].as_string() == family)
        for (const auto& id : s["app_ids"].items()) if (id.as_string() == fg) return true;
    return false;
}
}  // namespace

Json Engine::emu_files(const std::string& key, std::string& error) {
    const Json saves = emu_saves();
    const std::string dir = emu_dir(saves, key);
    if (dir.empty()) { error = "sauvegarde inconnue"; return Json(); }
    Json files = Json::array();
    std::int64_t at = 0, bytes = 0;
    walk(dir, "", files, at, bytes);
    Json j = Json::object();
    j.set("key", key); j.set("at", at); j.set("bytes", bytes); j.set("files", files);
    return j;
}

bool Engine::emu_read(const std::string& key, const std::string& path, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error) {
    if (!safe_rel_path(path)) { error = "chemin refusé"; return false; }
    const std::string dir = emu_dir(emu_saves(), key);
    if (dir.empty()) { error = "sauvegarde inconnue"; return false; }
    FILE* f = std::fopen(fs::host(fs::join(dir, path)).c_str(), "rb");
    if (!f) { error = "fichier introuvable"; return false; }
    limit = std::min<std::uint64_t>(limit ? limit : (8u << 20), 8u << 20);
    out.clear();
    if (::fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0) { out.resize(limit); out.resize(std::fread(out.data(), 1, limit, f)); }
    std::fclose(f);
    return true;
}

Json Engine::emu_plan(const std::string& peer) {
    Json out = Json::object();
    out.set("peer", peer);
    if (!link_) { out.set("error", "liaison indisponible"); return out; }
    Json there;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/emu/saves", Json(), there, err, 60) || !there.is_object()) { out.set("error", "autre PS5 injoignable : " + err); return out; }
    const Json here = emu_saves();
    const Json prefs = sync_prefs(peer), journal = parties_journal(peer);
    std::set<std::string> chosen;
    for (const auto& k : prefs["emu_games"].items()) chosen.insert(k.as_string());
    Json fg_there;
    link_->call(peer, "GET", "/api/v2/app/foreground", Json(), fg_there, err, 10);
    const std::string fg_here = foreground_app()["title_id"].as_string(), fgt = fg_there["title_id"].as_string();
    std::map<std::string, Json> h, t;
    for (const auto& s : here["saves"].items()) h[s["key"].as_string()] = s;
    for (const auto& s : there["saves"].items()) t[s["key"].as_string()] = s;
    std::set<std::string> keys;
    for (const auto& [k, v] : h) keys.insert(k);
    for (const auto& [k, v] : t) keys.insert(k);
    // Recevoir un jeu absent ici : il faut un profil du même nom dans le même émulateur
    std::set<std::string> here_profiles;
    for (const auto& p : here["profiles"].items()) here_profiles.insert(p["family"].as_string() + "|" + p["name"].as_string());
    Json titles = Json::array();
    std::int64_t need = 0;
    for (const auto& k : keys) {
        const Json& a = h.count(k) ? h[k] : Json();
        const Json& b = t.count(k) ? t[k] : Json();
        const Json& any = a.is_object() ? a : b;
        PartiesSide hs, ts;
        hs.at = a.is_object() ? a["at"].as_int() : 0;
        ts.at = b.is_object() ? b["at"].as_int() : 0;
        const Json rec = journal["emu|" + k];
        const std::string resolve = prefs["emu_resolve"][k].as_string();
        PartiesDecision d = parties_decide(hs, ts, rec.is_object(), rec["here_at"].as_int(), rec["there_at"].as_int(), resolve);
        if (rec.is_object() && hs.at && ts.at && std::llabs(hs.at - ts.at) <= 2) d = {"same", "synced"};  // mêmes heures : déjà au même niveau
        const std::string fam = any["family"].as_string();
        const std::string pname = any["profile_name"].as_string().empty() ? std::string("device") : lower(any["profile_name"].as_string());
        if (!a.is_object() && !here_profiles.count(fam + "|" + pname)) d = {"no_profile", "no_profile_here"};
        Json x = Json::object();
        // « title_id » = la clé (choix, conflits : mêmes écrans que les parties) ; « game_id » = le vrai numéro du jeu
        x.set("key", k); x.set("title_id", k); x.set("game_id", any["title_id"]); x.set("title", any["title"]);
        x.set("detail", any["emulator"].as_string() + (any["profile_name"].as_string().empty() ? std::string() : " · " + any["profile_name"].as_string()));
        x.set("family", fam); x.set("emulator", any["emulator"]); x.set("profile_name", any["profile_name"]);
        x.set("here_at", hs.at); x.set("there_at", ts.at);
        x.set("here_bytes", a["bytes"].as_int()); x.set("there_bytes", b["bytes"].as_int());
        x.set("state", d.state); x.set("reason", d.reason); x.set("conflict", d.state == "conflict");
        const bool followed = chosen.count(k) > 0;
        x.set("followed", followed);
        const bool run = (d.state == "there_newer" || d.state == "only_there") ? emu_open(fg_here, fam, here) : (d.state == "here_newer" || d.state == "only_here") ? emu_open(fgt, fam, there) : false;
        x.set("running", run);
        if (followed && d.state != "same" && d.state != "conflict" && d.state != "no_profile") ++need;
        titles.push(x);
    }
    out.set("titles", titles);
    out.set("need", need);
    out.set("chosen", static_cast<std::int64_t>(chosen.size()));
    out.set("running_here", emu_open(fg_here, "switch", here) || emu_open(fg_here, "switch2", here));
    out.set("running_there", emu_open(fgt, "switch", there) || emu_open(fgt, "switch2", there));
    return out;
}

// Va chercher chez l'autre PS5 la sauvegarde `key` et remplace la nôtre (copie de sécurité d'abord). Renvoie "" si ok.
std::string Engine::emu_pull_one(const std::string& peer, const std::string& key, const Json& here, std::int64_t& here_at, std::int64_t& there_at) {
    Json entry;
    std::string target = emu_dir(here, key, &entry);
    if (target.empty()) {  // jeu absent ici : dans le profil du même nom
        const auto p1 = key.find('|'), p2 = key.find('|', p1 + 1);
        const std::string fam = key.substr(0, p1), pname = key.substr(p1 + 1, p2 - p1 - 1), tid = key.substr(p2 + 1);
        for (const auto& p : here["profiles"].items())
            if (p["family"].as_string() == fam && p["name"].as_string() == pname) { target = fs::join(fs::join(fs::join(p["root"].as_string(), kSaveBase), p["profile"].as_string()), tid); break; }
        if (target.empty()) return "aucun profil de ce nom ici";
    }
    if (emu_open(foreground_app()["title_id"].as_string(), key.substr(0, key.find('|')), here)) return "l'émulateur est ouvert ici : fermez-le (rien n'a été modifié)";
    Json list;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/link/emu/files?key=" + url_encode(key), Json(), list, err, 60) || !list["files"].is_array()) return "liste illisible chez l'autre PS5 : " + err;
    there_at = list["at"].as_int();
    const std::string work = target + ".pbc-new", old = target + ".pbc-old";
    fs::remove_tree(fs::host(work));
    if (!fs::mkdirs(fs::host(work))) return "écriture impossible";
    for (const auto& f : list["files"].items()) {
        const std::string rel = f["path"].as_string();
        if (!safe_rel_path(rel)) { fs::remove_tree(fs::host(work)); return "chemin refusé : " + rel; }
        const std::string dst = fs::join(work, rel);
        fs::mkdirs(fs::host(std::string(fs::dirname(dst))));
        FILE* o = std::fopen(fs::host(dst).c_str(), "wb");
        if (!o) { fs::remove_tree(fs::host(work)); return "écriture impossible"; }
        const std::int64_t size = f["size"].as_int();
        std::int64_t off = 0;
        std::string why;
        while (off < size) {
            std::string chunk;
            bool got = false;
            for (int tries = 0; tries < 3 && !got; ++tries)
                got = link_->get_raw(peer, "/api/v2/link/emu/read?key=" + url_encode(key) + "&path=" + url_encode(rel) + "&offset=" + std::to_string(off) + "&limit=" + std::to_string(8u << 20), chunk, 60);
            if (!got || chunk.empty()) { why = "lecture interrompue chez l'autre PS5"; break; }
            if (std::fwrite(chunk.data(), 1, chunk.size(), o) != chunk.size()) { why = "écriture impossible (disque plein ?)"; break; }
            off += static_cast<std::int64_t>(chunk.size());
        }
        if (std::fflush(o) != 0 && why.empty()) why = "écriture impossible";
        ::fsync(fileno(o));
        std::fclose(o);
        if (why.empty() && off != size) why = "taille différente";
        if (!why.empty()) { fs::remove_tree(fs::host(work)); return why; }
        set_mtime(fs::host(dst), f["mtime"].as_int());  // l'heure de l'émulateur, pas celle de la copie
    }
    // Copie de sécurité du dossier remplacé (<état>/restore-safety), puis remplacement d'un bloc
    if (fs::is_dir(fs::host(target))) {
        char stamp[32];
        const std::time_t now = static_cast<std::time_t>(now_unix());
        std::tm tm{};
        gmtime_r(&now, &tm);
        std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%S", &tm);
        const std::string safety = fs::join(fs::join(dir_, "restore-safety"), std::string(stamp) + "-emu-" + std::string(fs::basename(target)) + "-" + std::to_string(std::rand() % 100000));
        if (!copy_dir(target, safety)) { fs::remove_tree(fs::host(work)); return "copie de sécurité impossible : rien n'a été modifié"; }
        fs::remove_tree(fs::host(old));
        if (std::rename(fs::host(target).c_str(), fs::host(old).c_str()) != 0) { fs::remove_tree(fs::host(work)); return "remplacement impossible : rien n'a été modifié"; }
    } else {
        fs::mkdirs(fs::host(std::string(fs::dirname(target))));
    }
    if (std::rename(fs::host(work).c_str(), fs::host(target).c_str()) != 0) {
        if (fs::is_dir(fs::host(old))) std::rename(fs::host(old).c_str(), fs::host(target).c_str());  // retour arrière
        fs::remove_tree(fs::host(work));
        return "remplacement impossible : l'ancienne sauvegarde est remise";
    }
    fs::remove_tree(fs::host(old));
    std::int64_t at = 0, bytes = 0;
    Json files = Json::array();
    walk(target, "", files, at, bytes);
    here_at = at;
    return "";
}

Json Engine::emu_start(const Json& body, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return Json(); }
    const std::string peer = body["peer"].as_string();
    if (peer.empty()) { error = "PS5 inconnue"; return Json(); }
    // L'autre PS5 nous demande de prendre chez elle ces sauvegardes (elle a déjà décidé : elles y sont plus récentes)
    const bool asked = body["direction"].as_string() == "here";
    std::vector<std::string> pulls, pushes;
    std::map<std::string, std::string> titles;
    if (asked) {
        for (const auto& k : body["keys"].items()) pulls.push_back(k.as_string());
    } else {
        std::set<std::string> only;
        for (const auto& k : body["keys"].items()) only.insert(k.as_string());
        const Json plan = emu_plan(peer);
        if (!plan["error"].as_string().empty()) { error = plan["error"].as_string(); return Json(); }
        for (const auto& t : plan["titles"].items()) {
            const std::string k = t["key"].as_string(), st = t["state"].as_string();
            if (only.empty() ? !t["followed"].as_bool() : !only.count(k)) continue;
            if (t["running"].as_bool()) { error = "un émulateur est ouvert (" + t["title"].as_string() + ") : fermez-le d'abord"; return Json(); }
            titles[k] = t["title"].as_string();
            if (st == "there_newer" || st == "only_there") pulls.push_back(k);
            else if (st == "here_newer" || st == "only_here") pushes.push_back(k);
        }
    }
    if (pulls.empty() && pushes.empty()) { error = "rien à mettre à niveau"; return Json(); }
    if (parties_running_.exchange(true)) { error = "une mise à niveau est déjà en cours"; return Json(); }
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", peer); parties_status_.set("section", "emu");
        parties_status_.set("total", static_cast<std::int64_t>(pulls.size() + pushes.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, peer, pulls, pushes, titles] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        auto title = [&](const std::string& k) { auto it = titles.find(k); return it != titles.end() ? it->second : k.substr(k.rfind('|') + 1); };
        int done = 0, failed = 0;
        const Json here = emu_saves();
        for (const auto& k : pulls) {
            set("current", title(k));
            std::int64_t ha = 0, ta = 0;
            const std::string why = emu_pull_one(peer, k, here, ha, ta);
            Json r = Json::object(); r.set("title", title(k)); r.set("direction", "pull"); r.set("ok", why.empty());
            if (!why.empty()) { r.set("error", why); ++failed; push("errors", r); }
            else { ++done; parties_remember(peer, "emu|" + k, ha, ta); push("results", r); }
            set("done", static_cast<std::int64_t>(done + failed));
        }
        if (!pushes.empty()) {  // l'autre PS5 vient chercher chez nous les plus récentes d'ici
            set("current", std::to_string(pushes.size()) + " · autre PS5");
            Json b = Json::object(), out, keys = Json::array();
            for (const auto& k : pushes) keys.push(k);
            b.set("peer", link_id_); b.set("direction", "here"); b.set("keys", keys);
            std::string err;
            bool ok = link_->call(peer, "POST", "/api/v2/sync/emu/start", b, out, err, 60);
            Json st;
            for (int i = 0; ok && i < 600; ++i) {  // au plus 30 min
                ::sleep(3);
                if (!link_->call(peer, "GET", "/api/v2/sync/parties/status", Json(), st, err, 15)) continue;
                if (st["section"].as_string() == "emu" && st["state"].as_string() != "running") break;
            }
            if (!ok) err = err.empty() ? "refusé" : err;
            std::set<std::string> bad;
            for (const auto& e : st["errors"].items()) bad.insert(e["title"].as_string());
            const Json now_here = emu_saves();
            for (const auto& k : pushes) {
                Json r = Json::object(); r.set("title", title(k)); r.set("direction", "push");
                const bool good = ok && st["section"].as_string() == "emu" && !bad.count(title(k));
                r.set("ok", good);
                if (!good) {
                    std::string why = !ok ? "autre PS5 : " + err : "échec chez l'autre PS5";
                    for (const auto& e : st["errors"].items()) if (e["title"].as_string() == title(k)) why = e["error"].as_string();
                    r.set("error", why); ++failed; push("errors", r);
                } else {
                    Json e;
                    emu_dir(now_here, k, &e);
                    const std::int64_t at = e["at"].as_int();
                    parties_remember(peer, "emu|" + k, at, at);  // heures gardées : les deux côtés au même niveau
                    ++done; push("results", r);
                }
                set("done", static_cast<std::int64_t>(done + failed));
            }
        }
        request_scan();
        set("current", "");
        set("state", failed ? (done ? "partial" : "failed") : "done");
        set("finished", now_unix());
        plans_invalidate();
        parties_running_ = false;
    }).detach();
    return parties_status();
}

}  // namespace pbc
