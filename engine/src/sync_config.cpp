// Réglages des applis (Synchro › Applis, modes « appli + réglages » et « réglages seuls »).
//
// Où est la config d'une appli : /data/<dossier>, trouvé par son nom (« Prospero Themes » → prospero-themes) ou son
// numéro. Ce qu'on en prend : un PROFIL par appli connue (fichiers précis), sinon une règle prudente — seulement de
// petits fichiers texte de config (.json .ini .conf .cfg .toml .xml), jamais jeux, sauvegardes, caches, journaux,
// listes de montage ni données propres à la console.
//
// Sûreté : c'est la console qui REÇOIT qui choisit le dossier (son propre profil) et refuse tout fichier hors profil ;
// avant d'écrire, une copie de sécurité des fichiers remplacés est faite (<état>/config-safety/<appli>/<date>/).
#include <sys/stat.h>

#include <algorithm>
#include <functional>
#include <cstdio>
#include <unistd.h>
#include <thread>
#include <map>
#include <cstring>
#include <set>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "images/identify.hpp"
#include "sync_parties.hpp"

namespace pbc {

namespace {

struct Profile { const char* folder; std::vector<std::string> files, dirs; };  // vides : règle générale
const std::vector<Profile>& profiles() {
    static const std::vector<Profile> p = {
        {"shadowmount", {"config.ini"}, {}},                                   // pas manual.lst (montages de CETTE console)
        {"prospero-themes", {"settings.json", "look.json", "community.json"}, {}},
        {"prosperoeden", {}, {"config"}},
        {"eden", {}, {"config"}},                                              // pas roms, keys, firmware, user (parties)
        // Églantine (Switch 2) : seulement ses réglages. Jamais config/remote/ (save-sync.json porte le jeton du serveur
        // de sauvegardes, et l'inscription de CETTE console).
        {"eglantineps5", {"config/eglantineps5.json"}, {}},
        {"porpoise", {"options.ini", "settings.ini", "recommended.ini"}, {}},
        {"breeze", {"breeze.conf", "presets.conf"}, {}},
    };
    return p;
}

std::string norm(const std::string& s) {  // « Prospero Themes » / « prospero-themes » → « prosperothemes »
    std::string o;
    for (unsigned char c : s) if (std::isalnum(c)) o += static_cast<char>(std::tolower(c));
    return o;
}

bool ends(const std::string& s, const char* suf) { const std::size_t n = std::strlen(suf); return s.size() >= n && s.compare(s.size() - n, n, suf) == 0; }

bool text_ok(const std::string& d) {  // texte UTF-8 sans octet nul (les fichiers binaires ne voyagent pas)
    if (d.find('\0') != std::string::npos) return false;
    for (std::size_t i = 0; i < d.size();) {
        const unsigned char c = static_cast<unsigned char>(d[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > d.size()) return false;
        for (int k = 1; k < n; ++k) if ((static_cast<unsigned char>(d[i + k]) >> 6) != 2) return false;
        i += static_cast<std::size_t>(n);
    }
    return true;
}

constexpr std::uint64_t max_file = 1u << 20;  // 1 Mo par fichier de config

// Ce fichier (chemin relatif au dossier de l'appli) fait-il partie des réglages ?
bool wanted(const Profile* pr, const std::string& rel, std::uint64_t size) {
    if (size > max_file || rel.find("..") != std::string::npos || rel.empty() || rel[0] == '/') return false;
    if (pr && (!pr->files.empty() || !pr->dirs.empty())) {
        for (const auto& f : pr->files) if (rel == f) return true;
        for (const auto& d : pr->dirs) if (rel.rfind(d + "/", 0) == 0 && std::count(rel.begin(), rel.end(), '/') <= 3) return true;
        return false;
    }
    // Règle générale : petits fichiers texte de config, au premier niveau ou sous « config » / « settings ».
    std::string low;
    for (char c : rel) low += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto slash = low.find('/');
    if (slash != std::string::npos) {
        const std::string top = low.substr(0, slash);
        if (top != "config" && top != "settings") return false;
        if (std::count(low.begin(), low.end(), '/') > 2) return false;
    }
    for (const char* bad : {"log", "lock", "cache", "history", "state", "index", "tmp", "token", "device", "session"})
        if (low.find(bad) != std::string::npos) return false;
    for (const char* ext : {".json", ".ini", ".conf", ".cfg", ".toml", ".xml", ".yaml", ".yml"}) if (ends(low, ext)) return true;
    return false;
}

void walk(const std::string& root, const std::string& rel, int depth, const Profile* pr, std::vector<std::pair<std::string, std::uint64_t>>& out) {
    if (depth > 3) return;
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(rel.empty() ? root : fs::join(root, rel)), es)) return;
    for (const auto& e : es) {
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
        if (e.dir) { walk(root, r, depth + 1, pr, out); continue; }
        if (wanted(pr, r, e.size)) out.push_back({r, e.size});
    }
}

}  // namespace

