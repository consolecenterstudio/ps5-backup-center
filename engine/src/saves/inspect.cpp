#include "saves/inspect.hpp"
#include "saves/progress.hpp"
#include "saves/catalog.hpp"
#include "images/images.hpp"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "saves/garlic_core.h"

namespace pbc::saves {

namespace {

bool main_image(std::string_view name) { return name.rfind("sdimg_", 0) == 0 && name.rfind("sdimg_sce_bu_", 0) != 0; }

std::string hex16(std::uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016" PRIX64, v);
    return b;
}

void garlic_log(const char* line) { log("garlic %s", line); }

bool hash_file(const std::string& host_path, std::string& out) {
    FILE* f = std::fopen(host_path.c_str(), "rb");
    if (!f) return false;
    Sha256 h;
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) h.add(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    if (ok) out = h.hex();
    return ok;
}

// Fichiers intérieurs d'une image montée. sce_sys/ (param.sfo, icône, clés) est écarté : il porte le compte et
// l'horodatage de la copie, pas la progression ; deux copies identiques du jeu y différeraient.
bool walk_mount(const std::string& root, const std::string& rel, std::vector<InnerFile>& out, int depth) {
    std::vector<fs::Entry> es;
    if (depth > 16 || !fs::list(fs::host(rel.empty() ? root : fs::join(root, rel)), es)) return false;
    for (const auto& e : es) {
        const std::string r = rel.empty() ? e.name : rel + "/" + e.name;
        if (e.dir) {
            if (rel.empty() && e.name == "sce_sys") continue;
            if (!walk_mount(root, r, out, depth + 1)) return false;
            continue;
        }
        InnerFile f;
        f.path = r;
        f.size = e.size;
        f.mtime = e.mtime;
        if (!hash_file(fs::host(fs::join(root, r)), f.sha256)) return false;
        out.push_back(std::move(f));
    }
    return true;
}

bool starts(const std::string& s, std::size_t i, std::string_view w) { return s.compare(i, w.size(), w) == 0; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
// Unité reconnue à la position i (la plus longue d'abord), suivie d'une fin de mot ; renvoie sa longueur.
std::size_t unit_at(const std::string& s, std::size_t i, std::initializer_list<std::string_view> units) {
    for (auto u : units)
        if (starts(s, i, u) && (i + u.size() >= s.size() || !is_alpha(s[i + u.size()]) || static_cast<unsigned char>(u[0]) >= 0x80)) return u.size();
    return 0;
}
constexpr std::initializer_list<std::string_view> kHours = {"hours", "hour", "hrs", "hr", "heures", "heure", "std", "h", "時間"};
constexpr std::initializer_list<std::string_view> kMinutes = {"minutes", "minute", "mins", "min", "mn", "m", "分"};

std::string text_of(const Json& img) {
    const std::string& s = img["subtitle"].as_string();
    const std::string& d = img["detail"].as_string();
    if (s.empty()) return d;
    if (d.empty() || d == s) return s;
    return s + " — " + d;
}

// Empreinte brute d'une copie : SHA-256 des « nom:empreinte du fichier » de ses images, triés. Deux copies faites
// des mêmes fichiers ont la même empreinte brute : la seconde n'a pas besoin d'être ouverte (montée) pour être lue.
std::string raw_signature(const std::vector<std::string>& paths, const std::atomic<bool>& stop) {
    std::vector<std::string> lines;
    std::vector<char> buf(1 << 20);
    for (const auto& p : paths) {
        const int fd = ::open(fs::host(p).c_str(), O_RDONLY);
        if (fd < 0) return "";
        Sha256 h;
        for (ssize_t n; (n = ::read(fd, buf.data(), buf.size())) > 0;) { h.add(buf.data(), static_cast<std::size_t>(n)); if (stop) { ::close(fd); return ""; } }
        ::close(fd);
        lines.push_back(p.substr(p.rfind('/') + 1) + ":" + h.hex());
    }
    if (lines.empty()) return "";
    std::sort(lines.begin(), lines.end());
    Sha256 all;
    for (const auto& l : lines) { all.add(l.data(), l.size()); all.add("\n", 1); }
    return all.hex();
}

// Empreinte brute lue dans le manifeste d'un backup fait par l'app (il note le SHA-256 de chaque fichier) : même
// calcul que raw_signature, sans rien copier ni télécharger. Vide si pas de manifeste ou pas d'images de ce jeu.
std::string manifest_signature(store::Store& st, const std::string& dir, const std::string& title_id, const std::string& user_id) {
    std::string text;
    Json m;
    if (!st.read_small(fs::join(dir, "manifest.json"), text, 8u << 20) || !Json::parse(text, m)) return "";
    std::map<std::string, std::pair<int, std::string>> best;  // nom d'image → (priorité, empreinte) ; le compte demandé d'abord
    for (const auto& f : m["files"].items()) {
        const std::string path = f["path"].as_string(), sha = f["sha256"].as_string();
        const std::string name(fs::basename(path));
        if (sha.empty() || !main_image(name) || path.find("/ps5/") == std::string::npos) continue;
        if (!title_id.empty() && path.find("/" + title_id + "/") == std::string::npos) continue;
        const int prio = !user_id.empty() && path.find("/ps5/" + user_id + "/") != std::string::npos ? 0 : 1;
        auto it = best.find(name);
        if (it == best.end() || prio < it->second.first) best[name] = {prio, sha};
    }
    if (best.empty()) return "";
    std::vector<std::string> lines;
    for (const auto& [name, v] : best) lines.push_back(name + ":" + v.second);
    std::sort(lines.begin(), lines.end());
    Sha256 all;
    for (const auto& l : lines) { all.add(l.data(), l.size()); all.add("\n", 1); }
    return all.hex();
}

std::mutex cache_mu;
std::string cache_path(const std::string& state_dir) { return fs::host(fs::join(state_dir, "inspect-cache.json")); }
Json load_cache(const std::string& state_dir) {
    std::string text;
    Json j;
    if (!fs::read(cache_path(state_dir), text) || !Json::parse(text, j) || !j["versions"].is_object()) { j = Json::object(); j.set("versions", Json::object()); }
    return j;
}

bool has_images(const std::string& dir) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(dir), es)) return false;
    for (const auto& e : es) if (!e.dir && main_image(e.name)) return true;
    return false;
}
// Sauvegarde en place : PS5 (savedata_prospero) d'abord, sinon PS4 (savedata, images + clés .bin).
std::string console_dir(const std::string& user_id, const std::string& title_id) {
    const std::string home = fs::join("/user/home", user_id);
    const std::string ps5 = fs::join(home, "savedata_prospero/" + title_id), ps4 = fs::join(home, "savedata/" + title_id);
    return !has_images(ps5) && has_images(ps4) ? ps4 : ps5;
}
// Signature des images en place (noms, tailles, dates) : la version console analysée est-elle encore à jour ?
std::string console_signature(const std::string& user_id, const std::string& title_id) {
    std::vector<fs::Entry> es;
    if (!fs::list(fs::host(console_dir(user_id, title_id)), es)) return "";
    std::string s;
    for (const auto& e : es) if (!e.dir && (main_image(e.name) || e.name.ends_with(".bin"))) s += e.name + ":" + std::to_string(e.size) + ":" + std::to_string(e.mtime) + "\n";
    return s.empty() ? "" : sha256_hex(s);
}
std::string cache_key(const Json& src, const std::string& title_id, const std::string& user_id) {
    if (src["kind"].as_string() == "console") return "console|" + user_id + "|" + title_id;
    return src["place"].as_string() + "|" + src["bundle"].as_string() + "|" + title_id;
}

}  // namespace

