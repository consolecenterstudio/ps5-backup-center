// Bibliothèques de jeux non installés (maquette docs/maquettes/bibliotheque-nas.html) : des dossiers déclarés (NAS,
// nuages, disque USB, l'autre PS5) où sont rangés des jeux qu'on n'a pas forcément installés. Le moteur les parcourt en
// fond, une fois, et garde un index (gamelibs.json) ; l'Accueil et la Bibliothèque montrent l'index, « Installer… »
// passe par install_from (la PS5 et le disque cibles). Rien n'est jamais écrit ni modifié dans ces dossiers.
//
// Performance : des listes de dossiers (noms, tailles) et, pour un .pkg, son en-tête seul (quelques centaines
// d'octets : jeu, mise à jour ou DLC ; PS4 ou PS5 ; fake ou officiel), gardé dans l'index et relu seulement si le
// fichier change de taille — jamais le contenu des jeux : dans un nuage, ouvrir chaque jeu serait lent et parfois payant. Un dossier est relu sur demande ou au plus une fois par jour, jamais pendant
// une partie ni pendant un transfert ; profondeur et nombre de dossiers bornés.
#include "engine.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <map>

#include "core/fs.hpp"
#include "files/files.hpp"
#include "images/pkg_head.hpp"
#include "store/store.hpp"

namespace pbc {

namespace {

constexpr int kMaxDepth = 5;
constexpr int kMaxDirs = 4000;
constexpr std::int64_t kRescanAfter = 24 * 3600;
constexpr std::uint64_t kMinPkg = 1ull << 20;  // plus petit : un thème, un avatar — jamais lu

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// PPSA01234 / CUSA01234 (le premier trouvé dans le nom).
std::string find_title_id(const std::string& s) {
    for (std::size_t i = 0; i + 9 <= s.size(); ++i) {
        const std::string p = s.substr(i, 4);
        if (p != "PPSA" && p != "CUSA") continue;
        bool ok = true;
        for (std::size_t k = i + 4; k < i + 9; ++k) ok = ok && std::isdigit(static_cast<unsigned char>(s[k]));
        if (ok && (i + 9 == s.size() || !std::isdigit(static_cast<unsigned char>(s[i + 9])))) return s.substr(i, 9);
    }
    return "";
}

// « v1.003 », « v01.003.000 », « 01.011 » (après un espace) : la version écrite dans le nom.
std::string find_version(const std::string& s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const bool v = (s[i] == 'v' || s[i] == 'V') && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])) && (i == 0 || !std::isalpha(static_cast<unsigned char>(s[i - 1])));
        if (!v) continue;
        std::size_t e = i + 1;
        while (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || s[e] == '.')) ++e;
        std::string r = s.substr(i + 1, e - i - 1);
        while (!r.empty() && r.back() == '.') r.pop_back();  // « v1.050.pkg » : le point de l'extension n'en fait pas partie
        if (r.find('.') != std::string::npos) return r;
    }
    // « 01.011 » seul (après une espace) : deux chiffres, un point, trois chiffres.
    for (std::size_t i = 1; i + 6 <= s.size(); ++i) {
        if (s[i - 1] != ' ') continue;
        const bool ok = std::isdigit(static_cast<unsigned char>(s[i])) && std::isdigit(static_cast<unsigned char>(s[i + 1])) && s[i + 2] == '.' &&
                        std::isdigit(static_cast<unsigned char>(s[i + 3])) && std::isdigit(static_cast<unsigned char>(s[i + 4])) && std::isdigit(static_cast<unsigned char>(s[i + 5]));
        if (ok && (i + 6 == s.size() || !std::isdigit(static_cast<unsigned char>(s[i + 6])))) return s.substr(i, 6);
    }
    return "";
}

