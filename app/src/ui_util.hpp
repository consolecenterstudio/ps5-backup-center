// Petits outils partagés par les écrans de l'interface télé.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include "core/json.hpp"
#include "i18n.hpp"

namespace app::ui_util {
inline constexpr int margin = 96;
// Clé d'un remplaçant de jaquette : « title:PPSA… » (jeu absent ici) → icône officielle par son numéro (« tid:ps5:… »).
inline std::string cover_stand_in_key(const std::string& key) {
    if (key.rfind("title:", 0) != 0 || key.size() != 15) return key;
    const std::string tid = key.substr(6);
    return std::string(tid.rfind("CUSA", 0) == 0 ? "tid:ps4:" : "tid:ps5:") + tid;
}
inline std::string size_text(std::uint64_t b) {
    const char* u[] = {tr("o"), tr("Ko"), tr("Mo"), tr("Go"), tr("To")};
    double v = static_cast<double>(b);
    int i = 0;
    while (v >= 1000 && i < 4) { v /= 1000; ++i; }
    char out[48];
    if (i && v < 10) std::snprintf(out, sizeof out, "%.1f %s", v, u[i]); else std::snprintf(out, sizeof out, "%.0f %s", v, u[i]);
    for (char* p = out; *p; ++p) if (*p == '.') { *p = i18n::decimal(); break; }
    return out;
}
inline std::string duration_text(double s) {
    char b[48];
    if (s < 60) std::snprintf(b, sizeof b, "%.0f s", s);
    else if (s < 3600) std::snprintf(b, sizeof b, "%d min %02d s", static_cast<int>(s / 60), static_cast<int>(s) % 60);
    else std::snprintf(b, sizeof b, "%d h %02d min", static_cast<int>(s / 3600), static_cast<int>(s / 60) % 60);
    return b;
}
// Libellés écrits en français par le moteur (composants, stockage interne) : traduits à l'affichage.
inline std::string engine_label(const std::string& l) {
    static const std::string save = "Sauvegarde · ", image = "Jeu (image ";
    if (l.rfind(save, 0) == 0) return std::string(tr("Sauvegarde")) + " · " + l.substr(save.size());
    if (l.rfind(image, 0) == 0 && l.size() > image.size() && l.back() == ')') return trf("Jeu (image %s)", l.substr(image.size(), l.size() - image.size() - 1).c_str());
    for (const char* k : {N_("Jeu"), N_("Configuration"), N_("Interne"), N_("Sauvegarde (comptes Switch)")}) if (l == k) return tr(k);
    return l;
}
inline const char* part_name(const std::string& p) {
    if (p == "game") return tr("Jeu"); if (p == "update") return tr("Mise à jour"); if (p == "dlc") return tr("DLC"); if (p == "save") return tr("Sauvegarde");
    if (p == "state") return tr("États"); if (p == "config") return tr("Configuration"); if (p == "app") return tr("Application");
    return p.c_str();
}
inline const char* state_name(const std::string& s) {
    if (s == "queued") return tr("En attente"); if (s == "running") return tr("En cours"); if (s == "paused") return tr("En pause");
    if (s == "done") return tr("Terminée"); if (s == "failed") return tr("Échec"); if (s == "cancelled") return tr("Annulée");
    return s.c_str();
}
inline bool in_group(const std::string& st, int g) {
    switch (g) {
        case 0: return st == "queued" || st == "running" || st == "paused";
        case 1: return st == "done";
        case 2: return st == "failed";
        default: return st == "cancelled";
    }
}
inline std::string url_encode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') o += static_cast<char>(c);
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}
inline std::string json_str(const std::string& s) { return pbc::Json(s).dump(); }

}  // namespace app::ui_util
