#include "saves/progress.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "saves/inspect.hpp"
#include "sqlite3.h"

namespace pbc::progress {

namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Libellés que la console ou les jeux mettent quand ils n'ont rien à dire.
bool generic(const std::string& t) {
    static const char* const g[] = {"données sauvegardées", "save data", "saved data", "savedata", "données du profil", "données système",
                                    "user settings", "user profile", "options data", "préférences", "settings", "system saved data"};
    const std::string l = lower(t);
    for (const char* x : g) if (l == x) return true;
    return false;
}

std::int64_t to_unix(const std::string& iso) {  // « 2026-11-08T16:53:23.00Z »
    std::tm tm{};
    if (iso.size() < 19 || std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return static_cast<std::int64_t>(timegm(&tm));
}

// Sauvegarde de profil, de réglages ou technique (pas une partie) : passe après les vraies parties.
bool side_save(const std::string& text, const std::string& dir) {
    const std::string l = lower(text + " " + dir);
    for (const char* k : {"profil", "profile", "settings", "paramètres", "parametres", "options", "option", "préférences", "system", "système",
                          "meta", "user information", "userdata", "user data", "config"})
        if (l.find(k) != std::string::npos) return true;
    return false;
}

}  // namespace

Parsed parse(const std::string& text) {
    Parsed p;
    const std::string t = lower(text);
    // Pourcentage : « 7.4% », « 12,5 % », « 100% ».
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '%') continue;
        std::size_t j = i;
        while (j > 0 && t[j - 1] == ' ') --j;
        std::size_t k = j;
        while (k > 0 && (digit(t[k - 1]) || t[k - 1] == '.' || t[k - 1] == ',')) --k;
        if (k == j || !digit(t[k])) continue;
        std::string num = t.substr(k, j - k);
        std::replace(num.begin(), num.end(), ',', '.');
        const double v = std::strtod(num.c_str(), nullptr);
        if (v >= 0 && v <= 100) p.percent = std::max(p.percent, v);
    }
    // Niveau : le nombre qui suit le mot.
    for (const char* k : {"level", "niveau", "nivel", "livello", "stufe", "lv.", "lv", "lvl", "nv.", "nv"}) {
        for (std::size_t at = t.find(k); at != std::string::npos; at = t.find(k, at + 1)) {
            if (at > 0 && std::isalpha(static_cast<unsigned char>(t[at - 1]))) continue;  // « travel », « novel »
            std::size_t i = at + std::string(k).size();
            while (i < t.size() && (t[i] == ' ' || t[i] == ':' || t[i] == '.')) ++i;
            if (i < t.size() && digit(t[i])) { p.level = std::max(p.level, std::atoi(t.c_str() + i)); break; }
        }
        if (p.level >= 0) break;
    }
    // Temps de jeu : d'abord les formes reconnues par l'inspection (mot-clé, « 34 h 12 », « 1:02:03 »)…
    p.play_seconds = saves::parse_play_time(text);
    // … puis « … - 00:31 » en fin de segment (Cyberpunk : « The Corpo-Rat - AutoSave-4 - 00:18 »).
    if (p.play_seconds < 0)
        for (std::size_t at = t.find(" - "); at != std::string::npos; at = t.find(" - ", at + 1)) {
            std::size_t i = at + 3, j = i;
            while (j < t.size() && digit(t[j])) ++j;
            if (j == i || j - i > 4 || j + 3 > t.size() || t[j] != ':' || !digit(t[j + 1]) || !digit(t[j + 2])) continue;
            if (j + 3 < t.size() && t[j + 3] != ' ' && t[j + 3] != ')' && t[j + 3] != ',') continue;
            const int m = (t[j + 1] - '0') * 10 + (t[j + 2] - '0');
            if (m < 60) p.play_seconds = std::atoll(t.c_str() + i) * 3600 + m * 60;
        }
    return p;
}

std::string text_of(const std::string& main_title, const std::string& sub_title, const std::string& detail) {
    std::vector<std::string> parts;
    for (const std::string& s : {main_title, sub_title, detail}) {
        if (s.empty() || generic(s)) continue;
        bool dup = false;
        for (auto& q : parts) {
            if (lower(q).find(lower(s)) != std::string::npos) dup = true;                    // déjà dit
            else if (lower(s).find(lower(q)) != std::string::npos) { q = s; dup = true; }  // plus complet : remplace
        }
        if (!dup) parts.push_back(s);
    }
    std::string out;
    for (const auto& s : parts) out += (out.empty() ? "" : " — ") + s;
    return out;
}