// « [PS5] Worms Armageddon - Anniversary Edition (2024) (PPSA20052) (EU) v1.003 » → « Worms Armageddon - Anniversary Edition ».
std::string clean_title(std::string s) {
    for (const char* ext : {".ffpfsc", ".ffpkg", ".ffpfs", ".exfat", ".pkg"}) {
        const std::string e = ext;
        if (s.size() > e.size() && lower(s.substr(s.size() - e.size())) == e) s.resize(s.size() - e.size());
    }
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '[' || c == '(') { ++depth; continue; }
        if ((c == ']' || c == ')') && depth > 0) { --depth; continue; }
        if (depth == 0) out += c;
    }
    const std::string v = find_version(out);
    if (!v.empty()) {
        auto p = out.find("v" + v);
        if (p == std::string::npos) p = out.find("V" + v);
        if (p == std::string::npos) p = out.find(" " + v);
        if (p != std::string::npos) out.erase(p);
    }
    const std::string tid = find_title_id(out);
    if (!tid.empty()) out.erase(out.find(tid), tid.size());
    for (auto& c : out) if (c == '_') c = ' ';
    while (!out.empty() && (out.back() == ' ' || out.back() == '-' || out.back() == '.')) out.pop_back();
    std::size_t b = 0;
    while (b < out.size() && (out[b] == ' ' || out[b] == '-')) ++b;
    out = out.substr(b);
    std::string squeezed;
    for (char c : out) if (!(c == ' ' && !squeezed.empty() && squeezed.back() == ' ')) squeezed += c;
    return squeezed;
}

// Ordre de préférence quand un même jeu existe en plusieurs formats.
int rank(const std::string& container) {
    if (container == "ffpfsc") return 0;
    if (container == "exfat") return 1;
    if (container == "ffpkg") return 2;
    if (container == "ffpfs") return 3;
    if (container == "pkg") return 4;
    return 5;  // dossier
}

std::string container_of(const std::string& name) {
    const std::string l = lower(name);
    for (const char* ext : {"ffpfsc", "ffpkg", "ffpfs", "exfat", "pkg"}) {
        const std::string e = std::string(".") + ext;
        if (l.size() > e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0) return ext;
    }
    return "";
}

struct Found { std::string path, dir, container, title_id, title_src; std::uint64_t size = 0; bool folder = false; Json pkg; };

// Les en-têtes de .pkg déjà lus (index d'avant), par chemin : relus seulement si la taille a changé.
using PkgCache = std::map<std::string, std::pair<std::uint64_t, Json>>;

// Garde-fou : lire les en-têtes ne doit jamais rendre la bibliothèque lente. Au-delà de 60 s pour un dossier (un
// appareil lent, un serveur qui traîne), on arrête de lire pour cette fois : les .pkg restants sont classés plus tard.
struct HeadBudget {
    std::chrono::steady_clock::duration used{};
    int unreadable = 0;  // dossiers dont la liste n'a pas pu être lue : l'index d'avant est gardé
    bool ok() const { return used < std::chrono::seconds(60); }
    void spent(std::chrono::steady_clock::duration d, const std::string& p) {
        used += d;
        if (d > std::chrono::seconds(3)) log("bibliothèque de jeux : en-tête lent (%d ms) %s", static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count()), p.c_str());
    }
};