// Dossier de config de l'appli sur CETTE console, et son profil (nullptr : règle générale).
static bool config_folder(const std::string& title_id, const std::string& title, std::string& folder, const Profile*& pr) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host("/data"), es)) return false;
    std::string n = norm(title), n2 = n;
    if (n2.size() > 3 && ends(n2, "ps5")) n2 = n2.substr(0, n2.size() - 3);
    for (const auto& e : es) {
        if (!e.dir) continue;
        const std::string d = norm(e.name);
        if (d.empty() || !(d == n || d == n2 || e.name == title_id)) continue;
        folder = "/data/" + e.name;
        pr = nullptr;
        for (const auto& p : profiles()) if (e.name == p.folder) pr = &p;
        return true;
    }
    return false;
}

Json Engine::app_config(const std::string& title_id, const std::string& title, bool with_data) {
    Json out = Json::object();
    std::string folder;
    const Profile* pr = nullptr;
    if (!config_folder(title_id, title, folder, pr)) return out;  // pas de dossier de config connu ici
    std::vector<std::pair<std::string, std::uint64_t>> files;
    walk(folder, "", 0, pr, files);
    std::sort(files.begin(), files.end());
    Sha256 all;
    std::int64_t newest = 0;
    Json list = Json::array();
    for (const auto& [rel, size] : files) {
        std::string data;
        if (!fs::read(fs::host(fs::join(folder, rel)), data, max_file) || !text_ok(data)) continue;
        struct ::stat st {};
        if (::stat(fs::host(fs::join(folder, rel)).c_str(), &st) == 0) newest = std::max<std::int64_t>(newest, st.st_mtime);
        all.add(rel.data(), rel.size()); all.add("\n", 1); all.add(data.data(), data.size());
        Json f = Json::object();
        f.set("rel", rel); f.set("size", static_cast<std::int64_t>(data.size())); f.set("sha", sha256_hex(data));
        if (with_data) f.set("data", data);
        list.push(f);
    }
    out.set("folder", folder);
    out.set("profile", pr ? std::string(pr->folder) : std::string("général"));
    out.set("files", list);
    out.set("hash", list.size() ? all.hex() : std::string());
    out.set("mtime", newest);
    return out;
}

// Réglages reçus de l'autre PS5 : écrits dans le dossier de CETTE console (son profil), après une copie de sécurité.
bool Engine::app_config_receive(const Json& body, std::string& error) {
    const std::string tid = body["title_id"].as_string();
    std::string folder, title;
    { MuGuard l(mu_, __FILE__, __LINE__); for (const auto& it : items_) if (it.title_id == tid && it.kind == Kind::app) { title = it.title; break; } }
    if (title.empty()) { error = "appli absente de cette console"; return false; }
    if (foreground_app()["title_id"].as_string() == tid) { error = "l'appli est ouverte : fermez-la d'abord (rien n'a été modifié)"; return false; }
    const Profile* pr = nullptr;
    if (!config_folder(tid, title, folder, pr)) {
        // Pas encore de dossier ici : seulement pour une appli au profil connu (dossier sûr), créé à l'identique.
        for (const auto& p : profiles()) if (norm(p.folder) == norm(title)) { folder = std::string("/data/") + p.folder; pr = &p; }
        if (folder.empty()) { error = "dossier de réglages inconnu sur cette console"; return false; }
    }
    // Tout est vérifié avant d'écrire quoi que ce soit.
    for (const auto& f : body["files"].items()) {
        const std::string rel = f["rel"].as_string(), data = f["data"].as_string();
        if (!wanted(pr, rel, data.size()) || !text_ok(data)) { error = "fichier refusé (hors réglages) : " + rel; return false; }
    }
    const std::string safety = fs::join(fs::join(dir_, "config-safety/" + tid), iso_time(now_unix()).substr(0, 19));
    for (const auto& f : body["files"].items()) {  // copie de sécurité des fichiers remplacés
        const std::string rel = f["rel"].as_string(), cur = fs::join(folder, rel);
        std::string old;
        if (fs::read(fs::host(cur), old, max_file) && !fs::write_atomic(fs::host(fs::join(safety, rel)), old)) { error = "copie de sécurité impossible : rien n'a été modifié"; return false; }
    }
    for (const auto& f : body["files"].items())
        if (!fs::write_atomic(fs::host(fs::join(folder, f["rel"].as_string())), f["data"].as_string())) { error = "écriture impossible : " + f["rel"].as_string() + " (copie de sécurité : " + safety + ")"; return false; }
    log("config: %s ← %d fichier(s) (sécurité : %s)", tid.c_str(), static_cast<int>(body["files"].size()), safety.c_str());
    return true;
}

}  // namespace pbc

