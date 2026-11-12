#include "backup/full.hpp"

#include <sys/stat.h>

#include <dirent.h>

#include <algorithm>

#include "core/fs.hpp"

namespace pbc::full {

namespace {

struct Cat {
    const char* id;
    const char* label;
    std::vector<const char*> dirs;  // dossiers copiés tels quels (chemins console)
};

// Réglages de démarrage connus (dans /data) : copiés à part, et retirés des homebrews.
const std::vector<const char*> boot_dirs = {"/data/etaHEN", "/data/shadowmount", "/data/ShadowMountPlus", "/data/kstuff",
                                           "/data/ps5_autoload", "/data/Arsenal", "/data/payloads", "/data/homebrew-autoload"};

const std::vector<Cat>& table() {
    static const std::vector<Cat> t = {
        {"accounts", "Comptes", {"/system_data/priv/home"}},  // + /user/home/<uid> sans les sauvegardes
        {"saves", "Sauvegardes de jeux", {"/system_data/savedata_prospero"}},  // + /user/home/<uid>/savedata*
        {"games", "Jeux (inscriptions)", {"/user/appmeta"}},  // + petits fichiers de /user/app/<TID>
        {"images", "Images des jeux", {}},                    // images restées sur l'interne
        {"homebrew", "Applications homebrew et leurs données", {"/data"}},
        {"settings", "Démarrage et réglages", {}},            // boot_dirs
        {"captures", "Captures et vidéos", {"/user/av_contents"}},
        {"system", "Système (expert)", {"/system_data/settings", "/system_data/priv/mms", "/user/license", "/system_data/priv/license"}},
    };
    return t;
}

constexpr std::uint64_t small_file = 16ull << 20;  // inscription d'un jeu : param, icônes, liens (pas l'image)

// Taille d'une arborescence, sans suivre les liens. `skip` : chemins console exclus.
void walk(const std::string& console, const std::vector<std::string>& skip, std::uint64_t& bytes, std::uint64_t& files, int depth = 0) {
    for (const auto& s : skip) if (console == s) return;
    const std::string h = fs::host(console);
    struct ::stat st {};
    if (::lstat(h.c_str(), &st) != 0 || S_ISLNK(st.st_mode)) return;
    if (S_ISREG(st.st_mode)) { bytes += static_cast<std::uint64_t>(st.st_size); ++files; return; }
    if (!S_ISDIR(st.st_mode) || depth > 24) return;
    DIR* d = ::opendir(h.c_str());
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        walk(fs::join(console, n), skip, bytes, files, depth + 1);
    }
    ::closedir(d);
}

bool exists_dir(const std::string& console) {
    struct ::stat st {};
    return ::lstat(fs::host(console).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Fichiers d'inscription des jeux : /user/app/<TID>/* de moins de 16 Mo (pas app.pkg ni l'image).
std::vector<Source> game_files(std::uint64_t& bytes, std::uint64_t& files) {
    std::vector<Source> out;
    std::vector<fs::Entry> titles;
    if (!fs::list(fs::host("/user/app"), titles)) return out;
    for (const auto& t : titles) {
        if (!t.dir) continue;
        const std::string dir = fs::join("/user/app", t.name);
        std::vector<fs::Entry> inner;
        if (!fs::list(fs::host(dir), inner)) continue;
        for (const auto& f : inner) {
            if (f.dir || f.size > small_file) continue;
            struct ::stat st {};
            if (::lstat(fs::host(fs::join(dir, f.name)).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            out.push_back({fs::join(dir, f.name), "user/app/" + t.name + "/" + f.name, false});
            bytes += f.size;
            ++files;
        }
    }
    return out;
}

// Dossiers de sauvegarde de chaque compte (/user/home/<uid>/savedata*).
bool is_save_dir(const std::string& n) { return n == "savedata_prospero" || n == "savedata_prospero_meta" || n == "savedata"; }

// Ce que l'app elle-même écrit dans /data (état, mises à jour, copies de travail, backups) : jamais
// recopié dans « homebrews ».
bool own_data(const std::string& n) {
    return n.rfind("pbck", 0) == 0 || n.rfind("pbc-", 0) == 0 || n.rfind("PBC", 0) == 0 || n == "save_files" || n == "mount_sd" || n == "mount_owner";
}

// Contenu de /user/home/<uid> : `saves` vrai = seulement les sauvegardes, faux = tout le reste.
std::vector<Source> home_parts(bool saves, const std::string& cat, std::uint64_t& bytes, std::uint64_t& files) {
    std::vector<Source> out;
    std::vector<fs::Entry> users;
    if (!fs::list(fs::host("/user/home"), users)) return out;
    for (const auto& u : users) {
        if (!u.dir) continue;
        const std::string dir = fs::join("/user/home", u.name);
        std::vector<fs::Entry> inner;
        if (!fs::list(fs::host(dir), inner)) continue;
        for (const auto& e : inner) {
            if (is_save_dir(e.name) != saves) continue;
            const std::string p = fs::join(dir, e.name);
            struct ::stat st {};
            if (::lstat(fs::host(p).c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
            out.push_back({p, cat + "/user/home/" + u.name + "/" + e.name, S_ISDIR(st.st_mode)});
            walk(p, {}, bytes, files);
        }
    }
    return out;
}

std::vector<std::string> data_skips() {
    std::vector<std::string> v;
    std::vector<fs::Entry> top;
    if (fs::list(fs::host("/data"), top)) for (const auto& e : top) if (own_data(e.name)) v.push_back(fs::join("/data", e.name));
    return v;
}

std::vector<std::string> boot_list() {
    std::vector<std::string> v;
    for (const char* d : boot_dirs) if (exists_dir(d)) v.emplace_back(d);
    return v;
}

// Les images de jeux PS5 restées sur l'interne (les autres sont déjà sur un disque).
std::vector<Source> internal_images(const std::vector<Item>& items, std::uint64_t& bytes, std::uint64_t& files, int& games) {
    std::vector<Source> out;
    for (const auto& it : items) {
        if ((it.kind != Kind::game_ps5 && it.kind != Kind::game_ps4) || it.place_id != "internal") continue;
        bool any = false;
        for (const auto& c : it.components) {
            if (c.part != Part::game) continue;
            for (const auto& s : c.sources) {
                if (s.source.rfind("/user/app/", 0) == 0) continue;  // inscription : catégorie « Jeux »
                out.push_back({s.source, "images/" + (it.title_id.empty() ? it.key : it.title_id) + "/" + std::string(fs::basename(s.source)), s.dir});
                any = true;
            }
            bytes += c.bytes;
        }
        if (any) { ++games; ++files; }
    }
    return out;
}

std::string strip_slash(const char* p) { return std::string(p + 1); }  // « /data » → « data »

}  // namespace

const std::vector<std::string>& categories() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> r;
        for (const auto& c : table()) r.emplace_back(c.id);
        return r;
    }();
    return v;
}

Json plan(const std::vector<Item>& items) {
    Json cats = Json::object();
    const auto boots = boot_list();
    for (const auto& c : table()) {
        std::uint64_t bytes = 0, files = 0;
        std::string detail;
        const std::string id = c.id;
        if (id == "games") {
            game_files(bytes, files);
            for (const char* d : c.dirs) walk(d, {}, bytes, files);
            int n = 0;
            for (const auto& it : items) if (it.kind == Kind::game_ps5 || it.kind == Kind::game_ps4) ++n;
            detail = std::to_string(n) + " jeux · leurs inscriptions (les images restent sur vos disques)";
        } else if (id == "images") {
            int games = 0;
            internal_images(items, bytes, files, games);
            detail = games ? std::to_string(games) + " image(s) sur l'interne, pas encore sur un disque" : "toutes vos images sont déjà sur un disque";
        } else if (id == "settings") {
            for (const auto& d : boots) walk(d, {}, bytes, files);
            detail = boots.empty() ? "aucun réglage de démarrage trouvé" : std::to_string(boots.size()) + " dossier(s) : payloads, shadowmount, kstuff…";
        } else if (id == "accounts" || id == "saves") {
            home_parts(id == "saves", id, bytes, files);
            for (const char* d : c.dirs) walk(d, {}, bytes, files);
        } else if (id == "homebrew") {
            auto skip = data_skips();
            skip.insert(skip.end(), boots.begin(), boots.end());
            for (const char* d : c.dirs) walk(d, skip, bytes, files);
            int n = 0;
            for (const auto& it : items) if (it.kind == Kind::app) ++n;
            detail = std::to_string(n) + " applis · émulateurs, profils, thèmes, BIOS (/data)";
        } else {
            for (const char* d : c.dirs) walk(d, {}, bytes, files);
        }
        Json j = Json::object();
        j.set("label", c.label);
        j.set("bytes", static_cast<double>(bytes));
        j.set("files", static_cast<double>(files));
        if (!detail.empty()) j.set("detail", detail);
        cats.set(c.id, std::move(j));
    }
    Json out = Json::object();
    out.set("categories", std::move(cats));
    return out;
}

bool make_item(const std::vector<std::string>& wanted, const std::vector<Item>& items, Item& out, std::string& error) {
    out = Item{};
    out.key = "system:full-backup";
    out.kind = Kind::app;
    out.title = "Cette PS5 — sauvegarde complète";
    out.platform = "PS5";
    out.place_id = "internal";
    out.image_type = "sauvegarde complète";
    const auto boots = boot_list();
    for (const auto& c : table()) {
        if (std::find(wanted.begin(), wanted.end(), c.id) == wanted.end()) continue;
        Component comp;
        comp.part = Part::config;
        comp.label = c.label;
        comp.meta = Json::object();
        comp.meta.set("category", c.id);
        const std::string id = c.id;
        std::uint64_t bytes = 0, files = 0;
        if (id == "games") {
            comp.sources = game_files(bytes, files);
            for (const char* d : c.dirs) if (exists_dir(d)) { comp.sources.push_back({d, id + "/" + strip_slash(d), true}); walk(d, {}, bytes, files); }
        } else if (id == "images") {
            int games = 0;
            comp.sources = internal_images(items, bytes, files, games);
        } else if (id == "settings") {
            for (const auto& d : boots) { comp.sources.push_back({d, id + "/" + d.substr(1), true}); walk(d, {}, bytes, files); }
        } else if (id == "homebrew") {
            // /data entier, sauf les réglages de démarrage (catégorie à part) : un dossier par entrée.
            std::vector<fs::Entry> top;
            if (fs::list(fs::host("/data"), top))
                for (const auto& e : top) {
                    const std::string p = fs::join("/data", e.name);
                    if (std::find(boots.begin(), boots.end(), p) != boots.end() || own_data(e.name)) continue;
                    struct ::stat st {};
                    if (::lstat(fs::host(p).c_str(), &st) != 0 || S_ISLNK(st.st_mode)) continue;
                    comp.sources.push_back({p, id + "/data/" + e.name, S_ISDIR(st.st_mode)});
                    walk(p, {}, bytes, files);
                }
        } else {
            if (id == "accounts" || id == "saves") comp.sources = home_parts(id == "saves", id, bytes, files);
            for (const char* d : c.dirs) if (exists_dir(d)) { comp.sources.push_back({d, id + "/" + strip_slash(d), true}); walk(d, {}, bytes, files); }
        }
        if (comp.sources.empty()) continue;
        comp.bytes = bytes;
        out.components.push_back(std::move(comp));
    }
    if (out.components.empty()) { error = "rien à sauvegarder dans les catégories choisies"; return false; }
    return true;
}

}  // namespace pbc::full
