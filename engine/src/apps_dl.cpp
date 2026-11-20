// Passerelle Orbit / BlackBox, étape 1 : voir leurs téléchargements (docs/PLAN-passerelle-orbit-blackbox.md).
// Lecture seule de leur fichier d'état : seuls les champs utiles sont repris (titre, état, tailles, fichier, disque) ;
// leurs comptes et jetons ne sont jamais renvoyés ni écrits dans le journal. Rien n'est jamais écrit chez eux.
#include "engine.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "images/images.hpp"
#include "images/pkg_head.hpp"
#include "images/pkg_install.hpp"
#include "core/notify.hpp"
#include "link/link.hpp"
#include "tasks/tasks.hpp"

#include <chrono>
#include <thread>
#include "store/peer.hpp"
#include "files/files.hpp"
#include <strings.h>

#include <unistd.h>

namespace pbc {

namespace {

struct AppDir { const char* id; const char* name; const char* dir; };
const AppDir kApps[] = {{"orbit", "Orbit", "/data/orbit-store"}, {"blackbox", "BlackBox", "/data/blackbox-v2"}};

std::string lower(std::string v) { for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; }

bool ends_with(const std::string& s, const char* suf) {
    const std::size_t n = std::char_traits<char>::length(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Un fichier qui n'est qu'un passage : archive reçue, morceau d'un téléchargement.
bool temp_name(const std::string& name) {
    const std::string l = lower(name);
    for (const char* e : {".7z", ".rar", ".zip", ".part", ".partial", ".tmp", ".download", ".crdownload"}) if (ends_with(l, e)) return true;
    return l.find(".7z.") != std::string::npos;  // archive en plusieurs morceaux (.7z.001…)
}

// L'état de leur téléchargement, dans nos mots : downloading, extracting, queued, paused, error, done.
std::string job_state(const Json& j) {
    const std::string st = lower(j["status"].as_string());
    if (st == "complete" || st == "completed" || st == "done" || st == "finished" || st == "installed") return "done";
    if (st.find("extract") != std::string::npos || lower(j["deliveryPhase"].as_string()).find("extract") != std::string::npos) return "extracting";
    if (st.find("pause") != std::string::npos) return "paused";
    if (st == "queued" || st == "pending" || st == "waiting") return "queued";
    if (st.find("error") != std::string::npos || st.find("fail") != std::string::npos) return "error";
    return "downloading";
}

}  // namespace

Json Engine::apps_downloads(bool fresh) {
    static std::mutex m;
    static Json cache;
    static std::int64_t at = 0;
    {
        std::lock_guard<std::mutex> l(m);
        if (!fresh && cache.is_object() && now_unix() - at < 5) return cache;
    }
    // Disque d'un chemin : le point de montage le plus long qui le contient ; sinon l'interne.
    struct Mnt { std::string mount, id, label; };
    std::vector<Mnt> mounts;
    const Json places = places_json();
    for (const auto& p : places.items())
        if (!p["mount"].as_string().empty()) mounts.push_back({p["mount"].as_string(), p["id"].as_string(), p["label"].as_string()});
    auto place_of = [&](const std::string& path, std::string& id, std::string& label) {
        std::size_t best = 0;
        id.clear(); label = "Interne";
        for (const auto& mt : mounts) {
            const std::string mp = mt.mount.back() == '/' ? mt.mount : mt.mount + "/";
            if ((path.rfind(mp, 0) == 0 || path == mt.mount) && mt.mount.size() > best) { best = mt.mount.size(); id = mt.id; label = mt.label; }
        }
    };
    auto size_of = [](const std::string& path, std::uint64_t& bytes) {
        fs::Entry e;
        if (path.empty() || !fs::stat(fs::host(path), e) || e.dir) return false;
        bytes = e.size;
        return true;
    };

    Json moved = Json::object();  // ancien chemin → nouveau (nos déplacements)
    {
        std::string mt;
        if (!fs::read(fs::host(fs::join(dir_, "apps-moved.json")), mt, 1u << 20) || !Json::parse(mt, moved) || !moved.is_object()) moved = Json::object();
    }
    Json apps = Json::array(), items = Json::array(), temps = Json::array();
    std::uint64_t temp_bytes = 0;
    for (const auto& a : kApps) {
        const bool present = fs::is_dir(fs::host(a.dir));
        Json app = Json::object();
        app.set("id", a.id); app.set("name", a.name); app.set("present", present);
        std::string txt;
        Json st;
        const bool ok = present && fs::read(fs::host(std::string(a.dir) + "/state.json"), txt, 8u << 20) && Json::parse(txt, st);
        app.set("readable", ok);
        apps.push(app);
        if (!ok) continue;
        // Ce que leur appli a installé ou monté (Orbit : installations.json, par téléchargement d'origine).
        std::map<std::string, Json> inst;
        {
            std::string it;
            Json ij;
            if (fs::read(fs::host(std::string(a.dir) + "/installations.json"), it, 8u << 20) && Json::parse(it, ij))
                for (const auto& x : ij.items()) inst[x["origin"].as_string()] = x;
        }
        std::set<std::string> known;   // fichiers que leurs téléchargements connaissent
        std::set<std::string> dirs;    // dossiers où ils rangent (pour les restes)
        for (const auto& j : st["jobs"].items()) {
            const std::string path = j["path"].as_string(), file = j["filename"].as_string();
            const std::string dir = !path.empty() ? std::string(fs::dirname(path)) : j["root"].as_string();
            const std::string archive = !dir.empty() && !file.empty() ? fs::join(dir, file) : std::string();
            if (!path.empty()) known.insert(path);
            if (!archive.empty()) known.insert(archive);
            if (!dir.empty()) dirs.insert(dir);
            std::string state = job_state(j);
            std::uint64_t bytes = 0;
            bool here = size_of(path, bytes);
            std::string cur = path, moved_from;
            if (!here && moved.has(path)) {  // déplacé par nous (Ranger) : leur liste garde l'ancien chemin
                const std::string np = moved[path].as_string();
                if (size_of(np, bytes)) { here = true; cur = np; moved_from = path; }
            }
            if (state == "done" && !here) state = "missing";  // leur liste le dit là, il n'y est plus
            Json x = Json::object();
            x.set("app", a.id); x.set("app_name", a.name);
            x.set("id", j["id"].as_string());
            x.set("title", j["title"].as_string().empty() ? file : j["title"].as_string());
            x.set("title_id", j["titleId"].as_string());
            x.set("state", state);
            x.set("received", j["received"].as_int()); x.set("total", j["total"].as_int());
            x.set("extracted", j["extracted"].as_int()); x.set("extract_total", j["extractTotal"].as_int());
            x.set("format", j["format"].as_string());
            x.set("compression", j["compression"].as_string());
            x.set("path", cur); x.set("file", std::string(fs::basename(cur.empty() ? file : cur)));
            if (!moved_from.empty()) x.set("moved_from", moved_from);
            if (here) x.set("bytes", static_cast<std::int64_t>(bytes));
            std::string pid, plabel;
            place_of(cur.empty() ? dir : cur, pid, plabel);
            x.set("place", pid); x.set("place_label", plabel);
            const std::string tid = j["titleId"].as_string();
            x.set("installed", tid.size() == 9 && fs::is_dir(fs::host("/user/app/" + tid)));
            const auto in = inst.find(j["id"].as_string());
            if (in != inst.end()) {
                const std::string sm = lower(in->second["shadowMount"].as_string());
                if (!sm.empty() && sm != "none") x.set("shadow_mount", sm);  // image : montée par ShadowMount (confirmed / unconfirmed)
            }
            if (!j["error"].as_string().empty()) x.set("error", j["error"].as_string());
            items.push(x);
            // Fichiers de passage : l'archive gardée après l'extraction ; le morceau d'un téléchargement pas fini.
            std::uint64_t ab = 0;
            if (!archive.empty() && archive != path && size_of(archive, ab)) {
                Json t = Json::object();
                t.set("app", a.id); t.set("app_name", a.name); t.set("title", x["title"].as_string());
                t.set("kind", state == "done" || state == "missing" ? "archive" : "partial");  // partial : en cours, jamais à libérer
                t.set("path", archive); t.set("bytes", static_cast<std::int64_t>(ab));
                place_of(archive, pid, plabel);
                t.set("place", pid); t.set("place_label", plabel);
                temps.push(t);
                temp_bytes += ab;
            }
        }
        // Restes : archive ou morceau dans leurs dossiers qu'aucun téléchargement ne connaît plus.
        for (const auto& o : st["orphans"].items()) if (o.is_object() && !o["path"].as_string().empty()) dirs.insert(std::string(fs::dirname(o["path"].as_string())));
        for (const auto& d : dirs) {
            std::vector<fs::Entry> es;
            if (!fs::list(fs::host(d), es)) continue;
            for (const auto& e : es) {
                const std::string p = fs::join(d, e.name);
                if (e.dir || known.count(p) || !temp_name(e.name)) continue;
                Json t = Json::object();
                t.set("app", a.id); t.set("app_name", a.name); t.set("title", e.name);
                t.set("kind", "leftover");
                t.set("path", p); t.set("bytes", static_cast<std::int64_t>(e.size));
                std::string pid, plabel;
                place_of(p, pid, plabel);
                t.set("place", pid); t.set("place_label", plabel);
                temps.push(t);
                temp_bytes += e.size;
            }
        }
    }
    Json out = Json::object();
    out.set("apps", apps);
    out.set("items", items);
    out.set("temps", temps);
    out.set("temp_bytes", static_cast<std::int64_t>(temp_bytes));
    out.set("at", now_unix());
    std::lock_guard<std::mutex> l(m);
    cache = out; at = now_unix();
    return out;
}

// Ranger (étape 2) : déplacer un téléchargement terminé vers un autre disque de cette console, dans son dossier de jeux
// (/data/homebrew, /mnt/usbN/homebrew : là où ShadowMount et nos Transferts les cherchent). Passe par la tâche de
// Fichiers (copie vérifiée, puis l'original retiré). Jamais un jeu ouvert ni une image montée ; place vérifiée.
std::string Engine::apps_dl_move(const Json& b, std::string& error) {
    const Json all = apps_downloads(true);
    Json it;
    for (const auto& x : all["items"].items()) if (x["app"].as_string() == b["app"].as_string() && x["id"].as_string() == b["id"].as_string()) it = x;
    if (!it.is_object()) { error = "téléchargement introuvable"; return ""; }
    if (it["state"].as_string() != "done") { error = it["state"].as_string() == "missing" ? "fichier introuvable : rien à déplacer" : "téléchargement pas terminé : on attend qu'il finisse"; return ""; }
    const std::string path = it["path"].as_string(), tid = it["title_id"].as_string(), from = it["place"].as_string();
    if (!tid.empty() && images::running_title() == tid) { error = "le jeu est ouvert : fermez-le avant de le déplacer"; return ""; }
    if (images::mounted(path, images::kind_of(path), tid)) { error = "image montée par ShadowMount : démontez-la avant de la déplacer"; return ""; }
    const std::string to = b["place"].as_string();
    if (to == from) { error = "déjà sur ce disque"; return ""; }
    std::string mount, label;
    std::int64_t free = -1;
    const Json places = places_json();
    for (const auto& p : places.items()) {
        if (p["id"].as_string() != to) continue;
        const std::string t = p["type"].as_string();
        if (t != "internal" && t != "usb") { error = "seulement vers un disque de cette console"; return ""; }
        mount = p["mount"].as_string(); label = p["label"].as_string(); free = p["free"].as_int(-1);
    }
    if (mount.empty()) { error = "disque introuvable"; return ""; }
    const std::int64_t need = it["bytes"].as_int() + (512ll << 20);  // une marge de 512 Mo
    if (free >= 0 && free < need) { error = "pas assez de place sur " + label; return ""; }
    const std::string dst_dir = images::games_root(mount), name(fs::basename(path)), dst = fs::join(dst_dir, name);
    if (fs::exists(fs::host(dst))) { error = "un fichier du même nom est déjà là : " + dst; return ""; }
    if (from.empty()) { error = "disque de départ inconnu"; return ""; }
    fs::mkdirs(fs::host(dst_dir));
    Json r = Json::object();
    r.set("src_place", from); r.set("src_dir", std::string(fs::dirname(path)));
    r.set("dst_place", to); r.set("dst_dir", dst_dir);
    Json names = Json::array();
    names.push(name);
    r.set("names", names);
    r.set("move", true);
    const std::string id = files_copy(r, error);
    if (id.empty()) return "";
    // Leur liste garde l'ancien chemin : on retient le nouveau pour toujours le montrer au bon endroit.
    Json moved = Json::object();
    std::string mt;
    if (!fs::read(fs::host(fs::join(dir_, "apps-moved.json")), mt, 1u << 20) || !Json::parse(mt, moved) || !moved.is_object()) moved = Json::object();
    const std::string orig = it["moved_from"].as_string().empty() ? path : it["moved_from"].as_string();
    moved.set(orig, dst);
    fs::write_atomic(fs::host(fs::join(dir_, "apps-moved.json")), moved.dump());
    log("apps-dl: déplacer %s (%s) → %s, tâche %s", it["title"].as_string().c_str(), it["app"].as_string().c_str(), dst.c_str(), id.c_str());
    return id;
}

// Libérer la place d'un fichier de passage (archive gardée, reste) : seulement avec la confirmation, jamais un
// téléchargement en cours, jamais pendant que leur appli télécharge.
std::string Engine::apps_dl_free(const Json& b, std::string& error) {
    if (b["confirm"].as_string() != "SUPPRIMER") { error = "confirmation manquante"; return ""; }
    const Json all = apps_downloads(true);
    Json t;
    for (const auto& x : all["temps"].items()) if (x["path"].as_string() == b["path"].as_string()) t = x;
    if (!t.is_object()) { error = "fichier introuvable"; return ""; }
    if (t["kind"].as_string() == "partial") { error = "téléchargement en cours : on n'y touche pas"; return ""; }
    for (const auto& x : all["items"].items()) {
        const std::string st = x["state"].as_string();
        if (x["app"].as_string() == t["app"].as_string() && st != "done" && st != "missing" && st != "error") {
            error = t["app_name"].as_string() + " télécharge en ce moment : réessayez quand il a fini";
            return "";
        }
    }
    if (t["place"].as_string().empty()) { error = "disque inconnu"; return ""; }
    Json r = Json::object();
    r.set("place", t["place"]); r.set("dir", std::string(fs::dirname(t["path"].as_string())));
    Json names = Json::array();
    names.push(std::string(fs::basename(t["path"].as_string())));
    r.set("names", names);
    r.set("confirm", "SUPPRIMER");
    const std::string id = files_delete(r, error);
    if (!id.empty()) log("apps-dl: libérer %s (%s), tâche %s", t["path"].as_string().c_str(), t["kind"].as_string().c_str(), id.c_str());
    return id;
}

// Paquets .pkg (Transferts › Paquets .pkg) : supprimer un paquet, avec confirmation. Seulement un vrai .pkg (en-tête
// vérifié), jamais pendant que son jeu tourne, jamais un fichier du système ; le fichier part pour de bon.
bool Engine::package_delete(const Json& b, std::string& error) {
    const std::string path = b["path"].as_string();
    if (b["confirm"].as_string() != "SUPPRIMER") { error = "confirmation manquante"; return false; }
    if (path.empty() || path[0] != '/' || path.find("/../") != std::string::npos || path.rfind("/user/", 0) == 0 || path.rfind("/system", 0) == 0) { error = "chemin invalide"; return false; }
    images::Info i;
    std::string e;
    if (!images::inspect(path, i, e, false) || i.kind != images::Kind::fpkg) { error = "ce n'est pas un paquet .pkg : " + path; return false; }
    if (!i.title_id.empty() && images::running_title() == i.title_id) { error = "le jeu de ce paquet est ouvert : fermez-le d'abord"; return false; }
    if (::unlink(fs::host(path).c_str()) != 0) { error = "suppression impossible : " + path; return false; }
    log("paquet supprimé : %s (%s)", path.c_str(), i.title_id.c_str());
    return true;
}

// Envoyer un paquet .pkg vers un autre appareil (Transferts › Paquets .pkg) : la tâche de Fichiers (copie vérifiée, l'original
// reste), comme la copie de fin d'un téléchargement BitTorrent. Une autre PS5 : dans son dossier de jeux (« homebrew » de
// l'endroit choisi), là où Kraken Mount et la liste des paquets les trouvent ; un NAS ou un nuage : « <racine>/Paquets .pkg ».
std::string Engine::package_send(const Json& b, std::string& error) {
    const std::string path = b["path"].as_string();
    images::Info i;
    std::string e;
    // « game » (Installer… sur une autre PS5) : un jeu qui se monte (image, dossier) part aussi, dans son dossier de jeux.
    const bool game = b["game"].as_bool();
    if (path.empty() || !images::inspect(path, i, e, false)) { error = "jeu introuvable : " + path; return ""; }
    if (game ? i.kind == images::Kind::unknown : i.kind != images::Kind::fpkg) { error = game ? "ce format ne s'installe pas : " + path : "ce n'est pas un paquet .pkg : " + path; return ""; }
    if (game && !i.title_id.empty() && images::running_title() == i.title_id) { error = "le jeu est ouvert : fermez-le d'abord"; return ""; }
    std::string src = "internal";
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        std::size_t best = 0;
        for (const auto& p : places_)
            if (!p.mount.empty() && path.rfind(p.mount + "/", 0) == 0 && p.mount.size() > best) { src = p.id; best = p.mount.size(); }
    }
    std::string dst = b["place"].as_string();
    {
        std::string inst, place;
        if (dst.rfind("peer:", 0) == 0 && store::parse_peer_id(dst, inst, place)) dst = "peerfiles:" + inst + ":" + place;
    }
    if (dst.rfind("peerfiles:", 0) != 0 && dst.rfind("remote:", 0) != 0) { error = "seulement vers un NAS, le nuage ou une autre PS5 (pour un disque d'ici : Copier ou déplacer)"; return ""; }
    if (game && dst.rfind("peerfiles:", 0) != 0) { error = "un jeu s'installe sur un disque de cette console ou sur une autre PS5, pas sur un NAS"; return ""; }
    if (read_only_place(dst)) { error = "partage en lecture seule"; return ""; }
    std::string root;
    bool found = false;
    const Json all = files_places();
    for (const auto& f : all.items())
        if (f["id"].as_string() == dst) { root = f["root"].as_string(); found = true; }
    if (!found) { error = "appareil introuvable ou éteint"; return ""; }
    while (!root.empty() && root.back() == '/') root.pop_back();
    const std::string dst_dir = dst.rfind("peerfiles:", 0) == 0 ? root + "/homebrew" : root + "/Paquets .pkg";
    Json r = Json::object();
    r.set("src_place", src);
    r.set("src_dir", std::string(fs::dirname(path)));
    r.set("dst_place", dst);
    r.set("dst_dir", dst_dir);
    Json names = Json::array();
    names.push(std::string(fs::basename(path)));
    r.set("names", names);
    r.set("move", false);
    const std::string id = files_copy(r, error);
    if (!id.empty()) log("paquet envoyé : %s → %s%s", path.c_str(), dst.c_str(), dst_dir.c_str());
    // « L'envoyer sur <PS5> et l'y installer » : une fois la copie vérifiée, l'autre PS5 l'installe par sa console.
    if (!id.empty() && b["install"].as_bool() && dst.rfind("peerfiles:", 0) == 0 && link_) {
        std::string inst, place;
        {
            const std::string rest = dst.substr(10);
            const auto c = rest.find(':');
            inst = rest.substr(0, c);
            place = c == std::string::npos ? std::string() : rest.substr(c + 1);
        }
        const std::string name(fs::basename(path)), title = i.title.empty() ? name : i.title;
        std::thread([this, id, inst, place, name, title] {
            for (int k = 0; k < 7200 && !quit_; ++k) {  // la copie : jusqu'à 4 h
                std::this_thread::sleep_for(std::chrono::seconds(2));
                tasks::Task t;
                if (!queue_->get(id, t)) continue;
                if (t.state == tasks::State::failed || t.state == tasks::State::cancelled) return;
                if (t.state != tasks::State::done) continue;
                Json places, out;
                std::string err, mount;
                if (link_->call(inst, "GET", "/api/v2/places", Json(), places, err))
                    for (const auto& p : places.items()) if (p["id"].as_string() == place) mount = p["mount"].as_string();
                if (mount.empty() || mount == "/user") mount = "/data";
                Json req = Json::object();
                req.set("path", fs::join(images::games_root(mount), name));
                const bool ok = err.empty() && link_->call(inst, "POST", "/api/v2/packages/install", req, out, err);
                log("paquet installé sur l'autre PS5 : %s — %s", name.c_str(), ok ? "demandé" : err.c_str());
                notify::send(ok ? "Installation demandée sur l'autre PS5" : "Installation refusée sur l'autre PS5", ok ? title : title + " : " + err);
                return;
            }
        }).detach();
    }
    return id;
}

// Étape 3 : ce qu'Orbit et BlackBox ont amené, et si c'est déjà sauvegardé. Une copie « complète » (le jeu, pas seulement
// ses parties) d'un même élément ou d'un même numéro, sur n'importe quel endroit : la liste des NAS et nuages est celle
// gardée en mémoire (jamais une lecture réseau ici).
Json Engine::arrivals(bool fresh) {
    static std::mutex m;
    static Json cache;
    static std::int64_t at = 0;
    {
        std::lock_guard<std::mutex> l(m);
        if (!fresh && cache.is_object() && now_unix() - at < 15) return cache;
    }
    const Json dl = apps_downloads(fresh);
    std::map<std::string, std::string> lib;  // numéro → élément de la bibliothèque (le jeu, pas « sauvegardes seulement »)
    std::vector<std::string> pids;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_)
            if ((it.kind == Kind::game_ps5 || it.kind == Kind::game_ps4) && it.title_id.size() == 9 && it.key.rfind("save:", 0) != 0) lib.emplace(it.title_id, it.key);
        for (const auto& p : places_) pids.push_back(p.id);
    }
    std::set<std::string> saved_keys, saved_tids;
    for (const auto& pid : pids) {
        const Json idx = pid.rfind("remote:", 0) == 0 ? remote_index_cached(pid) : backups_json(pid);
        for (const auto& b : idx["backups"].items()) {
            if (!b["complete"].as_bool()) continue;
            bool full = b["parts"].size() == 0;
            for (const auto& p : b["parts"].items()) if (p.as_string() != "save") full = true;
            if (!full) continue;
            saved_keys.insert(b["key"].as_string());
            saved_tids.insert(b["title_id"].as_string());
        }
    }
    std::set<std::string> later;
    {
        std::string t;
        Json j;
        if (fs::read(fs::host(fs::join(dir_, "arrivals-later.json")), t, 1u << 20) && Json::parse(t, j))
            for (const auto& x : j["title_ids"].items()) later.insert(x.as_string());
    }
    std::map<std::string, Json> by_tid;  // un jeu téléchargé deux fois : le plus récent
    for (const auto& x : dl["items"].items()) {
        const std::string tid = x["title_id"].as_string();
        if (x["state"].as_string() != "done" || tid.size() != 9) continue;
        Json a = Json::object();
        for (const char* k : {"app", "app_name", "id", "title", "title_id", "format", "path", "file", "place", "place_label", "bytes"}) a.set(k, x[k]);
        fs::Entry e;
        a.set("at", fs::stat(fs::host(x["path"].as_string()), e) ? e.mtime : 0);
        const auto li = lib.find(tid);
        const std::string key = li != lib.end() ? li->second : std::string();
        a.set("item", key);
        a.set("pkg", ends_with(lower(x["path"].as_string()), ".pkg"));
        a.set("saved", (!key.empty() && saved_keys.count(key)) || saved_tids.count(tid));
        if (later.count(tid)) a.set("later", true);
        const auto old = by_tid.find(tid);
        if (old == by_tid.end() || old->second["at"].as_int() < a["at"].as_int()) by_tid[tid] = a;
    }
    Json list = Json::array();
    int unsaved = 0, fresh_unsaved = 0, new_count = 0;
    for (auto& [tid, a] : by_tid) {
        if (!a["later"].as_bool()) ++new_count;
        if (!a["saved"].as_bool()) { ++unsaved; if (!a["later"].as_bool()) ++fresh_unsaved; }
        list.push(a);
    }
    Json out = Json::object();
    out.set("items", list);
    out.set("unsaved", unsaved);
    out.set("unsaved_new", fresh_unsaved);
    // Nouveaux jeux (pas encore vus au moment de « Vu ») : la bulle de l'Accueil, sauvegardés ou non — un jeu amené peut
    // rester là, être déplacé, compressé, installé ; ce n'est pas une partie à protéger (propriétaire, 19/11).
    out.set("new_count", new_count);
    std::lock_guard<std::mutex> l(m);
    cache = out;
    at = now_unix();
    return out;
}

