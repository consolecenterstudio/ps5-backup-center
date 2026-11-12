#include "fonts.hpp"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "native_assets.hpp"  // atlas Inter (glyphes de l'app d'origine)

namespace {
// Brouillon de rasterisation : stb_truetype alloue quelques Kio par glyphe, remis à zéro à chaque glyphe.
unsigned char* scratch = nullptr;
std::size_t scratch_cap = 0, scratch_used = 0;
void* scratch_alloc(std::size_t n) {
    n = (n + 15) & ~std::size_t{15};
    if (!scratch || n > scratch_cap - scratch_used) return nullptr;
    void* p = scratch + scratch_used;
    scratch_used += n;
    return p;
}
}  // namespace

#define STBTT_malloc(x, u) ((void)(u), scratch_alloc(x))
#define STBTT_free(x, u) ((void)(x), (void)(u))
#define STBTT_assert(x) ((void)0)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "stb_truetype.h"
#pragma clang diagnostic pop

namespace app::fonts {

namespace {

constexpr float em_px[4] = {22, 28, 36, 64};  // tailles de l'atlas (prepare-native-library.py)

struct Face {
    stbtt_fontinfo info;
    float scale[4];
    bool ok;
};
Face faces[slot_count] = {};

// ---- Cache de glyphes : table de hachage + rangées dans une zone alpha 8 bits ----
struct Entry {
    std::uint32_t key;  // 0 = libre
    std::uint16_t x, y, w, h;
    std::int16_t left, top, advance;
    std::uint8_t state;  // 0 = métriques seules, 1 = image prête, 2 = rien à dessiner
};
constexpr unsigned table_bits = 13, table_size = 1u << table_bits, table_max = table_size * 3 / 4;
constexpr unsigned resolve_size = 4096;
constexpr int area_w = 1024;
constexpr std::size_t scratch_bytes = 512u << 10;
Entry* table = nullptr;
unsigned table_count = 0;
struct Resolved { std::uint32_t key; std::uint16_t slot, glyph; };
Resolved* resolved = nullptr;
std::uint8_t* area = nullptr;
int area_h = 0, shelf_x = 0, shelf_y = 0, shelf_h = 0;

int lang_pref = 0;  // 0 = chinois simplifié, 1 = traditionnel, 2 = japonais, 3 = coréen
bool rtl_default = false;

void flush() {
    std::memset(table, 0, table_size * sizeof(Entry));
    table_count = 0;
    shelf_x = shelf_y = shelf_h = 0;
}

Entry* entry(unsigned slot, unsigned glyph, unsigned size) {
    if (!table) return nullptr;
    if (table_count >= table_max) flush();
    const std::uint32_t key = ((slot << 18) | (size << 16) | glyph) + 1;
    for (std::uint32_t h = (key * 2654435761u) >> (32 - table_bits);; h = (h + 1) & (table_size - 1)) {
        Entry& e = table[h];
        if (e.key == key) return &e;
        if (e.key) continue;
        const Face& f = faces[slot];
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&f.info, static_cast<int>(glyph), &adv, &lsb);
        e = {};
        e.key = key;
        e.advance = static_cast<std::int16_t>(static_cast<float>(adv) * f.scale[size] + 0.5f);
        ++table_count;
        return &e;
    }
}

int baseline(unsigned size) {
    static int cached[4] = {0, 0, 0, 0};
    if (!cached[size]) {
        const auto* g = atlas_glyph(size, 'H');
        static constexpr int fallback[4] = {22, 28, 35, 62};
        cached[size] = g ? g->top + static_cast<int>(g->height) : fallback[size];
    }
    return cached[size];
}

bool is_kana(std::uint32_t c) { return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x31F0 && c <= 0x31FF) || (c >= 0xFF66 && c <= 0xFF9F); }
bool is_hangul(std::uint32_t c) { return (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F); }
// Caractères invisibles (contrôles, marques de direction, espaces de largeur nulle) : ignorés.
bool invisible(std::uint32_t c) {
    return c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0x200B && c <= 0x200F) || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069) ||
           c == 0xFEFF;
}

