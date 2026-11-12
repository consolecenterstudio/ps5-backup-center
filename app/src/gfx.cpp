#include "gfx.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "fonts.hpp"
#include "native_assets.hpp"  // table des glyphes (générée pour l'app d'origine)

namespace app {

namespace {
// Index direct dans l'atlas (même résultat que la recherche linéaire d'origine).
const pbck::assets::Glyph* glyph(unsigned font, unsigned code) { return fonts::atlas_glyph(font, code); }
// Suite mise en forme d'une chaîne non triviale (fil de dessin uniquement).
fonts::Item items[1024];
int order[1024];
unsigned rune(std::string_view s, std::size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    unsigned v = c, n = 1;
    if (c >= 0xF0) { v = c & 7; n = 4; } else if (c >= 0xE0) { v = c & 15; n = 3; } else if (c >= 0xC0) { v = c & 31; n = 2; }
    for (unsigned k = 1; k < n && i + k < s.size(); ++k) v = (v << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
    i += n;
    return v;
}
// Couverture d'un coin arrondi (anticrénelage simple sur un pixel).
unsigned corner(int x, int y, int w, int h, int r) {
    if (r <= 0) return 255;
    int cx = x < r ? r : x >= w - r ? w - r - 1 : x;
    int cy = y < r ? r : y >= h - r ? h - r - 1 : y;
    if (cx == x || cy == y) return 255;
    const float dx = static_cast<float>(x - cx) + 0.5f, dy = static_cast<float>(y - cy) + 0.5f;
    const float d = std::sqrt(dx * dx + dy * dy) - static_cast<float>(r) + 0.5f;
    if (d <= 0) return 255;
    if (d >= 1) return 0;
    return static_cast<unsigned>((1 - d) * 255);
}
}  // namespace

// ---- Thèmes -------------------------------------------------------------------------------
// Mêmes valeurs que engine/web/app.css. Le premier (Nuit) est la référence : celle du code de dessin.
namespace theme {
namespace {
constexpr std::uint32_t hx(std::uint32_t v, unsigned a = 255) { return rgb((v >> 16) & 255, (v >> 8) & 255, v & 255, a); }
// clang-format off
constexpr Theme themes[] = {
    {"nuit", "Nuit",
     hx(0x06101d), hx(0x0a1729), hx(0x0f1f35), hx(0x142843), hx(0x1f3757), hx(0xe8f1fb), hx(0x8ea4bd), hx(0x7cc4ff), hx(0x3d8bff),
     hx(0x5fd39a), hx(0xf0b867), hx(0xff7b7b), hx(0x5fd39a), hx(0xa77bff), hx(0x000000, 140),
     hx(0x0a1e3a), hx(0x06101d), hx(0x285faa, 90), hx(0x5840be, 60), hx(0x5096ff), hx(0xffffff)},
    {"oled", "Minuit OLED",
     hx(0x000000), hx(0x07080c), hx(0x0d0f15), hx(0x181c26), hx(0x262c3a), hx(0xeef1f6), hx(0x8c96a6), hx(0x7cc4ff), hx(0x3d8bff),
     hx(0x5fd39a), hx(0xf0b867), hx(0xff7b7b), hx(0x5fd39a), hx(0xa77bff), hx(0x000000, 200),
     hx(0x000000), hx(0x000000), hx(0x000000, 0), hx(0x000000, 0), hx(0x5096ff), hx(0xffffff)},
    {"playstation", "PlayStation",
     hx(0x030a1a), hx(0x071430), hx(0x0c1f45), hx(0x153061), hx(0x24427a), hx(0xffffff), hx(0xa6b6d2), hx(0xffffff), hx(0x0070d1),
     hx(0x5fd39a), hx(0xf0b867), hx(0xff7b7b), hx(0x5fd39a), hx(0xa77bff), hx(0x000000, 160),
     hx(0x062a6e), hx(0x020816), hx(0x0a5ad2, 110), hx(0x00287a, 90), hx(0xc8dcff), hx(0xffffff)},
    {"aurore", "Aurore",
     hx(0x0e0a1e), hx(0x140f2a), hx(0x1c1638), hx(0x261e4a), hx(0x3a2e68), hx(0xf0ecfc), hx(0xa89ec8), hx(0x5eead4), hx(0x8b5cf6),
     hx(0x5fd39a), hx(0xf0b867), hx(0xff7b9b), hx(0x5fd39a), hx(0xc48cff), hx(0x000000, 150),
     hx(0x1e1240), hx(0x0a0818), hx(0x148c96, 80), hx(0x8c3cc8, 80), hx(0x5eead4), hx(0xffffff)},
    {"foret", "Forêt",
     hx(0x081410), hx(0x0c1c16), hx(0x12261e), hx(0x1a3428), hx(0x284838), hx(0xe6f2e8), hx(0x92ac9a), hx(0x86e0a0), hx(0x2ea064),
     hx(0x6fdc9a), hx(0xe8c070), hx(0xff8a7a), hx(0x86e0a0), hx(0xd6b46e), hx(0x000000, 150),
     hx(0x0e2a1e), hx(0x06100c), hx(0x287850, 80), hx(0x968240, 50), hx(0x78dc96), hx(0xffffff)},
    {"sakura", "Sakura",
     hx(0x1a0e18), hx(0x241422), hx(0x301c2e), hx(0x40263c), hx(0x5a3854), hx(0xfcecf4), hx(0xc8a4ba), hx(0xffaacd), hx(0xec6096),
     hx(0x7fdcae), hx(0xf4c27a), hx(0xff8080), hx(0x7fdcae), hx(0xbe96ff), hx(0x000000, 150),
     hx(0x34182e), hx(0x140a12), hx(0xdc64a0, 70), hx(0x7846b4, 60), hx(0xff96c3), hx(0xffffff)},
    {"clair", "Clair",
     hx(0xeaf0f7), hx(0xe0e8f2), hx(0xffffff), hx(0xd6e6fa), hx(0xc2d2e6), hx(0x101c2e), hx(0x52647c), hx(0x0062cc), hx(0x0070e0),
     hx(0x138a58), hx(0xa86400), hx(0xc8283a), hx(0x138a58), hx(0x6e46d2), hx(0x1e3250, 60),
     hx(0xf6f9fd), hx(0xdce6f2), hx(0x78b4ff, 70), hx(0xaa96ff, 50), hx(0x0062cc), hx(0x101c2e)},
    {"contraste", "Contraste élevé",
     hx(0x000000), hx(0x000000), hx(0x0e0e0e), hx(0x303030), hx(0x9a9a9a), hx(0xffffff), hx(0xe0e0e0), hx(0xffdd00), hx(0x00c8ff),
     hx(0x00ff80), hx(0xffaa00), hx(0xff5050), hx(0x00ff80), hx(0xc896ff), hx(0x000000, 200),
     hx(0x000000), hx(0x000000), hx(0x000000, 0), hx(0x000000, 0), hx(0xffdd00), hx(0xffffff)},
};
// clang-format on
constexpr int n_themes = static_cast<int>(sizeof themes / sizeof themes[0]);
int current_ = 0;

constexpr std::uint32_t Theme::*named[] = {&Theme::bg, &Theme::bg2, &Theme::panel, &Theme::panel2, &Theme::line, &Theme::text, &Theme::muted,
                                           &Theme::accent, &Theme::accent2, &Theme::ok, &Theme::warn, &Theme::bad, &Theme::rom, &Theme::app};
// Gris bleutés du thème Nuit, du plus sombre au plus clair : les teintes intermédiaires s'interpolent entre eux.
constexpr std::uint32_t Theme::*ramp[] = {&Theme::bg, &Theme::bg2, &Theme::panel, &Theme::panel2, &Theme::line, &Theme::muted, &Theme::text};
unsigned luma(std::uint32_t c) { return ((c & 255) * 54 + ((c >> 8) & 255) * 183 + ((c >> 16) & 255) * 19) >> 8; }
std::uint32_t with_alpha(std::uint32_t c, unsigned a) { return (c & 0xFFFFFFu) | (std::min(a, 255u) << 24); }
std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) {
    std::uint32_t out = 0;
    for (int sh = 0; sh < 24; sh += 8)
        out |= static_cast<std::uint32_t>(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t + 0.5f) << sh;
    return out;
}
}  // namespace

namespace detail {
bool active = false;
std::uint32_t last_in = 0, last_out = 0;
std::uint32_t translate(std::uint32_t c) {
    const Theme& ref = themes[0];
    const Theme& th = themes[current_];
    const unsigned a = c >> 24, r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    const std::uint32_t col = c & 0xFFFFFFu;
    std::uint32_t out = c;
    if (col == 0) out = with_alpha(th.shadow, a * (th.shadow >> 24) / 140);  // ombres
    else if (col == 0xFFFFFFu) out = a == 255 ? c : with_alpha(th.veil, a);  // blanc pur (texte sur pastille) / voiles
    else if (col == (hx(0x5096ff) & 0xFFFFFFu)) out = with_alpha(th.focus, a);
    else {
        bool found = false;
        for (auto f : named)
            if ((ref.*f & 0xFFFFFFu) == col) { out = with_alpha(th.*f, a); found = true; break; }
        // Autres gris bleutés (fonds de fenêtres, textes atténués) : position sur l'échelle sombre → claire.
        const unsigned hi = std::max({r, g, b}), lo = std::min({r, g, b});
        if (!found && hi - lo < 64 && b >= r) {
            const unsigned y = luma(c);
            const int n = static_cast<int>(sizeof ramp / sizeof ramp[0]);
            if (y <= luma(ref.*ramp[0])) out = with_alpha(th.*ramp[0], a);
            else if (y >= luma(ref.*ramp[n - 1])) out = with_alpha(th.*ramp[n - 1], a);
            else
                for (int i = 0; i + 1 < n; ++i) {
                    const unsigned y0 = luma(ref.*ramp[i]), y1 = luma(ref.*ramp[i + 1]);
                    if (y > y1) continue;
                    const float t = y1 > y0 ? static_cast<float>(y - y0) / static_cast<float>(y1 - y0) : 0.f;
                    out = with_alpha(mix(th.*ramp[i], th.*ramp[i + 1], t), a);
                    break;
                }
        }
    }
    last_in = c;
    last_out = out;
    return out;
}
}  // namespace detail

int count() { return n_themes; }
const Theme& at(int i) { return themes[std::clamp(i, 0, n_themes - 1)]; }
int index() { return current_; }
const Theme& current() { return themes[current_]; }
void set_index(int i) {
    current_ = std::clamp(i, 0, n_themes - 1);
    detail::active = current_ != 0;
    detail::last_in = detail::last_out = 0;  // 0 (noir transparent) : n'a pas de traduction, sans effet
}
bool set(std::string_view id) {
    for (int i = 0; i < n_themes; ++i)
        if (id == themes[i].id) { set_index(i); return true; }
    return false;
}
}  // namespace theme

void Surface::blend_raw(int x, int y, std::uint32_t c, unsigned coverage) {
    if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_) return;
    const unsigned a = ((c >> 24) * coverage) / 255;
    if (!a) return;
    std::uint32_t& d = px_[y * screen_w + x];
    if (a == 255) { d = c | 0xFF000000u; return; }
    const unsigned ia = 255 - a;
    const unsigned r = ((c & 255) * a + (d & 255) * ia) / 255;
    const unsigned g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * ia) / 255;
    const unsigned b = (((c >> 16) & 255) * a + ((d >> 16) & 255) * ia) / 255;
    d = 0xFF000000u | (b << 16) | (g << 8) | r;
}