std::int64_t parse_play_time(const std::string& text) {
    std::string t = text;
    for (auto& c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    // Mot-clé « temps de jeu » : la recherche commence après lui (et autorise « 34:12 » = h:mm).
    std::size_t from = 0;
    bool keyword = false;
    for (std::string_view k : {"temps de jeu", "temps joué", "durée de jeu", "durée", "play time", "playtime", "time played", "playing time",
                               "total time", "spielzeit", "tiempo de juego", "tempo di gioco", "tempo de jogo", "プレイ時間"}) {
        const auto at = t.find(k);
        if (at != std::string::npos && (!keyword || at < from)) { from = at + k.size(); keyword = true; }
    }
    auto number = [&](std::size_t& i, std::int64_t& v) {
        const std::size_t b = i;
        v = 0;
        while (i < t.size() && is_digit(t[i]) && i - b < 7) v = v * 10 + (t[i++] - '0');
        return i > b;
    };
    auto skip_spaces = [&](std::size_t i) { while (i < t.size() && (t[i] == ' ' || t[i] == '.')) ++i; return i; };
    auto starts_number = [&](std::size_t i) { return is_digit(t[i]) && (i == 0 || (!is_digit(t[i - 1]) && t[i - 1] != ':' && t[i - 1] != '.' && t[i - 1] != '/')); };
    auto scan = [&](std::size_t start) -> std::int64_t {
        // 1) h:mm:ss
        for (std::size_t i = start; i < t.size(); ++i) {
            if (!starts_number(i)) continue;
            std::size_t j = i;
            std::int64_t h, m, s;
            if (number(j, h) && j + 6 <= t.size() && t[j] == ':' && is_digit(t[j + 1]) && is_digit(t[j + 2]) && t[j + 3] == ':' && is_digit(t[j + 4]) && is_digit(t[j + 5])
                && (j + 6 == t.size() || !is_digit(t[j + 6]))) {
                m = (t[j + 1] - '0') * 10 + (t[j + 2] - '0');
                s = (t[j + 4] - '0') * 10 + (t[j + 5] - '0');
                if (m < 60 && s < 60) return h * 3600 + m * 60 + s;
            }
        }
        // 2) « 34h », « 34 h 12 min », « 34 hrs 5 mins », puis « 12 min » seul.
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t i = start; i < t.size(); ++i) {
                if (!starts_number(i)) continue;
                std::size_t j = i;
                std::int64_t a;
                if (!number(j, a)) continue;
                j = skip_spaces(j);
                if (pass == 0) {
                    const std::size_t hu = unit_at(t, j, kHours);
                    if (!hu) continue;
                    std::int64_t total = a * 3600, mn;
                    std::size_t k = skip_spaces(j + hu);
                    if (k < t.size() && is_digit(t[k]) && number(k, mn) && mn < 60) {
                        const std::size_t e = skip_spaces(k);
                        if (unit_at(t, e, kMinutes) || e >= t.size() || !is_alpha(t[e])) total += mn * 60;  // « 34h12 » : minutes implicites
                    }
                    return total;
                }
                if (unit_at(t, j, {"minutes", "minute", "mins", "min", "分"})) return a * 60;
            }
        }
        return -1;
    };
    if (const auto v = scan(from); v >= 0) return v;
    if (keyword) if (const auto v = scan(0); v >= 0) return v;  // « 34h de temps de jeu »
    // 3) « Temps de jeu 34:12 » : h:mm seulement après le mot-clé (sinon une heure du jour serait prise pour un temps de jeu).
    if (keyword)
        for (std::size_t i = from; i < t.size(); ++i) {
            if (!starts_number(i)) continue;
            std::size_t j = i;
            std::int64_t h;
            if (number(j, h) && j + 3 <= t.size() && t[j] == ':' && is_digit(t[j + 1]) && is_digit(t[j + 2]) && (j + 3 == t.size() || !is_digit(t[j + 3]))) {
                const std::int64_t m = (t[j + 1] - '0') * 10 + (t[j + 2] - '0');
                if (m < 60) return h * 3600 + m * 60;
            }
        }
    return -1;
}