namespace pbc {

// ---- DLC (Synchro › DLC, première version : repérer et comparer) --------------------------------------------------
// Switch (Eden) : les fichiers DLC et mises à jour rangés dans le dossier du jeu ; PS5 / PS4 : /user/addcont/<jeu>/<DLC>.
// Un DLC n'a de sens que si le jeu est des deux côtés : sinon, c'est le jeu entier qu'on envoie (Transferts).
namespace {
std::string bracket_hex16(const std::string& name) {  // « … [0100F2C0115B7001][v0].nsp » → 0100F2C0115B7001
    for (std::size_t i = name.find('['); i != std::string::npos; i = name.find('[', i + 1)) {
        const auto j = name.find(']', i);
        if (j == i + 17 && std::all_of(name.begin() + static_cast<long>(i) + 1, name.begin() + static_cast<long>(j), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
            return name.substr(i + 1, 16);
    }
    return "";
}
std::int64_t bracket_version(const std::string& name) {  // « …[v327680].nsp » → 327680 (−1 si absent)
    const auto i = name.find("[v");
    if (i == std::string::npos) return -1;
    std::int64_t v = 0;
    std::size_t j = i + 2;
    for (; j < name.size() && std::isdigit(static_cast<unsigned char>(name[j])); ++j) v = v * 10 + (name[j] - '0');
    return j > i + 2 && j < name.size() && name[j] == ']' ? v : -1;
}
}  // namespace

Json Engine::dlc_list() {
    std::vector<Item> items;
    { MuGuard l(mu_, __FILE__, __LINE__); items = items_; }
    Json games = Json::array(), installed = Json::array(), dirs = Json::object();
    std::set<std::string> seen;
    for (const auto& it : items) {
        if (it.title_id.empty()) continue;
        const bool sw = it.kind == Kind::rom && it.platform == "Switch";
        if (sw && !dirs.has(it.title_id))  // dossier du jeu : c'est là que vont ses DLC et mises à jour
            for (const auto& c : it.components) {
                if (c.part == Part::dlc || c.part == Part::update || c.sources.empty()) continue;
                const auto& s0 = c.sources[0];
                dirs.set(it.title_id, s0.dir ? s0.source : std::string(fs::dirname(s0.source)));
                break;
            }
        if (sw || it.kind == Kind::game_ps5 || it.kind == Kind::game_ps4) if (seen.insert(it.title_id).second) installed.push(it.title_id);
        if (!sw) continue;
        Json parts = Json::array();
        for (const auto& c : it.components) {
            if (c.part != Part::dlc && c.part != Part::update) continue;
            const std::string name = c.sources.empty() ? std::string() : std::string(fs::basename(c.sources[0].source));
            Json x = Json::object();
            x.set("part", c.part == Part::dlc ? "dlc" : "update");
            x.set("id", bracket_hex16(name).empty() ? name : bracket_hex16(name));
            x.set("label", c.label); x.set("file", name); x.set("bytes", static_cast<std::int64_t>(c.bytes));
            x.set("version", bracket_version(name));
            if (!c.sources.empty() && !c.sources[0].dir) x.set("path", c.sources[0].source);
            parts.push(x);
        }
        if (!parts.size()) continue;
        Json g = Json::object();
        g.set("title_id", it.title_id); g.set("title", it.title); g.set("kind", "switch"); g.set("parts", parts);
        games.push(g);
    }
    // PS5 / PS4 : un dossier par DLC installé (vide = rien d'installé)
    std::vector<fs::Entry> tids;
    fs::list(fs::host("/user/addcont"), tids);
    for (const auto& t : tids) {
        if (!t.dir) continue;
        std::vector<fs::Entry> ds;
        fs::list(fs::host(fs::join("/user/addcont", t.name)), ds);
        Json parts = Json::array();
        for (const auto& d : ds) {
            if (!d.dir) continue;
            std::vector<fs::Entry> inside;
            if (!fs::list(fs::host(fs::join(fs::join("/user/addcont", t.name), d.name)), inside) || inside.empty()) continue;
            Json x = Json::object();
            x.set("part", "dlc"); x.set("id", d.name); x.set("label", d.name); x.set("file", d.name);
            parts.push(x);
        }
        if (!parts.size()) continue;
        std::string title = t.name;
        for (const auto& it : items) if (it.title_id == t.name) { title = it.title; break; }
        Json g = Json::object();
        g.set("title_id", t.name); g.set("title", title); g.set("kind", t.name.rfind("CUSA", 0) == 0 ? "ps4" : "ps5"); g.set("parts", parts);
        games.push(g);
    }
    // Version de chaque jeu PS5 / PS4 installé (param.json de /user/app : la mise à jour y est fondue)
    Json versions = Json::object(), names = Json::object();
    for (const auto& it : items) if (!it.title_id.empty() && !it.title.empty() && !names.has(it.title_id)) names.set(it.title_id, it.title);
    for (const auto& t : installed.items()) {
        const std::string tid = t.as_string();
        if (tid.size() != 9 || !(tid.rfind("PPSA", 0) == 0 || tid.rfind("CUSA", 0) == 0)) continue;
        const Json id = images::identify("/user/app/" + tid);
        if (!id["version"].as_string().empty()) versions.set(tid, id["version"]);
    }
    // Où pourraient être rangés les paquets (étape suivante) : une clé USB sur la console ou un NAS
    Json stores = Json::array();
    const Json places = places_json();  // gardé en vie pendant la boucle
    for (const auto& p : places.items()) {
        const std::string ty = p["type"].as_string();
        if (ty != "usb" && ty != "ftp") continue;
        Json s = Json::object();
        s.set("id", p["id"]); s.set("type", ty == "ftp" ? "nas" : "usb"); s.set("label", p["label"]);
        stores.push(s);
    }
    Json out = Json::object();
    out.set("games", games);
    out.set("installed", installed);
    out.set("versions", versions);
    out.set("names", names);
    out.set("dirs", dirs);
    out.set("library", pkg_library()["packages"]);
    out.set("stores", stores);
    return out;
}

Json Engine::dlc_plan(const std::string& peer) {
    Json out = Json::object();
    out.set("peer", peer);
    if (!link_) { out.set("error", "liaison indisponible"); return out; }
    Json there;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/dlc/list", Json(), there, err, 60) || !there.is_object()) { out.set("error", "autre PS5 injoignable : " + err); return out; }
    Json cmp = dlc_compare(dlc_list(), there);
    out.set("titles", cmp["titles"]);
    out.set("stores", cmp["stores"]);
    out.set("summary", cmp["summary"]);
    return out;
}

namespace {
bool dlc_file_ok(const std::string& f) {  // un simple nom de paquet Switch, jamais un chemin
    if (f.empty() || f.find('/') != std::string::npos || f.find("..") != std::string::npos || f[0] == '.') return false;
    std::string low = f;
    for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const char* e : {".nsp", ".nsz", ".xci", ".xcz"}) if (low.size() > std::strlen(e) && low.compare(low.size() - std::strlen(e), std::string::npos, e) == 0) return true;
    return false;
}
}  // namespace

namespace {
const char* tr_internal() { return "Interne"; }
// « EP3969-PPSA11386_00-007FIRSTLIGHT000 » : région+éditeur, jeu, étiquette de 16 caractères.
bool find_content_id(const std::string& s, std::string& cid) {
    for (std::size_t i = 0; i + 36 <= s.size(); ++i) {
        auto up = [&](std::size_t k) { return s[k] >= 'A' && s[k] <= 'Z'; };
        auto dg = [&](std::size_t k) { return s[k] >= '0' && s[k] <= '9'; };
        auto al = [&](std::size_t k) { return up(k) || dg(k); };
        if (!(up(i) && up(i + 1) && dg(i + 2) && dg(i + 3) && dg(i + 4) && dg(i + 5) && s[i + 6] == '-')) continue;
        if (!(up(i + 7) && up(i + 8) && up(i + 9) && up(i + 10))) continue;
        bool ok = true;
        for (std::size_t k = i + 11; k < i + 16; ++k) ok = ok && dg(k);
        ok = ok && s[i + 16] == '_' && dg(i + 17) && dg(i + 18) && s[i + 19] == '-';
        for (std::size_t k = i + 20; ok && k < i + 36; ++k) ok = al(k);
        if (ok) { cid = s.substr(i, 36); return true; }
    }
    return false;
}
}  // namespace

Json Engine::pkg_library(bool fresh) {
    static std::mutex m;
    static Json cache;
    static std::int64_t at = 0;
    {
        std::lock_guard<std::mutex> l(m);
        if (!fresh && cache.is_object() && now_unix() - at < 300) return cache;
    }
    // Où chercher : la racine de chaque disque USB (quelques niveaux) et les dossiers de téléchargement connus.
    std::vector<std::pair<std::string, std::string>> roots;  // chemin, libellé
    const Json places = places_json();
    for (const auto& p : places.items()) if (p["type"].as_string() == "usb") roots.push_back({p["mount"].as_string(), p["label"].as_string()});
    for (const char* d : {"/data/pkgi", "/data/pkg", "/data/pkgs"}) if (fs::is_dir(fs::host(d))) roots.push_back({d, std::string(tr_internal())});
    static const std::set<std::string> skip = {"PBC Backup", "System Volume Information", "$RECYCLE.BIN", ".Trashes", ".Spotlight-V100", "ROMs", "roms", "PSP", "psx", "ps2", "Dreamcast", "genplus", "mupen64plus", "pce", "stella", "snes9x", "fbneo", "saves", "states"};
    // Identifiant de contenu de chaque jeu installé (param.json) : même étiquette → jeu / mise à jour ; autre → DLC.
    std::map<std::string, std::string> game_label;
    {
        std::vector<fs::Entry> apps;
        fs::list(fs::host("/user/app"), apps);
        for (const auto& a : apps) {
            if (!a.dir || a.name.size() != 9) continue;
            std::string txt;
            Json pj;
            if (fs::read(fs::host("/user/app/" + a.name + "/sce_sys/param.json"), txt, 1 << 20) && Json::parse(txt, pj) && pj["contentId"].as_string().size() >= 36)
                game_label[a.name] = pj["contentId"].as_string().substr(20, 16);
        }
    }
    Json list = Json::array();
    int seen = 0;
    std::function<void(const std::string&, const std::string&, int)> walk = [&](const std::string& dir, const std::string& label, int depth) {
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(dir), es)) return;
        for (const auto& e : es) {
            if (++seen > 20000) return;
            const std::string path = fs::join(dir, e.name);
            if (e.dir) { if (depth < 3 && !skip.count(e.name) && e.name[0] != '.') walk(path, label, depth + 1); continue; }
            if (e.name.size() < 5) continue;
            std::string low = e.name.substr(e.name.size() - 4);
            for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low != ".pkg") continue;
            std::string cid;
            if (!find_content_id(e.name, cid)) {  // pas dans le nom : l'en-tête du paquet le contient en clair
                std::string head;
                FILE* f = std::fopen(fs::host(path).c_str(), "rb");
                if (f) { head.resize(65536); head.resize(std::fread(head.data(), 1, head.size(), f)); std::fclose(f); }
                find_content_id(head, cid);
            }
            Json x = Json::object();
            x.set("file", e.name); x.set("path", path); x.set("place", label); x.set("bytes", static_cast<std::int64_t>(e.size));
            if (!cid.empty()) {
                const std::string tid = cid.substr(7, 9), lab = cid.substr(20, 16);
                x.set("content_id", cid); x.set("title_id", tid);
                const auto g = game_label.find(tid);
                x.set("kind", g == game_label.end() ? "unknown" : g->second == lab ? "game_or_update" : "dlc");
                x.set("installed", g != game_label.end());
            } else x.set("kind", "unknown");
            list.push(x);
        }
    };
    for (const auto& [d, l] : roots) walk(d, l, 0);
    Json out = Json::object();
    out.set("packages", list);
    out.set("at", now_unix());
    std::lock_guard<std::mutex> l(m);
    cache = out; at = now_unix();
    return out;
}