Json Engine::items_view(const std::string& kind, const std::string& place) {
    const Json a = arrivals();
    std::map<std::string, Json> by_key;
    for (const auto& x : a["items"].items()) {
        if (x["item"].as_string().empty()) continue;
        Json v = Json::object();
        for (const char* k : {"app", "app_name", "format", "at", "place_label", "saved"}) v.set(k, x[k]);
        by_key[x["item"].as_string()] = v;
    }
    const Json items = items_json(kind, place);
    if (by_key.empty()) return items;
    Json out = Json::array();
    for (auto it : items.items()) {
        const auto f = by_key.find(it["key"].as_string());
        if (f != by_key.end()) it.set("via", f->second);
        out.push(it);
    }
    return out;
}

Json Engine::install_here(const Json& b, std::string& error) {
    const std::string path = b["path"].as_string();
    images::Info i;
    std::string e;
    if (path.empty() || !fs::exists(fs::host(path))) { error = "jeu introuvable : " + path; return Json(); }
    if (!images::inspect(path, i, e, false)) { i = images::Info{}; i.path = path; i.kind = images::kind_of(path); }  // illisible : d'après l'extension
    // .pkg Kraken : notre moteur l'inscrit et le lance depuis le paquet (kraken.cpp) ; avec Kraken Mount installé
    // (Paramètres › Montage des jeux), il est seulement rangé dans le dossier de jeux, comme une image pour ShadowMount.
    const bool theirs = i.kind == images::Kind::fpkg && pkg_uses() == "theirs";
    if (i.kind == images::Kind::fpkg && !theirs) return kraken_mount(path, i.title_id, error);
    if (i.kind == images::Kind::unknown) { error = "ce format ne se monte pas : " + path; return Json(); }
    std::string mount;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& p : places_)
            if (!p.mount.empty() && (path.rfind(p.mount + "/", 0) == 0) && p.mount.size() > mount.size()) mount = p.mount;
    }
    if (mount.empty() || mount == "/user") mount = "/data";  // l'interne : /data (vu /user/data par le système)
    const std::string root = images::games_root(mount), name(fs::basename(path)), dst = fs::join(root, name);
    Json out = Json::object();
    if (std::string(fs::dirname(path)) == root || images::mounted(path, i.kind, i.title_id)) {
        out.set("already", true); out.set("path", path);
        if (theirs) out.set("by", "krakenmount");
        return out;
    }
    if (!i.title_id.empty() && images::running_title() == i.title_id) { error = "le jeu est ouvert : fermez-le d'abord"; return Json(); }
    if (fs::exists(fs::host(dst))) { error = "un jeu du même nom est déjà dans le dossier de jeux : " + dst; return Json(); }
    fs::mkdirs(fs::host(root));
    if (::rename(fs::host(path).c_str(), fs::host(dst).c_str()) != 0) { error = std::string("rangement impossible : ") + std::strerror(errno); return Json(); }
    // Un téléchargement d'Orbit / BlackBox : leur liste garde l'ancien chemin, on retient le nouveau (comme « Déplacer vers… »).
    const Json dl = apps_downloads(true);
    for (const auto& x : dl["items"].items()) {
        if (x["path"].as_string() != path) continue;
        Json moved = Json::object();
        std::string mt;
        if (!fs::read(fs::host(fs::join(dir_, "apps-moved.json")), mt, 1u << 20) || !Json::parse(mt, moved) || !moved.is_object()) moved = Json::object();
        moved.set(x["moved_from"].as_string().empty() ? path : x["moved_from"].as_string(), dst);
        fs::write_atomic(fs::host(fs::join(dir_, "apps-moved.json")), moved.dump());
    }
    log("installer ici : %s → %s%s", path.c_str(), dst.c_str(), theirs ? " (Kraken Mount le monte)" : "");
    ++revision_;
    out.set("path", dst);
    if (theirs) out.set("by", "krakenmount");
    else if (images_uses() == "ours") { out.set("by", "smp_ours"); smp_scan_now(); }  // notre ShadowMount : parcours tout de suite
    else out.set("by", "smp_theirs");  // le ShadowMountPlus installé : il ne parcourt pas pendant qu'une appli est ouverte (la nôtre comprise)
    return out;
}

