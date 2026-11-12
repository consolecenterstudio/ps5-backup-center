#include "saves/catalog.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <vector>

#include "http/web_assets.hpp"

namespace pbc::progress_catalog {

namespace {

const Json& catalog() {
    static Json c;
    static std::once_flag once;
    std::call_once(once, [] {
        for (unsigned i = 0; i < pbc::web::asset_count; ++i)
            if (std::strcmp(pbc::web::assets[i].path, "/catalog/progress.json") == 0) {
                Json::parse(std::string(reinterpret_cast<const char*>(pbc::web::assets[i].data), pbc::web::assets[i].size), c);
                break;
            }
        if (!c.is_object()) c = Json::object();
    });
    return c;
}

bool match(const std::string& pattern, const std::string& name) {  // « metadata.*.json » : un seul « * »
    const auto star = pattern.find('*');
    if (star == std::string::npos) return pattern == name;
    const std::string a = pattern.substr(0, star), b = pattern.substr(star + 1);
    return name.size() >= a.size() + b.size() && name.compare(0, a.size(), a) == 0 && name.compare(name.size() - b.size(), b.size(), b) == 0;
}

const Json* entry_for(const std::string& title_id, const std::string& file) {
    for (const auto& e : catalog()["games"].items()) {
        bool tid = false;
        for (const auto& t : e["titleIds"].items()) if (t.as_string() == title_id) tid = true;
        if (tid && match(e["file"].as_string(), file)) return &e;
    }
    return nullptr;
}

std::uint64_t num(const Json& v) { return v.is_number() ? static_cast<std::uint64_t>(v.as_number()) : std::strtoull(v.as_string().c_str(), nullptr, 0); }  // « 0x24C » ou 10

std::uint32_t u32(const std::string& d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return static_cast<unsigned char>(d[o]) | static_cast<unsigned char>(d[o + 1]) << 8 | static_cast<unsigned char>(d[o + 2]) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[o + 3])) << 24;
}

std::string utf16(const std::string& d, std::size_t o, std::size_t len) {
    std::string out;
    for (std::size_t i = 0; i + 1 < len && o + i + 1 < d.size(); i += 2) {
        const unsigned c = static_cast<unsigned char>(d[o + i]) | static_cast<unsigned char>(d[o + i + 1]) << 8;
        if (!c) break;
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | c >> 6); out += static_cast<char>(0x80 | (c & 63)); }
        else { out += static_cast<char>(0xE0 | c >> 12); out += static_cast<char>(0x80 | (c >> 6 & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    }
    return out;
}

}  // namespace

std::string pretty_place(const std::string& raw) {
    std::string dlc, s = raw;
    const auto d = s.find("/DLC/");
    if (d != std::string::npos) { const auto e = s.find('/', d + 5); dlc = s.substr(d + 5, e == std::string::npos ? std::string::npos : e - d - 5); }
    if (const auto sl = s.rfind('/'); sl != std::string::npos) s = s.substr(sl + 1);
    if (const auto dot = s.find('.'); dot != std::string::npos) s = s.substr(0, dot);
    for (const char* pre : {"Level_", "LVL_", "Lvl_", "PLVL_", "Map_", "L_"}) if (s.rfind(pre, 0) == 0) { s = s.substr(std::strlen(pre)); break; }
    // suffixes techniques : _Main, _V2, _P, _Persistent…
    for (bool again = true; again;) {
        again = false;
        const auto u = s.rfind('_');
        if (u == std::string::npos) break;
        const std::string tail = s.substr(u + 1);
        const bool version = tail.size() >= 2 && (tail[0] == 'V' || tail[0] == 'v') && tail.find_first_not_of("0123456789", 1) == std::string::npos;
        if (version || tail == "Main" || tail == "P" || tail == "Persistent" || tail == "Master" || tail.find_first_not_of("0123456789") == std::string::npos) { s.resize(u); again = true; }
    }
    std::string out;  // CamelCase et « _ » → mots
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '_') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        if (i && std::isupper(static_cast<unsigned char>(c)) && std::islower(static_cast<unsigned char>(s[i - 1])) && !out.empty() && out.back() != ' ') out += ' ';
        out += c;
    }
    if (out.empty()) return raw;
    return dlc.empty() || dlc == out ? out : dlc + " · " + out;
}

