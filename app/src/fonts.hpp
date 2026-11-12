// Texte de toutes les langues : l'atlas Inter reste la voie rapide du latin courant ; le reste
// (latin étendu, grec, cyrillique, arabe, chinois, japonais, coréen) vient de polices TrueType
// rasterisées à la demande (stb_truetype) dans un cache de glyphes. L'arabe est mis en forme
// (formes contextuelles, ligatures lam-alif) et les passages de droite à gauche sont réordonnés.
// Tout se fait sur le fil de dessin (pas de verrou) ; la mémoire est fournie par l'appelant.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pbck::assets { struct Glyph; }

namespace app::fonts {

enum Slot : unsigned { inter_medium, inter_semibold, arabic, cjk_sc, cjk_tc, cjk_jp, cjk_kr, slot_count };
// Fichiers du paquet (assets/fonts/…), générés par build-system/make_fonts.py.
inline constexpr const char* file_names[slot_count] = {"inter-ext-medium.ttf", "inter-ext-semibold.ttf", "arabic.ttf",
                                                       "cjk-sc.ttf", "cjk-tc.ttf", "cjk-jp.ttf", "cjk-kr.ttf"};
// Mémoire de travail conseillée : cache de glyphes + tables + brouillon de rasterisation.
inline constexpr std::size_t cache_bytes = 4u << 20;

// Les données restent à l'appelant (lues une fois au démarrage) ; faux si le fichier est illisible.
bool add(Slot s, const unsigned char* data, std::size_t size);
bool available(Slot s);
void set_cache(unsigned char* mem, std::size_t bytes);
// Langue de l'interface (« ja », « zh-Hant », « ko »…) : choisit la police des idéogrammes han.
void set_language(std::string_view tag);
// Sens par défaut d'un paragraphe sans lettre forte (chiffres seuls…) ; prévu pour l'interface en arabe.
void set_rtl(bool rtl);
bool rtl();

// ---- Interne au dessin (gfx.cpp) ----
const pbck::assets::Glyph* atlas_glyph(unsigned size, unsigned code);
// Vrai si la chaîne ne demande que l'atlas, sans mise en forme : dessin d'origine, au pixel près.
bool simple(std::string_view s, unsigned size);

struct Item {
    std::uint32_t cp;
    const pbck::assets::Glyph* atlas;  // sinon police TrueType (slot, glyph)
    std::uint16_t slot, glyph;
    std::int16_t advance;
    std::uint8_t level;  // niveau bidi (impair = droite à gauche)
};
// UTF-8 → suite logique mise en forme et résolue (atlas ou police de secours). `cap` éléments au plus.
int prepare(std::string_view s, unsigned size, Item* out, int cap);
// Niveaux bidi puis ordre visuel (indices dans `order`), parenthèses inversées dans les passages RTL.
void reorder(Item* items, int n, unsigned size, int* order);

struct Bitmap { const std::uint8_t* px; int w, h, stride, left, top; };
// Glyphe d'une police de secours (rasterisé au premier usage). `top` : depuis le haut de la ligne.
bool bitmap(unsigned slot, unsigned glyph, unsigned size, Bitmap& out);

}  // namespace app::fonts
