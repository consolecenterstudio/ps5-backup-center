#include "core/titles.hpp"
#include "saves/console_saves.hpp"
#include "saves/progress.hpp"

#include <algorithm>
#include <cstring>
#include <map>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::saves {

std::string sfo_title(const std::string& d) {
    auto u16 = [&](std::size_t o) -> std::uint32_t { return o + 2 <= d.size() ? static_cast<std::uint8_t>(d[o]) | static_cast<std::uint8_t>(d[o + 1]) << 8 : 0; };
    auto u32 = [&](std::size_t o) -> std::uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
    if (d.size() < 20 || std::memcmp(d.data(), "\0PSF", 4) != 0) return "";
    const std::uint32_t keys = u32(8), data = u32(12), count = u32(16);
    std::string main, title;
    for (std::uint32_t i = 0; i < count && i < 256; ++i) {
        const std::size_t e = 20 + i * 16;
        if (e + 16 > d.size()) break;
        const std::size_t k = keys + u16(e), v = data + u32(e + 12), len = u32(e + 4);
        if (k >= d.size() || v + len > d.size()) continue;
        const std::string key(d.c_str() + k);
        std::string val(d.data() + v, len);
        val = val.c_str();  // terminé par un zéro
        if (key == "MAINTITLE") main = val;
        else if (key == "TITLE") title = val;
    }
    return !main.empty() ? main : title;
}

namespace {

struct Found {
    std::string uid, account_id, name;
    std::string dir, meta_dir;     // chemins console
    std::uint64_t bytes = 0;
    std::int64_t mtime = 0;
    unsigned images = 0;
    std::string icon, sfo;
};

// Une famille : PS5 (savedata_prospero) ou PS4 (savedata).
void collect(const accounts::Account& a, const char* sub, const char* meta_sub, std::map<std::string, std::vector<Found>>& by_title) {
    const std::string home = fs::join("/user/home", a.user_id);
    const std::string root = fs::join(home, sub);
    std::vector<fs::Entry> titles;
    if (!fs::list(fs::host(root), titles)) return;
    for (const auto& t : titles) {
        if (!t.dir || t.name.rfind("sce_", 0) == 0) continue;
        Found f;
        f.uid = a.user_id;
        f.account_id = a.account_id;
        f.name = a.name;
        f.dir = fs::join(root, t.name);
        std::vector<fs::Entry> files;
        if (fs::list(fs::host(f.dir), files))
            for (const auto& x : files) if (!x.dir) { f.bytes += x.size; f.mtime = std::max(f.mtime, x.mtime); if (x.name.rfind("sdimg_", 0) == 0 && x.name.rfind("sdimg_sce_bu_", 0) != 0) ++f.images; }
        if (!f.images) continue;
        f.meta_dir = fs::join(fs::join(fs::join(home, meta_sub), "user"), t.name);
        std::vector<fs::Entry> meta;
        if (fs::list(fs::host(f.meta_dir), meta)) {
            for (const auto& x : meta) {
                if (x.dir) continue;
                f.bytes += x.size;
                if (f.icon.empty() && x.name.size() > 10 && x.name.compare(x.name.size() - 10, 10, "_icon0.png") == 0 && x.name.rfind("sce_bu_", 0) != 0) f.icon = fs::join(f.meta_dir, x.name);
                if (f.sfo.empty() && x.name.size() > 4 && x.name.compare(x.name.size() - 4, 4, ".sfo") == 0) f.sfo = fs::join(f.meta_dir, x.name);
            }
        } else f.meta_dir.clear();
        by_title[t.name].push_back(std::move(f));
    }
}

}  // namespace

void attach(std::vector<Item>& items, const std::vector<accounts::Account>& accounts, const std::map<std::string, appdb::Info>& apps) {
    std::map<std::string, std::vector<Found>> ps5, ps4;
    for (const auto& a : accounts) {
        collect(a, "savedata_prospero", "savedata_prospero_meta", ps5);
        collect(a, "savedata", "savedata_meta", ps4);
    }
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < items.size(); ++i) if (!items[i].title_id.empty()) index[items[i].title_id] = i;
    unsigned attached = 0, standalone = 0;
    // Base des sauvegardes de chaque compte, lue une fois par relevé.
    std::map<std::string, std::map<std::string, std::vector<progress::SaveText>>> by_user;
    auto add = [&](std::map<std::string, std::vector<Found>>& by_title, bool is_ps5) {
        for (auto& [tid, list] : by_title) {
            Item* it = nullptr;
            auto hit = index.find(tid);
            if (hit != index.end()) { it = &items[hit->second]; ++attached; }
            else {
                Item n;
                n.key = "save:" + tid;
                const auto known = apps.find(tid);
                n.kind = known != apps.end() && known->second.is_app() ? Kind::app : is_ps5 ? Kind::game_ps5 : Kind::game_ps4;
                if (known != apps.end()) n.title = known->second.title;
                n.title_id = tid;
                n.platform = is_ps5 ? "PS5" : "PS4";
                n.place_id = "internal";
                n.image_type = "sauvegarde";
                for (const auto& f : list) {
                    std::string sfo;
                    if (n.title.empty() && !f.sfo.empty() && fs::read(fs::host(f.sfo), sfo, 1u << 20)) n.title = sfo_title(sfo);
                    if (n.cover.empty() && !f.icon.empty()) n.cover = f.icon;
                }
                // Le titre d'une sauvegarde (« Préférences », « Save Data »…) n'est pas le nom du jeu :
                // on prend d'abord la table des jeux PS4 / PS5 livrée avec l'app.
                if (const std::string known_name = titles::name(tid); !known_name.empty()) n.title = known_name;
                if (n.title.empty()) n.title = tid;
                items.push_back(std::move(n));
                index[tid] = items.size() - 1;
                it = &items.back();
                ++standalone;
            }
            for (const auto& f : list) {
                Component c;
                c.part = Part::save;
                c.label = "Sauvegarde · " + (f.name.empty() ? f.uid : f.name);
                const std::string base = std::string(is_ps5 ? "ps5" : "ps4") + "/" + f.uid + "/" + tid;
                c.sources.push_back({f.dir, base + "/images", true});
                if (!f.meta_dir.empty()) c.sources.push_back({f.meta_dir, base + "/meta", true});
                c.bytes = f.bytes;
                c.restore_to = f.dir;
                Json m = Json::object();
                m.set("format", is_ps5 ? "ps5-savedata" : "ps4-savedata");
                m.set("user_id", f.uid);
                m.set("account_id", f.account_id);
                m.set("account_name", f.name);
                m.set("images", static_cast<std::uint64_t>(f.images));
                m.set("mtime", f.mtime);
                // Avancée écrite par le jeu (base des sauvegardes de la console) : gardée dans le manifeste du backup,
                // la copie la montre ensuite sans être ouverte.
                {
                    const std::string ukey = std::string(is_ps5 ? "5:" : "4:") + f.uid;
                    auto u = by_user.find(ukey);
                    if (u == by_user.end()) u = by_user.emplace(ukey, progress::console(f.uid, !is_ps5)).first;
                    const auto pt = u->second.find(tid);
                    if (pt != u->second.end()) m.set("progress", progress::summary(pt->second));
                }
                c.meta = m;
                it->components.push_back(std::move(c));
            }
        }
    };
    add(ps5, true);
    add(ps4, false);
    log("console saves attached=%u standalone=%u", attached, standalone);
}

}  // namespace pbc::saves