std::map<std::string, std::vector<SaveText>> console(const std::string& user_id, bool ps4) {
    std::map<std::string, std::vector<SaveText>> out;
    if (user_id.empty()) return out;
    const std::string path = fs::host(std::string(ps4 ? "/system_data/savedata/" : "/system_data/savedata_prospero/") + user_id + "/db/user/savedata.db");
    if (!fs::exists(path)) return out;
    sqlite3* db = nullptr;
    const std::string uri = "file:" + path + "?immutable=1";  // lecture seule, sans verrou : la console l'utilise
    if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) { log("progress: open failed %s", path.c_str()); sqlite3_close(db); return out; }
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT title_id, dir_name, main_title, sub_title, detail, mtime FROM savedata", -1, &st, nullptr) == SQLITE_OK) {
        auto col = [&](int i) { const auto* s = reinterpret_cast<const char*>(sqlite3_column_text(st, i)); return std::string(s ? s : ""); };
        while (sqlite3_step(st) == SQLITE_ROW) {
            SaveText s;
            s.dir = col(1);
            s.text = text_of(col(2), col(3), col(4));
            s.mtime = to_unix(col(5));
            s.p = parse(col(2) + " " + col(3) + " " + col(4));
            out[col(0)].push_back(std::move(s));
        }
    } else log("progress: query failed %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

Json to_json(const Parsed& p) {
    Json j = Json::object();
    if (p.percent >= 0) j.set("percent", p.percent); else j.set("percent", nullptr);
    if (p.level >= 0) j.set("level", p.level); else j.set("level", nullptr);
    if (p.play_seconds >= 0) j.set("play_time_seconds", p.play_seconds); else j.set("play_time_seconds", nullptr);
    return j;
}

Json summary(const std::vector<SaveText>& saves) {
    const SaveText* best = nullptr;
    std::string by = "date";
    auto better = [&](const SaveText& a, const SaveText& b) {  // a va-t-il plus loin que b ?
        const bool sa = side_save(a.text, a.dir), sb = side_save(b.text, b.dir);
        if (sa != sb) return !sa;  // une vraie partie passe avant un profil ou des réglages
        if (a.p.play_seconds != b.p.play_seconds && (a.p.play_seconds >= 0 || b.p.play_seconds >= 0)) return a.p.play_seconds > b.p.play_seconds;
        if (a.p.percent != b.p.percent && (a.p.percent >= 0 || b.p.percent >= 0)) return a.p.percent > b.p.percent;
        if (a.p.level != b.p.level && (a.p.level >= 0 || b.p.level >= 0)) return a.p.level > b.p.level;
        if (a.text.empty() != b.text.empty()) return !a.text.empty();
        return a.mtime > b.mtime;
    };
    for (const auto& s : saves) if (!best || better(s, *best)) best = &s;
    Json j = to_json(best ? best->p : Parsed{});
    if (best) by = best->p.play_seconds >= 0 ? "play_time" : best->p.percent >= 0 ? "percent" : best->p.level >= 0 ? "level" : best->text.empty() ? "date" : "text";
    j.set("text", best ? best->text : std::string());
    j.set("dir", best ? best->dir : std::string());
    std::int64_t newest = 0;
    for (const auto& s : saves) newest = std::max(newest, s.mtime);
    j.set("mtime", newest);
    j.set("saves", static_cast<std::int64_t>(saves.size()));
    j.set("by", by);
    return j;
}

Tracker::Tracker(std::string path) : path_(std::move(path)) {
    std::string text;
    Json j;
    if (fs::read(path_, text) && Json::parse(text, j) && j.is_object())
        for (const auto& [k, v] : j.members()) if (v.as_int() > 0) total_[k] = v.as_int();
}

void Tracker::tick(const std::string& running, std::int64_t now) {
    // Notre app (PPSA99950), les écrans système (NPXS…) et un titre inconnu ne comptent pas.
    const bool counts = !running.empty() && running != "?" && running != "PPSA99950" && running.rfind("NPXS", 0) != 0;
    const std::string cur = counts ? running : std::string();
    if (!current_.empty() && since_ > 0 && now > since_) {
        const std::int64_t d = now - since_;
        if (d < 600) { total_[current_] += d; dirty_ = true; }  // un trou (console en veille) ne compte pas
    }
    current_ = cur;
    since_ = cur.empty() ? 0 : now;
    if (dirty_ && now - last_save_ >= 300) save();
    if (!last_save_) last_save_ = now;
}

void Tracker::save() {
    Json j = Json::object();
    for (const auto& [k, v] : total_) j.set(k, v);
    if (fs::write_atomic(path_, j.dump() + "\n")) { dirty_ = false; last_save_ = now_unix(); }
}

std::int64_t Tracker::seconds(const std::string& t) const { const auto it = total_.find(t); return it == total_.end() ? 0 : it->second; }

Json Tracker::all() const {
    Json j = Json::object();
    for (const auto& [k, v] : total_) j.set(k, v);
    return j;
}

}  // namespace pbc::progress