// Parcours d'un dossier déclaré : les jeux (images, .pkg, dossiers de jeu) et les dossiers de backport.
void walk(store::Store& st, const std::string& dir, int depth, int& dirs, std::vector<Found>& out, std::vector<std::pair<std::string, std::string>>& backports,
          const PkgCache& cache, HeadBudget& budget, const std::atomic<bool>& quit) {
    if (quit || depth > kMaxDepth || ++dirs > kMaxDirs) return;
    std::vector<fs::Entry> es;
    if (!st.list(dir, es)) { ++budget.unreadable; return; }
    for (const auto& e : es)
        if (e.dir && e.name == "sce_sys") {  // un dossier de jeu : lui-même est le jeu
            Found f;
            f.path = dir; f.dir = dir; f.folder = true;
            const auto slash = dir.find_last_of('/');
            f.title_src = slash == std::string::npos ? dir : dir.substr(slash + 1);
            f.title_id = find_title_id(f.title_src);
            out.push_back(f);
            return;
        }
    for (const auto& e : es) {
        if (quit) return;
        if (!files::safe_name(e.name) || e.name.empty() || e.name[0] == '#' || e.name[0] == '@' || e.name[0] == '.') continue;
        const std::string p = fs::join(dir, e.name);
        if (e.dir) {
            if (lower(e.name).find("backport") != std::string::npos || lower(e.name).find("backpork") != std::string::npos) {
                std::string fw;  // « [4.xx+] » : le système qu'il demande
                const auto b = e.name.find(".xx+]");
                if (b != std::string::npos) { auto a = e.name.rfind('[', b); if (a != std::string::npos) fw = e.name.substr(a + 1, b + 4 - a - 1); }
                backports.push_back({p, fw});
                continue;  // ses fichiers ne sont pas des jeux
            }
            walk(st, p, depth + 1, dirs, out, backports, cache, budget, quit);
            continue;
        }
        const std::string c = container_of(e.name);
        if (c.empty() || (c == "pkg" && e.size < kMinPkg)) continue;
        Found f;
        f.path = p; f.dir = dir; f.container = c; f.size = e.size; f.title_src = e.name;
        f.title_id = find_title_id(e.name);
        if (c == "pkg") {  // ce que le paquet dit de lui-même : jeu, mise à jour ou DLC ; PS4 ou PS5 ; fake ou officiel
            const auto hit = cache.find(p);
            // Gardé seulement s'il est complet (un paquet PS5 dont le conteneur a été lu : son identifiant de contenu).
            const bool whole = hit != cache.end() && hit->second.first == e.size && !hit->second.second["kind"].as_string().empty() &&
                               (hit->second.second["container"].as_string() != "fih" || !hit->second.second["content_id"].as_string().empty());
            if (whole) f.pkg = hit->second.second;
            else if (budget.ok()) {
                const auto t0 = std::chrono::steady_clock::now();
                pkghead::Head h;
                if (pkghead::read(st, p, h)) f.pkg = pkghead::to_json(h);
                budget.spent(std::chrono::steady_clock::now() - t0, p);
            }
            if (f.pkg.is_object() && f.title_id.empty()) f.title_id = pkghead::from_json(f.pkg).title_id();
            if (!f.pkg.is_object() && e.size < (200ull << 20)) continue;  // illisible et petit : pas un jeu
        }
        out.push_back(f);
    }
}

}  // namespace

std::string Engine::gamelib_file() const { return fs::host(fs::join(dir_, "gamelibs.json")); }

void Engine::gamelib_load() {
    std::string t;
    Json j;
    std::lock_guard<std::mutex> l(gamelib_mu_);
    gamelib_ = fs::read(gamelib_file(), t, 32u << 20) && Json::parse(t, j) && j.is_object() ? j : Json::object();
    if (!gamelib_["libs"].is_array()) gamelib_.set("libs", Json::array());
}

void Engine::gamelib_save_locked() { fs::write_atomic(gamelib_file(), gamelib_.dump()); }

Json Engine::gamelib_list() {
    const Json places = files_places();  // gardé : jamais de boucle sur un temporaire
    std::lock_guard<std::mutex> l(gamelib_mu_);
    Json a = Json::array();
    for (const auto& x : gamelib_["libs"].items()) {
        Json j = Json::object();
        j.set("place", x["place"]); j.set("path", x["path"]);
        std::string label;
        for (const auto& p : places.items()) if (p["id"].as_string() == x["place"].as_string()) label = p["label"].as_string();
        j.set("label", label);
        j.set("online", !label.empty());
        j.set("games", static_cast<std::int64_t>(x["games"].size()));
        j.set("scanned_at", x["scanned_at"]);
        j.set("error", x["error"]);
        j.set("scanning", gamelib_scanning_ == x["place"].as_string() + "|" + x["path"].as_string());
        a.push(j);
    }
    Json out = Json::object();
    out.set("libs", a);
    return out;
}