void Surface::clear(std::uint32_t c) { std::fill(px_, px_ + screen_w * screen_h, c | 0xFF000000u); }

void Surface::backdrop() {
    const Theme& th = theme::current();
    auto ch = [](std::uint32_t c, int sh) { return static_cast<float>((c >> sh) & 255); };
    for (int y = 0; y < screen_h; ++y) {
        const float t = static_cast<float>(y) / screen_h;
        const unsigned r = static_cast<unsigned>(ch(th.top, 0) * (1 - t) + ch(th.bottom, 0) * t),
                       g = static_cast<unsigned>(ch(th.top, 8) * (1 - t) + ch(th.bottom, 8) * t),
                       b = static_cast<unsigned>(ch(th.top, 16) * (1 - t) + ch(th.bottom, 16) * t);
        std::fill(px_ + y * screen_w, px_ + (y + 1) * screen_w, rgb(r, g, b));
    }
    // Halo en haut à droite, second halo en bas à gauche : de la profondeur sans charger.
    const unsigned a1 = th.glow1 >> 24, a2 = th.glow2 >> 24;
    if (a1)
        for (int y = 0; y < 560; ++y)
            for (int x = 900; x < screen_w; ++x) {
                const float dx = (x - 1500) / 760.f, dy = (y + 120) / 560.f, d = dx * dx + dy * dy;
                if (d < 1) blend_raw(x, y, th.glow1 | 0xFF000000u, static_cast<unsigned>((1 - d) * (1 - d) * a1));
            }
    if (a2)
        for (int y = 620; y < screen_h; ++y)
            for (int x = 0; x < 1100; ++x) {
                const float dx = (x - 200) / 900.f, dy = (y - 1180) / 560.f, d = dx * dx + dy * dy;
                if (d < 1) blend_raw(x, y, th.glow2 | 0xFF000000u, static_cast<unsigned>((1 - d) * (1 - d) * a2));
            }
}