std::string Engine::install_from(const Json& b, std::string& error) {
    const std::string sp = b["src_place"].as_string(), path = b["path"].as_string(), dp = b["dst_place"].as_string();
    const std::string how = b["how"].as_string().empty() ? std::string("mount") : b["how"].as_string();
    const bool dir = b["dir"].as_bool();
    const std::string name(fs::basename(path));
    if (sp.empty() || path.empty() || path[0] != '/' || name.empty() || !files::safe_name(name)) { error = "source invalide"; return ""; }
    if (how != "mount" && how != "console") { error = "manière d'installer inconnue"; return ""; }
    auto ends = [&](const char* x) { const std::size_t n = std::strlen(x); return name.size() > n && strcasecmp(name.c_str() + name.size() - n, x) == 0; };
    const bool pkg = !dir && ends(".pkg");
    if (!dir && !pkg && !ends(".exfat") && !ends(".ffpkg") && !ends(".ffpfsc") && !ends(".ffpfs")) { error = "ce format ne s'installe pas : " + name; return ""; }
    if (how == "console" && !pkg) { error = "seul un .pkg s'installe par la console"; return ""; }
    // Format : l'image telle quelle, le .pkg (monté ou par la console), ou l'image convertie en .pkg sur la PS5 cible.
    std::string as = b["as"].as_string();
    if (as.empty()) as = pkg ? (how == "console" ? "console" : "pkg") : "image";
    if (as != "image" && as != "pkg" && as != "console" && as != "convert") { error = "format d'installation inconnu"; return ""; }
    if (as == "convert" && (pkg || dir)) { error = "seule une image se convertit en .pkg"; return ""; }
    // La cible : une PS5 (celle-ci ou une autre jumelée) et ses disques — jamais un NAS ni le nuage (pas d'installation à distance).
    const bool peer = dp.rfind("peerfiles:", 0) == 0 || dp.rfind("peer:", 0) == 0;
    std::string dst = dp, mount;
    if (dp.rfind("peer:", 0) == 0) { std::string inst, place; if (store::parse_peer_id(dp, inst, place)) dst = "peerfiles:" + inst + ":" + place; }
    if (dst.rfind("remote:", 0) == 0) { error = "un jeu s'installe sur une PS5 (un de ses disques), pas sur un NAS ni le nuage"; return ""; }
    if (!peer) {
        MuGuard l(mu_, __FILE__, __LINE__);
        bool ok = false;
        for (const auto& p : places_) if (p.id == dst && !p.mount.empty() && (p.type == Place::Type::internal || p.type == Place::Type::usb)) { ok = true; mount = p.mount; }
        if (!ok) { error = "disque introuvable sur cette console"; return ""; }
        if (mount == "/user") mount = "/data";
    }
    if (sp == dst) { error = "le jeu est déjà sur ce disque : « Le monter là où il est »"; return ""; }
    if (as == "pkg") {  // Kraken ne lance qu'un jeu PS5 en fake PKG : vérifié sur l'en-tête avant de rien copier
        std::string e2;
        pkghead::Head ph;
        if (auto st = open_files_place(sp, e2); st && pkghead::read(*st, path, ph)) {
            const std::string no = pkghead::kraken_refusal(ph);
            if (!no.empty()) { error = no; return ""; }
        }
    }
    if (as == "convert") {
        std::string why;
        if (peer) { error = "conversion : seulement sur cette PS5 pour l'instant"; return ""; }
        if (b["platform"].as_string() == "ps4" || path.find("CUSA") != std::string::npos) { error = "jeu PS4 : la conversion fabrique un .pkg PS5"; return ""; }
        if (!pkg_ok_at(dst, why)) { error = ".pkg impossible ici : " + why; return ""; }
    }
    const std::string dst_dir = peer ? std::string("/homebrew") : images::games_root(mount);
    Json r = Json::object();
    r.set("src_place", sp);
    r.set("src_dir", std::string(fs::dirname(path)));
    r.set("dst_place", dst);
    r.set("dst_dir", dst_dir);
    Json names = Json::array();
    names.push(name);
    r.set("names", names);
    // La source reste, sauf un fichier d'attente d'un envoi depuis le PC (retiré après la copie vérifiée).
    r.set("move", b["move"].as_bool() && path.rfind("/data/pbc-upload/", 0) == 0);
    const std::string id = files_copy(r, error);
    if (id.empty()) return "";
    log("installer depuis %s : %s → %s%s (%s)", sp.c_str(), path.c_str(), dst.c_str(), dst_dir.c_str(), how.c_str());
    // Une fois la copie vérifiée : (conversion en .pkg,) montage ou installation par la console, ici ou sur l'autre PS5 ;
    // une seule tâche à étapes, suivie dans Transferts et Tâches (install_jobs.cpp).
    InstallJobSpec spec;
    spec.as = as; spec.how = how; spec.src_place = sp; spec.dst = dst; spec.dst_dir = dst_dir; spec.name = name; spec.copy_task = id; spec.peer = peer;
    spec.bytes = static_cast<std::uint64_t>(b["size"].as_int());
    spec.title = b["title"].as_string().empty() ? name : b["title"].as_string();
    {
        const Json places = files_places();
        for (const auto& p : places.items()) {
            if (p["id"].as_string() == sp) spec.src_label = p["label"].as_string();
            if (p["id"].as_string() == dst) spec.target_label = p["kind"].as_string() == "peer" ? p["label"].as_string() : "cette PS5 · " + p["label"].as_string();
        }
    }
    if (spec.target_label.empty()) spec.target_label = dst;
    return install_job_start(spec);
}

