#include "images/identify.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <unistd.h>
#include <vector>

#include "core/fs.hpp"
#include "images/gc_image_files.h"

namespace pbc::images {
namespace {

// Accès aux fichiers d'un jeu : dossier réel ou image (lecture seule).
struct Source {
    pbc_img* img = nullptr;
    std::string dir;  // dossier (chemin hôte) si pas d'image
    ~Source() { if (img) pbc_img_close(img); }

    bool read(const std::string& rel, std::uint64_t off, std::size_t size, std::string& out, std::uint64_t* total = nullptr) {
        out.assign(size, '\0');
        if (img) {
            std::size_t got = 0;
            std::uint64_t fsz = 0;
            char err[256];
            if (pbc_img_read(img, rel.c_str(), off, out.data(), size, &got, &fsz, err, sizeof err) != 0) return false;
            out.resize(got);
            if (total) *total = fsz;
            return true;
        }
        const int fd = ::open(fs::join(dir, rel).c_str(), O_RDONLY);
        if (fd < 0) return false;
        const ssize_t n = ::pread(fd, out.data(), size, static_cast<off_t>(off));
        if (total) { const off_t e = ::lseek(fd, 0, SEEK_END); *total = e > 0 ? static_cast<std::uint64_t>(e) : 0; }
        ::close(fd);
        if (n < 0) return false;
        out.resize(static_cast<std::size_t>(n));
        return true;
    }
    void list(const std::string& rel, std::vector<fs::Entry>& out) {
        out.clear();
        if (img) {
            char err[256];
            pbc_img_list(img, rel.c_str(), [](void* ctx, const char* name, int is_dir, std::uint64_t size) {
                fs::Entry e;
                e.name = name; e.dir = is_dir != 0; e.size = size;
                static_cast<std::vector<fs::Entry>*>(ctx)->push_back(std::move(e));
            }, &out, err, sizeof err);
            return;
        }
        fs::list(rel.empty() ? dir : fs::join(dir, rel), out);
    }
};

std::uint16_t r16(const std::string& b, std::size_t o) { return o + 2 <= b.size() ? static_cast<std::uint16_t>(static_cast<unsigned char>(b[o]) | (static_cast<unsigned char>(b[o + 1]) << 8)) : 0; }
std::uint32_t r32(const std::string& b, std::size_t o) { return o + 4 <= b.size() ? static_cast<std::uint32_t>(r16(b, o)) | (static_cast<std::uint32_t>(r16(b, o + 2)) << 16) : 0; }
std::uint64_t r64(const std::string& b, std::size_t o) { return o + 8 <= b.size() ? static_cast<std::uint64_t>(r32(b, o)) | (static_cast<std::uint64_t>(r32(b, o + 4)) << 32) : 0; }

bool lower_ends(const std::string& s, const char* suf) {
    const std::size_t n = std::strlen(suf);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suf[i]) return false;
    return true;
}

// SDK d'un exécutable : ELF déchiffré → segment SCE_PROCPARAM (0x61000001) ou SCE_MODULE_PARAM (0x61000002).
Json exe_sdk(Source& src, const std::string& rel) {
    Json j = Json::object();
    j.set("path", rel);
    std::string h;
    if (!src.read(rel, 0, 64, h) || h.size() < 64) { j.set("format", "?"); return j; }
    const unsigned char* u = reinterpret_cast<const unsigned char*>(h.data());
    if (u[0] == 0x7f && u[1] == 'E' && u[2] == 'L' && u[3] == 'F') {
        j.set("format", "elf");
        const std::uint64_t phoff = r64(h, 0x20);
        const std::uint16_t phentsize = r16(h, 0x36), phnum = r16(h, 0x38);
        if (phentsize < 56 || phnum == 0 || phnum > 64) return j;
        std::string ph;
        if (!src.read(rel, phoff, static_cast<std::size_t>(phentsize) * phnum, ph)) return j;
        for (std::uint16_t i = 0; i < phnum; ++i) {
            const std::size_t o = static_cast<std::size_t>(i) * phentsize;
            const std::uint32_t type = r32(ph, o);
            if (type != 0x61000001u && type != 0x61000002u) continue;
            std::string p;
            if (!src.read(rel, r64(ph, o + 8), 0x20, p) || p.size() < 0x18) continue;
            const std::string v = sdk_text(r64(p, 0x10));
            if (!v.empty()) { j.set("sdk", v); break; }
        }
        return j;
    }
    // SELF (PS4 4F 15 3D 1D, PS5 54 14 F5 EE) : en-tête de 0x20 octets, table d'entrées (0x20 chacune), puis
    // l'en-tête ELF et ses segments. Un SELF « fake-signé » garde ses segments en clair : on retrouve le segment
    // qui contient SCE_PROCPARAM / SCE_MODULE_PARAM et on lit sa version de SDK. Chiffré ou compressé : non lisible.
    if ((u[0] == 0x4f && u[1] == 0x15 && u[2] == 0x3d && u[3] == 0x1d) || (u[0] == 0x54 && u[1] == 0x14 && u[2] == 0xf5 && u[3] == 0xee)) {
        j.set("format", "self");
        const std::uint16_t n = r16(h, 0x18);
        if (n == 0 || n > 64) return j;
        std::string ent;
        if (!src.read(rel, 0x20, static_cast<std::size_t>(n) * 0x20, ent) || ent.size() < static_cast<std::size_t>(n) * 0x20) return j;
        const std::uint64_t elf_off = 0x20 + static_cast<std::uint64_t>(n) * 0x20;
        std::string eh;
        if (!src.read(rel, elf_off, 64, eh) || eh.size() < 64 || eh[0] != 0x7f || eh[1] != 'E') return j;
        const std::uint16_t phentsize = r16(eh, 0x36), phnum = r16(eh, 0x38);
        if (phentsize < 56 || phnum == 0 || phnum > 64) return j;
        std::string ph;
        if (!src.read(rel, elf_off + r64(eh, 0x20), static_cast<std::size_t>(phentsize) * phnum, ph)) return j;
        for (std::uint16_t i = 0; i < phnum; ++i) {
            const std::size_t o = static_cast<std::size_t>(i) * phentsize;
            const std::uint32_t type = r32(ph, o);
            if (type != 0x61000001u && type != 0x61000002u) continue;
            const std::uint64_t want = r64(ph, o + 8);
            // Segment ELF qui contient ce paramètre, puis son entrée SELF (index du segment dans props >> 20).
            for (std::uint16_t k = 0; k < phnum; ++k) {
                const std::size_t q = static_cast<std::size_t>(k) * phentsize;
                const std::uint64_t off = r64(ph, q + 8), sz = r64(ph, q + 0x20);
                if (want < off || want >= off + sz) continue;
                for (std::uint16_t e = 0; e < n; ++e) {
                    const std::uint64_t props = r64(ent, static_cast<std::size_t>(e) * 0x20);
                    if (((props >> 20) & 0xffff) != k || (props & 0x0a)) continue;  // autre segment, ou chiffré / compressé
                    std::string p;
                    if (src.read(rel, r64(ent, static_cast<std::size_t>(e) * 0x20 + 8) + (want - off), 0x20, p) && p.size() >= 0x18) {
                        const std::string v = sdk_text(r64(p, 0x10));
                        const std::uint32_t magic = r32(p, 8);
                        if (!v.empty() && (magic == 0x4942524fu || magic == 0x3c13f4bfu)) { j.set("sdk", v); return j; }
                    }
                }
            }
        }
        return j;
    }
    j.set("format", "?");
    return j;
}

int sdk_number(const std::string& v) {
    const auto d = v.find('.');
    if (d == std::string::npos) return 0;
    return std::atoi(v.substr(0, d).c_str()) * 100 + std::atoi(v.substr(d + 1, 2).c_str());
}

}  // namespace