bool parse_param(const std::string& d, ParamInfo& out) {
    out = ParamInfo{};
    auto u16 = [&](std::size_t o) -> std::uint32_t { return o + 2 <= d.size() ? static_cast<std::uint8_t>(d[o]) | static_cast<std::uint8_t>(d[o + 1]) << 8 : 0; };
    auto u32 = [&](std::size_t o) -> std::uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
    if (d.size() < 20 || std::memcmp(d.data(), "\0PSF", 4) != 0) return false;
    const std::uint32_t keys = u32(8), data = u32(12), count = u32(16);
    for (std::uint32_t i = 0; i < count && i < 256; ++i) {
        const std::size_t e = 20 + i * 16;
        if (e + 16 > d.size()) break;
        const std::size_t k = keys + u16(e), v = data + u32(e + 12);
        std::size_t len = u32(e + 4);
        if (k >= d.size() || v >= d.size()) continue;
        if (v + len > d.size()) len = d.size() - v;
        const std::size_t kend = d.find('\0', k);
        if (kend == std::string::npos) continue;
        const std::string key = d.substr(k, kend - k);
        const std::string raw = d.substr(v, len);
        const std::string str = raw.c_str();  // chaîne terminée par un zéro
        if (key == "TITLE_ID") out.title_id = str;
        else if (key == "SAVEDATA_DIRECTORY") out.dir_name = str;
        else if (key == "MAINTITLE" || (key == "TITLE" && out.main_title.empty())) out.main_title = str;
        else if (key == "SUBTITLE") out.subtitle = str;
        else if (key == "DETAIL") out.detail = str;
        else if (key == "ACCOUNT_ID" && len >= 8) out.account_id = static_cast<std::uint64_t>(u32(v)) | static_cast<std::uint64_t>(u32(v + 4)) << 32;
        // PARAMS (1024 o) : 0x04 utilisateur, 0x08 HMAC, 0x2C jeu… 0x68 horodatage de l'enregistrement (Garlic,
        // build_sfo : « 0x68 timestamp », écrit avec l'heure courante comme le fait la console).
        else if (key == "PARAMS" && len >= 0x6C) out.save_time = u32(v + 0x68);
        // SAVEDATA_LIST_PARAM : entier libre du jeu (sceSaveDataSetParam), parfois un indice de progression.
        else if (key == "SAVEDATA_LIST_PARAM" && len >= 4) out.user_param = u32(v);
    }
    return true;
}

std::string image_fingerprint(std::vector<InnerFile> files) {
    std::sort(files.begin(), files.end(), [](const InnerFile& a, const InnerFile& b) { return a.path < b.path; });
    Sha256 h;
    for (const auto& f : files) {
        const std::string line = f.path + "\t" + std::to_string(f.size) + "\t" + f.sha256 + "\n";
        h.add(line.data(), line.size());
    }
    return h.hex();
}

Json aggregate_version(const Json& images) {
    std::vector<const Json*> imgs;
    for (const auto& i : images.items()) imgs.push_back(&i);
    std::sort(imgs.begin(), imgs.end(), [](const Json* a, const Json* b) { return (*a)["name"].as_string() < (*b)["name"].as_string(); });
    Json v = Json::object();
    std::int64_t newest = 0, saved = 0, play = -1, level = -1;
    double percent = -1;
    std::uint64_t bytes = 0, files = 0;
    Sha256 h;
    for (const Json* i : imgs) {
        newest = std::max(newest, (*i)["newest_inner_mtime"].as_int());
        saved = std::max(saved, (*i)["save_time"].as_int());
        bytes += static_cast<std::uint64_t>((*i)["bytes"].as_int());
        play = std::max(play, (*i)["play_time_seconds"].as_int(-1));
        if ((*i)["percent"].is_number()) percent = std::max(percent, (*i)["percent"].as_number());
        level = std::max(level, (*i)["level"].as_int(-1));
        files += static_cast<std::uint64_t>((*i)["file_count"].as_int());
        const std::string line = (*i)["name"].as_string() + ":" + (*i)["fingerprint"].as_string() + "\n";
        h.add(line.data(), line.size());
    }
    // Texte de progression : celui des images les plus récemment écrites d'abord, sans doublon (3 au plus).
    std::vector<const Json*> by_date = imgs;
    std::stable_sort(by_date.begin(), by_date.end(), [](const Json* a, const Json* b) { return (*a)["newest_inner_mtime"].as_int() > (*b)["newest_inner_mtime"].as_int(); });
    std::vector<std::string> texts;
    std::string main_title;
    for (const Json* i : by_date) {
        if (main_title.empty()) main_title = (*i)["main_title"].as_string();
        const std::string t = (*i)["progress_text"].is_string() ? (*i)["progress_text"].as_string() : text_of(*i);
        if (!t.empty() && texts.size() < 3 && std::find(texts.begin(), texts.end(), t) == texts.end()) texts.push_back(t);
    }
    std::string summary;
    for (const auto& t : texts) summary += (summary.empty() ? "" : " · ") + t;
    v.set("image_count", static_cast<std::uint64_t>(imgs.size()));
    v.set("file_count", files);
    v.set("bytes", bytes);
    v.set("newest_inner_mtime", newest);
    v.set("save_time", saved);
    v.set("fingerprint", imgs.empty() ? std::string() : h.hex());
    v.set("main_title", main_title);
    v.set("progress_text", summary);
    if (play >= 0) v.set("play_time_seconds", play);
    else v.set("play_time_seconds", nullptr);
    if (percent >= 0) v.set("percent", percent); else v.set("percent", nullptr);
    if (level >= 0) v.set("level", level); else v.set("level", nullptr);
    bool settings_only = !imgs.empty();  // aucune image n'a de fichier de partie : seulement des réglages
    for (const Json* i : imgs) settings_only = settings_only && (*i)["settings_only"].as_bool();
    if (settings_only) v.set("settings_only", true);
    return v;
}