namespace {
bool game_file_name(const std::string& name) {
    auto ends = [&](const char* x) { const std::size_t n = std::strlen(x); return name.size() > n && strcasecmp(name.c_str() + name.size() - n, x) == 0; };
    return ends(".pkg") || ends(".exfat") || ends(".ffpkg") || ends(".ffpfsc") || ends(".ffpfs");
}
}  // namespace

// Où écrire un fichier envoyé depuis le PC : le dossier de jeux du disque choisi (cette PS5) ; pour une autre PS5, un
// dossier d'attente sur l'interne d'ici, /data/pbc-upload (la copie vers elle suit, puis ce fichier est retiré).
bool Engine::upload_path(const std::string& place, const std::string& name, std::string& path, std::string& error) {
    if (name.empty() || !files::safe_name(name) || !game_file_name(name)) { error = "seulement une image de jeu ou un .pkg : " + name; return false; }
    if (place.rfind("peerfiles:", 0) == 0 || place.rfind("peer:", 0) == 0) { path = "/data/pbc-upload/" + name; return true; }  // hors de notre dossier (Fichiers n'y copie pas)
    if (place.rfind("remote:", 0) == 0) { error = "un jeu s'installe sur une PS5 (un de ses disques), pas sur un NAS ni le nuage"; return false; }
    std::string mount;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& p : places_) if (p.id == place && !p.mount.empty() && (p.type == Place::Type::internal || p.type == Place::Type::usb)) mount = p.mount;
    }
    if (mount.empty()) { error = "disque introuvable sur cette console"; return false; }
    if (mount == "/user") mount = "/data";
    path = fs::join(images::games_root(mount), name);
    return true;
}