// Police de secours : latin étendu/grec/cyrillique, arabe, puis CJK dans l'ordre de préférence.
bool resolve(std::uint32_t cp, unsigned size, int pref, std::uint16_t& slot, std::uint16_t& glyph) {
    static constexpr Slot cjk[4][4] = {{cjk_sc, cjk_tc, cjk_jp, cjk_kr}, {cjk_tc, cjk_sc, cjk_jp, cjk_kr},
                                       {cjk_jp, cjk_sc, cjk_tc, cjk_kr}, {cjk_kr, cjk_sc, cjk_tc, cjk_jp}};
    const std::uint32_t key = (cp | (static_cast<std::uint32_t>(pref) << 21) | ((size >= 2 ? 1u : 0u) << 23)) | 0x80000000u;
    Resolved* r = resolved ? &resolved[(cp * 2654435761u ^ static_cast<std::uint32_t>(pref) * 40503u ^ (size >= 2 ? 77u : 0u)) % resolve_size] : nullptr;
    if (r && r->key == key) { slot = r->slot; glyph = r->glyph; return slot != 0xFFFF; }
    const Slot order[6] = {size >= 2 ? inter_semibold : inter_medium, arabic, cjk[pref][0], cjk[pref][1], cjk[pref][2], cjk[pref][3]};
    slot = 0xFFFF;
    glyph = 0;
    for (Slot s : order) {
        if (!faces[s].ok) continue;
        if (const int g = stbtt_FindGlyphIndex(&faces[s].info, static_cast<int>(cp)); g > 0 && g < 65536) {
            slot = static_cast<std::uint16_t>(s);
            glyph = static_cast<std::uint16_t>(g);
            break;
        }
    }
    if (r) *r = {key, slot, glyph};
    return slot != 0xFFFF;
}

