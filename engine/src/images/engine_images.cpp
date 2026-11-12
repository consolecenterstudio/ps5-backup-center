// Moteur : images de jeux (compression .ffpfsc, images exFAT / PFS, validation, transferts
// interne ↔ USB). Adapté de PS5 Game Compressor v1.0.4 (juma-sayeh, GPL-3.0), voir images.hpp.
// Fichier séparé de engine.cpp pour garder les modifications du moteur localisées.
#include <algorithm>
#include <functional>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "images/identify.hpp"
#include "images/images.hpp"
#include "places/places.hpp"
#include "store/peer.hpp"

#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>

namespace pbc {

namespace {
// Chemin du jeu d'un élément du scanner : l'image montée (mount_img.lnk) ou le dossier
// (mount.lnk d'une inscription ShadowMountPlus, sinon le dossier lui-même).
std::string item_source(const Item& it) {
    for (const auto& c : it.components) {
        if (c.part != Part::app && c.part != Part::game) continue;
        if (c.meta.is_object() && !c.meta["image"].as_string().empty()) return c.meta["image"].as_string();
        for (const auto& s : c.sources) {
            if (!s.dir) continue;
            std::string lnk;
            if (s.source.rfind("/user/app/", 0) == 0 && fs::read(fs::host(fs::join(s.source, "mount.lnk")), lnk, 4096)) {
                lnk = lnk.substr(0, lnk.find_first_of("\r\n"));
                if (!lnk.empty() && lnk[0] == '/' && fs::is_dir(fs::host(lnk))) return lnk;
            }
            return s.source;
        }
    }
    return {};
}
}  // namespace

Json Engine::images_json(bool measure) {
    images::set_state_dir(dir_);
    std::vector<Item> items;
    std::vector<Place> places;
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        items = items_;
        places = places_;
    }
    // Disque qui porte les données (l'image ou le dossier, pas l'inscription dans /user/app).
    auto disk_of = [&](const std::string& path) {
        const Place* best = nullptr;
        for (const auto& p : places)
            if (p.type != Place::Type::internal && path.rfind(p.mount + "/", 0) == 0 && (!best || p.mount.size() > best->mount.size())) best = &p;
        Json d = Json::object();
        if (best) { d.set("id", best->id); d.set("label", best->label); d.set("mount", best->mount); return d; }
        for (const char* ext : {"/mnt/ext0", "/mnt/ext1"})
            if (path.rfind(std::string(ext) + "/", 0) == 0) {
                d.set("id", std::string("mount:") + ext);
                d.set("label", std::string(ext) == "/mnt/ext1" ? "Stockage M.2" : "Stockage étendu");
                d.set("mount", ext);
                return d;
            }
        if (path.rfind("/mnt/usb", 0) == 0) {  // disque pas encore vu par la détection des emplacements
            const std::string m = path.substr(0, path.find('/', 5) == std::string::npos ? path.size() : path.find('/', 5));
            d.set("id", "mount:" + m);
            d.set("label", "USB " + m.substr(8));
            d.set("mount", m);
            return d;
        }
        d.set("id", "internal");
        d.set("label", "Interne");
        d.set("mount", "/data");
        return d;
    };
    Json a = Json::array();
    for (const auto& i : images::discover(measure)) {
        Json j = i.to_json();
        j.set("disk", disk_of(i.path));
        for (const auto& it : items)
            if (!i.title_id.empty() && it.title_id == i.title_id) {
                j.set("item", it.key);
                if (i.title.empty()) j.set("title", it.title);
                break;
            }
        a.push(j);
    }
    Json out = Json::object();
    out.set("images", a);
    out.set("running", images::running_title());
    return out;
}