bool wants(const std::string& title_id, const std::string& file_name) { return entry_for(title_id, file_name) != nullptr; }

int has_progress_file(const std::string& title_id, const std::vector<std::string>& names) {
    for (const auto& e : catalog()["games"].items()) {
        if (!e["progress_files"].is_array()) continue;
        bool tid = false;
        for (const auto& t : e["titleIds"].items()) if (t.as_string() == title_id) tid = true;
        if (!tid) continue;
        for (const auto& n : names) for (const auto& pat : e["progress_files"].items()) if (match(pat.as_string(), n)) return 1;
        return 0;
    }
    return -1;
}

// json : chemins « A.B.c » dans un fichier JSON en clair (Cyberpunk : metadata.*.json).
Json read_json(const Json& e, const std::string& data) {
    Json doc, out = Json::object();
    if (!Json::parse(data, doc)) return out;
    for (const auto& f : e["fields"].items()) {
        const Json* cur = &doc;
        std::string path = f["path"].as_string();
        for (std::size_t s = 0; cur && !path.empty();) {
            const auto dot = path.find('.', s);
            const std::string k = path.substr(s, dot == std::string::npos ? std::string::npos : dot - s);
            cur = cur->has(k) ? &(*cur)[k] : nullptr;
            if (dot == std::string::npos) break;
            s = dot + 1;
        }
        if (!cur) continue;
        if (cur->is_number()) {
            const double v = cur->as_number();
            if (f["id"].as_string() == "play_time_seconds" || f["id"].as_string() == "level") out.set(f["id"].as_string(), static_cast<std::int64_t>(v));
            else out.set(f["id"].as_string(), v);
        } else if (cur->is_string() && !cur->as_string().empty()) out.set(f["id"].as_string(), cur->as_string());
    }
    return out;
}

// slots : emplacements à positions fixes (ELDEN RING : 10 personnages) ; on garde l'emplacement actif qui a le plus
// grand « pick » (temps de jeu), et la liste de tous les emplacements actifs.
Json read_slots(const Json& e, const std::string& d) {
    Json out = Json::object();
    const std::string magic = e["magic"].as_string();
    for (std::size_t i = 0; i + 1 < magic.size(); i += 2)
        if (i / 2 >= d.size() || static_cast<unsigned char>(d[i / 2]) != std::strtoul(magic.substr(i, 2).c_str(), nullptr, 16)) return out;
    const std::uint64_t active = num(e["slots"]["active"]), base = num(e["slots"]["base"]), stride = num(e["slots"]["stride"]);
    const std::uint64_t count = num(e["slots"]["count"]);
    const std::string pick = e["pick"].as_string();
    Json slots = Json::array();
    std::int64_t best = -1;
    for (std::uint64_t s = 0; s < count && s < 32; ++s) {
        if (active + s >= d.size() || !d[active + s]) continue;
        const std::uint64_t o = base + s * stride;
        if (o + stride > d.size()) break;
        Json slot = Json::object();
        for (const auto& f : e["fields"].items()) {
            const std::uint64_t fo = o + num(f["offset"]);
            if (f["type"].as_string() == "u32") slot.set(f["id"].as_string(), static_cast<std::int64_t>(u32(d, fo)));
            else if (f["type"].as_string() == "utf16le") slot.set(f["id"].as_string(), utf16(d, fo, static_cast<std::size_t>(f["len"].as_int(32))));
        }
        slots.push(slot);
        if (slot[pick].as_int(-1) > best) { best = slot[pick].as_int(-1); out = slot; }
    }
    if (slots.size() > 1) out.set("slots", slots);
    return out;
}