Json Engine::upload_done(const Json& b, std::string& error) {
    const std::string place = b["place"].as_string(), name = b["name"].as_string();
    const std::string how = b["how"].as_string().empty() ? std::string("mount") : b["how"].as_string();
    std::string path;
    if (!upload_path(place, name, path, error)) return Json();
    fs::Entry st;
    if (!fs::stat(fs::host(path + ".part"), st) || st.size != static_cast<std::uint64_t>(b["size"].as_int())) { error = "envoi incomplet : reprenez-le"; return Json(); }
    if (fs::exists(fs::host(path))) { error = "un fichier du même nom est déjà là : " + path; return Json(); }
    if (::rename(fs::host(path + ".part").c_str(), fs::host(path).c_str()) != 0) { error = std::string("rangement impossible : ") + std::strerror(errno); return Json(); }
    log("envoi depuis le PC : %s (%llu octets) → %s", name.c_str(), static_cast<unsigned long long>(st.size), path.c_str());
    Json out = Json::object();
    out.set("path", path);
    Json req = Json::object();
    if (place.rfind("peerfiles:", 0) == 0 || place.rfind("peer:", 0) == 0) {  // vers l'autre PS5 : copie vérifiée, puis l'attente est retirée
        std::string internal;
        {
            MuGuard l(mu_, __FILE__, __LINE__);
            for (const auto& p : places_) if (p.type == Place::Type::internal) internal = p.id;
        }
        req.set("src_place", internal); req.set("path", path); req.set("dst_place", place); req.set("how", how); req.set("move", true);
        const std::string id = install_from(req, error);
        if (id.empty()) return Json();
        out.set("task", id);
        return out;
    }
    req.set("path", path);
    const Json r = how == "console" ? package_install(req, error) : install_here(req, error);
    if (r.is_null()) return Json();
    out.set("result", r);
    return out;
}