bool Engine::dlc_read(const std::string& title_id, const std::string& id, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error) {
    std::string path;  // seulement un paquet DLC / mise à jour d'un jeu Switch scanné ici
    {
        MuGuard l(mu_, __FILE__, __LINE__);
        for (const auto& it : items_) {
            if (it.title_id != title_id || it.kind != Kind::rom || it.platform != "Switch") continue;
            for (const auto& c : it.components) {
                if ((c.part != Part::dlc && c.part != Part::update) || c.sources.empty() || c.sources[0].dir) continue;
                const std::string name(fs::basename(c.sources[0].source));
                const std::string cid = bracket_hex16(name).empty() ? name : bracket_hex16(name);
                if (cid == id && dlc_file_ok(name)) path = c.sources[0].source;
            }
        }
    }
    if (path.empty()) { error = "paquet inconnu"; return false; }
    FILE* f = std::fopen(fs::host(path).c_str(), "rb");
    if (!f) { error = "fichier introuvable"; return false; }
    limit = std::min<std::uint64_t>(limit ? limit : (8u << 20), 8u << 20);
    out.clear();
    if (::fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0) { out.resize(limit); out.resize(std::fread(out.data(), 1, limit, f)); }
    std::fclose(f);
    return true;
}

Json Engine::dlc_start(const Json& body, std::string& error) {
    if (!link_) { error = "liaison indisponible"; return Json(); }
    const std::string peer = body["peer"].as_string(), dir = body["direction"].as_string();
    std::set<std::string> only;
    for (const auto& t : body["title_ids"].items()) only.insert(t.as_string());
    if (dir == "there") {  // le pair vient chercher chez nous : on le lui demande
        Json b = Json::object(), out;
        b.set("peer", link_id_); b.set("direction", "here"); b.set("title_ids", body["title_ids"]);
        std::string err;
        if (!link_->call(peer, "POST", "/api/v2/sync/dlc/start", b, out, err, 60)) { error = "autre PS5 : " + err; return Json(); }
        Json r = out.is_object() ? out : Json::object();
        r.set("remote", true);
        return r;
    }
    if (dir != "here") { error = "direction inconnue"; return Json(); }
    // Eden ouvert : il lit ses dossiers, on n'y ajoute rien
    const std::string fg = foreground_app()["title_id"].as_string();
    if (fg == "PPSA99008") { error = "Eden est ouvert : fermez-le d'abord (rien n'a été copié)"; return Json(); }
    Json there;
    std::string err;
    if (!link_->call(peer, "GET", "/api/v2/dlc/list", Json(), there, err, 60) || !there.is_object()) { error = "autre PS5 injoignable : " + err; return Json(); }
    const Json here = dlc_list();
    const Json cmp = dlc_compare(here, there);
    struct Job { std::string tid, title, id, file, dir; std::int64_t bytes = 0; };
    std::vector<Job> jobs;
    std::int64_t need = 0;
    std::string skipped;
    for (const auto& t : cmp["titles"].items()) {
        const std::string tid = t["title_id"].as_string(), st = t["state"].as_string();
        if ((!only.empty() && !only.count(tid)) || t["kind"].as_string() != "switch" || !(st == "missing_here" || st == "both")) continue;
        const std::string d = here["dirs"][tid].as_string();
        if (d.empty() || !fs::is_dir(fs::host(d))) { skipped += (skipped.empty() ? "" : ", ") + t["title"].as_string(); continue; }
        std::vector<Json> parts;
        for (const auto& x : t["missing_here_list"].items()) parts.push_back(x);
        if (t["update"].as_string() == "there_newer") {  // la mise à jour la plus récente de là-bas (l'ancienne d'ici reste en place)
            Json best;
            for (const auto& g : there["games"].items()) if (g["title_id"].as_string() == tid)
                for (const auto& x : g["parts"].items()) if (x["part"].as_string() == "update" && (!best.is_object() || x["version"].as_int(-1) > best["version"].as_int(-1))) best = x;
            if (best.is_object()) parts.push_back(best);
        }
        for (const auto& x : parts) {
            const std::string file = x["file"].as_string();
            if (!dlc_file_ok(file) || fs::exists(fs::host(fs::join(d, file)))) continue;  // jamais d'écrasement
            jobs.push_back({tid, t["title"].as_string(), x["id"].as_string(), file, d, x["bytes"].as_int()});
            need += x["bytes"].as_int();
        }
    }
    if (jobs.empty()) { error = skipped.empty() ? "rien à compléter ici" : "dossier du jeu introuvable ici : " + skipped; return Json(); }
    // Place : tout doit tenir, avec une marge de 1 Go par disque
    std::map<std::string, std::int64_t> per_dir;
    for (const auto& j : jobs) per_dir[j.dir] += j.bytes;
    for (const auto& [d, b] : per_dir) {
        fs::Space sp;
        if (fs::space(fs::host(d), sp) && static_cast<std::int64_t>(sp.free) < b + (1ll << 30)) { error = "pas assez de place pour " + std::string(fs::basename(d)) + " : " + std::to_string(b >> 20) + " Mo nécessaires"; return Json(); }
    }
    if (parties_running_.exchange(true)) { error = "une mise à niveau est déjà en cours"; return Json(); }
    {
        std::lock_guard<std::mutex> l(sync_mu_);
        parties_status_ = Json::object();
        parties_status_.set("state", "running"); parties_status_.set("peer", peer); parties_status_.set("section", "dlc");
        parties_status_.set("total", static_cast<std::int64_t>(jobs.size())); parties_status_.set("done", static_cast<std::int64_t>(0));
        parties_status_.set("bytes", need); parties_status_.set("bytes_done", static_cast<std::int64_t>(0));
        parties_status_.set("started", now_unix()); parties_status_.set("errors", Json::array()); parties_status_.set("results", Json::array());
    }
    std::thread([this, peer, jobs] {
        auto set = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); parties_status_.set(k, v); };
        auto push = [this](const char* k, const Json& v) { std::lock_guard<std::mutex> l(sync_mu_); Json a = parties_status_[k]; a.push(v); parties_status_.set(k, a); };
        std::int64_t done_bytes = 0;
        int done = 0, failed = 0;
        for (const auto& j : jobs) {
            set("current", j.title + " · " + j.file);
            const std::string final_path = fs::join(j.dir, j.file), part = final_path + ".pbc-part";
            std::string why;
            FILE* f = std::fopen(fs::host(part).c_str(), "wb");
            if (!f) why = "écriture impossible";
            std::uint64_t off = 0;
            while (f && why.empty() && static_cast<std::int64_t>(off) < j.bytes) {
                std::string chunk;
                bool got = false;
                for (int tries = 0; tries < 3 && !got; ++tries)
                    got = link_->get_raw(peer, "/api/v2/link/dlc/read?title_id=" + j.tid + "&id=" + j.id + "&offset=" + std::to_string(off) + "&limit=" + std::to_string(8u << 20), chunk, 60);
                if (!got || chunk.empty()) { why = "lecture interrompue chez l'autre PS5"; break; }
                if (std::fwrite(chunk.data(), 1, chunk.size(), f) != chunk.size()) { why = "écriture impossible (disque plein ?)"; break; }
                off += chunk.size();
                set("bytes_done", done_bytes + static_cast<std::int64_t>(off));
            }
            if (f) { if (std::fflush(f) != 0 && why.empty()) why = "écriture impossible"; ::fsync(fileno(f)); std::fclose(f); }
            if (why.empty() && static_cast<std::int64_t>(off) != j.bytes) why = "taille différente";
            if (why.empty() && fs::exists(fs::host(final_path))) why = "un fichier du même nom est apparu : rien n'est écrasé";
            if (why.empty() && std::rename(fs::host(part).c_str(), fs::host(final_path).c_str()) != 0) why = "renommage impossible";
            if (!why.empty()) {
                ::unlink(fs::host(part).c_str());  // seulement notre fichier partiel
                ++failed;
                Json e = Json::object(); e.set("title", j.title); e.set("file", j.file); e.set("error", why); push("errors", e);
            } else {
                ++done;
                Json r = Json::object(); r.set("title", j.title); r.set("file", j.file); r.set("bytes", j.bytes); push("results", r);
            }
            done_bytes += j.bytes;
            set("done", static_cast<std::int64_t>(done + failed));
        }
        request_scan();  // les nouveaux DLC apparaissent dans la bibliothèque et l'inventaire
        set("current", "");
        set("state", failed ? (done ? "partial" : "failed") : "done");
        set("finished", now_unix());
        parties_running_ = false;
    }).detach();
    Json r = parties_status();
    return r;
}

