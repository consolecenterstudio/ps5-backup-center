// Dessin logiciel 1920×1080 : rectangles arrondis avec transparence, images mises à l'échelle,
// texte avec l'atlas de police de l'app. Pixels 0xAABBGGRR (octets R,G,B,A), comme la sortie vidéo.
#pragma once
#include <cstdint>
#include <string_view>

namespace app {

inline constexpr int screen_w = 1920, screen_h = 1080;

constexpr std::uint32_t rgb(unsigned r, unsigned g, unsigned b, unsigned a = 255) { return (a << 24) | (b << 16) | (g << 8) | r; }

// Couleurs de référence : le thème « Nuit » (bleu nuit, accents bleu clair, comme le web). Le code dessine avec
// elles ; la surface les traduit au dessin dans le thème choisi (voir `theme::map`).
namespace color {
inline constexpr auto bg = rgb(6, 16, 29), bg2 = rgb(10, 23, 41), panel = rgb(15, 31, 53), panel2 = rgb(20, 40, 67),
                      line = rgb(31, 55, 87), text = rgb(232, 241, 251), muted = rgb(142, 164, 189), accent = rgb(124, 196, 255),
                      accent2 = rgb(61, 139, 255), ok = rgb(95, 211, 154), warn = rgb(240, 184, 103), bad = rgb(255, 123, 123),
                      rom = rgb(95, 211, 154), app = rgb(167, 123, 255), shadow = rgb(0, 0, 0, 140);
}

// Thèmes (Paramètres › Général) : mêmes palettes que le web (engine/web/app.css, [data-theme=…]).
struct Theme {
    const char* id;
    const char* name;
    std::uint32_t bg, bg2, panel, panel2, line, text, muted, accent, accent2, ok, warn, bad, rom, app, shadow;
    std::uint32_t top, bottom;   // dégradé du fond, de haut en bas
    std::uint32_t glow1, glow2;  // halos du fond (alpha = intensité)
    std::uint32_t focus;         // halo autour de l'élément choisi
    std::uint32_t veil;          // voiles translucides (blanc sur fond sombre, encre sur fond clair)
};
namespace theme {
int count();
const Theme& at(int i);
int index();
const Theme& current();
void set_index(int i);
bool set(std::string_view id);  // faux si inconnu (le thème actuel reste)
// Traduit une couleur du thème Nuit dans le thème courant (identité pour Nuit). Appelé à chaque pixel :
// chemin rapide en ligne (thème Nuit, ou même couleur que la précédente).
namespace detail {
extern bool active;
extern std::uint32_t last_in, last_out;
std::uint32_t translate(std::uint32_t c);
}  // namespace detail
inline std::uint32_t map(std::uint32_t c) {
    if (!detail::active) return c;
    if (c == detail::last_in) return detail::last_out;
    return detail::translate(c);
}
}  // namespace theme

struct Image {
    int w = 0, h = 0;
    const std::uint32_t* px = nullptr;
    bool valid() const { return px && w > 0 && h > 0; }
};

// Atlas de police : 4 tailles (0 : 16 px, 1 : 21 px, 2 : 26 px, 3 : 47 px), latin courant.
// Tout autre caractère (latin étendu, grec, cyrillique, arabe, CJK) passe par fonts.hpp.
struct Font { Image atlas; };
enum FontSize : unsigned { small = 0, body = 1, large = 2, title = 3 };

class Surface {
public:
    explicit Surface(std::uint32_t* px) : px_(px) {}
    std::uint32_t* pixels() const { return px_; }

    void clear(std::uint32_t c);
    // Fond dégradé vertical bleu nuit avec un halo en haut à droite.
    void backdrop();
    void rect(int x, int y, int w, int h, std::uint32_t c, int radius = 0);
    void outline(int x, int y, int w, int h, std::uint32_t c, int radius, int thickness);
    void image(const Image& img, int x, int y, int w, int h, int radius = 0, unsigned alpha = 255);
    // Image agrandie en douceur (bilinéaire) : fond flou du carrousel.
    void image_soft(const Image& img, int x, int y, int w, int h, unsigned alpha = 255);
    // Voile dégradé de la couleur `c` : opaque à `from`, transparent à `to` (horizontal ou vertical).
    void fade(int x, int y, int w, int h, std::uint32_t c, bool horizontal, bool opaque_first);
    // Texte : renvoie la largeur dessinée. `max_w` > 0 tronque avec « … ».
    int text(const Font& f, int x, int y, std::string_view s, FontSize size, std::uint32_t c, int max_w = 0);
    static int measure(std::string_view s, FontSize size);
    static int line_height(FontSize size);
    // Sens par défaut des paragraphes sans lettre forte (interface en arabe). La mise en page ne
    // s'inverse pas encore : seul l'ordre des lettres suit le sens du texte (premier caractère fort).
    static void set_rtl(bool rtl);
    static bool rtl();
    void push_clip(int x, int y, int w, int h);
    void pop_clip();
    // Couleurs exactes (sans traduction par le thème) : aperçus des thèmes dans les Paramètres.
    void exact_colors(bool on) { exact_ = on; }

private:
    void blend(int x, int y, std::uint32_t c, unsigned coverage) { blend_raw(x, y, exact_ ? c : theme::map(c), coverage); }
    void blend_raw(int x, int y, std::uint32_t c, unsigned coverage);
    bool exact_ = false;
    int text_complex(const Font& f, int x, int y, std::string_view s, FontSize size, std::uint32_t c, int max_w);
    std::uint32_t* px_;
    int cx0_ = 0, cy0_ = 0, cx1_ = screen_w, cy1_ = screen_h;
    int saved_[8][4] = {};
    int depth_ = 0;
};

// Mise à l'échelle bilinéaire (jaquettes : décodage puis vignette fixe).
// `anchor_y` : où recadrer en hauteur (0 = haut, 0,5 = milieu).
void scale_rgba(const std::uint32_t* src, int sw, int sh, std::uint32_t* dst, int dw, int dh, bool cover_crop, float anchor_y = 0.5f);

}  // namespace app
