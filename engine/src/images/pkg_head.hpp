// Ce qu'un .pkg dit de lui-même dans son en-tête (quelques centaines d'octets, jamais le reste) : un jeu, une mise à
// jour ou un contenu additionnel ; PS4 ou PS5 ; fake PKG (signé « debug ») ou paquet officiel de Sony.
// Règles reprises de nos sources tierces (engine/third_party/fpkg/ps5upload-pkg : kind.rs et lib.rs ;
// engine/third_party/kraken-mount/crates/kraken-mount-pkg/src/pkg.rs pour ce que Kraken accepte).
#pragma once

#include <cstdint>
#include <string>

namespace pbc {
namespace store { class Store; }
class Json;

namespace pkghead {

struct Head {
    bool ok = false;           // en-tête lu et reconnu
    std::string container;     // "fih" (PS5), "lih" (mise à jour PS5), "cnt" (PS4, ou métadonnées seules)
    std::string kind;          // "game", "patch", "dlc" ou "" (inconnu)
    std::string platform;      // "ps4", "ps5" ou ""
    int fake = -1;             // 1 : fake PKG (debug) ; 0 : officiel (signé Sony) ; -1 : l'en-tête ne le dit pas (PS4)
    std::string content_id;    // EP0002-PPSA01234_00-…
    std::string title_id() const;
};

// Lit l'en-tête de `path` sur `st` (au plus deux petites lectures). Faux si illisible ou si ce n'est pas un .pkg.
bool read(store::Store& st, const std::string& path, Head& out);
// Depuis les octets (pour les tests) : `head` = début du fichier, `cnt` = le conteneur CNT (FIH : à son décalage).
bool parse(const std::string& head, Head& out, std::uint64_t& cnt_offset);
void parse_cnt(const std::string& cnt, Head& out);

// Kraken (le nôtre ou Kraken Mount) ne lance qu'un jeu PS5 en fake PKG complet ; sinon la raison (texte traduit à l'écran).
std::string kraken_refusal(const Head& h);

Json to_json(const Head& h);
Head from_json(const Json& j);

}  // namespace pkghead
}  // namespace pbc