namespace {
bool image_options(Engine& e, const Json& body, images::Options& o, std::string& title, std::string& error,
                   const std::function<bool(const std::string&, Place&, std::string&)>& place_of,
                   const std::function<bool(const std::string&, Item&)>& item_of) {
    Json b = body;
    std::string type = b["type"].as_string();
    if (type.empty()) { type = b["op"].as_string(); b.set("type", type); }
    images::Op op;
    if (!images::parse_task_type(type, op) && !images::parse_op(type, op)) { error = "type inconnu"; return false; }
    // Élément du scanner (app télé) : on retrouve l'image ou le dossier du jeu.
    if (b["source"].as_string().empty() && !b["item"].as_string().empty()) {
        Item it;
        if (!item_of(b["item"].as_string(), it)) { error = "élément introuvable"; return false; }
        const std::string src = item_source(it);
        if (src.empty()) { error = "aucun dossier ni image pour cet élément"; return false; }
        b.set("source", src);
        title = it.title;
    }
    // Destination donnée comme emplacement (« internal », identifiant de disque USB) : dossier de jeux de ce stockage.
    const std::string to = b["to"].as_string();
    const bool to_is_format = to == "folder" || to == "exfat" || to == "image";  // décompression : format, pas emplacement
    const std::string place_id = !to.empty() && !to_is_format ? to : b["target"].as_string();
    if (b["target_dir"].as_string().empty() && !place_id.empty()) {
        if (place_id.rfind("remote:", 0) == 0) { error = "transfer_remote"; return false; }
        Place p;
        if (!place_of(place_id, p, error)) return false;
        b.set("target_dir", images::games_root(p.mount));
    }
    (void)e;
    if (!images::Options::from_json(b, o, error)) return false;
    if (title.empty()) title = std::string(fs::basename(o.source));
    return true;
}
}  // namespace

Json Engine::image_plan(const Json& body, std::string& error) {
    images::set_state_dir(dir_);
    images::Options o;
    std::string title;
    if (!image_options(*this, body, o, title, error,
                       [this](const std::string& id, Place& p, std::string& err) { return resolve_place(id, p, err); },
                       [this](const std::string& k, Item& it) { return find_item(k, it); }))
        return Json();
    Json j = images::plan(o).to_json();
    j.set("type", images::task_type(o.op));
    j.set("source", o.source);
    return j;
}

bool Engine::image_task(const Json& body, std::string& task_id, std::string& error) {
    images::set_state_dir(dir_);
    images::Options o;
    std::string title;
    if (!image_options(*this, body, o, title, error,
                       [this](const std::string& id, Place& p, std::string& err) { return resolve_place(id, p, err); },
                       [this](const std::string& k, Item& it) { return find_item(k, it); }))
        return false;
    // Garde-fous et place libre vérifiés tout de suite (puis de nouveau au lancement de la tâche).
    const images::Plan p = images::plan(o);
    if (!p.ok) { error = p.error; return false; }
    Json req = o.to_json();
    if (!p.title_id.empty()) req.set("title_id", p.title_id);
    if (!body["item"].as_string().empty()) req.set("item_key", body["item"]);
    if (body.has("saves")) req.set("saves", body["saves"].as_bool());  // transfert local : les sauvegardes restent sur la console
    task_id = queue_->add_image(req, title);
    return true;
}