// gvas : sauvegarde Unreal Engine (« GVAS ») ; on cherche une propriété par son nom et on lit sa valeur selon son type.
// Deux dispositions d'en-tête de propriété : UE5.4+ (GVAS version 3 : 4 octets, taille u32, 1 octet d'indicateurs) et
// classique (taille i32, index i32, 1 octet « a un GUID » pour les types simples).
Json read_gvas(const Json& e, const std::string& d) {
    Json out = Json::object();
    if (d.size() < 8 || d.compare(0, 4, "GVAS") != 0) return out;
    const std::uint32_t ver = u32(d, 4);
    for (const auto& f : e["fields"].items()) {
        const std::string name = f["property"].as_string(), type = f["type"].as_string();
        if (type == "after") {  // « clé » puis sa valeur, deux FString à la suite (tables de chaînes d'un état de jeu)
            const std::string key = name + std::string("\0", 1);
            const auto at = d.find(key);
            if (at == std::string::npos) continue;
            const std::size_t v = at + key.size();
            const std::uint32_t sl = u32(d, v);
            if (sl > 1 && sl < 512 && v + 4 + sl <= d.size()) out.set(f["id"].as_string(), d.substr(v + 4, sl - 1));
            continue;
        }
        const std::string needle = std::string("\0", 1) + name + std::string("\0", 1);  // fin du u32 de longueur + nom + zéro
        for (std::size_t at = d.find(needle); at != std::string::npos; at = d.find(needle, at + 1)) {
            std::size_t p = at + needle.size();
            const std::uint32_t tl = u32(d, p);  // nom du type (FString)
            if (tl == 0 || tl > 64 || p + 4 + tl > d.size()) continue;
            const std::string tname = d.substr(p + 4, tl - 1);
            p += 4 + tl;
            std::size_t v;
            if (ver >= 3) v = p + 4 + 4 + 1;  // paramètres de type (0), taille, indicateurs
            else v = p + 4 + 4 + 1;           // taille, index, a-un-GUID
            if (type == "number" && tname == "StructProperty" && ver < 3) {  // UE4 : taille u64, nom de structure, GUID 16 o, 1 o
                const std::size_t sn = p + 8;
                const std::uint32_t sl = u32(d, sn);
                if (sl == 9 && sn + 4 + sl + 17 + 8 <= d.size() && d.compare(sn + 4, 8, "Timespan") == 0) {
                    std::int64_t ticks;
                    std::memcpy(&ticks, d.data() + sn + 4 + sl + 17, 8);
                    if (ticks > 0 && ticks / 10000000 < 1000000000) { out.set(f["id"].as_string(), ticks / 10000000); break; }
                }
                continue;
            }
            if (type == "number") {  // lecteur générique : n'importe quel nombre
                double x = -1;
                if ((tname == "DoubleProperty") && v + 8 <= d.size()) std::memcpy(&x, d.data() + v, 8);
                else if (tname == "FloatProperty" && v + 4 <= d.size()) { float f; std::memcpy(&f, d.data() + v, 4); x = f; }
                else if ((tname == "IntProperty" || tname == "UInt32Property") && v + 4 <= d.size()) x = static_cast<double>(tname == "IntProperty" ? static_cast<std::int32_t>(u32(d, v)) : u32(d, v));
                else if ((tname == "Int64Property" || tname == "UInt64Property") && v + 8 <= d.size()) { std::int64_t i; std::memcpy(&i, d.data() + v, 8); x = static_cast<double>(i); }
                if (x >= 0 && x < 1e9) { out.set(f["id"].as_string(), static_cast<std::int64_t>(x)); break; }
                continue;
            }
            if (type == "double" && tname == "DoubleProperty" && v + 8 <= d.size()) {
                double x;
                std::memcpy(&x, d.data() + v, 8);
                if (x >= 0 && x < 1e9) { out.set(f["id"].as_string(), static_cast<std::int64_t>(x)); break; }
            } else if (type == "int" && tname == "IntProperty" && v + 4 <= d.size()) {
                out.set(f["id"].as_string(), static_cast<std::int64_t>(static_cast<std::int32_t>(u32(d, v))));
                break;
            } else if (type == "name" && (tname == "NameProperty" || tname == "StrProperty")) {
                const std::uint32_t sl = u32(d, v);
                if (sl > 0 && sl < 256 && v + 4 + sl <= d.size()) { out.set(f["id"].as_string(), d.substr(v + 4, sl - 1)); break; }
            }
        }
    }
    return out;
}

bool known(const std::string& title_id) {
    for (const auto& e : catalog()["games"].items())
        for (const auto& t : e["titleIds"].items()) if (t.as_string() == title_id) return true;
    return false;
}