// ---- Arabe : formes contextuelles (Formes de présentation A/B) ----
struct Forms { std::uint16_t base, iso, fin, ini, med; };
constexpr Forms arabic_forms[] = {
    {0x0621, 0xFE80, 0, 0, 0}, {0x0622, 0xFE81, 0xFE82, 0, 0}, {0x0623, 0xFE83, 0xFE84, 0, 0}, {0x0624, 0xFE85, 0xFE86, 0, 0},
    {0x0625, 0xFE87, 0xFE88, 0, 0}, {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}, {0x0627, 0xFE8D, 0xFE8E, 0, 0},
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92}, {0x0629, 0xFE93, 0xFE94, 0, 0}, {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},
    {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C}, {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0}, {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}, {0x062F, 0xFEA9, 0xFEAA, 0, 0}, {0x0630, 0xFEAB, 0xFEAC, 0, 0},
    {0x0631, 0xFEAD, 0xFEAE, 0, 0}, {0x0632, 0xFEAF, 0xFEB0, 0, 0}, {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8}, {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC}, {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},
    {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4}, {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8}, {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0}, {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4}, {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}, {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}, {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
    {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8}, {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}, {0x0648, 0xFEED, 0xFEEE, 0, 0},
    {0x0649, 0xFEEF, 0xFEF0, 0, 0}, {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
    // Persan et ourdou courants.
    {0x0671, 0xFB50, 0xFB51, 0, 0}, {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59}, {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D},
    {0x0698, 0xFB8A, 0xFB8B, 0, 0}, {0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91}, {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95},
    {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},
};
const Forms* forms(std::uint32_t c) {
    if (c < 0x0621 || c > 0x06CC) return nullptr;
    for (const auto& f : arabic_forms) if (f.base == c) return &f;
    return nullptr;
}
bool transparent(std::uint32_t c) {
    return (c >= 0x610 && c <= 0x61A) || (c >= 0x64B && c <= 0x65F) || c == 0x670 || (c >= 0x6D6 && c <= 0x6DC) || (c >= 0x6DF && c <= 0x6E4) ||
           c == 0x6E7 || c == 0x6E8 || (c >= 0x6EA && c <= 0x6ED);
}
// Type de jonction : 'D' double, 'R' à droite seulement, 'C' provoque la jonction, 'T' transparent, 'U' aucune.
char joining(std::uint32_t c) {
    if (const Forms* f = forms(c)) return f->ini ? 'D' : f->fin ? 'R' : 'U';
    if (c == 0x640 || c == 0x200D) return 'C';
    if (transparent(c)) return 'T';
    return 'U';
}

// ---- Bidi (sous-ensemble de l'UAX #9 suffisant pour des libellés) ----
enum Cls : std::uint8_t { L, R, EN, AN, CS, ON, NSM, WS };
Cls bidi_class(std::uint32_t c) {
    if (c >= '0' && c <= '9') return EN;
    if (c == ' ' || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200A)) return WS;
    if (c == ',' || c == '.' || c == ':' || c == '/' || c == 0xA0) return CS;
    if (c < 0x80) return ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') ? L : ON;
    if (c >= 0x660 && c <= 0x669) return AN;
    if (c >= 0x6F0 && c <= 0x6F9) return EN;
    if (transparent(c) || (c >= 0x300 && c <= 0x36F)) return NSM;
    if ((c >= 0x590 && c <= 0x8FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return R;
    if (c < 0xC0) return (c == 0xAA || c == 0xB5 || c == 0xBA) ? L : ON;
    if (c == 0xD7 || c == 0xF7) return ON;
    if ((c >= 0x2010 && c <= 0x2BFF) || (c >= 0x3001 && c <= 0x3003) || (c >= 0x3008 && c <= 0x3020) || (c >= 0xFE30 && c <= 0xFE4F) ||
        (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65))
        return ON;
    return L;
}
std::uint32_t mirrored(std::uint32_t c) {
    switch (c) {
    case '(': return ')'; case ')': return '(';
    case '[': return ']'; case ']': return '[';
    case '{': return '}'; case '}': return '{';
    case '<': return '>'; case '>': return '<';
    case 0xAB: return 0xBB; case 0xBB: return 0xAB;
    case 0x2039: return 0x203A; case 0x203A: return 0x2039;
    default: return c;
    }
}

std::uint32_t next_rune(std::string_view s, std::size_t& i) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::uint32_t v = c;
    unsigned n = 1;
    if (c >= 0xF0) { v = c & 7; n = 4; } else if (c >= 0xE0) { v = c & 15; n = 3; } else if (c >= 0xC0) { v = c & 31; n = 2; }
    for (unsigned k = 1; k < n && i + k < s.size(); ++k) v = (v << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
    i += n;
    return v;
}

}  // namespace

const pbck::assets::Glyph* atlas_glyph(unsigned size, unsigned code) {
    // Index direct (premier glyphe trouvé, comme la recherche linéaire d'origine).
    constexpr unsigned span = 0x2200;
    static std::int16_t index[4][span];
    static bool ready = false;
    if (!ready) {
        for (auto& row : index) for (auto& v : row) v = -1;
        for (std::size_t i = 0; i < std::size(pbck::assets::glyphs); ++i) {
            const auto& g = pbck::assets::glyphs[i];
            if (g.font < 4 && g.code < span && index[g.font][g.code] < 0) index[g.font][g.code] = static_cast<std::int16_t>(i);
        }
        ready = true;
    }
    if (size < 4 && code < span) { const int i = index[size][code]; return i < 0 ? nullptr : &pbck::assets::glyphs[i]; }
    for (const auto& g : pbck::assets::glyphs) if (g.font == size && g.code == code) return &g;
    return nullptr;
}

bool add(Slot s, const unsigned char* data, std::size_t size) {
    if (s >= slot_count || !data || size < 12) return false;
    Face& f = faces[s];
    f.ok = stbtt_InitFont(&f.info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
    if (f.ok) for (int i = 0; i < 4; ++i) f.scale[i] = stbtt_ScaleForMappingEmToPixels(&f.info, em_px[i]);
    if (resolved) std::memset(resolved, 0, resolve_size * sizeof(Resolved));
    return f.ok;
}
bool available(Slot s) { return s < slot_count && faces[s].ok; }

void set_cache(unsigned char* mem, std::size_t bytes) {
    const std::size_t fixed = scratch_bytes + table_size * sizeof(Entry) + resolve_size * sizeof(Resolved);
    if (!mem || bytes < fixed + static_cast<std::size_t>(area_w) * 256) { table = nullptr; resolved = nullptr; area = nullptr; return; }
    scratch = mem;
    scratch_cap = scratch_bytes;
    table = reinterpret_cast<Entry*>(mem + scratch_bytes);
    resolved = reinterpret_cast<Resolved*>(mem + scratch_bytes + table_size * sizeof(Entry));
    std::memset(resolved, 0, resolve_size * sizeof(Resolved));
    area = mem + fixed;
    area_h = static_cast<int>(std::min<std::size_t>((bytes - fixed) / area_w, 65535));
    flush();
}

void set_language(std::string_view t) {
    auto starts = [&](std::string_view p) { return t.size() >= p.size() && t.substr(0, p.size()) == p; };
    lang_pref = starts("ja") ? 2 : starts("ko") ? 3
              : (starts("zh-Hant") || starts("zh-TW") || starts("zh-HK") || starts("zh-MO") || starts("zh_TW") || starts("zh_HK")) ? 1 : 0;
}
void set_rtl(bool r) { rtl_default = r; }
bool rtl() { return rtl_default; }

bool simple(std::string_view s, unsigned size) {
    bool letter = false;
    for (std::size_t i = 0; i < s.size();) {
        const std::uint32_t c = next_rune(s, i);
        if (c < 0x80) { letter |= (c | 0x20) >= 'a' && (c | 0x20) <= 'z'; continue; }  // ASCII : atlas (ou ignoré, comme avant)
        if (!atlas_glyph(size, c) && !invisible(c)) return false;
        letter |= bidi_class(c) == L;
    }
    // Interface de droite à gauche : un texte sans lettre (« 12:30 — (3/5) ») passe par le bidi.
    return letter || !rtl_default;
}

int prepare(std::string_view s, unsigned size, Item* out, int cap) {
    static std::uint32_t cps[1024];
    int n = 0;
    bool kana = false, hangul = false;
    for (std::size_t i = 0; i < s.size() && n < static_cast<int>(std::size(cps));) {
        const std::uint32_t c = next_rune(s, i);
        kana |= is_kana(c);
        hangul |= is_hangul(c);
        cps[n++] = c;
    }
    const int pref = kana ? 2 : hangul ? 3 : lang_pref;
    int count = 0;
    auto push = [&](std::uint32_t c) {
        if (count >= cap || invisible(c)) return;
        Item it{c, atlas_glyph(size, c), 0xFFFF, 0, 0, 0};
        if (it.atlas) it.advance = static_cast<std::int16_t>(it.atlas->advance);
        else if (resolve(c, size, pref, it.slot, it.glyph)) {
            if (Entry* e = entry(it.slot, it.glyph, size)) it.advance = e->advance;
            else return;
        } else return;  // ni atlas ni police : ignoré, comme avant
        out[count++] = it;
    };
    for (int i = 0; i < n; ++i) {
        const std::uint32_t c = cps[i];
        const char t = joining(c);
        if (t != 'D' && t != 'R') { push(c); continue; }
        int p = i - 1, q = i + 1;
        while (p >= 0 && joining(cps[p]) == 'T') --p;
        while (q < n && joining(cps[q]) == 'T') ++q;
        const char tp = p >= 0 ? joining(cps[p]) : 'U', tq = q < n ? joining(cps[q]) : 'U';
        const bool join_prev = tp == 'D' || tp == 'C';
        // Lam + alif : une seule ligature (l'alif est absorbé).
        if (c == 0x644 && q < n && (cps[q] == 0x622 || cps[q] == 0x623 || cps[q] == 0x625 || cps[q] == 0x627)) {
            const std::uint32_t lig = cps[q] == 0x622 ? 0xFEF5 : cps[q] == 0x623 ? 0xFEF7 : cps[q] == 0x625 ? 0xFEF9 : 0xFEFB;
            push(lig + (join_prev ? 1 : 0));
            for (int k = i + 1; k < q; ++k) push(cps[k]);  // signes diacritiques entre les deux
            i = q;
            continue;
        }
        const bool join_next = t == 'D' && (tq == 'D' || tq == 'R' || tq == 'C');
        const Forms* f = forms(c);
        std::uint32_t g = join_prev && join_next ? f->med : join_prev ? f->fin : join_next ? f->ini : f->iso;
        if (!g) g = join_prev && f->fin ? f->fin : f->iso;
        push(g);
    }
    return count;
}

void reorder(Item* it, int n, unsigned size, int* order) {
    static Cls cls[1024];
    if (n > static_cast<int>(std::size(cls))) n = static_cast<int>(std::size(cls));
    // Sens du paragraphe : première lettre forte (P2/P3), sinon le réglage de l'interface.
    int base = rtl_default ? 1 : 0;
    for (int i = 0; i < n; ++i) {
        const Cls c = bidi_class(it[i].cp);
        if (c == L) { base = 0; break; }
        if (c == R) { base = 1; break; }
    }
    bool any_rtl = base == 1;
    for (int i = 0; i < n; ++i) {
        cls[i] = bidi_class(it[i].cp);
        any_rtl |= cls[i] == R || cls[i] == AN;
    }
    for (int i = 0; i < n; ++i) order[i] = i;
    if (!any_rtl) { for (int i = 0; i < n; ++i) it[i].level = 0; return; }
    const Cls sos = base ? R : L;
    // W1 : marques non espaçantes = classe précédente.
    for (int i = 0; i < n; ++i) if (cls[i] == NSM) cls[i] = i ? cls[i - 1] : sos;
    // W2/W7 : chiffres européens après de l'arabe → AN ; après du latin (ou en début de paragraphe LTR) → L.
    Cls strong = base ? ON : L;
    for (int i = 0; i < n; ++i) {
        if (cls[i] == L || cls[i] == R) strong = cls[i];
        else if (cls[i] == EN) cls[i] = strong == R ? AN : strong == L ? L : EN;
    }
    // W4 : séparateur entre deux nombres du même type.
    for (int i = 1; i + 1 < n; ++i)
        if (cls[i] == CS && cls[i - 1] == cls[i + 1] && (cls[i - 1] == EN || cls[i - 1] == AN)) cls[i] = cls[i - 1];
    auto dir = [](Cls c) { return c == L ? 0 : (c == R || c == EN || c == AN) ? 1 : -1; };
    // N0 : une paire de parenthèses prend un seul sens (BD16 simplifié, 63 niveaux d'imbrication).
    {
        int open_at[63], depth = 0;
        for (int i = 0; i < n; ++i) {
            const std::uint32_t c = it[i].cp;
            if (c == '(' || c == '[' || c == '{') {
                if (depth == 63) break;
                open_at[depth++] = i;
                continue;
            }
            if (c != ')' && c != ']' && c != '}') continue;
            const std::uint32_t want = c == ')' ? '(' : c == ']' ? '[' : '{';
            int k = depth - 1;
            while (k >= 0 && it[open_at[k]].cp != want) --k;
            if (k < 0) continue;
            const int o = open_at[k];
            depth = k;
            bool same = false, opposite = false;
            for (int j = o + 1; j < i; ++j) {
                const int d = dir(cls[j]);
                same |= d == base;
                opposite |= d >= 0 && d != base;
            }
            if (!same && !opposite) continue;
            int d = base;
            if (!same) {  // seulement le sens opposé dedans : on le garde si le contexte d'avant l'a aussi
                int before = base;
                for (int j = o - 1; j >= 0; --j) if (dir(cls[j]) >= 0) { before = dir(cls[j]); break; }
                d = before;
            }
            cls[o] = cls[i] = d ? R : L;
        }
    }
    // N1/N2 : neutres entre deux sens identiques (nombres comptés RTL), sinon le sens du paragraphe.
    for (int i = 0; i < n;) {
        if (dir(cls[i]) >= 0) { ++i; continue; }
        int j = i;
        while (j < n && dir(cls[j]) < 0) ++j;
        const int before = i ? dir(cls[i - 1]) : base, after = j < n ? dir(cls[j]) : base;
        const Cls res = before == after ? (before ? R : L) : (base ? R : L);
        for (int k = i; k < j; ++k) cls[k] = res;
        i = j;
    }
    // I1/I2 : niveaux.
    for (int i = 0; i < n; ++i) {
        const Cls c = cls[i];
        it[i].level = static_cast<std::uint8_t>(base == 0 ? (c == L ? 0 : c == R ? 1 : 2) : (c == R ? 1 : 2));
    }
    // L1 : espaces de fin au niveau du paragraphe.
    for (int i = n - 1; i >= 0 && bidi_class(it[i].cp) == WS; --i) it[i].level = static_cast<std::uint8_t>(base);
    // L2 : inversion des suites, du niveau le plus haut au plus bas niveau impair.
    int hi = 0, lo_odd = 255;
    for (int i = 0; i < n; ++i) {
        hi = it[i].level > hi ? it[i].level : hi;
        if (it[i].level & 1) lo_odd = it[i].level < lo_odd ? it[i].level : lo_odd;
    }
    if (lo_odd == 255) lo_odd = hi + 1;
    for (int lvl = hi; lvl >= lo_odd; --lvl)
        for (int i = 0; i < n;) {
            if (it[order[i]].level < lvl) { ++i; continue; }
            int j = i;
            while (j < n && it[order[j]].level >= lvl) ++j;
            for (int a = i, b = j - 1; a < b; ++a, --b) { const int t = order[a]; order[a] = order[b]; order[b] = t; }
            i = j;
        }
    // L4 : parenthèses et guillemets en miroir dans les passages RTL.
    for (int i = 0; i < n; ++i)
        if (it[i].level & 1)
            if (const std::uint32_t m = mirrored(it[i].cp); m != it[i].cp)
                if (const auto* g = atlas_glyph(size, m)) { it[i].cp = m; it[i].atlas = g; it[i].advance = static_cast<std::int16_t>(g->advance); }
}

bool bitmap(unsigned slot, unsigned glyph, unsigned size, Bitmap& out) {
    if (slot >= slot_count || !faces[slot].ok || size > 3 || !area) return false;
    Entry* e = entry(slot, glyph, size);
    if (!e) return false;
    if (e->state == 0) {
        const Face& f = faces[slot];
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&f.info, static_cast<int>(glyph), f.scale[size], f.scale[size], &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0 || w > area_w || h > area_h || w > 4096 || h > 4096) { e->state = 2; return false; }
        if (shelf_x + w > area_w) { shelf_x = 0; shelf_y += shelf_h + 1; shelf_h = 0; }
        if (shelf_y + h > area_h) {
            // Zone pleine : on repart de zéro (les glyphes déjà dessinés ne sont plus nécessaires).
            const std::int16_t adv = e->advance;
            flush();
            e = entry(slot, glyph, size);
            e->advance = adv;
        }
        e->x = static_cast<std::uint16_t>(shelf_x);
        e->y = static_cast<std::uint16_t>(shelf_y);
        e->w = static_cast<std::uint16_t>(w);
        e->h = static_cast<std::uint16_t>(h);
        e->left = static_cast<std::int16_t>(x0);
        e->top = static_cast<std::int16_t>(y0);
        shelf_x += w + 1;
        shelf_h = h > shelf_h ? h : shelf_h;
        std::uint8_t* dst = area + static_cast<std::size_t>(e->y) * area_w + e->x;
        for (int r = 0; r < h; ++r) std::memset(dst + static_cast<std::size_t>(r) * area_w, 0, static_cast<std::size_t>(w));
        scratch_used = 0;
        stbtt_MakeGlyphBitmap(&f.info, dst, w, h, area_w, f.scale[size], f.scale[size], static_cast<int>(glyph));
        e->state = 1;
    }
    if (e->state != 1) return false;
    out = {area + static_cast<std::size_t>(e->y) * area_w + e->x, e->w, e->h, area_w, e->left, baseline(size) + e->top};
    return true;
}

}  // namespace app::fonts