// --- Compatibilité (constat seulement ; la gestion des correctifs est dans PS5 Center) -----------------
namespace {
// Backports ShadowMountPlus présents sur cette console : /data/shadowmount/backports/<ID>, <jeux>/backports/<ID>.
Json local_backports() {
    Json out = Json::array();
    std::vector<std::string> roots = {"/data/shadowmount/backports", "/data/homebrew/backports"};
    for (int i = 0; i < 8; ++i) roots.push_back("/mnt/usb" + std::to_string(i) + "/homebrew/backports");
    for (const auto& r : roots) {
        std::vector<fs::Entry> ents;
        if (!fs::list(fs::host(r), ents)) continue;
        for (const auto& e : ents) if (e.dir && e.name.size() == 9) {
            Json b = Json::object();
            b.set("title_id", e.name);
            b.set("path", fs::join(r, e.name));
            b.set("files", images::libraries(fs::join(r, e.name)));
            out.push(b);
        }
    }
    return out;
}

// État et preuves (codes) pour un jeu identifié et une console visée.
Json assess(const Json& id, const Json& console, const std::string& source_ext) {
    Json reasons = Json::array();
    auto reason = [&](Json r) { reasons.push(r); };
    const std::string fw = console["firmware"].as_string(), declared = id["declared_min"].as_string();
    const std::string tid = id["title_id"].as_string(), sdk = id["exe_sdk_max"].as_string();
    const int f = firmware_number(fw), d = firmware_number(declared), x = firmware_number(sdk);
    std::string state = "insufficient";
    bool below = false;
    if (!id["readable"].as_bool(true)) { Json r = Json::object(); r.set("code", "unreadable"); r.set("error", id["error"]); reason(r); }
    if (f == 0) { Json r = Json::object(); r.set("code", "no_firmware"); reason(r); }
    if (d > 0 && f > 0) {
        Json r = Json::object();
        r.set("code", "declared"); r.set("declared", declared); r.set("firmware", fw); r.set("meets", f >= d);
        reason(r);
        below = f < d;
        if (!below) state = "system_ok";  // « vérifiée » est réservé aux essais prouvés (PS5 Center)
    }
    int exes = 0, signed_exes = 0;
    for (const auto& e : id["executables"].items()) { ++exes; if (e["format"].as_string() == "self" && e["sdk"].as_string().empty()) ++signed_exes; }
    if (!sdk.empty()) { Json r = Json::object(); r.set("code", "exe_sdk"); r.set("sdk", sdk); r.set("count", exes); r.set("fits", f > 0 && x <= f); reason(r); }
    if (signed_exes) { Json r = Json::object(); r.set("code", "exe_signed"); r.set("count", signed_exes); reason(r); }
    const int libs = static_cast<int>(id["own_libs"].items().size());
    if (libs) {
        Json r = Json::object();
        r.set("code", "own_libs"); r.set("count", libs); r.set("sdk", id["own_libs_sdk_max"]);
        Json names = Json::array();
        for (const auto& l : id["own_libs"].items()) names.push(l["name"]);
        r.set("names", names);
        reason(r);
    }
    bool backport = false;
    for (const auto& b : console["backports"].items()) if (b["title_id"].as_string() == tid && !tid.empty()) {
        backport = true;
        Json r = Json::object();
        r.set("code", "backport"); r.set("path", b["path"]); r.set("count", static_cast<std::int64_t>(b["files"].items().size()));
        reason(r);
    }
    if ((libs || backport) && console["shadowmount"].is_object() && console["shadowmount"].has("backport_fakelib")) {
        Json r = Json::object();
        r.set("code", "smp_fakelib"); r.set("on", console["shadowmount"]["backport_fakelib"].as_bool());
        reason(r);
    }
    // Système déclaré non atteint : il faut un backport. S'il existe (fakelib du jeu ou backport ShadowMountPlus),
    // on le dit ; sinon le jeu risque de ne pas fonctionner (PS5 Center pourra aider, fonctions expérimentales).
    if (below) state = (backport || libs) ? "backport_present" : "no_backport";
    // Format : échecs de montage observés sur la console visée.
    std::string recommended = "keep";
    const Json& m = console["mounts"][source_ext];
    if (!source_ext.empty() && m["failed"].as_int() > 0) {
        Json r = Json::object();
        r.set("code", "format_failed"); r.set("ext", source_ext); r.set("failed", m["failed"]); r.set("ok", m["ok"]);
        reason(r);
        if (m["ok"].as_int() == 0 && source_ext == "ffpfsc") recommended = "exfat";
    }
    Json j = Json::object();
    j.set("state", state);
    j.set("reasons", reasons);
    j.set("ok", !below);  // l'interface prévient avant de continuer quand le système déclaré n'est pas atteint
    j.set("recommended_format", recommended);
    j.set("console", console["name"]);
    j.set("firmware", fw);
    j.set("required", declared);  // compatibilité avec les clients 2.51
    j.set("identity", id);
    // Backport disponible pour ce jeu : celui du jeu (fakelib) et ceux de la console visée.
    Json bp = Json::object();
    bp.set("game_libs", id["own_libs"]);
    bp.set("game_libs_sdk", id["own_libs_sdk_max"]);
    Json cbp = Json::array();
    for (const auto& b : console["backports"].items()) if (b["title_id"].as_string() == tid && !tid.empty()) cbp.push(b);
    bp.set("console", cbp);
    bp.set("smp", console["shadowmount"]);
    bp.set("apr", id["apr"]);
    j.set("backport", bp);
    return j;
}

std::string ext_of(const std::string& p) {
    const auto dot = p.rfind('.');
    const auto slash = p.rfind('/');
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : p.substr(dot + 1);
}
}  // namespace

Json Engine::foreground_app() {
    Json j = Json::object();
    int app_id = -1;
    std::string tid;
    j.set("open", images::foreground_app(app_id, tid));
    if (tid.empty()) return j;
    j.set("title_id", tid);
    j.set("ours", tid == "PPSA99950");
    std::string title, kind;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (it.title_id == tid) { title = it.title; kind = to_string(it.kind); break; } }
    if (title.empty()) title = images::identify("/user/app/" + tid)["name"].as_string();
    j.set("title", title.empty() ? tid : title);
    j.set("game", kind.rfind("game", 0) == 0);
    return j;
}

bool Engine::close_foreground(const std::string& expected, std::string& error) {
    int app_id = -1;
    std::string tid;
    if (!images::foreground_app(app_id, tid)) { error = "aucune application ouverte"; return false; }
    // Jamais autre chose que ce qui a été confirmé : si l'utilisateur a changé d'application entre-temps, on ne ferme rien.
    if (expected.empty() || tid != expected) { error = "une autre application est ouverte maintenant : rien n'a été fermé"; return false; }
    log("close_foreground: %s (app %d)", tid.c_str(), app_id);
    if (images::kill_app(app_id) != 0) { error = "fermeture refusée par le système"; return false; }
    return true;
}

