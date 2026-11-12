#include "core/image.hpp"

#include <cstdlib>

#include "zlib.h"
#include "src/webp/decode.h"  // libwebp (BSD) : jaquettes PS4 / PS5 des sites *patches.com

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) do { if (!(x)) __builtin_trap(); } while (0)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#include "stb_image.h"
#pragma clang diagnostic pop

#include <cstdint>
#include <algorithm>
#include <vector>

namespace pbc::image {

namespace {
std::uint32_t crc32(const unsigned char* p, std::size_t n, std::uint32_t c = 0) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t v = i; for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; table[i] = v; }
        ready = true;
    }
    c = ~c;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}
void be32(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s += static_cast<char>((v >> (i * 8)) & 0xFF); }
void chunk(std::string& png, const char* type, const std::string& data) {
    be32(png, static_cast<std::uint32_t>(data.size()));
    std::string body = std::string(type, 4) + data;
    png += body;
    be32(png, crc32(reinterpret_cast<const unsigned char*>(body.data()), body.size()));
}
}  // namespace

void encode_png(const void* rgba_data, unsigned w, unsigned hgt, std::string& png) {
    const auto* src = static_cast<const unsigned char*>(rgba_data);
    std::string raw;
    raw.reserve((static_cast<std::size_t>(w) * 4 + 1) * hgt);
    for (unsigned y = 0; y < hgt; ++y) {
        raw += '\0';
        raw.append(reinterpret_cast<const char*>(src) + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4);
    }
    z_stream zs{};
    zs.zalloc = [](voidpf, uInt n, uInt sz) -> voidpf { return std::calloc(n, sz); };
    zs.zfree = [](voidpf, voidpf p) { std::free(p); };
    uLong zlen = 0;
    std::string z;
    if (deflateInit(&zs, 6) != Z_OK) { png.clear(); return; }
    z.assign(deflateBound(&zs, static_cast<uLong>(raw.size())) + 16, '\0');
    zs.next_in = reinterpret_cast<Bytef*>(raw.data());
    zs.avail_in = static_cast<uInt>(raw.size());
    zs.next_out = reinterpret_cast<Bytef*>(z.data());
    zs.avail_out = static_cast<uInt>(z.size());
    if (deflate(&zs, Z_FINISH) == Z_STREAM_END) zlen = zs.total_out;
    deflateEnd(&zs);
    z.resize(zlen);
    png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    be32(ihdr, w); be32(ihdr, hgt);
    ihdr += '\x08'; ihdr += '\x06'; ihdr += '\0'; ihdr += '\0'; ihdr += '\0';
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
}

bool tga_to_png(std::string_view t, std::string& png) {
    if (t.size() < 18) return false;
    const auto* h = reinterpret_cast<const unsigned char*>(t.data());
    const unsigned id_len = h[0], type = h[2], bpp = h[16], desc = h[17];
    const unsigned w = h[12] | (h[13] << 8), hgt = h[14] | (h[15] << 8);
    if (type != 2 || (bpp != 32 && bpp != 24) || h[1] != 0 || !w || !hgt || w > 4096 || hgt > 4096) return false;
    const std::size_t px = bpp / 8, off = 18 + id_len;
    if (t.size() < off + static_cast<std::size_t>(w) * hgt * px) return false;
    const bool top = desc & 0x20;
    std::string rgba;
    rgba.reserve(static_cast<std::size_t>(w) * hgt * 4);
    for (unsigned y = 0; y < hgt; ++y) {
        const unsigned row = top ? y : hgt - 1 - y;
        const unsigned char* p = h + off + static_cast<std::size_t>(row) * w * px;
        for (unsigned x = 0; x < w; ++x, p += px) {
            rgba += static_cast<char>(p[2]); rgba += static_cast<char>(p[1]); rgba += static_cast<char>(p[0]);
            rgba += static_cast<char>(px == 4 ? p[3] : 255);
        }
    }
    encode_png(rgba.data(), w, hgt, png);
    return true;
}

bool decode(std::string_view data, std::vector<std::uint32_t>& out, int& w, int& h) {
    std::string png;
    std::string_view src = data;
    if (data.size() > 18 && static_cast<unsigned char>(data[2]) == 2 && data.substr(1, 3) != "PNG" && tga_to_png(data, png)) src = png;
    if (data.size() > 12 && data.substr(0, 4) == "RIFF" && data.substr(8, 4) == "WEBP") {
        std::uint8_t* px = WebPDecodeRGBA(reinterpret_cast<const std::uint8_t*>(data.data()), data.size(), &w, &h);
        if (!px) return false;
        out.assign(reinterpret_cast<const std::uint32_t*>(px), reinterpret_cast<const std::uint32_t*>(px) + static_cast<std::size_t>(w) * h);
        WebPFree(px);
        return true;
    }
    int n = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(src.data()), static_cast<int>(src.size()), &w, &h, &n, 4);
    if (!px) return false;
    out.assign(reinterpret_cast<const std::uint32_t*>(px), reinterpret_cast<const std::uint32_t*>(px) + static_cast<std::size_t>(w) * h);
    stbi_image_free(px);
    return true;
}

void fit(const std::vector<std::uint32_t>& src, int w, int h, int max, std::vector<std::uint32_t>& dst, int& dw, int& dh) {
    const double k = std::min(1.0, static_cast<double>(max) / std::max(w, h));
    dw = std::max(1, static_cast<int>(w * k + 0.5));
    dh = std::max(1, static_cast<int>(h * k + 0.5));
    dst.assign(static_cast<std::size_t>(dw) * dh, 0);
    // Moyenne des pixels source couverts (réduction nette, sans crénelage).
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
            unsigned long long acc[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx) {
                    const std::uint32_t p = src[static_cast<std::size_t>(yy) * w + xx];
                    for (int c = 0; c < 4; ++c) acc[c] += (p >> (c * 8)) & 255;
                    ++n;
                }
            std::uint32_t v = 0;
            for (int c = 0; c < 4; ++c) v |= static_cast<std::uint32_t>(acc[c] / n) << (c * 8);
            dst[static_cast<std::size_t>(y) * dw + x] = v;
        }
    }
}

}  // namespace pbc::image