void Surface::rect(int x, int y, int w, int h, std::uint32_t c, int radius) {
    radius = std::min({radius, w / 2, h / 2});
    const int x0 = std::max(x, cx0_), y0 = std::max(y, cy0_), x1 = std::min(x + w, cx1_), y1 = std::min(y + h, cy1_);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const unsigned cov = radius ? corner(xx - x, yy - y, w, h, radius) : 255;
            if (cov) blend(xx, yy, c, cov);
        }
}

void Surface::outline(int x, int y, int w, int h, std::uint32_t c, int radius, int t) {
    const int x0 = std::max(x, cx0_), y0 = std::max(y, cy0_), x1 = std::min(x + w, cx1_), y1 = std::min(y + h, cy1_);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const unsigned outer = corner(xx - x, yy - y, w, h, radius);
            if (!outer) continue;
            const bool inside = xx - x >= t && yy - y >= t && xx - x < w - t && yy - y < h - t;
            const unsigned inner = inside ? corner(xx - x - t, yy - y - t, w - 2 * t, h - 2 * t, std::max(0, radius - t)) : 0;
            if (outer > inner) blend(xx, yy, c, outer - inner);
        }
}

void Surface::image(const Image& img, int x, int y, int w, int h, int radius, unsigned alpha) {
    if (!img.valid() || w <= 0 || h <= 0) return;
    const int x0 = std::max(x, cx0_), y0 = std::max(y, cy0_), x1 = std::min(x + w, cx1_), y1 = std::min(y + h, cy1_);
    const bool same = w == img.w && h == img.h;
    for (int yy = y0; yy < y1; ++yy) {
        // Agrandi ou réduit : lissage bilinéaire (pas d'escalier sur les jaquettes en grand).
        const float fy = std::max(0.f, (yy - y + 0.5f) * img.h / h - 0.5f);
        const int sy = std::min(static_cast<int>(fy), img.h - 1), sy2 = std::min(sy + 1, img.h - 1);
        const unsigned ty = static_cast<unsigned>((fy - sy) * 256);
        for (int xx = x0; xx < x1; ++xx) {
            const unsigned cov = radius ? corner(xx - x, yy - y, w, h, radius) : 255;
            if (!cov) continue;
            std::uint32_t s;
            if (same) s = img.px[(yy - y) * img.w + (xx - x)];
            else {
                const float fx = std::max(0.f, (xx - x + 0.5f) * img.w / w - 0.5f);
                const int sx = std::min(static_cast<int>(fx), img.w - 1), sx2 = std::min(sx + 1, img.w - 1);
                const unsigned tx = static_cast<unsigned>((fx - sx) * 256);
                const std::uint32_t p00 = img.px[sy * img.w + sx], p01 = img.px[sy * img.w + sx2], p10 = img.px[sy2 * img.w + sx], p11 = img.px[sy2 * img.w + sx2];
                s = 0;
                for (int sh = 0; sh < 32; sh += 8) {
                    const unsigned a = (p00 >> sh) & 255, b = (p01 >> sh) & 255, c = (p10 >> sh) & 255, d = (p11 >> sh) & 255;
                    const unsigned top = a * (256 - tx) + b * tx, bot = c * (256 - tx) + d * tx;
                    s |= (((top * (256 - ty) + bot * ty) >> 16) & 255) << sh;
                }
            }
            blend_raw(xx, yy, s, cov * alpha / 255);
        }
    }
}