// Les jeux de toutes les bibliothèques (un par jeu et par bibliothèque, le meilleur format d'abord).
Json Engine::gamelib_games() {
    const Json places = files_places();
    std::lock_guard<std::mutex> l(gamelib_mu_);
    Json a = Json::array();
    for (const auto& x : gamelib_["libs"].items()) {
        std::string label;
        for (const auto& p : places.items()) if (p["id"].as_string() == x["place"].as_string()) label = p["label"].as_string();
        for (const auto& g : x["games"].items()) {
            Json j = g;
            j.set("place", x["place"]); j.set("place_label", label); j.set("lib", x["path"]);
            a.push(j);
        }
    }
    Json out = Json::object();
    out.set("games", a);
    out.set("revision", static_cast<std::int64_t>(gamelib_rev_));
    return out;
}

bool Engine::gamelib_add(const std::string& place, const std::string& path, std::string& error) {
    Json probe;
    if (!files_list(place, path, probe, error)) return false;  // chemin sûr et lisible
    {
        std::lock_guard<std::mutex> l(gamelib_mu_);
        for (const auto& x : gamelib_["libs"].items())
            if (x["place"].as_string() == place && x["path"].as_string() == path) { error = "déjà une bibliothèque de jeux"; return false; }
        Json j = Json::object();
        j.set("place", place); j.set("path", path); j.set("games", Json::array()); j.set("scanned_at", 0); j.set("rescan", true);
        Json libs = gamelib_["libs"];
        libs.push(j);
        gamelib_.set("libs", libs);
        gamelib_save_locked();
        ++gamelib_rev_;
    }
    log("bibliothèque de jeux ajoutée : %s %s", place.c_str(), path.c_str());
    return true;
}

bool Engine::gamelib_remove(const std::string& place, const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> l(gamelib_mu_);
    Json keep = Json::array();
    bool found = false;
    for (const auto& x : gamelib_["libs"].items()) {
        if (x["place"].as_string() == place && x["path"].as_string() == path) { found = true; continue; }
        keep.push(x);
    }
    if (!found) { error = "bibliothèque inconnue"; return false; }
    gamelib_.set("libs", keep);
    gamelib_save_locked();
    ++gamelib_rev_;
    return true;
}

void Engine::gamelib_rescan(const std::string& place, const std::string& path) {
    std::lock_guard<std::mutex> l(gamelib_mu_);
    Json libs = Json::array();
    for (auto x : gamelib_["libs"].items()) {
        if (place.empty() || (x["place"].as_string() == place && (path.empty() || x["path"].as_string() == path))) x.set("rescan", true);
        libs.push(x);
    }
    gamelib_.set("libs", libs);
}