namespace {
// Noms courants, d'un jeu à l'autre (comparés sans tenir compte des majuscules en JSON).
struct Want { const char* id; const char* type; std::vector<const char*> names; };
const std::vector<Want>& wants_generic() {
    static const std::vector<Want> w = {
        {"play_time_seconds", "number", {"TotalPlayTime", "PlayTime", "TimePlayed", "PlayedTime", "TotalTimePlayed", "PlayTimeSeconds", "TotalGameTime", "GameTime", "PlayingTime"}},
        {"level", "number", {"PlayerLevel", "CharacterLevel", "Level"}},
        {"chapter", "number", {"CurrentChapter", "ChapterIndex", "Chapter"}},
        {"place", "name", {"MapToLoad", "CurrentMap", "CurrentLevel", "LevelName", "MapName", "CheckpointName", "CurrentArea", "Location"}},
    };
    return w;
}
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
// JSON : parcours (6 niveaux au plus), première clé qui correspond, par ordre de préférence des noms.
void json_find(const Json& j, const std::string& key, int depth, const Json*& found) {
    if (found || depth > 6) return;
    if (j.is_object()) {
        for (const auto& [k, v] : j.members()) if (lower(k) == key && (v.is_number() || (v.is_string() && !v.as_string().empty()))) { found = &v; return; }
        for (const auto& [k, v] : j.members()) json_find(v, key, depth + 1, found);
    } else if (j.is_array()) for (const auto& v : j.items()) json_find(v, key, depth + 1, found);
}
}  // namespace

// Nom d'un champ de structure Blueprint : « CurrentLevel_49_97AB711D48E18088A93C8DADFD96F854 » → « CurrentLevel ».
std::string strip_bp(const std::string& n) {
    const auto u2 = n.rfind('_');
    if (u2 == std::string::npos || n.size() - u2 - 1 != 32 || n.find_first_not_of("0123456789ABCDEF", u2 + 1) != std::string::npos) return n;
    const auto u1 = n.rfind('_', u2 - 1);
    if (u1 == std::string::npos || u1 + 1 == u2 || n.find_first_not_of("0123456789", u1 + 1) < u2) return n;
    return n.substr(0, u1);
}

// Toutes les propriétés simples d'une sauvegarde Unreal : nom (sans suffixe Blueprint), type, position de la valeur.
struct Prop { std::string name, type; std::size_t value; };
std::vector<Prop> scan_props(const std::string& data) {
    std::vector<Prop> out;
    for (std::size_t i = 4; i + 8 < data.size() && out.size() < 20000; ++i) {
        const std::uint32_t len = u32(data, i);
        if (len < 3 || len > 96 || i + 4 + len + 4 > data.size() || data[i + 4 + len - 1] != '\0') continue;
        const std::string name = data.substr(i + 4, len - 1);
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) continue;
        const std::uint32_t tl = u32(data, i + 4 + len);
        if (tl < 9 || tl > 32 || i + 8 + len + tl > data.size()) continue;
        const std::string tname = data.substr(i + 8 + len, tl - 1);
        if (tname.size() < 8 || tname.compare(tname.size() - 8, 8, "Property") != 0) continue;
        out.push_back({strip_bp(name), tname, i + 8 + len + tl + 4 + 4 + 1});
        i += 3 + len;
    }
    return out;
}

Json survey(const std::string& data) {
    Json out = Json::array();
    if (data.size() < 8 || data.compare(0, 4, "GVAS") != 0) return out;
    static const char* keys[] = {"time", "level", "chapter", "map", "progress", "percent", "mission", "quest", "checkpoint", "act", "stage", "location", "area", "region", "difficulty"};
    std::set<std::string> seen;
    // nom de propriété = FString (u32 longueur, texte, zéro) suivi du nom d'un type « …Property »
    for (std::size_t i = 0; i + 8 < data.size() && out.size() < 40; ++i) {
        const std::uint32_t len = u32(data, i);
        if (len < 3 || len > 64 || i + 4 + len + 4 > data.size() || data[i + 4 + len - 1] != '\0') continue;
        const std::string name = data.substr(i + 4, len - 1);
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) continue;
        const std::uint32_t tl = u32(data, i + 4 + len);
        if (tl < 9 || tl > 32 || i + 8 + len + tl > data.size()) continue;
        const std::string tname = data.substr(i + 8 + len, tl - 1);
        if (tname.size() < 8 || tname.compare(tname.size() - 8, 8, "Property") != 0) continue;
        const std::string low = lower(name);
        bool interesting = false;
        for (const char* k : keys) if (low.find(k) != std::string::npos) interesting = true;
        if (interesting && seen.insert(strip_bp(name)).second) out.push(strip_bp(name) + ":" + tname);
        i += 3 + len;
    }
    return out;
}

