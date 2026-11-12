#include "core/titles.hpp"

#include <cctype>
#include <map>
#include <mutex>

#include "core/fs.hpp"

namespace pbc::titles {
namespace {
std::mutex mu;
std::string dir;
struct Row { std::string name, extra; };
std::map<std::string, Row> ps, sw;
bool ps_loaded = false, sw_loaded = false;

void load(const char* file, std::map<std::string, Row>& table) {
    std::string text;
    if (dir.empty() || !fs::read(fs::host(fs::join(dir, file)), text, 16u << 20)) return;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::size_t tab = text.find('\t', pos);
        if (tab != std::string::npos && tab < end) {
            const std::size_t tab2 = text.find('\t', tab + 1);
            Row r;
            if (tab2 != std::string::npos && tab2 < end) { r.name = text.substr(tab + 1, tab2 - tab - 1); r.extra = text.substr(tab2 + 1, end - tab2 - 1); }
            else r.name = text.substr(tab + 1, end - tab - 1);
            table.emplace(text.substr(pos, tab - pos), std::move(r));
        }
        pos = end + 1;
    }
}
}  // namespace

void set_app_dir(std::string app_dir) {
    std::lock_guard<std::mutex> l(mu);
    dir = std::move(app_dir);
    ps_loaded = sw_loaded = false;
    ps.clear();
    sw.clear();
}

// Ligne de la table pour ce numéro (nullptr si inconnu) ; mu doit être tenu.
static const Row* find(const std::string& raw) {
    std::string tid;
    for (char c : raw) tid += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (tid.size() >= 9 && (tid.rfind("PPSA", 0) == 0 || tid.rfind("CUSA", 0) == 0)) {
        if (!ps_loaded) { ps_loaded = true; load("assets/ps-titles.tsv", ps); }
        auto it = ps.find(tid.substr(0, 9));
        return it == ps.end() ? nullptr : &it->second;
    }
    if (tid.size() == 16) {
        if (!sw_loaded) { sw_loaded = true; load("assets/switch-titles.tsv", sw); }
        tid.replace(13, 3, "000");  // mises à jour et contenus additionnels → jeu de base
        auto it = sw.find(tid);
        return it == sw.end() ? nullptr : &it->second;
    }
    return nullptr;
}

std::string name(const std::string& tid) {
    std::lock_guard<std::mutex> l(mu);
    const Row* r = find(tid);
    return r ? r->name : std::string();
}

std::string ps_concept(const std::string& tid) {
    std::lock_guard<std::mutex> l(mu);
    const Row* r = find(tid);
    return r && tid.size() >= 4 && (tid[0] == 'P' || tid[0] == 'C' || tid[0] == 'p' || tid[0] == 'c') ? r->extra : std::string();
}

std::string ps_by_name(const std::string& title, const std::string& prefix) {
    auto low = [](const std::string& s) { std::string o; for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return o; };
    const std::string want = low(title);
    if (want.empty()) return "";
    std::lock_guard<std::mutex> l(mu);
    if (!ps_loaded) { ps_loaded = true; load("assets/ps-titles.tsv", ps); }
    for (const auto& [tid, row] : ps)
        if (tid.rfind(prefix, 0) == 0 && low(row.name) == want) return tid;
    return "";
}

std::string switch_icon_url(const std::string& tid) {
    std::lock_guard<std::mutex> l(mu);
    const Row* r = find(tid);
    return r && tid.size() == 16 && r->extra.size() == 64 ? "https://img-eshop.cdn.nintendo.net/i/" + r->extra + ".jpg" : std::string();
}

}  // namespace pbc::titles