// Lit un dossier déclaré et range ses jeux dans l'index.
void Engine::gamelib_scan(const std::string& place, const std::string& path) {
    std::string err;
    std::vector<Found> found;
    std::vector<std::pair<std::string, std::string>> backports;
    int dirs = 0;
    PkgCache cache;
    {
        std::lock_guard<std::mutex> l(gamelib_mu_);
        auto keep = [&](const Json& x) { if (x["pkg"].is_object()) cache[x["path"].as_string()] = {static_cast<std::uint64_t>(x["size"].as_int()), x["pkg"]}; };
        for (const auto& x : gamelib_["libs"].items())
            if (x["place"].as_string() == place && x["path"].as_string() == path)
                for (const auto& g : x["games"].items()) {
                    keep(g);
                    for (const char* k : {"variants", "updates", "dlcs"}) for (const auto& v : g[k].items()) keep(v);
                }
    }
    {
        auto st = open_files_place(place, err);
        HeadBudget budget;
        if (st) {
            walk(*st, path, 0, dirs, found, backports, cache, budget, quit_);
            if (budget.unreadable) { err = "lecture incomplète"; log("bibliothèque de jeux : %d dossier(s) illisible(s) dans %s", budget.unreadable, path.c_str()); }
        } else if (err.empty()) err = "injoignable";
    }
    if (quit_) return;
    // Sans identifiant dans son nom (un dossier « fix », « original »…) : celui du dossier parent le plus proche.
    for (auto& f : found) {
        if (!f.title_id.empty()) continue;
        std::string d = f.dir;
        while (d.size() > path.size() && d.rfind(path, 0) == 0) {
            const auto s = d.find_last_of('/');
            const std::string name = d.substr(s + 1);
            const std::string tid = find_title_id(name);
            if (!tid.empty()) { f.title_id = tid; if (f.folder) f.title_src = name; f.dir = f.folder ? d : f.dir; break; }
            d = d.substr(0, s);
        }
    }
    // Les mises à jour et les contenus additionnels (.pkg) ne sont jamais « le jeu » : rangés sous leur jeu.
    std::map<std::string, Json> updates, dlcs;
    {
        std::vector<Found> games_only;
        for (auto& f : found) {
            const std::string k = f.pkg["kind"].as_string();
            if (f.container == "pkg" && (k == "patch" || k == "dlc")) {
                Json v = Json::object();
                v.set("path", f.path); v.set("size", static_cast<std::int64_t>(f.size)); v.set("pkg", f.pkg);
                const std::string ver = find_version(f.title_src);
                if (!ver.empty()) v.set("version", ver);
                Json& list = (k == "patch" ? updates : dlcs)[f.title_id];
                if (!list.is_array()) list = Json::array();
                // Le même paquet rangé à deux endroits (même contenu, même taille) : une seule fois.
                bool twin = false;
                const std::string cid = f.pkg["content_id"].as_string();
                for (const auto& o : list.items())
                    twin = twin || (!cid.empty() && o["pkg"]["content_id"].as_string() == cid && o["size"].as_int() == static_cast<std::int64_t>(f.size));
                if (!twin) list.push(v);
                continue;
            }
            games_only.push_back(std::move(f));
        }
        found.swap(games_only);
    }
    // Un jeu par identifiant (ou par fichier, sans identifiant), le meilleur format d'abord ; les autres en variantes.
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.title_id != b.title_id ? a.title_id < b.title_id : rank(a.container) < rank(b.container); });
    Json games = Json::array();
    for (std::size_t i = 0; i < found.size();) {
        std::size_t e = i + 1;
        if (!found[i].title_id.empty()) while (e < found.size() && found[e].title_id == found[i].title_id) ++e;
        const Found& f = found[i];
        // Titre : le dossier du jeu s'il le nomme (« [PS5] Titre (PPSA…) »), sinon le fichier.
        const auto slash = f.dir.find_last_of('/');
        const std::string dname = slash == std::string::npos ? f.dir : f.dir.substr(slash + 1);
        // Le dossier ne nomme le jeu que s'il porte son identifiant (ou s'il est le jeu) : un « Autre/Jeu.pkg » garde son nom.
        std::string title = f.dir != path && (f.folder || find_title_id(dname) == f.title_id) ? clean_title(dname) : std::string();
        if (title.empty() || title.size() < 2) title = clean_title(f.title_src);
        std::string version = find_version(f.title_src);
        if (version.empty() && f.dir != path) version = find_version(dname);
        Json g = Json::object();
        g.set("title_id", f.title_id);
        g.set("title", title.empty() ? f.title_id : title);
        // PS4 ou PS5 : ce que dit l'en-tête d'un .pkg du jeu, sinon son identifiant (CUSA : PS4).
        std::string platform;
        for (std::size_t k = i; k < e && platform.empty(); ++k) platform = found[k].pkg["platform"].as_string();
        if (platform.empty()) platform = f.title_id.rfind("CUSA", 0) == 0 ? "ps4" : "ps5";
        g.set("kind", platform == "ps4" ? "game_ps4" : "game_ps5");
        g.set("platform", platform);
        g.set("path", f.path); g.set("dir", f.folder); g.set("container", f.container); g.set("size", static_cast<std::int64_t>(f.size));
        if (f.pkg.is_object()) g.set("pkg", f.pkg);
        if (!version.empty()) g.set("version", version);
        // Un backport rangé dans le dossier du jeu (ou dessous) : signalé, jamais ouvert.
        for (const auto& [bp, fw] : backports)
            if (f.dir != path && bp.rfind(f.dir + "/", 0) == 0) { g.set("backport", true); if (!fw.empty()) g.set("backport_fw", fw); }
        Json vars = Json::array();
        for (std::size_t k = i + 1; k < e; ++k) {
            Json v = Json::object();
            v.set("path", found[k].path); v.set("container", found[k].container); v.set("size", static_cast<std::int64_t>(found[k].size)); v.set("dir", found[k].folder);
            if (found[k].pkg.is_object()) v.set("pkg", found[k].pkg);
            vars.push(v);
        }
        if (vars.size()) g.set("variants", vars);
        if (!f.title_id.empty()) {
            if (auto u = updates.find(f.title_id); u != updates.end()) g.set("updates", u->second);
            if (auto d = dlcs.find(f.title_id); d != dlcs.end()) g.set("dlcs", d->second);
        }
        games.push(g);
        i = e;
    }
    {
        std::lock_guard<std::mutex> l(gamelib_mu_);
        Json libs = Json::array();
        for (auto x : gamelib_["libs"].items()) {
            if (x["place"].as_string() == place && x["path"].as_string() == path) {
                // Injoignable ou lecture incomplète : on garde l'index d'avant (jamais de jeux perdus pour une coupure).
                if (err.empty() || !x["games"].size()) x.set("games", games);
                x.set("error", err);
                x.set("scanned_at", static_cast<std::int64_t>(std::time(nullptr)));
                x.set("rescan", false);
            }
            libs.push(x);
        }
        gamelib_.set("libs", libs);
        gamelib_save_locked();
        ++gamelib_rev_;
    }
    log("bibliothèque de jeux lue : %s %s — %d jeu(x), %d dossier(s)%s%s", place.c_str(), path.c_str(), static_cast<int>(games.size()), dirs,
        err.empty() ? "" : " — ", err.c_str());
}