Json Engine::compat_console() {
    static std::mutex mu;
    static Json cache;
    static std::int64_t at = 0;
    const std::int64_t now = now_unix_engine();
    std::lock_guard<std::mutex> l(mu);
    if (cache.is_object() && now - at < 60) return cache;
    Json j = Json::object();
    { std::lock_guard<std::mutex> l2(console_name_mu_); j.set("name", console_name_); }
    j.set("firmware", firmware());
    j.set("mounts", images::observed_mounts());
    j.set("backports", local_backports());
    // Réglages de ShadowMountPlus utiles aux backports (lecture seule) : backport_fakelib, global_fakelib.
    {
        std::string ini;
        Json smp = Json::object();
        if (fs::read(fs::host("/data/shadowmount/config.ini"), ini, 1 << 20)) {
            std::size_t pos = 0;
            while (pos < ini.size()) {
                auto nl = ini.find('\n', pos);
                if (nl == std::string::npos) nl = ini.size();
                std::string line = ini.substr(pos, nl - pos);
                pos = nl + 1;
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string k = line.substr(0, eq), v = line.substr(eq + 1);
                while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
                if (k == "backport_fakelib" || k == "global_fakelib") smp.set(k, v == "1" || v == "true");
            }
        }
        j.set("shadowmount", smp);
    }
    cache = j;
    at = now;
    return j;
}

Json Engine::compat_item(const std::string& item_key) {
    Item it;
    if (!find_item(item_key, it)) { Json j = Json::object(); j.set("error", "élément introuvable"); return j; }
    const std::string src = item_source(it);
    Json id = images::identify(src);
    // Jeu inscrit : son param.json reste lisible dans /user/app même si l'image ne l'est pas ici.
    if (id["title_id"].as_string().empty() && !it.title_id.empty()) {
        const Json reg = images::identify("/user/app/" + it.title_id);
        if (reg["readable"].as_bool()) for (const char* k : {"title_id", "name", "version", "master_version", "declared_min", "declared_sdk"}) id.set(k, reg[k]);
    }
    id.set("source", src);
    Json j = assess(id, compat_console(), ext_of(src));
    // Les autres PS5 jumelées et allumées : même constat, avec leur système, pour l'avoir sous les yeux.
    Json peers = Json::array();
    if (link_) {
        const Json ps = link_->peers();
        for (const auto& p : ps.items()) {
            if (!p["paired"].as_bool() || !p["online"].as_bool()) continue;
            Json console = Json::object(), out;
            std::string err;
            if (link_->call(p["instance"].as_string(), "GET", "/api/v2/compat/console", Json::object(), out, err) && out.is_object()) console = out;
            else if (link_->call(p["instance"].as_string(), "GET", "/api/v2/status", Json::object(), out, err)) console.set("firmware", out["firmware"]);
            console.set("name", p["name"].as_string().empty() ? p["host"] : p["name"]);
            Json a = assess(id, console, ext_of(src));
            a.set("identity", Json());  // déjà donnée une fois
            peers.push(a);
        }
    }
    j.set("peers", peers);
    return j;
}

Json Engine::send_check(const std::string& peer_place, const std::string& item_key) {
    std::string inst, remote_place;
    store::parse_peer_id(peer_place, inst, remote_place);
    Item it;
    Json id = Json::object();
    std::string src;
    if (find_item(item_key, it)) {
        src = item_source(it);
        id = images::identify(src);
        if (id["title_id"].as_string().empty() && !it.title_id.empty()) {
            const Json reg = images::identify("/user/app/" + it.title_id);
            if (reg["readable"].as_bool()) for (const char* k : {"title_id", "name", "version", "master_version", "declared_min", "declared_sdk"}) id.set(k, reg[k]);
        }
        id.set("source", src);
    }
    Json console = Json::object();
    if (link_ && !inst.empty()) {
        Json out;
        std::string err;
        if (link_->call(inst, "GET", "/api/v2/compat/console", Json::object(), out, err) && out.is_object()) console = out;
        else if (link_->call(inst, "GET", "/api/v2/status", Json::object(), out, err)) console.set("firmware", out["firmware"]);
        if (console["name"].as_string().empty()) {
            const Json peers = link_->peers();
            for (const auto& p : peers.items()) if (p["instance"].as_string() == inst) console.set("name", p["name"]);
        }
    }
    return assess(id, console, ext_of(src));
}

