#include "model/model.hpp"

#include <string_view>

namespace pbc {

namespace {
constexpr const char* kinds[] = {"game_ps5", "game_ps4", "app", "rom"};
constexpr const char* parts[] = {"game", "update", "dlc", "save", "state", "config", "app", "backport"};
template <class E, std::size_t N>
bool parse_enum(std::string_view s, const char* const (&names)[N], E& out) {
    for (std::size_t i = 0; i < N; ++i) if (s == names[i]) { out = static_cast<E>(i); return true; }
    return false;
}
}  // namespace

const char* to_string(Kind k) { return kinds[static_cast<int>(k)]; }
const char* to_string(Part p) { return parts[static_cast<int>(p)]; }
bool parse_kind(std::string_view s, Kind& k) { return parse_enum(s, kinds, k); }
bool parse_part(std::string_view s, Part& p) { return parse_enum(s, parts, p); }

Json Item::to_json() const {
    Json j = Json::object();
    j.set("key", key);
    j.set("kind", to_string(kind));
    j.set("title", title);
    j.set("title_id", title_id);
    j.set("platform", platform);
    j.set("place", place_id);
    j.set("image_type", image_type);
    Json a = Json::array();
    for (const auto& s : apps) a.push(s);
    j.set("apps", a);
    if (!cover.empty()) j.set("cover", cover);
    Json comps = Json::array();
    for (const auto& c : components) {
        Json cj = Json::object();
        cj.set("part", to_string(c.part));
        cj.set("label", c.label);
        cj.set("bytes", c.bytes);
        cj.set("restore_to", c.restore_to);
        if (c.meta.is_object()) cj.set("meta", c.meta);
        Json srcs = Json::array();
        for (const auto& s : c.sources) {
            Json sj = Json::object();
            sj.set("source", s.source);
            sj.set("relative", s.relative);
            sj.set("dir", s.dir);
            srcs.push(sj);
        }
        cj.set("sources", srcs);
        comps.push(cj);
    }
    j.set("components", comps);
    return j;
}

bool Item::from_json(const Json& j, Item& it) {
    if (!j.is_object() || !parse_kind(j["kind"].as_string(), it.kind)) return false;
    it.key = j["key"].as_string();
    it.title = j["title"].as_string();
    it.title_id = j["title_id"].as_string();
    it.platform = j["platform"].as_string();
    it.place_id = j["place"].as_string();
    it.image_type = j["image_type"].as_string();
    it.cover = j["cover"].as_string();
    it.apps.clear();
    for (const auto& a : j["apps"].items()) it.apps.push_back(a.as_string());
    it.components.clear();
    for (const auto& cj : j["components"].items()) {
        Component c;
        if (!parse_part(cj["part"].as_string(), c.part)) return false;
        c.label = cj["label"].as_string();
        c.bytes = static_cast<std::uint64_t>(cj["bytes"].as_int());
        c.restore_to = cj["restore_to"].as_string();
        if (cj["meta"].is_object()) c.meta = cj["meta"];
        for (const auto& sj : cj["sources"].items()) c.sources.push_back({sj["source"].as_string(), sj["relative"].as_string(), sj["dir"].as_bool()});
        it.components.push_back(std::move(c));
    }
    return !it.key.empty();
}

}  // namespace pbc
