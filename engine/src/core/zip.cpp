#include "core/zip.hpp"

#include <cstdio>
#include <cstdlib>

#include "zlib.h"

namespace pbc::zip {

namespace {
std::uint32_t u16(const unsigned char* p) { return p[0] | p[1] << 8; }
std::uint32_t u32(const unsigned char* p) { return u16(p) | u16(p + 2) << 16; }
}  // namespace

bool list(const std::string& path, std::vector<Entry>& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    // Fin du répertoire central : dans les derniers 64 Kio + 22 octets.
    const long tail = size < 65557 ? size : 65557;
    std::vector<unsigned char> t(static_cast<std::size_t>(tail));
    std::fseek(f, size - tail, SEEK_SET);
    if (std::fread(t.data(), 1, t.size(), f) != t.size()) { std::fclose(f); return false; }
    long eocd = -1;
    for (long i = tail - 22; i >= 0; --i) if (u32(&t[static_cast<std::size_t>(i)]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { std::fclose(f); return false; }
    const unsigned char* e = &t[static_cast<std::size_t>(eocd)];
    const std::uint32_t count = u16(e + 10), cd_size = u32(e + 12), cd_off = u32(e + 16);
    std::vector<unsigned char> cd(cd_size);
    std::fseek(f, static_cast<long>(cd_off), SEEK_SET);
    if (std::fread(cd.data(), 1, cd.size(), f) != cd.size()) { std::fclose(f); return false; }
    std::fclose(f);
    std::size_t p = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 46 > cd.size() || u32(&cd[p]) != 0x02014b50) return false;
        Entry en;
        en.method = static_cast<std::uint16_t>(u16(&cd[p + 10]));
        en.crc = u32(&cd[p + 16]);
        en.packed = u32(&cd[p + 20]);
        en.size = u32(&cd[p + 24]);
        const std::uint32_t nlen = u16(&cd[p + 28]), xlen = u16(&cd[p + 30]), clen = u16(&cd[p + 32]);
        en.header = u32(&cd[p + 42]);
        if (p + 46 + nlen > cd.size()) return false;
        en.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), nlen);
        out.push_back(std::move(en));
        p += 46 + nlen + xlen + clen;
    }
    return true;
}

bool list_tail(const std::string& tail, std::uint64_t size, std::vector<Entry>& out) {
    out.clear();
    if (tail.size() < 22) return false;
    const auto* t = reinterpret_cast<const unsigned char*>(tail.data());
    long eocd = -1;
    for (long i = static_cast<long>(tail.size()) - 22; i >= 0; --i) if (u32(t + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) return false;
    const std::uint32_t count = u16(t + eocd + 10), cd_size = u32(t + eocd + 12), cd_off = u32(t + eocd + 16);
    const std::uint64_t tail_start = size - tail.size();
    if (cd_off < tail_start || cd_off + cd_size > size) return false;
    std::size_t p = static_cast<std::size_t>(cd_off - tail_start);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p + 46 > tail.size() || u32(t + p) != 0x02014b50) return false;
        Entry en;
        en.method = static_cast<std::uint16_t>(u16(t + p + 10));
        en.crc = u32(t + p + 16);
        en.packed = u32(t + p + 20);
        en.size = u32(t + p + 24);
        const std::uint32_t nlen = u16(t + p + 28), xlen = u16(t + p + 30), clen = u16(t + p + 32);
        en.header = u32(t + p + 42);
        if (p + 46 + nlen > tail.size()) return false;
        en.name.assign(tail.data() + p + 46, nlen);
        out.push_back(std::move(en));
        p += 46 + nlen + xlen + clen;
    }
    return true;
}

bool read(const std::string& path, const Entry& en, std::string& out, std::string& err) {
    out.clear();
    if (en.size > (512u << 20)) { err = "fichier trop gros dans l'archive"; return false; }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "archive illisible"; return false; }
    unsigned char lh[30];
    std::fseek(f, static_cast<long>(en.header), SEEK_SET);
    if (std::fread(lh, 1, 30, f) != 30 || u32(lh) != 0x04034b50) { std::fclose(f); err = "entrée abîmée : " + en.name; return false; }
    std::fseek(f, static_cast<long>(en.header + 30 + u16(lh + 26) + u16(lh + 28)), SEEK_SET);
    std::string packed(static_cast<std::size_t>(en.packed), '\0');
    if (std::fread(packed.data(), 1, packed.size(), f) != packed.size()) { std::fclose(f); err = "entrée tronquée : " + en.name; return false; }
    std::fclose(f);
    if (en.method == 0) out = std::move(packed);
    else if (en.method == 8) {
        out.resize(static_cast<std::size_t>(en.size));
        z_stream zs{};
        zs.zalloc = [](voidpf, uInt n, uInt sz) -> voidpf { return std::calloc(n, sz); };
        zs.zfree = [](voidpf, voidpf p) { std::free(p); };
        if (inflateInit2(&zs, -15) != Z_OK) { err = "décompression impossible"; return false; }
        zs.next_in = reinterpret_cast<Bytef*>(packed.data());
        zs.avail_in = static_cast<uInt>(packed.size());
        zs.next_out = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = static_cast<uInt>(out.size());
        const int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (rc != Z_STREAM_END || zs.total_out != en.size) { err = "décompression échouée : " + en.name; return false; }
    } else { err = "méthode de compression non prise en charge : " + en.name; return false; }
    // La copie doit être exacte.
    if (crc32(out) != en.crc) { err = "contrôle CRC faux : " + en.name; return false; }
    return true;
}

std::uint32_t crc32(const std::string& data) {
    static std::uint32_t table[256];
    static bool init = false;
    if (!init) { for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } init = true; }
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string build(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, cd;
    auto put16 = [](std::string& s, std::uint32_t v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); };
    auto put32 = [&](std::string& s, std::uint32_t v) { put16(s, v & 0xFFFF); put16(s, v >> 16); };
    for (const auto& [name, data] : files) {
        const std::uint32_t crc = crc32(data), off = static_cast<std::uint32_t>(out.size());
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0x0800); put16(out, 0); put16(out, 0); put16(out, 0x21);
        put32(out, crc); put32(out, static_cast<std::uint32_t>(data.size())); put32(out, static_cast<std::uint32_t>(data.size()));
        put16(out, static_cast<std::uint32_t>(name.size())); put16(out, 0);
        out += name;
        out += data;
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0x0800); put16(cd, 0); put16(cd, 0); put16(cd, 0x21);
        put32(cd, crc); put32(cd, static_cast<std::uint32_t>(data.size())); put32(cd, static_cast<std::uint32_t>(data.size()));
        put16(cd, static_cast<std::uint32_t>(name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off);
        cd += name;
    }
    const std::uint32_t cd_off = static_cast<std::uint32_t>(out.size());
    out += cd;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, static_cast<std::uint32_t>(files.size())); put16(out, static_cast<std::uint32_t>(files.size()));
    put32(out, static_cast<std::uint32_t>(cd.size())); put32(out, cd_off); put16(out, 0);
    return out;
}

}  // namespace pbc::zip