Json Engine::install_check(const std::string& place_id, const std::string& bundle_name) {
    Json id = Json::object();
    std::string src;
    Place place;
    std::string err, folder;
    if (bundle_name.find("..") == std::string::npos && bundle_name.find('/') == std::string::npos && resolve_place(place_id, place, err) &&
        places::backup_folder(place, settings().custom_folder, folder)) {
        const std::string bundle = fs::join(folder, bundle_name);
        // L'image de la copie d'abord (SDK des exécutables), sinon l'inscription copiée (param.json seul).
        std::vector<fs::Entry> ents;
        fs::list(fs::host(fs::join(bundle, "data/app/image")), ents);
        for (const auto& e : ents) if (!e.dir) { src = fs::join(fs::join(bundle, "data/app/image"), e.name); break; }
        if (!src.empty()) id = images::identify(src);
        if (id["title_id"].as_string().empty()) {
            fs::list(fs::host(fs::join(bundle, "data/app/registration")), ents);
            for (const auto& e : ents) if (e.dir) {
                const Json reg = images::identify(fs::join(fs::join(bundle, "data/app/registration"), e.name));
                for (const char* k : {"title_id", "name", "version", "master_version", "declared_min", "declared_sdk"}) id.set(k, reg[k]);
                if (!id.has("readable")) id.set("readable", reg["readable"]);
                break;
            }
        }
        id.set("source", src);
    }
    return assess(id, compat_console(), ext_of(src));
}

// Applis installées ici et leur version (param.json), pour la synchro des applis. Backup Center lui-même n'y est pas :
// il a ses propres mises à jour. « folder » : appli en dossier (mise à jour possible par la synchro).
Json Engine::apps_versions() {
    std::vector<Item> apps;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (it.kind == Kind::app && it.title_id != "PPSA99950" && !it.title_id.empty()) apps.push_back(it); }
    Json out = Json::array();
    for (const auto& it : apps) {
        const std::string src = item_source(it);
        Json id = images::identify(src);
        if (id["version"].as_string().empty()) { const Json reg = images::identify("/user/app/" + it.title_id); if (reg["readable"].as_bool()) id.set("version", reg["version"]); }
        Json a = Json::object();
        a.set("title_id", it.title_id); a.set("title", it.title); a.set("key", it.key);
        a.set("version", id["version"]); a.set("place", it.place_id);
        a.set("folder", fs::is_dir(fs::host(src)));
        if (fs::is_dir(fs::host(src))) {  // empreinte : même version mais fichiers différents (homebrew recompilé) → à trancher
            static std::mutex hm;
            static std::map<std::string, std::string> cache;  // chemin|taille|date → empreinte
            std::string key;
            for (const char* f : {"eboot.bin", "sce_sys/param.json"}) { fs::Entry e; if (fs::stat(fs::host(fs::join(src, f)), e)) key += std::string(f) + std::to_string(e.size) + ":" + std::to_string(e.mtime) + ";"; }
            std::string h;
            { std::lock_guard<std::mutex> l(hm); const auto c = cache.find(src + "|" + key); if (c != cache.end()) h = c->second; }
            if (h.empty() && !key.empty()) {
                Sha256 sh;
                // Par morceaux : un eboot.bin de plusieurs centaines de Mo ne passe plus en mémoire (pic de 780 Mo vu sur la 01,
                // deux calculs en même temps). Même empreinte qu'avant : mêmes octets, dans le même ordre.
                for (const char* f : {"eboot.bin", "sce_sys/param.json"}) {
                    FILE* fp = std::fopen(fs::host(fs::join(src, f)).c_str(), "rb");
                    if (!fp) continue;
                    char buf[65536];
                    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, fp)) > 0;) sh.add(buf, n);
                    std::fclose(fp);
                }
                h = sh.hex();
                std::lock_guard<std::mutex> l(hm);
                cache[src + "|" + key] = h;
            }
            a.set("hash", h);
        }
        const Json cfg = app_config(it.title_id, it.title, false);  // réglages (sans leur contenu)
        if (cfg.has("folder")) { Json c = Json::object(); c.set("folder", cfg["folder"]); c.set("profile", cfg["profile"]); c.set("hash", cfg["hash"]); c.set("mtime", cfg["mtime"]); c.set("files", static_cast<std::int64_t>(cfg["files"].size())); a.set("config", c); }
        out.push(a);
    }
    return out;
}

}  // namespace pbc
