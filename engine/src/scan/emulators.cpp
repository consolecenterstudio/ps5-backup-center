#include "scan/emulators.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::scan {

extern const char* const emulators_catalog_json;

namespace {

std::vector<std::string> strings(const Json& a) {
    std::vector<std::string> v;
    for (const auto& x : a.items()) v.push_back(x.as_string());
    return v;
}

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

std::string ext_of(std::string_view name) {
    auto dot = name.rfind('.');
    return dot == std::string_view::npos ? std::string() : lower(std::string(name.substr(dot + 1)));
}
std::string stem_of(std::string_view name) {
    auto dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

// Valeurs de réglages : ini « clé = valeur », json « a.b », xml <clé>valeur</clé>, tsv colonne n.
std::vector<std::string> config_values(const Json& spec) {
    std::vector<std::string> out;
    std::string text;
    if (!fs::read(fs::host(spec["file"].as_string()), text, 4u << 20)) return out;
    const std::string format = spec["format"].as_string(), key = spec["key"].as_string();
    if (format == "ini") {
        std::size_t pos = 0;
        while (pos < text.size()) {
            auto end = text.find('\n', pos);
            std::string_view line(text.data() + pos, (end == std::string::npos ? text.size() : end) - pos);
            auto eq = line.find('=');
            if (eq != std::string_view::npos && trim(line.substr(0, eq)) == key) {
                std::string v = trim(line.substr(eq + 1));
                if (!v.empty()) out.push_back(v);
            }
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    } else if (format == "json") {
        Json j;
        if (!Json::parse(text, j)) return out;
        const Json* cur = &j;
        std::string_view k(key);
        while (true) {
            auto dot = k.find('.');
            cur = &(*cur)[k.substr(0, dot)];
            if (dot == std::string_view::npos) break;
            k.remove_prefix(dot + 1);
        }
        if (cur->is_string() && !cur->as_string().empty()) out.push_back(cur->as_string());
        for (const auto& v : cur->items()) if (v.is_string()) out.push_back(v.as_string());
    } else if (format == "xml") {
        const std::string open = "<" + key + ">", close = "</" + key + ">";
        std::size_t pos = 0;
        while ((pos = text.find(open, pos)) != std::string::npos) {
            auto e = text.find(close, pos);
            if (e == std::string::npos) break;
            std::string v = trim(std::string_view(text).substr(pos + open.size(), e - pos - open.size()));
            if (!v.empty()) out.push_back(v);
            pos = e;
        }
    } else if (format == "tsv") {
        const auto col = static_cast<std::size_t>(spec["column"].as_int());
        std::size_t pos = 0;
        while (pos < text.size()) {
            auto end = text.find('\n', pos);
            std::string line = text.substr(pos, (end == std::string::npos ? text.size() : end) - pos);
            if (!line.empty() && line[0] != '#') {
                std::vector<std::string> cells;
                std::size_t s = 0;
                while (true) { auto t = line.find('\t', s); cells.push_back(line.substr(s, t == std::string::npos ? std::string::npos : t - s)); if (t == std::string::npos) break; s = t + 1; }
                // Une entrée de bibliothèque désigne un fichier de jeu : son dossier de jeux est deux niveaux au-dessus.
                if (col < cells.size() && !trim(cells[col]).empty()) out.push_back(std::string(fs::dirname(fs::dirname(trim(cells[col])))));
            }
            if (end == std::string::npos) break;
            pos = end + 1;
        }
    }
    return out;
}

std::string platform_of_subdir(std::string_view n) {
    static const std::map<std::string, std::string> names = {
        {"MegaDrive", "Mega Drive"}, {"SegaCD", "Mega-CD"}, {"MasterSystem", "Master System"}, {"GameGear", "Game Gear"},
        {"SG1000", "SG-1000"}, {"PCEngine", "PC Engine"}, {"PCEngineCD", "PC Engine CD"}, {"SuperGrafx", "SuperGrafx"},
        {"Atari2600", "Atari 2600"}, {"Atari5200", "Atari 5200"}, {"Atari7800", "Atari 7800"}, {"AtariLynx", "Atari Lynx"}};
    auto it = names.find(std::string(n));
    return it == names.end() ? std::string(n) : it->second;
}

std::vector<std::string> expand_usb(const std::string& pattern, const std::vector<std::string>& mounts) {
    if (pattern.rfind("{usb}", 0) != 0) return {pattern};
    std::vector<std::string> out;
    for (const auto& m : mounts) out.push_back(m + pattern.substr(5));
    return out;
}

std::uint64_t tree_bytes(const std::string& host, int depth = 0) {
    fs::Entry e;
    if (depth > 10 || !fs::stat(host, e)) return 0;
    if (!e.dir) return e.size;
    std::vector<fs::Entry> entries;
    fs::list(host, entries);
    std::uint64_t t = 0;
    for (const auto& c : entries) t += c.dir ? tree_bytes(fs::join(host, c.name), depth + 1) : c.size;
    return t;
}

}  // namespace

std::string normalize_title(std::string_view name) {
    std::string out;
    int depth = 0;
    for (unsigned char c : name) {
        if (c == '(' || c == '[' || c == '{') { ++depth; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth) --depth; continue; }
        if (depth) continue;
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

bool switch_title_id(std::string_view name, std::string& tid) {
    for (std::size_t i = 0; i + 17 < name.size(); ++i) {
        if (name[i] != '[' || name[i + 17] != ']') continue;
        bool hex = true;
        for (std::size_t k = 1; k <= 16; ++k) if (!std::isxdigit(static_cast<unsigned char>(name[i + k]))) hex = false;
        if (hex) { tid = std::string(name.substr(i + 1, 16)); for (auto& c : tid) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return true; }
    }
    return false;
}

std::string switch_base_id(const std::string& tid) {
    unsigned long long v = std::strtoull(tid.c_str(), nullptr, 16);
    const unsigned low = static_cast<unsigned>(v & 0xFFF);
    unsigned long long base = low == 0 || low == 0x800 ? (v & ~0xFFFULL) : (v & ~0xFFFULL) - 0x1000;
    char b[17];
    std::snprintf(b, sizeof b, "%016llX", base);
    return b;
}

bool Catalog::load(const std::string& override_host) {
    emulators_.clear();
    auto add = [&](const Json& list) {
        for (const auto& e : list.items()) {
            Emulator em;
            em.id = e["id"].as_string();
            if (em.id.empty()) continue;
            em.name = e["name"].as_string();
            em.platform = e["platform"].as_string();
            em.data = e["data"].as_string();
            em.title_ids = strings(e["title_ids"]);
            em.config_files = strings(e["config_files"]);
            em.rom_ext = strings(e["rom_ext"]);
            em.saves_patterns = strings(e["saves"]);
            em.states_patterns = strings(e["states"]);
            em.covers = strings(e["covers"]);
            em.switch_saves = e["switch_saves"].as_string();
            em.config_roots = e["config_roots"];
            em.rom_roots_default = strings(e["rom_roots"]);
            auto same = std::find_if(emulators_.begin(), emulators_.end(), [&](const Emulator& x) { return x.id == em.id; });
            if (same != emulators_.end()) *same = std::move(em); else emulators_.push_back(std::move(em));
        }
    };
    Json builtin;
    if (!Json::parse(emulators_catalog_json, builtin)) { log("emulator catalog: built-in JSON invalid"); return false; }
    add(builtin);
    std::string text;
    Json extra;
    if (!override_host.empty() && fs::read(override_host, text) && Json::parse(text, extra)) {
        add(extra);
        log("emulator catalog: override loaded file=%s", override_host.c_str());
    }
    return true;
}

void Catalog::resolve(const std::vector<std::string>& usb, const std::vector<std::string>& title_ids) {
    for (auto& em : emulators_) {
        em.roots.clear();
        em.saves.clear();
        em.states.clear();
        em.installed = (!em.data.empty() && fs::is_dir(fs::host(em.data))) ||
                       std::any_of(em.title_ids.begin(), em.title_ids.end(), [&](const std::string& t) {
                           return std::find(title_ids.begin(), title_ids.end(), t) != title_ids.end();
                       });
        if (!em.installed) continue;
        std::set<std::string> seen;
        auto add_root = [&](const std::string& path, const std::string& source) {
            std::string p = path;
            while (p.size() > 1 && p.back() == '/') p.pop_back();
            if (p.size() > 2 && p.substr(p.size() - 2) == "/*") {  // un sous-dossier par système
                std::string parent = p.substr(0, p.size() - 2);
                std::vector<fs::Entry> entries;
                if (!fs::list(fs::host(parent), entries)) return;
                for (const auto& e : entries)
                    if (e.dir && !e.name.empty() && e.name[0] != '.' && seen.insert(fs::join(parent, e.name)).second)
                        em.roots.push_back({fs::join(parent, e.name), platform_of_subdir(e.name), source});
                return;
            }
            if (!fs::is_dir(fs::host(p)) || !seen.insert(p).second) return;
            em.roots.push_back({p, em.platform, source});
        };
        // 0. Réglages manuels de l'utilisateur : ses dossiers de ROM remplacent la détection.
        for (const auto& p : em.manual_roms) add_root(p, "manuel");
        // 1. Ce que dit le fichier de réglages de l'émulateur (ou celui choisi par l'utilisateur).
        for (Json spec : em.config_roots.items()) {
            if (!em.manual_roms.empty()) break;
            if (!em.manual_config.empty()) spec.set("file", em.manual_config);
            for (std::string v : config_values(spec)) {
                v += spec["suffix"].as_string();
                if (spec["relative_to_usb"].as_bool()) { for (const auto& m : usb) add_root(fs::join(m, v), "config"); }
                else if (!v.empty() && v[0] == '/') add_root(v, "config");
            }
        }
        // 2. Les dossiers par défaut qui existent.
        if (em.manual_roms.empty())
        for (const auto& pattern : em.rom_roots_default)
            for (const auto& p : expand_usb(pattern, usb)) add_root(p, "défaut");
        // 3. Saves/états sur USB ({usb}) : développés pour la correspondance.
        if (!em.manual_saves.empty()) em.saves.push_back(em.manual_saves);
        for (const auto& s : em.saves_patterns) for (const auto& p : expand_usb(s, usb)) em.saves.push_back(p);
        for (const auto& s : em.states_patterns) for (const auto& p : expand_usb(s, usb)) em.states.push_back(p);
    }
}

Json Catalog::to_json() const {
    Json a = Json::array();
    for (const auto& em : emulators_) {
        Json j = Json::object();
        j.set("id", em.id);
        j.set("name", em.name);
        j.set("installed", em.installed);
        Json tids = Json::array();
        for (const auto& t : em.title_ids) tids.push(t);
        j.set("title_ids", tids);  // pour afficher l'icône de l'émulateur
        Json roots = Json::array();
        for (const auto& r : em.roots) {
            Json rj = Json::object();
            rj.set("path", r.path);
            rj.set("platform", r.platform);
            rj.set("source", r.source);
            roots.push(rj);
        }
        j.set("roots", roots);
        j.set("platform", em.platform);
        Json cfg = Json::array();
        for (const auto& c : em.config_files) cfg.push(c);
        j.set("config_files", cfg);
        Json manual = Json::object(), mr = Json::array();
        for (const auto& r : em.manual_roms) mr.push(r);
        manual.set("roms", mr);
        manual.set("config", em.manual_config);
        manual.set("saves", em.manual_saves);
        j.set("manual", manual);
        j.set("is_manual", !em.manual_roms.empty() || !em.manual_config.empty() || !em.manual_saves.empty());
        a.push(j);
    }
    return a;
}

void Catalog::load_manual(const std::string& host_file) {
    std::string text;
    Json j;
    if (!fs::read(host_file, text) || !Json::parse(text, j)) return;
    for (auto& em : emulators_) {
        const Json& m = j[em.id];
        if (!m.is_object()) continue;
        em.manual_roms = strings(m["roms"]);
        em.manual_config = m["config"].as_string();
        em.manual_saves = m["saves"].as_string();
    }
}

bool Catalog::set_manual(const std::string& id, const Json& m, const std::string& host_file) {
    bool found = false;
    for (auto& em : emulators_)
        if (em.id == id) {
            found = true;
            em.manual_roms = strings(m["roms"]);
            em.manual_config = m["config"].as_string();
            em.manual_saves = m["saves"].as_string();
        }
    if (!found) return false;
    Json all = Json::object();
    for (const auto& em : emulators_) {
        if (em.manual_roms.empty() && em.manual_config.empty() && em.manual_saves.empty()) continue;
        Json e = Json::object(), r = Json::array();
        for (const auto& x : em.manual_roms) r.push(x);
        e.set("roms", r);
        e.set("config", em.manual_config);
        e.set("saves", em.manual_saves);
        all.set(em.id, e);
    }
    return fs::write_atomic(host_file, all.dump() + "\n");
}

std::vector<Root> RomRecognizer::roots() const {
    std::vector<Root> out;
    for (const auto& em : catalog_.all())
        for (const auto& r : em.roots)
            if (std::none_of(out.begin(), out.end(), [&](const Root& x) { return x.path == r.path; })) out.push_back({r.path, 0});
    return out;
}

namespace {

struct Match {
    std::vector<std::pair<std::string, fs::Entry>> entries;  // (dossier, entrée)
};

// Liste à plat des sauvegardes/états/jaquettes d'un émulateur (un niveau de sous-dossiers).
void list_flat(const std::vector<std::string>& dirs, Match& out) {
    for (const auto& d : dirs) {
        std::vector<fs::Entry> entries;
        if (!fs::list(fs::host(d), entries)) continue;
        for (const auto& e : entries) {
            if (e.name.empty() || e.name[0] == '.') continue;
            if (e.dir) {
                std::vector<fs::Entry> sub;
                if (fs::list(fs::host(fs::join(d, e.name)), sub))
                    for (const auto& s : sub) if (!s.dir) out.entries.push_back({fs::join(d, e.name), s});
            }
            out.entries.push_back({d, e});
        }
    }
}

// Nom d'une sauvegarde sans l'empreinte CRC finale (« …-45676429 ») ni l'extension.
std::string save_key(const std::string& name) {
    std::string s = stem_of(name);
    if (s.size() > 9 && s[s.size() - 9] == '-' &&
        std::all_of(s.end() - 8, s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
        s.resize(s.size() - 9);
    // Numéro d'emplacement ou de carte mémoire : « jeu_1.mcd », « jeu_2 ».
    auto us = s.rfind('_');
    if (us != std::string::npos && us + 1 < s.size() && us + 3 >= s.size() &&
        std::all_of(s.begin() + static_cast<long>(us) + 1, s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        s.resize(us);
    return normalize_title(s);
}

}  // namespace

Verdict RomRecognizer::visit(const DirView& dir, std::vector<Item>& out) {
    // Quels émulateurs ont ce dossier pour dossier de jeux ?
    std::vector<std::pair<const Emulator*, const RomRoot*>> owners;
    for (const auto& em : catalog_.all())
        for (const auto& r : em.roots)
            if (r.path == dir.path) owners.push_back({&em, &r});
    if (owners.empty()) return Verdict::descend;

    for (const auto& [em, root] : owners) {
        std::set<std::string> exts(em->rom_ext.begin(), em->rom_ext.end());
        Match saves, states, covers;
        list_flat(em->saves, saves);
        list_flat(em->states, states);
        list_flat(em->covers, covers);
        const bool has_cue = std::any_of(dir.entries->begin(), dir.entries->end(), [](const fs::Entry& e) { return !e.dir && ext_of(e.name) == "cue"; });

        for (const auto& e : *dir.entries) {
            if (e.name.empty() || e.name[0] == '.' || e.name[0] == '_') continue;
            const std::string path = fs::join(dir.path, e.name);
            std::vector<fs::Entry> files;  // contenu (un niveau, plus les sous-dossiers « Disc N »)
            std::string main_ext;
            if (e.dir) {
                std::vector<fs::Entry> inner;
                if (!fs::list(fs::host(path), inner)) continue;
                for (const auto& f : inner) {
                    if (!f.dir) files.push_back(f);
                    else {
                        std::vector<fs::Entry> disc;
                        if (fs::list(fs::host(fs::join(path, f.name)), disc))
                            for (auto d : disc) if (!d.dir) { d.name = f.name + "/" + d.name; files.push_back(d); }
                    }
                }
                bool any = false;
                for (const auto& f : files) {
                    const std::string x = ext_of(f.name);
                    if (!exts.count(x)) continue;
                    any = true;
                    if (main_ext.empty() || x == "m3u" || x == "cue" || x == "gdi") main_ext = x;
                }
                if (!any) continue;
            } else {
                main_ext = ext_of(e.name);
                if (!exts.count(main_ext)) continue;
                if (main_ext == "bin" && has_cue) continue;  // piste d'un CUE posé à la racine
                files.push_back(e);
            }

            Item it;
            it.key = "rom:" + path;
            it.kind = Kind::rom;
            it.place_id = dir.place ? dir.place->id : "";
            it.platform = root->platform;
            it.apps.push_back(em->name);
            std::string display = e.dir ? e.name : stem_of(e.name);
            std::string tid;
            const bool is_switch = switch_title_id(e.name, tid) ||
                                   std::any_of(files.begin(), files.end(), [&](const fs::Entry& f) { return switch_title_id(f.name, tid); });
            // Nom affiché : sans identifiants entre crochets.
            std::string clean;
            int depth = 0;
            for (char c : display) { if (c == '[') ++depth; else if (c == ']') { if (depth) --depth; } else if (!depth) clean += c; }
            it.title = trim(clean).empty() ? display : trim(clean);
            it.title_id = is_switch ? switch_base_id(tid) : "";
            it.image_type = main_ext == "cue" ? "CUE/BIN" : main_ext == "m3u" ? "M3U (multi-disque)" : lower(main_ext);
            for (auto& c : it.image_type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

            if (is_switch && e.dir) {
                // Jeu de base, mises à jour et DLC : un composant chacun.
                for (const auto& f : files) {
                    std::string ftid;
                    if (!exts.count(ext_of(f.name)) || !switch_title_id(f.name, ftid)) continue;
                    const unsigned low = static_cast<unsigned>(std::strtoull(ftid.c_str(), nullptr, 16) & 0xFFF);
                    Component c;
                    c.part = low == 0 ? Part::game : low == 0x800 ? Part::update : Part::dlc;
                    c.label = c.part == Part::game ? "Jeu de base" : c.part == Part::update ? "Mise à jour" : "DLC";
                    auto vpos = f.name.find("[v");
                    if (c.part != Part::game && vpos != std::string::npos) c.label += " " + f.name.substr(vpos + 1, f.name.find(']', vpos) - vpos - 1);
                    c.sources.push_back({fs::join(path, f.name), fs::join(e.name, f.name), false});
                    c.bytes = f.size;
                    c.restore_to = path;
                    it.components.push_back(std::move(c));
                }
            } else {
                Component g;
                g.part = Part::game;
                g.label = "Jeu";
                g.sources.push_back({path, e.name, e.dir});
                for (const auto& f : files) g.bytes += f.size;
                g.restore_to = dir.path;
                it.components.push_back(std::move(g));
            }

            // Sauvegardes et états : par nom de jeu (ou identifiant de titre pour la Switch).
            const std::string want = normalize_title(e.dir ? e.name : stem_of(e.name));
            auto attach = [&](const Match& m, Part part, const char* label) {
                Component c;
                c.part = part;
                c.label = label;
                for (const auto& [d, f] : m.entries) {
                    if (want.empty() || save_key(f.name) != want) continue;
                    c.sources.push_back({fs::join(d, f.name), fs::join(std::string(fs::basename(d)), f.name), f.dir});
                    c.bytes += f.dir ? tree_bytes(fs::host(fs::join(d, f.name))) : f.size;
                    if (c.restore_to.empty()) c.restore_to = d;
                }
                if (!c.sources.empty()) it.components.push_back(std::move(c));
            };
            attach(saves, Part::save, "Sauvegarde");
            attach(states, Part::state, "États");
            if (is_switch && !em->switch_saves.empty() && !it.title_id.empty()) {
                // …/save/0000000000000000/<compte>/<titre> : un dossier par compte.
                std::string pattern = em->switch_saves;
                auto star = pattern.find("/*/");
                if (star != std::string::npos) {
                    const std::string parent = pattern.substr(0, star);
                    std::string tail = pattern.substr(star + 3);
                    auto t = tail.find("{tid}");
                    if (t != std::string::npos) tail.replace(t, 5, it.title_id);
                    std::vector<fs::Entry> accounts;
                    Component c;
                    c.part = Part::save;
                    c.label = "Sauvegarde (comptes Switch)";
                    if (fs::list(fs::host(parent), accounts))
                        for (const auto& a : accounts) {
                            const std::string p = fs::join(fs::join(parent, a.name), tail);
                            if (!a.dir || !fs::is_dir(fs::host(p))) continue;
                            c.sources.push_back({p, fs::join(a.name, it.title_id), true});
                            c.bytes += tree_bytes(fs::host(p));
                        }
                    c.restore_to = parent;
                    if (!c.sources.empty()) it.components.push_back(std::move(c));
                }
            }
            const std::string want_title = normalize_title(it.title);
            for (const auto& [d, f] : covers.entries) {
                const std::string x = ext_of(f.name);
                if (!f.dir && (x == "png" || x == "jpg" || x == "jpeg") && normalize_title(stem_of(f.name)) == want) { it.cover = fs::join(d, f.name); break; }
                if (!f.dir && x == "name" && f.size < 512) {  // « <id>.name » contient le nom du jeu de « <id>.tga »
                    std::string text;
                    if (fs::read(fs::host(fs::join(d, f.name)), text, 512) && normalize_title(text) == want_title &&
                        fs::exists(fs::host(fs::join(d, stem_of(f.name) + ".tga")))) { it.cover = fs::join(d, stem_of(f.name) + ".tga"); break; }
                }
            }
            out.push_back(std::move(it));
        }
    }
    return Verdict::claimed;
}

}  // namespace pbc::scan