std::string sdk_text(std::uint64_t v) {
    if (v > 0xffffffffULL) v >>= 32;  // 64 bits (param.json) → 32 bits utiles
    if (!v) return "";
    const unsigned major = static_cast<unsigned>((v >> 24) & 0xff), minor = static_cast<unsigned>((v >> 16) & 0xff);
    char t[16];
    std::snprintf(t, sizeof t, "%x.%02x", major, minor);
    return t;
}

std::string sdk_text_hex(const std::string& hex) {
    std::string h = hex;
    if (h.rfind("0x", 0) == 0 || h.rfind("0X", 0) == 0) h = h.substr(2);
    if (h.empty() || h.size() > 16) return "";
    return sdk_text(std::strtoull(h.c_str(), nullptr, 16));
}

Json identify(const std::string& p) {
    Json j = Json::object();
    Source src;
    const std::string host = fs::host(p);
    if (lower_ends(p, ".ffpfsc") || lower_ends(p, ".exfat")) {
        char err[256] = {0};
        if (pbc_img_open(host.c_str(), &src.img, err, sizeof err) != 0) {
            j.set("readable", false);
            j.set("error", std::string(err[0] ? err : "image illisible"));
            return j;
        }
    } else if (fs::is_dir(host)) {
        src.dir = host;
    } else {
        j.set("readable", false);
        j.set("error", "format non lisible ici (seuls dossiers, .exfat et .ffpfsc)");
        return j;
    }
    j.set("readable", true);
    std::string text;
    Json param;
    if (src.read("sce_sys/param.json", 0, 1 << 20, text) && Json::parse(text, param)) {
        j.set("title_id", param["titleId"].as_string());
        j.set("version", param["contentVersion"].as_string());
        j.set("master_version", param["masterVersion"].as_string());
        j.set("declared_min", sdk_text_hex(param["requiredSystemSoftwareVersion"].as_string()));
        j.set("declared_sdk", sdk_text_hex(param["sdkVersion"].as_string()));
        const Json& loc = param["localizedParameters"];
        std::string name;
        for (const char* l : {"fr-FR", "en-US", "en-GB"}) if (name.empty()) name = loc[l]["titleName"].as_string();
        if (name.empty()) name = loc[loc["defaultLanguage"].as_string()]["titleName"].as_string();
        j.set("name", name);
    }
    // Exécutables : racine (eboot.bin, *.elf, *.prx, *.sprx) et sce_module/.
    Json exes = Json::array();
    std::string max_sdk;
    std::vector<fs::Entry> ents;
    int n = 0;
    for (const char* dirc : {"", "sce_module"}) {
        const std::string dir = dirc;
        src.list(dir, ents);
        for (const auto& e : ents) {
            if (e.dir || n >= 48) continue;
            if (!(lower_ends(e.name, ".bin") && e.name.size() >= 5 && e.name.compare(0, 5, "eboot") == 0) &&
                !lower_ends(e.name, ".elf") && !lower_ends(e.name, ".prx") && !lower_ends(e.name, ".sprx")) continue;
            const Json x = exe_sdk(src, dir.empty() ? e.name : dir + "/" + e.name);
            const std::string v = x["sdk"].as_string();
            if (!v.empty() && sdk_number(v) > sdk_number(max_sdk)) max_sdk = v;
            exes.push(x);
            ++n;
        }
    }
    j.set("executables", exes);
    j.set("exe_sdk_max", max_sdk);
    // Backport du jeu : bibliothèques propres (fakelib/), chacune avec la version de SDK lue dedans.
    Json libs = Json::array();
    std::string libs_max;
    src.list("fakelib", ents);
    for (const auto& e : ents) {
        if (e.dir) continue;
        Json l = exe_sdk(src, "fakelib/" + e.name);
        l.set("name", e.name);
        l.set("size", e.size);
        const std::string v = l["sdk"].as_string();
        if (!v.empty() && sdk_number(v) > sdk_number(libs_max)) libs_max = v;
        libs.push(l);
    }
    j.set("own_libs", libs);
    j.set("own_libs_sdk_max", libs_max);
    // APR-EMU (libSceAmpr de remplacement, comme Game Compressor la cherche) et son index ampr_emu.index.
    for (const char* rel : {"fakelib/libSceAmpr.sprx", "fakelib/libSceAmpr.prx", "sce_module/libSceAmpr.sprx", "sce_module/libSceAmpr.prx"}) {
        std::string probe;
        if (!src.read(rel, 0, 4, probe) || probe.size() < 4) continue;
        Json a = exe_sdk(src, rel);
        std::string idx;
        a.set("index", src.read("ampr_emu.index", 0, 4, idx) && !idx.empty());
        j.set("apr", a);
        break;
    }
    return j;
}