Json compare_versions(const Json& versions) {
    const auto& vs = versions.items();
    auto valid = [&](std::size_t i) { return !vs[i]["fingerprint"].as_string().empty(); };
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < vs.size(); ++i)
        if (valid(i)) groups[vs[i]["fingerprint"].as_string()].push_back(i);
    Json same = Json::array();
    for (const auto& [fp, idx] : groups) {
        if (idx.size() < 2) continue;
        Json g = Json::array();
        for (auto i : idx) g.push(static_cast<std::uint64_t>(i));
        same.push(g);
    }
    // Plus récente : fichier écrit par le jeu le plus récent, puis horodatage PARAMS (indicatif seulement).
    auto date = [&](std::size_t i) { return std::make_pair(vs[i]["newest_inner_mtime"].as_int(), vs[i]["save_time"].as_int()); };
    std::int64_t newest = -1;
    for (std::size_t i = 0; i < vs.size(); ++i) {
        if (!valid(i) || (date(i).first == 0 && date(i).second == 0)) continue;
        if (newest < 0 || date(i) > date(static_cast<std::size_t>(newest))) newest = static_cast<std::int64_t>(i);
    }
    // Plus avancée : le plus grand temps de jeu connu ; sinon le plus grand pourcentage, sinon le plus haut niveau
    // (écrits par le jeu) ; à égalité, la plus récente.
    auto pct = [&](std::size_t i) { return vs[i]["percent"].is_number() ? vs[i]["percent"].as_number() : -1.0; };
    std::string by = "inconnu";
    std::int64_t best = -1;
    std::size_t known = 0, total = 0;
    for (std::size_t i = 0; i < vs.size(); ++i) if (valid(i)) ++total;
    for (const char* crit : {"play_time", "percent", "level"}) {
        const std::string c = crit;
        auto val = [&](std::size_t i) { return c == "play_time" ? static_cast<double>(vs[i]["play_time_seconds"].as_int(-1)) : c == "percent" ? pct(i) : static_cast<double>(vs[i]["level"].as_int(-1)); };
        best = -1;
        known = 0;
        for (std::size_t i = 0; i < vs.size(); ++i) {
            if (!valid(i) || val(i) < 0) continue;
            ++known;
            const auto b = static_cast<std::size_t>(best);
            if (best < 0 || val(i) > val(b) || (val(i) == val(b) && date(i) > date(b))) best = static_cast<std::int64_t>(i);
        }
        if (best >= 0) { by = c; break; }
    }
    Json most = Json::object();
    most.set("index", best);
    most.set("by", by);
    std::string note;
    if (best < 0) note = "temps de jeu inconnu : comparez le texte de progression ; la date seule ne prouve rien";
    else {
        const auto b = static_cast<std::size_t>(best);
        for (std::size_t i = 0; i < vs.size(); ++i)
            if (by == "play_time" && i != b && valid(i) && vs[i]["play_time_seconds"].as_int(-1) == vs[b]["play_time_seconds"].as_int() && vs[i]["fingerprint"].as_string() != vs[b]["fingerprint"].as_string()) {
                note = "même temps de jeu que d'autres versions au contenu différent";
                break;
            }
        if (note.empty() && known < total) note = "avancée inconnue pour " + std::to_string(total - known) + " version(s)";
    }
    if (!note.empty()) most.set("note", note);
    Json out = Json::object();
    out.set("identical_groups", same);
    out.set("distinct", static_cast<std::uint64_t>(groups.size()));
    out.set("newest", newest);
    out.set("most_advanced", most);
    return out;
}

// Diagnostic (catalogue de l'avancée) : fichiers intérieurs à copier hors de la sauvegarde montée, pour les étudier.
// Lecture seule ; posé par une tâche de comparaison qui le demande (« extract »), vide sinon.
static std::set<std::string> g_extract_names;
static std::string g_extract_dir;

bool reader_outdated(const Json& j) {
    const Json& imgs = j.is_array() ? j : j["images"];
    for (const auto& im : imgs.items()) if (im["reader_v"].as_int(0) < kReaderVersion) return true;
    return false;
}