Json dlc_compare(const Json& here, const Json& there) {
    std::set<std::string> inst_h, inst_t;
    for (const auto& t : here["installed"].items()) inst_h.insert(t.as_string());
    for (const auto& t : there["installed"].items()) inst_t.insert(t.as_string());
    std::map<std::string, std::pair<Json, Json>> by;
    for (const auto& g : here["games"].items()) by[g["title_id"].as_string()].first = g;
    for (const auto& g : there["games"].items()) by[g["title_id"].as_string()].second = g;
    // Jeux PS5 / PS4 des deux côtés en versions différentes : une mise à jour manque d'un côté
    for (const auto& [tid, v] : here["versions"].members())
        if (there["versions"].has(tid) && there["versions"][tid].as_string() != v.as_string() && !by.count(tid)) by[tid];
    std::map<std::string, Json> lib;  // paquets rangés ici, par jeu
    for (const auto& p : here["library"].items()) {
        const std::string tid = p["title_id"].as_string();
        if (tid.empty()) continue;
        lib[tid].push(p);
        if ((inst_h.count(tid) || inst_t.count(tid)) && !by.count(tid)) by[tid];
    }
    auto title_of = [&](const std::string& tid, const Json& h, const Json& t) {
        if (h.is_object()) return h["title"].as_string();
        if (t.is_object()) return t["title"].as_string();
        if (!here["names"][tid].as_string().empty()) return here["names"][tid].as_string();
        if (!there["names"][tid].as_string().empty()) return there["names"][tid].as_string();
        return tid;
    };
    Json titles = Json::array();
    int n_here = 0, n_there = 0, n_same = 0, n_game = 0;
    for (const auto& [tid, p] : by) {
        const Json &h = p.first, &t = p.second;
        std::map<std::string, Json> ph, pt;  // DLC seulement (la mise à jour se compare par version)
        std::int64_t uh = -1, ut = -1;
        Json uhj, utj;
        for (const auto& x : h["parts"].items()) { if (x["part"].as_string() == "update") { if (x["version"].as_int(-1) > uh) { uh = x["version"].as_int(-1); uhj = x; } } else ph[x["id"].as_string()] = x; }
        for (const auto& x : t["parts"].items()) { if (x["part"].as_string() == "update") { if (x["version"].as_int(-1) > ut) { ut = x["version"].as_int(-1); utj = x; } } else pt[x["id"].as_string()] = x; }
        Json x = Json::object();
        x.set("title_id", tid);
        x.set("title", title_of(tid, h, t));
        x.set("kind", h.is_object() ? h["kind"] : t.is_object() ? t["kind"] : Json(tid.rfind("CUSA", 0) == 0 ? "ps4" : "ps5"));
        x.set("here", static_cast<std::int64_t>(ph.size())); x.set("there", static_cast<std::int64_t>(pt.size()));
        Json mh = Json::array(), mt = Json::array();
        std::int64_t bh = 0, bt = 0;
        for (const auto& [id, d] : ph) if (!pt.count(id)) { mt.push(d); bt += d["bytes"].as_int(); }
        for (const auto& [id, d] : pt) if (!ph.count(id)) { mh.push(d); bh += d["bytes"].as_int(); }
        // Version de mise à jour : Switch → [vNNN] du fichier ; PS5 / PS4 → version du jeu installé
        std::string vh, vt, upd;
        if (uh >= 0 || ut >= 0) {
            vh = uh >= 0 ? "v" + std::to_string(uh) : ""; vt = ut >= 0 ? "v" + std::to_string(ut) : "";
            if (uh > ut) { upd = "here_newer"; bt += uhj["bytes"].as_int(); } else if (ut > uh) { upd = "there_newer"; bh += utj["bytes"].as_int(); } else upd = "same";
        } else {
            vh = here["versions"][tid].as_string(); vt = there["versions"][tid].as_string();
            if (!vh.empty() && !vt.empty()) { const int c = compare_versions(vh, vt); upd = c > 0 ? "here_newer" : c < 0 ? "there_newer" : "same"; }
        }
        x.set("here_version", vh); x.set("there_version", vt); x.set("update", upd);
        x.set("missing_here", static_cast<std::int64_t>(mh.size())); x.set("missing_there", static_cast<std::int64_t>(mt.size()));
        x.set("missing_here_list", mh); x.set("missing_there_list", mt);
        x.set("bytes_missing_here", bh); x.set("bytes_missing_there", bt);
        const bool need_h = mh.size() || upd == "there_newer", need_t = mt.size() || upd == "here_newer";
        std::string st;
        if (!inst_t.count(tid)) st = "game_missing_there";       // le jeu n'est pas là-bas : Transferts (jeu entier)
        else if (!inst_h.count(tid)) st = "game_missing_here";
        else st = need_h && need_t ? "both" : need_t ? "missing_there" : need_h ? "missing_here" : "same";
        x.set("state", st);
        if (lib.count(tid)) x.set("library", lib[tid]);
        n_here += st == "missing_here" || st == "both"; n_there += st == "missing_there" || st == "both"; n_same += st == "same"; n_game += st.rfind("game_missing", 0) == 0;
        titles.push(x);
    }
    Json sum = Json::object();
    sum.set("to_complete_here", n_here); sum.set("to_complete_there", n_there); sum.set("same", n_same); sum.set("game_missing", n_game);
    Json out = Json::object();
    out.set("titles", titles);
    out.set("stores", here["stores"]);
    out.set("summary", sum);
    return out;
}

}  // namespace pbc
