#include "covers/fetch.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/titles.hpp"
#include "core/json.hpp"
#include "core/util.hpp"

namespace pbc::covers {

namespace {
constexpr const char* base_url = "http://thumbnails.libretro.com/";
constexpr int matcher_version = 4;  // à augmenter quand best_match change

std::string url_encode(const std::string& s) {
    std::string o;
    char b[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

std::string url_decode(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
        else o += s[i];
    }
    return o;
}

// Nom de fichier libretro : &*/:`<>?\| remplacés par _.
std::string libretro_file(const std::string& name) {
    std::string o = name;
    for (auto& c : o) if (std::string("&*/:`<>?\\|").find(c) != std::string::npos) c = '_';
    return o;
}

std::string region_of(const std::string& name) {
    for (const char* r : {"Europe", "France", "USA", "Japan", "World", "Germany", "Spain", "Italy"})
        if (name.find(r) != std::string::npos) return r;
    return "";
}
}  // namespace

std::string libretro_system(const std::string& p) {
    static const std::map<std::string, std::string> m = {
        {"Nintendo 64", "Nintendo - Nintendo 64"}, {"Mega Drive", "Sega - Mega Drive - Genesis"}, {"Dreamcast", "Sega - Dreamcast"},
        {"PSP", "Sony - PlayStation Portable"}, {"PlayStation", "Sony - PlayStation"}, {"PlayStation 2", "Sony - PlayStation 2"},
        {"Super Nintendo", "Nintendo - Super Nintendo Entertainment System"}, {"SNES", "Nintendo - Super Nintendo Entertainment System"},
        {"NES", "Nintendo - Nintendo Entertainment System"}, {"Game Boy", "Nintendo - Game Boy"}, {"Game Boy Color", "Nintendo - Game Boy Color"},
        {"Game Boy Advance", "Nintendo - Game Boy Advance"}, {"Nintendo DS", "Nintendo - Nintendo DS"}, {"Nintendo 3DS", "Nintendo - Nintendo 3DS"},
        {"GameCube", "Nintendo - GameCube"}, {"Wii", "Nintendo - Wii"}, {"Saturn", "Sega - Saturn"}, {"Master System", "Sega - Master System - Mark III"},
        {"Game Gear", "Sega - Game Gear"}, {"Mega-CD", "Sega - Mega-CD - Sega CD"}, {"32X", "Sega - 32X"},
        {"PC Engine", "NEC - PC Engine - TurboGrafx 16"}, {"PC Engine CD", "NEC - PC Engine CD - TurboGrafx-CD"},
        {"Atari 2600", "Atari - 2600"}, {"Arcade", "FBNeo - Arcade Games"}, {"Neo Geo", "SNK - Neo Geo"},
        {"WonderSwan", "Bandai - WonderSwan"}, {"WonderSwan Color", "Bandai - WonderSwan Color"},
    };
    auto it = m.find(p);
    return it == m.end() ? "" : it->second;
}

std::string normalize(const std::string& name) {
    std::string s, out;
    int depth = 0;
    for (char c : name) {
        if (c == '(' || c == '[') ++depth;
        else if ((c == ')' || c == ']') && depth) --depth;
        else if (!depth) s += c;
    }
    // « Legend of Zelda, The - … » → « The Legend of Zelda - … »
    const auto the = s.find(", The");
    if (the != std::string::npos) { s.erase(the, 5); s = "The " + s; }
    // « & », « and » et « _ » (libretro remplace & par _ dans ses fichiers) ne comptent pas.
    std::string word, joined;
    auto flush = [&] { if (!word.empty() && word != "and") { if (!joined.empty()) joined += ' '; joined += word; } word.clear(); };
    for (unsigned char c : s) {
        if (std::isalnum(c) || c >= 0x80) word += static_cast<char>(std::tolower(c));
        else if (c == '\'') continue;  // « Majora's » = « Majoras »
        else flush();
    }
    flush();
    out = joined;
    return out;
}

std::string best_match_key(const std::string& wanted, const std::string& key, const std::vector<std::string>& names);

std::string best_match(const std::string& wanted, const std::vector<std::string>& names) {
    // 1. nom exact ; 2. même nom simplifié, région la plus proche ; 3. ROM modifiée (« … Redux »,
    // « … with Mario Characters ») : le jeu d'origine, sans les mots ajoutés par le hack.
    for (const auto& n : names) if (n == wanted) return n;
    std::string key = normalize(wanted);
    std::string found = best_match_key(wanted, key, names);
    if (!found.empty()) return found;
    if (const auto with = key.find(" with "); with != std::string::npos) {
        found = best_match_key(wanted, key.substr(0, with), names);
        if (!found.empty()) return found;
    }
    static const char* extras[] = {"redux", "hack", "remastered", "restored", "enhanced", "reloaded", "plus", "hd", "deluxe", "remix",
                                   "reworked", "fixed", "patched", "translated", "translation", "english", "improved", "ultimate", "master", "quest"};
    for (int pass = 0; pass < 3; ++pass) {
        const auto sp = key.rfind(' ');
        if (sp == std::string::npos) break;
        const std::string last = key.substr(sp + 1);
        bool extra = false;
        for (const char* e : extras) if (last == e) extra = true;
        if (!extra) break;
        key.resize(sp);
        found = best_match_key(wanted, key, names);
        if (!found.empty()) return found;
    }
    return {};
}

std::string best_match_key(const std::string& wanted, const std::string& key, const std::vector<std::string>& names) {
    const std::string region = region_of(wanted);
    std::string best;
    int best_score = -1;
    for (const auto& n : names) {
        if (normalize(n) != key) continue;
        const std::string r = region_of(n);
        int score = r == region && !r.empty() ? 50 : r == "Europe" ? 40 : r == "World" ? 35 : r == "USA" ? 30 : r == "France" ? 25 : 10;
        // Versions particulières : en dernier recours seulement.
        for (const char* odd : {"(Beta", "(Proto", "(Demo", "(Debug", "(Kiosk", "(Sega Channel", "(GameCube", "(Alt", "(Virtual Console", "(Sample", "(Pirate", "(Hack"})
            if (n.find(odd) != std::string::npos) score -= 25;
        // À égalité, le nom le plus court (le moins d'étiquettes) gagne.
        score = score * 1000 - static_cast<int>(std::min<std::size_t>(n.size(), 999));
        if (score > best_score) { best_score = score; best = n; }
    }
    return best;
}

void Fetcher::start() {
    std::lock_guard<std::mutex> l(mu_);
    if (!loaded_) {
        loaded_ = true;
        std::string text;
        Json j;
        if (fs::read(fs::host(fs::join(dir_, "covers-dl/state.json")), text) && Json::parse(text, j)) {
            // Résultats d'une ancienne règle de correspondance : on refait tout (trouvés comme échecs).
            if (j["matcher"].as_int() == matcher_version) {
                for (const auto& [k, v] : j["found"].members()) found_[k] = v.as_string();
                for (const auto& [k, v] : j["missed"].members()) missed_[k] = v.as_int();
            }
        }
    }
    quit_ = false;
    thread_ = std::thread([this] { loop(); });
}

void Fetcher::stop() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string Fetcher::lookup(const std::string& key, const std::string& platform, const std::string& title) {
    std::lock_guard<std::mutex> l(mu_);
    auto f = found_.find(key);
    if (f != found_.end() && fs::exists(fs::host(f->second))) return f->second;
    if (!enabled_) return "";
    // « pspatches » / « eshop » : image officielle par numéro de jeu (titre = PPSA… / numéro Switch), en https.
    const std::string system = platform == "pspatches" || platform == "eshop" ? platform : libretro_system(platform);
    if (system.empty()) return "";
    auto m = missed_.find(key);
    if (m != missed_.end() && now_unix() - m->second < 7 * 86400) return "";
    for (const auto& w : queue_) if (w.key == key) return "";
    queue_.push_back({key, system, title});
    cv_.notify_one();
    return "";
}

bool Fetcher::names_for(const std::string& system, std::vector<std::string>& out) {
    auto c = lists_.find(system);
    if (c != lists_.end()) { out = c->second; return true; }
    const std::string cache = fs::join(fs::join(dir_, "covers-dl/lists"), safe_name(system) + ".txt");
    fs::Entry e;
    std::string text;
    if (!(fs::stat(fs::host(cache), e) && now_unix() - e.mtime < 30 * 86400 && fs::read(fs::host(cache), text, 16u << 20))) {
        std::string html;
        const auto r = http::get_text(base_url + url_encode(system) + "/Named_Boxarts/", html, 16u << 20);
        if (!r.error.empty()) { log("covers list failed system=%s error=%s", system.c_str(), r.error.c_str()); return false; }
        text.clear();
        for (std::size_t p = 0; (p = html.find("href=\"", p)) != std::string::npos;) {
            p += 6;
            const auto end = html.find('"', p);
            if (end == std::string::npos) break;
            const std::string h = url_decode(html.substr(p, end - p));
            if (h.size() > 4 && h.compare(h.size() - 4, 4, ".png") == 0 && h.find('/') == std::string::npos) text += h.substr(0, h.size() - 4) + "\n";
            p = end;
        }
        fs::mkdirs(fs::host(std::string(fs::dirname(cache))));
        fs::write_atomic(fs::host(cache), text);
    }
    std::vector<std::string> names;
    for (std::size_t p = 0, q; p < text.size(); p = q + 1) {
        q = text.find('\n', p);
        if (q == std::string::npos) q = text.size();
        if (q > p) names.push_back(text.substr(p, q - p));
    }
    log("covers list system=%s names=%zu", system.c_str(), names.size());
    lists_[system] = names;
    out = std::move(names);
    return true;
}

void Fetcher::save_state() {
    Json j = Json::object(), f = Json::object(), m = Json::object();
    for (const auto& [k, v] : found_) f.set(k, v);
    for (const auto& [k, v] : missed_) m.set(k, v);
    j.set("found", f);
    j.set("missed", m);
    j.set("matcher", matcher_version);
    fs::mkdirs(fs::host(fs::join(dir_, "covers-dl")));
    fs::write_atomic(fs::host(fs::join(dir_, "covers-dl/state.json")), j.dump() + "\n");
}

// Image officielle d'un jeu par son numéro :
// - PS5 / PS4 : icône du jeu (icon0, 300×300) d'après prosperopatches.com / orbispatches.com, comme
//   BlackBox — les adresses de Sony sont souvent bloquées sur une console modifiée ;
// - Switch : icône du CDN Nintendo (adresse dans la table des jeux livrée avec l'app).
std::string Fetcher::official(const std::string& system, const std::string& tid) {
    std::string url, ext = ".jpg";
    if (system == "pspatches") {
        const bool ps5 = tid.rfind("PPSA", 0) == 0;
        const std::string site = ps5 ? "prosperopatches.com" : "orbispatches.com";
        std::string html;
        const auto r = http::get_text("https://" + site + "/" + tid.substr(0, 9), html, 4u << 20);
        if (!r.error.empty()) { log("covers %s page failed tid=%s error=%s", site.c_str(), tid.c_str(), r.error.c_str()); return ""; }
        const std::string marker = "https://cdn." + site + "/titles/" + tid.substr(0, 9) + "_";
        const auto p = html.find(marker);
        if (p == std::string::npos) return "";
        const auto end = html.find_first_of("\"' )<", p);
        std::string found = html.substr(p, end == std::string::npos ? std::string::npos : end - p);
        const auto slash = found.find('/', marker.size());
        if (slash == std::string::npos) return "";
        url = found.substr(0, slash) + "/icon0.webp";
        ext = ".webp";
    } else {
        url = titles::switch_icon_url(tid);
        if (url.empty()) return "";
    }
    std::string data;
    const auto r = http::get_text(url, data, 8u << 20);
    if (!r.error.empty() || data.size() < 12) { log("covers %s image failed tid=%s error=%s", system.c_str(), tid.c_str(), r.error.c_str()); return ""; }
    const bool png = data.compare(0, 4, "\x89PNG") == 0, jpg = static_cast<unsigned char>(data[0]) == 0xFF && static_cast<unsigned char>(data[1]) == 0xD8;
    const bool webp = data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WEBP") == 0;
    if (!png && !jpg && !webp) return "";
    const std::string dst = fs::join(fs::join(fs::join(dir_, "covers-dl"), system), safe_name(tid) + (png ? ".png" : webp ? ".webp" : ext));
    fs::mkdirs(fs::host(std::string(fs::dirname(dst))));
    return fs::write_atomic(fs::host(dst), data) ? dst : "";
}

void Fetcher::loop() {
    unsigned fresh = 0;
    while (!quit_) {
        Want w;
        {
            std::unique_lock<std::mutex> l(mu_);
            if (queue_.empty() && fresh) { save_state(); fresh = 0; }
            cv_.wait(l, [&] { return quit_ || !queue_.empty(); });
            if (quit_) break;
            w = queue_.front();
            queue_.pop_front();
        }
        if (w.system == "pspatches" || w.system == "eshop") {
            const std::string path = official(w.system, w.title);
            {
                std::lock_guard<std::mutex> l(mu_);
                if (!path.empty()) { found_[w.key] = path; missed_.erase(w.key); }
                else missed_[w.key] = now_unix();
            }
            log("covers %s %s title=%s", w.system.c_str(), path.empty() ? "miss" : "found", w.title.c_str());
            if (!path.empty()) { ++fresh; if (on_found) on_found(); }
            continue;
        }
        std::vector<std::string> names;
        const bool listed = names_for(w.system, names);  // lists_ n'est touché que par ce fil
        std::string match = listed ? best_match(w.title, names) : "";
        // Rien pour cette console (portage non officiel, homebrew…) : même jeu sur une autre console
        // connue, à nom identique (ex. Flashback sur Dreamcast → boîte Mega Drive).
        if (match.empty() && listed)
            for (const char* other : {"Sega - Mega Drive - Genesis", "Nintendo - Super Nintendo Entertainment System", "Sony - PlayStation",
                                      "Sega - Saturn", "Nintendo - Nintendo 64", "Sega - Dreamcast", "Nintendo - Game Boy Advance"}) {
                if (w.system == other) continue;
                std::vector<std::string> alt;
                if (!names_for(other, alt)) continue;
                match = best_match(w.title, alt);
                if (!match.empty()) { w.system = other; break; }
            }
        std::string path;
        if (!match.empty()) {
            const std::string dst = fs::join(fs::join(fs::join(dir_, "covers-dl"), safe_name(w.system)), safe_name(match, 120) + ".png");
            fs::mkdirs(fs::host(std::string(fs::dirname(dst))));
            std::string png;
            const auto r = http::get_text(base_url + url_encode(w.system) + "/Named_Boxarts/" + url_encode(libretro_file(match)) + ".png", png, 8u << 20);
            if (r.error.empty() && png.size() > 8 && png.compare(0, 4, "\x89PNG") == 0 && fs::write_atomic(fs::host(dst), png)) path = dst;
            else log("covers download failed name=%s error=%s", match.c_str(), r.error.c_str());
        }
        {
            std::lock_guard<std::mutex> l(mu_);
            if (!path.empty()) { found_[w.key] = path; missed_.erase(w.key); }
            else if (listed) missed_[w.key] = now_unix();  // liste injoignable : on réessaiera au prochain scan
        }
        log("covers %s title=%s match=%s", path.empty() ? "miss" : "found", w.title.c_str(), match.c_str());
        if (!path.empty()) { ++fresh; if (on_found) on_found(); }
    }
}

}  // namespace pbc::covers