Json Engine::package_install(const Json& b, std::string& error) {
    static std::mutex one;
    std::unique_lock<std::mutex> l(one, std::try_to_lock);
    if (!l.owns_lock()) { error = "une installation est déjà en cours de demande : un instant"; return Json(); }
    const std::string path = b["path"].as_string();
    images::Info i;
    std::string e;
    if (path.empty() || !images::inspect(path, i, e, false) || i.kind != images::Kind::fpkg) { error = "ce n'est pas un paquet .pkg : " + path; return Json(); }
    const std::string run = images::running_title();
    if (!run.empty() && run != "PPSA99950") { error = "un jeu est ouvert : fermez-le avant d'installer"; return Json(); }
    const std::string cid = i.content_id;  // lu dans l'en-tête par inspect
    if (cid.empty()) { error = "paquet sans identifiant de contenu : la console ne peut pas l'installer"; return Json(); }
    if (!pkginst::install(path, cid, i.title, error)) { log("installer un paquet : %s — %s", path.c_str(), error.c_str()); return Json(); }
    log("installer un paquet : %s (%s) demandé à la console", path.c_str(), cid.c_str());
    Json out = Json::object();
    out.set("content_id", cid);
    out.set("started", true);
    return out;
}

bool Engine::arrivals_later() {
    const Json a = arrivals(true);
    Json ids = Json::array();
    for (const auto& x : a["items"].items()) ids.push(x["title_id"]);
    Json j = Json::object();
    j.set("title_ids", ids);
    const bool ok = fs::write_atomic(fs::host(fs::join(dir_, "arrivals-later.json")), j.dump() + "\n");
    (void)arrivals(true);
    ++revision_;
    return ok;
}