Json inspect_images(const std::vector<std::string>& image_paths, std::string& error, const std::atomic<bool>* stop, const backup::ProgressFn& progress) {
    gc_set_log(garlic_log);
    Json out = Json::array();
    std::vector<std::string> paths;
    for (const auto& p : image_paths) if (main_image(fs::basename(p))) paths.push_back(p);
    std::sort(paths.begin(), paths.end());
    backup::Progress pr;
    pr.files_total = static_cast<unsigned>(paths.size());
    for (const auto& path : paths) {
        const std::string name(fs::basename(path));
        if (stop && *stop) { error = "interrompue"; return Json::array(); }
        pr.current = "lecture · " + name;
        if (progress) progress(pr);
        gc_mount_info mi{};
        char err[512] = {0};
        if (gc_mount_save(fs::host(path).c_str(), &mi, err, sizeof err) != 0) {
            gc_unmount();
            error = name + " : montage impossible (" + err + ")";
            return Json::array();
        }
        ParamInfo p;
        const bool sfo = mi.sfo && mi.sfo_len && parse_param(std::string(reinterpret_cast<const char*>(mi.sfo), mi.sfo_len), p);
        std::vector<InnerFile> files;
        const bool read_ok = walk_mount(mi.mount_point, "", files, 0);
        // Catalogue de l'avancée : pour un jeu connu, lire le fichier qui contient la partie (temps, niveau, lieu).
        Json from_catalog = Json::object();
        const std::string cat_tid = p.title_id.empty() ? std::string(mi.title_id) : p.title_id;
        Json survey_names = Json::array();
        if (read_ok && !cat_tid.empty() && !progress_catalog::known(cat_tid))  // jeu absent du catalogue : lecteur générique
            for (const auto& f : files) {
                if (f.size < 8 || f.size > (16u << 20)) continue;
                std::string data;
                if (!fs::read(fs::host(fs::join(mi.mount_point, f.path)), data, 16u << 20)) continue;
                const Json r = progress_catalog::read_generic(data);
                const Json sv = progress_catalog::survey(data);
                if (sv.size() && !survey_names.size()) survey_names = sv;
                if (r.size() > 2 && (!from_catalog.size() || r["play_time_seconds"].as_int(-1) > from_catalog["play_time_seconds"].as_int(-1))) from_catalog = r;
            }
        if (read_ok && !cat_tid.empty())
            for (const auto& f : files) {
                const std::string base(fs::basename(f.path));
                if (f.size > (64u << 20) || !progress_catalog::wants(cat_tid, base)) continue;
                std::string data;
                if (!fs::read(fs::host(fs::join(mi.mount_point, f.path)), data, 64u << 20)) continue;
                const Json r = progress_catalog::read(cat_tid, base, data);
                if (r.size() && (!from_catalog.size() || r["play_time_seconds"].as_int(-1) > from_catalog["play_time_seconds"].as_int(-1))) from_catalog = r;
            }
        if (read_ok && !g_extract_names.empty() && !g_extract_dir.empty())
            for (const auto& f : files) {
                const std::string base(fs::basename(f.path));
                if (!g_extract_names.count(base) || f.size > (64u << 20)) continue;
                std::string data;
                if (!fs::read(fs::host(fs::join(mi.mount_point, f.path)), data, 64u << 20)) continue;
                const std::string dst = fs::join(fs::join(g_extract_dir, name), base);
                fs::mkdirs(fs::host(fs::join(g_extract_dir, name)));
                fs::write_atomic(fs::host(dst), data);
                log("inspect extract %s/%s bytes=%zu", name.c_str(), base.c_str(), data.size());
            }
        gc_unmount();
        if (!read_ok) { error = name + " : lecture des fichiers impossible"; return Json::array(); }
        Json img = Json::object();
        img.set("name", name);
        img.set("reader_v", static_cast<std::int64_t>(kReaderVersion));
        if (survey_names.size()) img.set("survey", survey_names);
        img.set("title_id", p.title_id.empty() ? std::string(mi.title_id) : p.title_id);
        img.set("main_title", p.main_title.empty() ? std::string(mi.main_title) : p.main_title);
        img.set("subtitle", p.subtitle);
        img.set("detail", p.detail);
        // Même lecture que la base de la console : titre, sous-titre et détail ; pourcentage, niveau, temps de jeu.
        const std::string mt = img["main_title"].as_string();
        const std::string text = pbc::progress::text_of(mt, p.subtitle, p.detail);
        img.set("progress_text", text);
        const pbc::progress::Parsed parsed = pbc::progress::parse(mt + " " + p.subtitle + " " + p.detail);
        if (parsed.play_seconds >= 0) img.set("play_time_seconds", parsed.play_seconds);
        else img.set("play_time_seconds", nullptr);
        if (parsed.percent >= 0) img.set("percent", parsed.percent); else img.set("percent", nullptr);
        if (parsed.level >= 0) img.set("level", parsed.level); else img.set("level", nullptr);
        if (read_ok && !cat_tid.empty()) {  // seulement des réglages (aucun fichier de partie connu du catalogue)
            std::vector<std::string> names;
            for (const auto& f : files) names.push_back(std::string(fs::basename(f.path)));
            if (progress_catalog::has_progress_file(cat_tid, names) == 0) img.set("settings_only", true);
        }
        if (from_catalog.size()) {  // le contenu de la partie l'emporte sur le texte quand il est plus précis
            img.set("catalog", from_catalog);
            if (from_catalog["play_time_seconds"].as_int(-1) >= 0) img.set("play_time_seconds", from_catalog["play_time_seconds"]);
            if (from_catalog["level"].as_int(-1) >= 0) img.set("level", from_catalog["level"]);
            std::string extra;
            for (const char* k : {"name", "place", "life_path"}) if (from_catalog[k].is_string()) extra += (extra.empty() ? "" : " · ") + from_catalog[k].as_string();
            if (!extra.empty()) img.set("progress_text", text.empty() ? extra : text + " — " + extra);
        }
        const std::int64_t play = parsed.play_seconds;
        if (p.user_param >= 0) img.set("user_param", p.user_param);
        img.set("save_time", p.save_time);
        if (p.save_time) img.set("save_time_iso", iso_time(p.save_time));
        img.set("account_id", hex16(p.account_id ? p.account_id : mi.account_id));
        img.set("param", sfo ? (mi.from_blob ? "ps5param" : "param.sfo") : "illisible");
        std::int64_t newest = 0;
        std::uint64_t bytes = 0;
        Json fl = Json::array();
        for (const auto& f : files) {
            newest = std::max(newest, f.mtime);
            bytes += f.size;
            Json j = Json::object();
            j.set("path", f.path);
            j.set("size", f.size);
            j.set("mtime", f.mtime);
            j.set("sha256", f.sha256);
            fl.push(j);
        }
        img.set("files", fl);
        img.set("file_count", static_cast<std::uint64_t>(files.size()));
        img.set("newest_inner_mtime", newest);
        img.set("bytes", bytes);
        img.set("fingerprint", image_fingerprint(files));
        out.push(img);
        ++pr.files_done;
        if (progress) progress(pr);
        log("inspect image=%s files=%zu newest=%" PRId64 " save_time=%" PRId64 " play=%" PRId64 " text=%s", name.c_str(), files.size(), newest, p.save_time, play, text.c_str());
    }
    if (paths.empty()) error = "aucune image de sauvegarde PS5";
    return out;
}