Json libraries(const std::string& console_dir) {
    Json out = Json::array();
    Source src;
    src.dir = fs::host(console_dir);
    std::vector<fs::Entry> ents;
    src.list("", ents);
    for (const auto& e : ents) {
        if (e.dir) continue;
        Json l = (lower_ends(e.name, ".sprx") || lower_ends(e.name, ".prx") || lower_ends(e.name, ".elf") || lower_ends(e.name, ".bin")) ? exe_sdk(src, e.name) : Json::object();
        l.set("name", e.name);
        l.set("size", e.size);
        out.push(l);
    }
    return out;
}

Json observed_mounts() {
    Json out = Json::object();
    std::string text, t1;
    fs::read(fs::host("/data/shadowmount/debug.log.1"), t1, 8u << 20);
    fs::read(fs::host("/data/shadowmount/debug.log"), text, 8u << 20);
    text = t1 + "\n" + text;
    std::string pending;  // extension de l'image en cours de montage
    std::size_t pos = 0;
    auto ext_of = [](const std::string& line) {
        const auto colon = line.find("): ");
        if (colon == std::string::npos) return std::string();
        std::string path = line.substr(colon + 3);
        path = path.substr(0, path.find(" -> "));
        const auto dot = path.rfind('.');
        return dot == std::string::npos ? std::string() : path.substr(dot + 1);
    };
    auto bump = [&](const std::string& ext, const char* key, const std::string& err) {
        if (ext.empty()) return;
        Json e = out[ext].is_object() ? out[ext] : Json::object();
        e.set(key, e[key].as_int() + 1);
        if (!err.empty()) e.set("last_error", err);
        out.set(ext, e);
    };
    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.find("[IMG] Mounting image (") != std::string::npos) { pending = ext_of(line); continue; }
        if (pending.empty()) continue;
        if (line.find("] Mounted (") != std::string::npos) { bump(pending, "ok", ""); pending.clear(); }
        else if (line.find("failed:") != std::string::npos && line.find("[IMG]") != std::string::npos) {
            bump(pending, "failed", line.substr(line.find("failed:") - std::min<std::size_t>(line.find("failed:"), 14)));
            pending.clear();
        }
    }
    return out;
}

}  // namespace pbc::images
