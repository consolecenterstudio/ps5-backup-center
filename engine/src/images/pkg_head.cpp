#include "images/pkg_head.hpp"

#include "core/json.hpp"
#include "store/store.hpp"

namespace pbc::pkghead {

namespace {

std::uint32_t be32(const std::string& b, std::size_t o) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(b[o + 3]));
}
std::uint64_t le64(const std::string& b, std::size_t o) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[o + static_cast<std::size_t>(i)]);
    return v;
}

constexpr std::size_t kHead = 0x100, kCnt = 0xA0;

bool read_at(store::Store& st, const std::string& path, std::uint64_t off, std::size_t n, std::string& out) {
    std::string err;
    auto r = st.open_at(path, off, err);
    if (!r) return false;
    out.assign(n, '\0');
    std::size_t got = 0;
    while (got < n) {
        const long k = r->read(out.data() + got, n - got);
        if (k <= 0) break;
        got += static_cast<std::size_t>(k);
    }
    out.resize(got);
    return got == n;
}

}  // namespace

std::string Head::title_id() const {
    const auto a = content_id.find('-');
    if (a == std::string::npos) return "";
    const auto b = content_id.find('_', a + 1);
    return b == std::string::npos ? "" : content_id.substr(a + 1, b - a - 1);
}

// Conteneur CNT (big-endian) : type de contenu en 0x74, drapeaux en 0x78, identifiant de contenu en 0x40.
void parse_cnt(const std::string& c, Head& h) {
    if (c.size() < kCnt || be32(c, 0) != 0x7F434E54u) return;
    const std::uint32_t type = be32(c, 0x74), flags = be32(c, 0x78);
    std::string cid = c.substr(0x40, 36);
    if (const auto z = cid.find('\0'); z != std::string::npos) cid.resize(z);
    h.content_id = cid;
    // 0x1B / 0x21 : contenu additionnel ; drapeaux 0x40000000 (mise à jour suivante) ou 0x00100000 (première) : une mise à jour.
    if (type == 0x1B || type == 0x21) h.kind = "dlc";
    else if (flags & 0x40100000u) h.kind = "patch";
    else if (type == 0x1A || type == 0x20 || type == 0x26) h.kind = "game";
    if (h.platform.empty()) {
        if (type == 0x1A || type == 0x1B) h.platform = "ps4";
        else if (type == 0x20 || type == 0x21 || type == 0x26) h.platform = "ps5";
    }
    if (h.platform.empty()) {
        const std::string t = h.title_id();
        h.platform = t.rfind("CUSA", 0) == 0 ? "ps4" : t.rfind("PPSA", 0) == 0 ? "ps5" : "";
    }
    h.ok = true;
}

bool parse(const std::string& head, Head& h, std::uint64_t& cnt_offset) {
    cnt_offset = 0;
    if (head.size() < kCnt || static_cast<unsigned char>(head[0]) != 0x7F) return false;
    const std::string m = head.substr(1, 3);
    if (m == "FIH") {  // paquet PS5 complet ; octet 5 : 0x00 fake (debug), 0x80 officiel (signé Sony)
        h.container = "fih"; h.platform = "ps5";
        const unsigned char s = static_cast<unsigned char>(head[5]);
        h.fake = s == 0x00 ? 1 : s == 0x80 ? 0 : -1;
        if (head.size() < 0x60) return false;
        cnt_offset = le64(head, 0x58);
        h.ok = true;  // jeu, mise à jour ou DLC : dit par le conteneur (read)
        return true;
    }
    if (m == "LIH") {  // paquet de mise à jour PS5
        h.container = "lih"; h.platform = "ps5"; h.kind = "patch";
        const unsigned char s = static_cast<unsigned char>(head[5]);
        h.fake = s == 0x00 ? 1 : s == 0x80 ? 0 : -1;
        h.ok = true;
        return true;
    }
    if (m == "CNT") {
        h.container = "cnt";
        parse_cnt(head, h);
        return h.ok;
    }
    return false;
}

bool read(store::Store& st, const std::string& path, Head& h) {
    h = Head();
    std::string head;
    if (!read_at(st, path, 0, kHead, head)) return false;
    std::uint64_t cnt = 0;
    if (!parse(head, h, cnt)) return false;
    if (h.container == "fih") {
        // Jeu, mise à jour ou DLC : c'est le conteneur CNT qui le dit. Sans lui, rien n'est sûr : pas de réponse
        // (relu la fois suivante) plutôt qu'un « jeu » deviné — un DLC de 1 Mo pris pour le jeu (Kena, 20/11).
        std::string c;
        h.kind.clear();
        if (cnt < kHead || !read_at(st, path, cnt, kCnt, c)) { h.ok = false; return false; }
        parse_cnt(c, h);
        if (h.kind.empty() || h.content_id.empty()) { h.ok = false; return false; }
    }
    return h.ok;
}

std::string kraken_refusal(const Head& h) {
    if (!h.ok) return "";
    if (h.kind == "patch") return "mise à jour : elle s'installe par la console, une fois le jeu installé";
    if (h.kind == "dlc") return "contenu additionnel : il s'installe par la console, une fois le jeu installé";
    if (h.platform == "ps4" || h.container == "cnt") return "jeu PS4 : Kraken ne lance que des .pkg PS5";
    if (h.fake == 0) return "paquet officiel de Sony : Kraken ne lance que des fake PKG";
    return "";
}

Json to_json(const Head& h) {
    Json j = Json::object();
    j.set("container", h.container); j.set("kind", h.kind); j.set("platform", h.platform);
    j.set("fake", static_cast<std::int64_t>(h.fake));
    if (!h.content_id.empty()) j.set("content_id", h.content_id);
    return j;
}

Head from_json(const Json& j) {
    Head h;
    if (!j.is_object()) return h;
    h.ok = true;
    h.container = j["container"].as_string(); h.kind = j["kind"].as_string(); h.platform = j["platform"].as_string();
    h.fake = j.has("fake") ? static_cast<int>(j["fake"].as_int()) : -1;
    h.content_id = j["content_id"].as_string();
    return h;
}

}  // namespace pbc::pkghead
