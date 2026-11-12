// Conversion d'images pour le web : TGA non compressé (jaquettes ProsperoEden/Eden) → PNG.
#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace pbc::image {
// TGA type 2 (vraies couleurs, 24 ou 32 bits, non compressé) → PNG RGBA. Faux si format non pris en charge.
bool tga_to_png(std::string_view tga, std::string& png);
// Pixels RGBA (octets R,G,B,A en mémoire), ligne par ligne → PNG (compressé avec zlib).
void encode_png(const void* rgba, unsigned width, unsigned height, std::string& png);
// Décode PNG, JPEG ou TGA en RGBA. Faux si l'image est illisible.
bool decode(std::string_view data, std::vector<std::uint32_t>& rgba, int& width, int& height);
// Réduit pour tenir dans max×max en gardant les proportions (jamais d'agrandissement).
void fit(const std::vector<std::uint32_t>& src, int w, int h, int max, std::vector<std::uint32_t>& dst, int& dw, int& dh);
}
