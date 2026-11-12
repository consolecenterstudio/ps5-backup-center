#include "scan/recognizers/title_dir.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::scan {

bool read_param(const std::string& path, std::string& title_id, std::string& title, int& category) {
    std::string text;
    Json j;
    if (!fs::read(fs::host(path), text, 256 * 1024) || !Json::parse(text, j)) return false;
    title_id = j["titleId"].as_string();
    category = static_cast<int>(j["applicationCategoryType"].as_int(0));
    const Json& loc = j["localizedParameters"];
    const std::string& def = loc["defaultLanguage"].as_string();
    for (const char* lang : {"fr-FR", "fr-CA", "en-US", def.c_str()}) {
        const std::string& t = loc[lang]["titleName"].as_string();
        if (!t.empty()) { title = t; break; }
    }
    if (title.empty()) title = title_id;
    return !title_id.empty();
}

bool is_retail(const std::string& path) {
    std::string text;
    Json j;
    if (!fs::read(fs::host(path), text, 256 * 1024) || !Json::parse(text, j)) return false;
    return j["applicationDrmType"].as_string() == "standard";
}

namespace {
std::uint64_t tree_bytes(const std::string& host_dir, int depth = 0) {
    if (depth > 12) return 0;
    std::vector<fs::Entry> entries;
    if (!fs::list(host_dir, entries)) return 0;
    std::uint64_t total = 0;
    for (const auto& e : entries) total += e.dir ? tree_bytes(fs::join(host_dir, e.name), depth + 1) : e.size;
    return total;
}
}  // namespace

Verdict TitleDirRecognizer::visit(const DirView& dir, std::vector<Item>& out) {
    if (!dir.has("sce_sys", true)) return Verdict::descend;
    const std::string param = fs::join(dir.path, "sce_sys/param.json");
    std::string title_id, title;
    int category = 0;
    if (!read_param(param, title_id, title, category)) return Verdict::descend;

    const bool homebrew = dir.path.rfind("/data/homebrew/", 0) == 0;
    Item it;
    it.key = "title:" + title_id;
    // applicationCategoryType : 0 = jeu ; autre (65536…) = application. Les homebrews sont des applications.
    it.kind = homebrew || category != 0 || !is_retail(param) ? Kind::app : Kind::game_ps5;
    it.title = title;
    it.title_id = title_id;
    it.platform = "PS5";
    it.place_id = dir.place ? dir.place->id : "";
    it.image_type = "dossier";
    if (fs::exists(fs::host(fs::join(dir.path, "sce_sys/icon0.png")))) it.cover = fs::join(dir.path, "sce_sys/icon0.png");

    Component app;
    app.part = Part::app;
    app.label = it.kind == Kind::app ? "Application" : "Jeu installé";
    // Jeu monté par shadowmount depuis une image : /user/app/<ID> n'est que son inscription (sce_sys,
    // icône, mount.lnk) ; mount_img.lnk donne l'image (.ffpfsc / .exfat / .ffpkg) — le vrai jeu.
    std::string image;
    {
        std::string lnk;
        if (fs::read(fs::host(fs::join(dir.path, "mount_img.lnk")), lnk, 4096)) {
            image = lnk.substr(0, lnk.find_first_of("\r\n"));
            while (!image.empty() && (image.back() == ' ' || image.back() == '\t')) image.pop_back();
        }
    }
    fs::Entry img;
    if (!image.empty() && image[0] == '/' && fs::stat(fs::host(image), img) && !img.dir) {
        const auto dot = image.rfind('.');
        it.image_type = "image " + (dot == std::string::npos ? std::string("") : image.substr(dot));
        app.label = "Jeu (image " + (dot == std::string::npos ? std::string("") : image.substr(dot)) + ")";
        app.sources.push_back({image, "image/" + std::string(fs::basename(image)), false});
        app.sources.push_back({dir.path, "registration/" + std::string(fs::basename(dir.path)), true});
        app.bytes = img.size + tree_bytes(fs::host(dir.path));
        app.restore_to = std::string(fs::dirname(image));
        Json m = Json::object();
        m.set("image", image);
        m.set("registration", dir.path);
        app.meta = m;
    } else {
        app.sources.push_back({dir.path, std::string(fs::basename(dir.path)), true});
        app.bytes = tree_bytes(fs::host(dir.path));
        app.restore_to = dir.path;
    }
    it.components.push_back(std::move(app));

    auto cfg = config_dirs_.find(title_id);
    if (cfg != config_dirs_.end() && fs::is_dir(fs::host(cfg->second))) {
        Component c;
        c.part = Part::config;
        c.label = "Configuration";
        c.sources.push_back({cfg->second, std::string(fs::basename(cfg->second)), true});
        c.bytes = tree_bytes(fs::host(cfg->second));
        c.restore_to = cfg->second;
        it.components.push_back(std::move(c));
    }
    auto files = config_files_.find(title_id);
    if (files != config_files_.end() && cfg == config_dirs_.end()) {
        Component c;
        c.part = Part::config;
        c.label = "Configuration";
        for (const auto& f : files->second) {
            fs::Entry e;
            if (!fs::stat(fs::host(f), e) || e.dir) continue;
            c.sources.push_back({f, fs::join(std::string(fs::basename(fs::dirname(f))), std::string(fs::basename(f))), false});
            c.bytes += e.size;
            if (c.restore_to.empty()) c.restore_to = std::string(fs::dirname(f));
        }
        if (!c.sources.empty()) it.components.push_back(std::move(c));
    }
    // Backport ShadowMountPlus du jeu (dossier backports/<Title ID>) : une partie du jeu, copiée et envoyée avec lui.
    {
        std::vector<std::string> roots = {"/data/shadowmount/backports", "/data/homebrew/backports"};
        for (int u = 0; u < 8; ++u) roots.push_back("/mnt/usb" + std::to_string(u) + "/homebrew/backports");
        for (const auto& r : roots) {
            const std::string d = fs::join(r, title_id);
            if (!fs::is_dir(fs::host(d))) continue;
            Component c;
            c.part = Part::backport;
            c.label = "Backport (ShadowMountPlus)";
            c.sources.push_back({d, title_id, true});
            c.bytes = tree_bytes(fs::host(d));
            c.restore_to = d;
            it.components.push_back(std::move(c));
            break;
        }
    }
    out.push_back(std::move(it));
    return Verdict::claimed;
}

}  // namespace pbc::scan
