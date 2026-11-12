#include "scan/scanner.hpp"

#include <algorithm>
#include <chrono>

#include "core/util.hpp"

namespace pbc::scan {

bool DirView::has(std::string_view name, bool dir) const { const auto* e = find(name); return e && e->dir == dir; }
const fs::Entry* DirView::find(std::string_view name) const {
    if (!entries) return nullptr;
    for (const auto& e : *entries) if (e.name == name) return &e;
    return nullptr;
}

bool is_ignored_dir(std::string_view n) {
    return n.empty() || n[0] == '.' || n[0] == '$' || n == "System Volume Information" || n == "PBC Backup";
}

std::vector<Root> Scanner::default_roots(const Place& p) {
    if (p.type == Place::Type::internal) return {{"/data/homebrew", 1}, {"/user/app", 1}};
    return {{p.mount, 5}};
}

void Scanner::walk(const Place& place, const std::string& path, int depth, int max_depth, std::vector<Item>& out, Stats& st, bool no_cache) {
    const std::string hp = fs::host(path);
    fs::Entry self;
    if (!fs::stat(hp, self) || !self.dir) return;

    auto it = cache_.find(path);
    std::vector<fs::Entry> entries;
    bool reuse = false;
    if (!no_cache && it != cache_.end() && it->second.mtime == self.mtime) {
        reuse = true;
    } else if (!fs::list(hp, entries)) {
        return;
    }
    ++st.dirs;

    Cached c;
    if (reuse) {
        ++st.reused;
        c = it->second;
    } else {
        c.mtime = self.mtime;
        c.count = entries.size();
        DirView view{path, &place, &entries, depth};
        for (auto& r : recognizers_) {
            if (r->visit(view, c.items) == Verdict::claimed) c.claimed = true;
        }
        if (!c.claimed && depth < max_depth)
            for (const auto& e : entries) if (e.dir && !is_ignored_dir(e.name)) c.children.push_back(fs::join(path, e.name));
        cache_[path] = c;
    }
    for (const auto& item : c.items) out.push_back(item);
    for (const auto& child : c.children) walk(place, child, depth + 1, max_depth, out, st);
}

std::vector<Item> Scanner::run(const std::vector<Place>& places, const std::vector<Root>& extra, Stats* stats) {
    const auto t0 = std::chrono::steady_clock::now();
    Stats st;
    std::vector<Item> out;
    for (const auto& r : extra) {
        const Place* owner = nullptr;
        for (const auto& p : places)
            if (p.type == Place::Type::usb && r.path.rfind(p.mount + "/", 0) == 0) owner = &p;
        if (!owner && !places.empty()) owner = &places[0];
        if (owner) walk(*owner, r.path, 0, 0, out, st, true);
    }
    for (const auto& p : places)
        for (const auto& root : default_roots(p)) walk(p, root.path, 0, root.max_depth, out, st);
    merge(out);
    st.items = static_cast<unsigned>(out.size());
    st.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    log("scan done items=%u dirs=%u reused=%u ms=%lld", st.items, st.dirs, st.reused, static_cast<long long>(st.ms));
    if (stats) *stats = st;
    return out;
}

void merge(std::vector<Item>& items) {
    std::vector<Item> out;
    for (auto& it : items) {
        auto found = std::find_if(out.begin(), out.end(), [&](const Item& o) { return o.key == it.key; });
        if (found == out.end()) { out.push_back(std::move(it)); continue; }
        for (const auto& a : it.apps)
            if (std::find(found->apps.begin(), found->apps.end(), a) == found->apps.end()) found->apps.push_back(a);
        if (found->cover.empty()) found->cover = it.cover;
        for (auto& c : it.components) {
            auto same = std::find_if(found->components.begin(), found->components.end(), [&](const Component& o) { return o.part == c.part; });
            if (same == found->components.end()) { found->components.push_back(std::move(c)); continue; }
            // Même composant vu à deux endroits : /user/app n'est que l'inscription système d'un titre
            // dont le vrai contenu est ailleurs (/data/homebrew, USB) — on garde le vrai contenu.
            auto registered = [](const Component& x) { return !x.sources.empty() && x.sources[0].source.rfind("/user/app/", 0) == 0; };
            if (registered(*same) && !registered(c)) { *same = std::move(c); found->place_id = it.place_id; found->kind = it.kind; }
        }
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    items = std::move(out);
}

}  // namespace pbc::scan

namespace pbc::scan {

// À augmenter quand une brique de reconnaissance change ce qu'elle produit : l'ancien cache est ignoré.
// 2 : jeux PS5 du commerce distingués des homebrews (signature « standard »).
// 3 : jeux montés depuis une image (mount_img.lnk) : le jeu = l'image, plus son inscription.
constexpr int cache_version = 3;

bool Scanner::save_cache(const std::string& file) const {
    Json root = Json::object();
    root.set("version", cache_version);
    Json dirs = Json::object();
    for (const auto& [path, c] : cache_) {
        Json d = Json::object();
        d.set("mtime", c.mtime);
        d.set("claimed", c.claimed);
        Json items = Json::array();
        for (const auto& it : c.items) items.push(it.to_json());
        d.set("items", items);
        Json children = Json::array();
        for (const auto& ch : c.children) children.push(ch);
        d.set("children", children);
        dirs.set(path, d);
    }
    root.set("dirs", dirs);
    return fs::write_atomic(file, root.dump() + "\n");
}

bool Scanner::load_cache(const std::string& file) {
    std::string text;
    Json root;
    if (!fs::read(file, text, 64u << 20) || !Json::parse(text, root) || root["version"].as_int() != cache_version) return false;
    cache_.clear();
    for (const auto& [path, d] : root["dirs"].members()) {
        Cached c;
        c.mtime = d["mtime"].as_int();
        c.claimed = d["claimed"].as_bool();
        for (const auto& ij : d["items"].items()) { Item it; if (Item::from_json(ij, it)) c.items.push_back(std::move(it)); }
        for (const auto& ch : d["children"].items()) c.children.push_back(ch.as_string());
        cache_[path] = std::move(c);
    }
    log("scan cache loaded dirs=%zu", cache_.size());
    return true;
}

}  // namespace pbc::scan