void Surface::image_soft(const Image& img, int x, int y, int w, int h, unsigned alpha) {
    if (!img.valid() || w <= 0 || h <= 0) return;
    const int x0 = std::max(x, cx0_), y0 = std::max(y, cy0_), x1 = std::min(x + w, cx1_), y1 = std::min(y + h, cy1_);
    for (int yy = y0; yy < y1; ++yy) {
        const float fy = std::max(0.f, (yy - y + 0.5f) * img.h / h - 0.5f);
        const int sy = std::min(static_cast<int>(fy), img.h - 1), sy2 = std::min(sy + 1, img.h - 1);
        const unsigned ty = static_cast<unsigned>((fy - sy) * 256);
        for (int xx = x0; xx < x1; ++xx) {
            const float fx = std::max(0.f, (xx - x + 0.5f) * img.w / w - 0.5f);
            const int sx = std::min(static_cast<int>(fx), img.w - 1), sx2 = std::min(sx + 1, img.w - 1);
            const unsigned tx = static_cast<unsigned>((fx - sx) * 256);
            const std::uint32_t p00 = img.px[sy * img.w + sx], p01 = img.px[sy * img.w + sx2], p10 = img.px[sy2 * img.w + sx], p11 = img.px[sy2 * img.w + sx2];
            std::uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const unsigned a = (p00 >> sh) & 255, b = (p01 >> sh) & 255, c = (p10 >> sh) & 255, d = (p11 >> sh) & 255;
                const unsigned top = a * (256 - tx) + b * tx, bot = c * (256 - tx) + d * tx;
                out |= (((top * (256 - ty) + bot * ty) >> 16) & 255) << sh;
            }
            blend_raw(xx, yy, out, alpha);
        }
    }
}