std::vector<std::string> fetch_images(store::Store& st, const std::string& dir, const std::string& staging,
                                      const std::string& title_id, const std::string& user_id,
                                      const backup::ProgressFn& progress, const std::atomic<bool>& stop, std::string& error,
                                      bool allow_ps4) {
    std::vector<std::pair<std::string, std::uint64_t>> found;
    std::map<std::string, std::uint64_t> keys;  // clés PS4 « <dossier>.bin » (chemin complet → taille)
    std::function<void(const std::string&, int)> walk = [&](const std::string& d, int depth) {
        std::vector<fs::Entry> es;
        if (depth > 6 || !st.list(d, es)) return;
        for (const auto& e : es) {
            if (e.dir) walk(fs::join(d, e.name), depth + 1);
            else if (main_image(e.name)) found.push_back({fs::join(d, e.name), e.size});
            else if (e.name.ends_with(".bin")) keys[fs::join(d, e.name)] = e.size;
        }
    };
    walk(dir, 0);
    // Dans un .pbck : data/save/ps5/<compte>/<jeu>/images/ (ps4/ seulement à défaut, si permis), le compte demandé d'abord.
    std::vector<std::pair<std::string, std::uint64_t>> files;
    auto keep = [&](const std::string& p, const char* family) {
        const auto at = p.find("/save/");
        if (at == std::string::npos) return std::string_view(family) == "ps5";
        if (p.compare(at, 10, std::string("/save/") + family + "/") != 0) return false;
        return title_id.empty() || p.find("/" + title_id + "/", at) != std::string::npos;
    };
    for (const auto& f : found) if (keep(f.first, "ps5")) files.push_back(f);
    if (files.empty() && allow_ps4)
        for (const auto& f : found) if (keep(f.first, "ps4")) files.push_back(f);
    std::stable_sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
        const bool ua = !user_id.empty() && a.first.find("/ps5/" + user_id + "/") != std::string::npos;
        const bool ub = !user_id.empty() && b.first.find("/ps5/" + user_id + "/") != std::string::npos;
        return ua && !ub;
    });
    std::set<std::string> names;
    std::vector<std::pair<std::string, std::uint64_t>> unique;
    for (const auto& f : files) {
        if (!names.insert(std::string(fs::basename(f.first))).second) continue;
        unique.push_back(f);
        // Image PS4 : sa clé scellée voisine (sdimg_<dossier> → <dossier>.bin), indispensable au montage.
        const std::string key = fs::join(std::string(fs::dirname(f.first)), std::string(fs::basename(f.first)).substr(6) + ".bin");
        if (auto k = keys.find(key); k != keys.end()) unique.push_back(*k);
    }
    std::vector<std::string> out;
    if (files.empty()) { error = "aucune image de sauvegarde PS5 dans ce dossier"; return out; }
    fs::mkdirs(fs::host(staging));
    backup::Progress pg;
    for (const auto& f : unique) pg.bytes_total += f.second;
    pg.files_total = static_cast<unsigned>(unique.size());
    for (const auto& [path, size] : unique) {
        if (stop) { error = "interrompue"; return {}; }
        const std::string name(fs::basename(path));
        pg.current = "copie · " + name;
        if (progress) progress(pg);
        std::string e2;
        auto in = st.open(path, e2);
        const std::string to = fs::join(staging, name);
        FILE* o = std::fopen(fs::host(to).c_str(), "wb");
        bool ok = in && o;
        char buf[65536];
        for (long n; ok && (n = in->read(buf, sizeof buf)) > 0;) {
            ok = std::fwrite(buf, 1, static_cast<std::size_t>(n), o) == static_cast<std::size_t>(n);
            pg.bytes_done += static_cast<std::uint64_t>(n);
        }
        if (o && std::fclose(o) != 0) ok = false;
        if (!ok) { error = "copie impossible : " + name + (e2.empty() ? "" : " (" + e2 + ")"); return {}; }
        if (main_image(name)) out.push_back(to);
        ++pg.files_done;
        if (progress) progress(pg);
    }
    return out;
}