// « Sauvegarder maintenant » (carte de l'Accueil) : un jeu installé se sauvegarde comme les autres (vers l'endroit choisi,
// sinon l'endroit habituel) ; un .pkg pas installé est copié tel quel vers un NAS, le nuage ou une autre PS5 (« Envoyer »).
Json Engine::arrivals_backup(const Json& b, std::string& error) {
    const Json a = arrivals(true);
    std::set<std::string> want;
    for (const auto& x : b["title_ids"].items()) want.insert(x.as_string());
    const std::string place = b["place"].as_string();
    Json tasks = Json::array(), skipped = Json::array();
    for (const auto& x : a["items"].items()) {
        if (x["saved"].as_bool() || (!want.empty() && !want.count(x["title_id"].as_string()))) continue;
        std::string id, err;
        if (!x["item"].as_string().empty()) {
            if (!backup(x["item"].as_string(), {}, place, id, err)) id.clear();
        } else if (x["pkg"].as_bool()) {
            if (place.rfind("remote:", 0) != 0 && place.rfind("peer:", 0) != 0) err = "paquet .pkg : choisissez un NAS, le nuage ou une autre PS5";
            else { Json s = Json::object(); s.set("path", x["path"]); s.set("place", place); id = package_send(s, err); }
        } else err = "pas encore dans la bibliothèque";
        if (!id.empty()) tasks.push(id);
        else { Json k = Json::object(); k.set("title", x["title"]); k.set("error", err); skipped.push(k); }
    }
    if (tasks.size() == 0 && skipped.size() == 0) { error = "rien à sauvegarder : tout est déjà sauvegardé"; return Json(); }
    Json out = Json::object();
    out.set("tasks", tasks);
    out.set("skipped", skipped);
    return out;
}

}  // namespace pbc