void Surface::fade(int x, int y, int w, int h, std::uint32_t c, bool horizontal, bool opaque_first) {
    const int x0 = std::max(x, cx0_), y0 = std::max(y, cy0_), x1 = std::min(x + w, cx1_), y1 = std::min(y + h, cy1_);
    const int span = horizontal ? w : h;
    if (span <= 0) return;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) {
            const int pos = horizontal ? xx - x : yy - y;
            const unsigned t = static_cast<unsigned>(255 * pos / span);
            blend(xx, yy, c, opaque_first ? 255 - t : t);
        }
}

int Surface::measure(std::string_view s, FontSize size) {
    if (!fonts::simple(s, size)) {
        const int n = fonts::prepare(s, size, items, static_cast<int>(std::size(items)));
        int w = 0;
        for (int i = 0; i < n; ++i) w += items[i].advance;
        return w;
    }
    int w = 0;
    for (std::size_t i = 0; i < s.size();) if (const auto* g = glyph(size, rune(s, i))) w += g->advance;
    return w;
}
int Surface::line_height(FontSize size) { return size == small ? 22 : size == body ? 30 : size == large ? 36 : 62; }

void Surface::set_rtl(bool rtl) { fonts::set_rtl(rtl); }
bool Surface::rtl() { return fonts::rtl(); }

int Surface::text(const Font& f, int x, int y, std::string_view s, FontSize size, std::uint32_t c, int max_w) {
    if (!f.atlas.valid()) return 0;
    if (!fonts::simple(s, size)) return text_complex(f, x, y, s, size, c, max_w);
    const bool cut = max_w > 0 && measure(s, size) > max_w;
    const int limit = cut ? max_w - measure("…", size) : 1 << 30;
    int used = 0;
    auto draw = [&](const pbck::assets::Glyph& g, int at) {
        for (unsigned yy = 0; yy < g.height; ++yy)
            for (unsigned xx = 0; xx < g.width; ++xx) {
                const unsigned a = f.atlas.px[(g.y + yy) * static_cast<unsigned>(f.atlas.w) + g.x + xx] >> 24;
                if (a) blend(at + g.left + static_cast<int>(xx), y + g.top + static_cast<int>(yy), c, a);
            }
    };
    for (std::size_t i = 0; i < s.size();) {
        const auto* g = glyph(size, rune(s, i));
        if (!g) continue;
        if (used + g->advance > limit) break;
        draw(*g, x + used);
        used += g->advance;
    }
    if (cut) if (const auto* g = glyph(size, 0x2026)) { draw(*g, x + used); used += g->advance; }
    return used;
}