InspectResult inspect_save(const std::string& task_id, const Json& request, const OpenRemote& open_remote,
                           const backup::ProgressFn& progress, const std::atomic<bool>& stop) {
    InspectResult res;
    auto fail = [&](const std::string& code, const std::string& msg) { res.code = code; res.error = msg; log("inspect-save failed code=%s %s", code.c_str(), msg.c_str()); return res; };
    const std::string title_id = request["title_id"].as_string(), user_id = request["user_id"].as_string(), state_dir = request["state_dir"].as_string();
    if (title_id.empty() || state_dir.empty() || !request["sources"].is_array()) return fail("bad_request", "demande incomplète");
    // Jeu ouvert : on ne touche pas à ses sauvegardes (même en copie, l'état serait celui d'un enregistrement en cours).
    char running[16] = {0};
    if (gc_running_title(running) && title_id == running) return fail("game_running", std::string("fermez d'abord le jeu ") + running);

    Json cache;
    { std::lock_guard<std::mutex> l(cache_mu); cache = load_cache(state_dir); }
    const std::string work = fs::join(fs::join(state_dir, "inspect"), task_id);
    g_extract_names.clear();
    g_extract_dir.clear();
    for (const auto& x : request["extract"].items()) if (!x.as_string().empty() && x.as_string().find('/') == std::string::npos) g_extract_names.insert(x.as_string());
    if (!g_extract_names.empty()) g_extract_dir = fs::join(fs::join(state_dir, "diag"), title_id);
    const bool extracting = !g_extract_names.empty();
    fs::remove_tree(fs::host(work));
    const auto& sources = request["sources"].items();
    Json versions = Json::array(), updates = Json::object(), updates_raw = Json::object();
    std::string first_error;
    unsigned analysed = 0;
    for (std::size_t n = 0; n < sources.size(); ++n) {
        if (stop) { fs::remove_tree(fs::host(work)); return fail("stopped", "interrompue"); }
        const Json& src = sources[n];
        const std::string kind = src["kind"].as_string();
        const std::string key = cache_key(src, title_id, user_id);
        const std::string label = kind == "console" ? std::string("console") : src["bundle"].as_string();
        Json v;
        // Un backup ne change jamais ; la version console analysée reste bonne tant que ses images (noms,
        // tailles, dates) n'ont pas bougé.
        const std::string signature = kind == "console" && !user_id.empty() ? console_signature(user_id, title_id) : std::string();
        const Json& hit = cache["versions"][key];
        if (!request["force"].as_bool() && hit.is_object() && !reader_outdated(hit) && (kind != "console" || (!signature.empty() && hit["signature"].as_string() == signature))) {
            v = hit;
            v.set("cached", true);
            versions.push(v);
            ++analysed;
            continue;
        }
        auto step = [&](const backup::Progress& p) {
            if (!progress) return;
            backup::Progress q = p;
            q.current = "version " + std::to_string(n + 1) + "/" + std::to_string(sources.size()) + " · " + p.current;
            progress(q);
        };
        const std::string staging = fs::join(work, std::to_string(n));
        std::string err;
        std::vector<std::string> paths;
        // Backup fait par l'app : son manifeste donne l'empreinte de chaque fichier. Si ce contenu a déjà été lu
        // (même empreinte brute), on reprend son analyse sans rien copier ni télécharger.
        std::string early_raw;
        if (!extracting && (kind == "local" || kind == "remote")) {
            std::string e2;
            auto st = kind == "local" ? store::local() : (open_remote ? open_remote(src["remote"].as_string(), e2) : nullptr);
            if (st) early_raw = manifest_signature(*st, src["path"].as_string(), title_id, user_id);
            const Json* known = nullptr;
            if (!early_raw.empty()) {
                if (updates_raw.has(early_raw)) known = &updates_raw[early_raw];
                else if (cache["raw"][early_raw].is_array()) known = &cache["raw"][early_raw];
                if (known && reader_outdated(*known)) known = nullptr;  // lue avec un lecteur plus ancien : on relit
            }
            if (known) {
                v = aggregate_version(*known);
                v.set("images", *known);
                v.set("inspected", now_unix());
                v.set("title_id", title_id);
                v.set("raw_signature", early_raw);
                v.set("from_manifest", true);
                ++analysed;
                Json s = Json::object();
                s.set("kind", kind);
                for (const char* k : {"place", "bundle", "remote"}) if (!src[k].as_string().empty()) s.set(k, src[k]);
                if (!src["label"].as_string().empty()) s.set("label", src["label"]);
                v.set("source", s);
                updates.set(key, v);
                versions.push(v);
                continue;
            }
        }
        if (kind == "console") {
            if (user_id.empty()) err = "compte manquant";
            else if (images::running_title() == title_id) err = "jeu en cours : sa sauvegarde sera lue quand il sera fermé";  // on n'y touche jamais
            else {  // copie des images en place (jamais montées directement)
                auto st = store::local();
                paths = fetch_images(*st, console_dir(user_id, title_id), staging, "", "", step, stop, err, true);
            }
        } else if (kind == "local") {
            auto st = store::local();
            paths = fetch_images(*st, src["path"].as_string(), staging, title_id, user_id, step, stop, err, true);
        } else if (kind == "remote") {
            auto st = open_remote ? open_remote(src["remote"].as_string(), err) : nullptr;
            if (!st) err = "appareil injoignable" + (err.empty() ? std::string() : " : " + err);
            else paths = fetch_images(*st, src["path"].as_string(), staging, title_id, user_id, step, stop, err, true);
        } else err = "source inconnue";
        Json images = Json::array();
        // Copie identique octet pour octet à une copie déjà lue (dans cette tâche ou une précédente) : on reprend
        // son analyse sans ouvrir la sauvegarde — beaucoup plus rapide quand un jeu a des dizaines de copies.
        std::string raw;
        if (err.empty()) raw = raw_signature(paths, stop);
        const Json* same = nullptr;
        if (!raw.empty()) {
            if (updates_raw.has(raw)) same = &updates_raw[raw];
            else if (cache["raw"][raw].is_array()) same = &cache["raw"][raw];
            if (same && reader_outdated(*same)) same = nullptr;
        }
        if (err.empty() && same && !extracting) images = *same;
        else if (err.empty()) {
            images = inspect_images(paths, err, &stop, step);
            if (err.empty() && !raw.empty()) updates_raw.set(raw, images);
        }
        fs::remove_tree(fs::host(staging));
        if (stop) { fs::remove_tree(fs::host(work)); return fail("stopped", "interrompue"); }
        if (!err.empty()) {
            v = Json::object();
            v.set("error", label + " : " + err);
            if (first_error.empty()) first_error = label + " : " + err;
        } else {
            v = aggregate_version(images);
            v.set("images", images);
            v.set("inspected", now_unix());
            if (!raw.empty()) v.set("raw_signature", raw);
            v.set("title_id", title_id);
            if (kind == "console") { v.set("user_id", user_id); v.set("signature", signature); }
            ++analysed;
        }
        Json s = Json::object();
        s.set("kind", kind);
        for (const char* k : {"place", "bundle", "remote"}) if (!src[k].as_string().empty()) s.set(k, src[k]);
        if (!src["label"].as_string().empty()) s.set("label", src["label"]);
        v.set("source", s);
        if (!v.has("error")) updates.set(key, v);
        versions.push(v);
    }
    fs::remove_tree(fs::host(work));
    if (updates.size() || updates_raw.size()) {
        std::lock_guard<std::mutex> l(cache_mu);
        Json fresh = load_cache(state_dir);  // une autre tâche a pu écrire entre-temps : on fusionne
        Json vs = fresh["versions"];
        for (const auto& [k, val] : updates.members()) vs.set(k, val);
        fresh.set("versions", vs);
        Json rs = fresh["raw"].is_object() ? fresh["raw"] : Json::object();
        for (const auto& [k, val] : updates_raw.members()) rs.set(k, val);
        fresh.set("raw", rs);
        fs::write_atomic(cache_path(state_dir), fresh.dump(true) + "\n");
    }
    if (!analysed) return fail(first_error.find("non disponible") != std::string::npos ? "unsupported" : "inspect", first_error.empty() ? "aucune version" : first_error);
    Json cmp = compare_versions(versions);
    res.report.set("title_id", title_id);
    res.report.set("user_id", user_id);
    res.report.set("versions", versions);
    for (const auto& [k, val] : cmp.members()) res.report.set(k, val);
    res.ok = true;
    log("inspect-save done title=%s versions=%zu analysed=%u most_advanced=%" PRId64 " newest=%" PRId64, title_id.c_str(), versions.size(), analysed,
        cmp["most_advanced"]["index"].as_int(), cmp["newest"].as_int());
    return res;
}