void Engine::gamelib_loop() {
    for (int i = 0; i < 200 && !quit_; ++i) ::usleep(100 * 1000);  // laisse le moteur démarrer
    while (!quit_) {
        std::string tid, ttl;
        const bool busy = game_running_.load() || queue_->running(tid, ttl) || sync_running_.load() || parties_running_.load();
        std::string place, path;
        if (!busy) {
            const std::int64_t now = std::time(nullptr);
            std::lock_guard<std::mutex> l(gamelib_mu_);
            for (const auto& x : gamelib_["libs"].items())
                if (x["rescan"].as_bool() || now - x["scanned_at"].as_int() > kRescanAfter) { place = x["place"].as_string(); path = x["path"].as_string(); break; }
            if (!place.empty()) gamelib_scanning_ = place + "|" + path;
        }
        if (!place.empty()) {
            ++gamelib_rev_;
            gamelib_scan(place, path);
            std::lock_guard<std::mutex> l(gamelib_mu_);
            gamelib_scanning_.clear();
            continue;
        }
        for (int i = 0; i < 50 && !quit_; ++i) {  // 5 s, ou tout de suite si on le demande
            ::usleep(100 * 1000);
            std::lock_guard<std::mutex> l(gamelib_mu_);
            bool want = false;
            for (const auto& x : gamelib_["libs"].items()) want = want || x["rescan"].as_bool();
            if (want && i > 30) break;
        }
    }
}

}  // namespace pbc