Json read_generic(const std::string& data) {
    Json out = Json::object();
    if (data.size() >= 8 && data.compare(0, 4, "GVAS") == 0) {
        std::vector<Prop> props;
        bool scanned = false;
        for (const auto& w : wants_generic()) {
            for (const char* n : w.names) {
                Json e = Json::object(), fields = Json::array(), f = Json::object();
                f.set("id", w.id); f.set("property", n); f.set("type", w.type);
                fields.push(f); e.set("fields", fields);
                const Json r = read_gvas(e, data);
                if (r.has(w.id)) { out.set(w.id, r[w.id]); break; }
            }
            if (out.has(w.id)) continue;
            if (!scanned) { props = scan_props(data); scanned = true; }
            const bool number = std::string(w.type) == "number";
            for (const char* n : w.names) {
                for (const auto& p : props) {
                    if (p.name != n) continue;
                    if (number && (p.type == "IntProperty" || p.type == "UInt32Property") && p.value + 4 <= data.size()) {
                        const std::int64_t x = p.type == "IntProperty" ? static_cast<std::int32_t>(u32(data, p.value)) : u32(data, p.value);
                        if (x >= 0 && x < 1000000000) { out.set(w.id, x); break; }
                    } else if (number && (p.type == "DoubleProperty" || p.type == "FloatProperty") && p.value + 8 <= data.size()) {
                        double x;
                        if (p.type == "DoubleProperty") std::memcpy(&x, data.data() + p.value, 8);
                        else { float f; std::memcpy(&f, data.data() + p.value, 4); x = f; }
                        if (x >= 0 && x < 1e9) { out.set(w.id, static_cast<std::int64_t>(x)); break; }
                    } else if (!number && (p.type == "StrProperty" || p.type == "NameProperty") && p.value + 4 <= data.size()) {
                        const std::uint32_t sl = u32(data, p.value);
                        if (sl > 1 && sl < 256 && p.value + 4 + sl <= data.size()) { out.set(w.id, data.substr(p.value + 4, sl - 1)); break; }
                    }
                }
                if (out.has(w.id)) break;
            }
        }
    } else if (!data.empty() && (data[0] == '{' || data[0] == '[') && data.size() < (4u << 20)) {
        Json doc;
        if (!Json::parse(data, doc)) return out;
        for (const auto& w : wants_generic())
            for (const char* n : w.names) {
                const Json* v = nullptr;
                json_find(doc, lower(n), 0, v);
                if (!v) continue;
                if (std::string(w.type) == "number" && v->is_number() && v->as_number() >= 0 && v->as_number() < 1e9) { out.set(w.id, static_cast<std::int64_t>(v->as_number())); break; }
                if (std::string(w.type) == "name" && v->is_string()) { out.set(w.id, v->as_string()); break; }
            }
    }
    if (out["place"].is_string()) out.set("place", pretty_place(out["place"].as_string()));
    if (out.size()) { out.set("source", "generic"); out.set("verified", false); }
    return out;
}

Json read(const std::string& title_id, const std::string& file_name, const std::string& data) {
    const Json* e = entry_for(title_id, file_name);
    if (!e) return Json::object();
    const std::string r = (*e)["reader"].as_string();
    Json out = r == "json" ? read_json(*e, data) : r == "slots" ? read_slots(*e, data) : r == "gvas" ? read_gvas(*e, data) : Json::object();
    if (out["place"].is_string()) out.set("place", pretty_place(out["place"].as_string()));
    if (out.size()) { out.set("source", "catalog"); out.set("verified", (*e)["verified"]); }
    return out;
}

}  // namespace pbc::progress_catalog