// Tout le reste : polices de secours, arabe mis en forme, passages de droite à gauche.
// Même règle de coupure que le latin : on garde le début logique du texte, puis « … ».
int Surface::text_complex(const Font& f, int x, int y, std::string_view s, FontSize size, std::uint32_t c, int max_w) {
    int n = fonts::prepare(s, size, items, static_cast<int>(std::size(items)) - 1);
    int total = 0;
    for (int i = 0; i < n; ++i) total += items[i].advance;
    if (max_w > 0 && total > max_w) {
        const auto* dots = glyph(size, 0x2026);
        const int limit = max_w - (dots ? dots->advance : 0);
        int used = 0, k = 0;
        while (k < n && used + items[k].advance <= limit) used += items[k++].advance;
        n = k;
        if (dots) items[n++] = {0x2026, dots, 0xFFFF, 0, static_cast<std::int16_t>(dots->advance), 0};
    }
    fonts::reorder(items, n, size, order);
    int pen = x;
    for (int v = 0; v < n; ++v) {
        const fonts::Item& it = items[order[v]];
        if (it.atlas) {
            const auto& g = *it.atlas;
            for (unsigned yy = 0; yy < g.height; ++yy)
                for (unsigned xx = 0; xx < g.width; ++xx) {
                    const unsigned a = f.atlas.px[(g.y + yy) * static_cast<unsigned>(f.atlas.w) + g.x + xx] >> 24;
                    if (a) blend(pen + g.left + static_cast<int>(xx), y + g.top + static_cast<int>(yy), c, a);
                }
        } else if (fonts::Bitmap b; fonts::bitmap(it.slot, it.glyph, size, b)) {
            for (int yy = 0; yy < b.h; ++yy) {
                const std::uint8_t* row = b.px + static_cast<std::size_t>(yy) * static_cast<std::size_t>(b.stride);
                for (int xx = 0; xx < b.w; ++xx)
                    if (row[xx]) blend(pen + b.left + xx, y + b.top + yy, c, row[xx]);
            }
        }
        pen += it.advance;
    }
    return pen - x;
}

void Surface::push_clip(int x, int y, int w, int h) {
    if (depth_ < 8) { saved_[depth_][0] = cx0_; saved_[depth_][1] = cy0_; saved_[depth_][2] = cx1_; saved_[depth_][3] = cy1_; ++depth_; }
    cx0_ = std::max(cx0_, x); cy0_ = std::max(cy0_, y); cx1_ = std::min(cx1_, x + w); cy1_ = std::min(cy1_, y + h);
}
void Surface::pop_clip() {
    if (!depth_) return;
    --depth_;
    cx0_ = saved_[depth_][0]; cy0_ = saved_[depth_][1]; cx1_ = saved_[depth_][2]; cy1_ = saved_[depth_][3];
}

void scale_rgba(const std::uint32_t* src, int sw, int sh, std::uint32_t* dst, int dw, int dh, bool crop, float anchor_y) {
    // Recadrage centré pour remplir un carré sans déformer (jaquettes rectangulaires).
    float ox = 0, oy = 0, vw = static_cast<float>(sw), vh = static_cast<float>(sh);
    if (crop) {
        const float sr = static_cast<float>(sw) / sh, dr = static_cast<float>(dw) / dh;
        if (sr > dr) { vw = sh * dr; ox = (sw - vw) / 2; } else { vh = sw / dr; oy = (sh - vh) * anchor_y; }
    }
    for (int y = 0; y < dh; ++y) {
        const float fy = oy + (y + 0.5f) * vh / dh - 0.5f;
        const int y0 = std::clamp(static_cast<int>(fy), 0, sh - 1), y1 = std::min(y0 + 1, sh - 1);
        const float ty = std::clamp(fy - y0, 0.f, 1.f);
        for (int x = 0; x < dw; ++x) {
            const float fx = ox + (x + 0.5f) * vw / dw - 0.5f;
            const int x0 = std::clamp(static_cast<int>(fx), 0, sw - 1), x1 = std::min(x0 + 1, sw - 1);
            const float tx = std::clamp(fx - x0, 0.f, 1.f);
            std::uint32_t out = 0;
            for (int ch = 0; ch < 4; ++ch) {
                auto c = [&](int xx, int yy) { return static_cast<float>((src[yy * sw + xx] >> (ch * 8)) & 255); };
                const float v = (c(x0, y0) * (1 - tx) + c(x1, y0) * tx) * (1 - ty) + (c(x0, y1) * (1 - tx) + c(x1, y1) * tx) * ty;
                out |= static_cast<std::uint32_t>(std::clamp(v + 0.5f, 0.f, 255.f)) << (ch * 8);
            }
            dst[y * dw + x] = out;
        }
    }
}

}  // namespace app