Json account_stats(const std::string& state_dir) {
    Json cache;
    { std::lock_guard<std::mutex> l(cache_mu); cache = load_cache(state_dir); }
    struct A { std::int64_t images = 0, first = 0, last = 0; std::map<std::string, std::set<std::string>> titles; };
    std::map<std::string, A> acc;
    for (const auto& [key, v] : cache["versions"].members()) {
        const std::string tid = v["title_id"].as_string(), fp = v["fingerprint"].as_string();
        std::set<std::string> owners;
        for (const auto& im : v["images"].items()) {
            const std::string id = im["account_id"].as_string();
            if (id.empty() || id == "0000000000000000") continue;
            A& a = acc[id];
            ++a.images;
            const std::int64_t t = im["newest_inner_mtime"].as_int();
            if (t > 0) { a.first = a.first ? std::min(a.first, t) : t; a.last = std::max(a.last, t); }
            owners.insert(id);
        }
        for (const auto& id : owners) if (!tid.empty()) { auto& s2 = acc[id].titles[tid]; if (!fp.empty()) s2.insert(fp); }
    }
    Json out = Json::object();
    for (const auto& [id, a] : acc) {
        Json j = Json::object();
        j.set("images", a.images);
        j.set("first", a.first);
        j.set("last", a.last);
        Json ts = Json::object();
        for (const auto& [tid, fps] : a.titles) { Json f = Json::array(); for (const auto& x : fps) f.push(x); ts.set(tid, f); }
        j.set("titles", ts);
        out.set(id, j);
    }
    return out;
}

Json cached_inspections(const std::string& state_dir, const std::string& title_id, const std::string& user_id) {
    Json cache;
    { std::lock_guard<std::mutex> l(cache_mu); cache = load_cache(state_dir); }
    Json versions = Json::array();
    // Résultats lus avant une mise à jour du catalogue : lieux rendus lisibles et « réglages seulement » recalculés
    // à partir de ce qui est déjà noté (noms des fichiers, valeurs lues), sans relire les copies.
    auto refresh = [&](Json v) {
        bool all_settings = true;
        std::string text = v["progress_text"].as_string();
        Json imgs = Json::array();
        for (auto im : v["images"].items()) {
            std::vector<std::string> names;
            for (const auto& f : im["files"].items()) { const std::string p = f["path"].as_string(); names.push_back(p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1)); }
            const bool so = !names.empty() && progress_catalog::has_progress_file(title_id, names) == 0;
            if (so) im.set("settings_only", true);
            all_settings = all_settings && so;
            const std::string raw = im["catalog"]["place"].as_string(), pretty = raw.empty() ? raw : progress_catalog::pretty_place(raw);
            if (!raw.empty() && pretty != raw) {
                Json cat = im["catalog"]; cat.set("place", pretty); im.set("catalog", cat);
                for (std::size_t at; (at = text.find(raw)) != std::string::npos;) text.replace(at, raw.size(), pretty);
                std::string it = im["progress_text"].as_string();
                for (std::size_t at; (at = it.find(raw)) != std::string::npos;) it.replace(at, raw.size(), pretty);
                im.set("progress_text", it);
            }
            imgs.push(im);
        }
        if (imgs.size()) { v.set("images", imgs); v.set("progress_text", text); }
        if (imgs.size() && all_settings) v.set("settings_only", true);
        return v;
    };
    for (const auto& [key, v0] : cache["versions"].members()) {
        if (v0["title_id"].as_string() != title_id) continue;
        const Json v = refresh(v0);
        if (v["source"]["kind"].as_string() == "console") {
            if (v["user_id"].as_string() != user_id) continue;
            Json c = v;
            c.set("stale", console_signature(user_id, title_id) != v["signature"].as_string());
            versions.push(c);
        } else versions.push(v);
    }
    Json out = compare_versions(versions);
    out.set("title_id", title_id);
    out.set("user_id", user_id);
    out.set("versions", versions);
    return out;
}

}  // namespace pbc::saves
