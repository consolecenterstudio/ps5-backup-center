// Lecture d'archives ZIP (stockées ou « deflate ») : sauvegardes JKSV de la Switch, imports.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pbc::zip {

struct Entry {
    std::string name;
    std::uint16_t method = 0;          // 0 stocké, 8 deflate
    std::uint32_t crc = 0;
    std::uint64_t packed = 0, size = 0, header = 0;
    bool dir() const { return !name.empty() && name.back() == '/'; }
};

// Liste des entrées (répertoire central). Faux si l'archive est illisible.
bool list(const std::string& host_path, std::vector<Entry>& out);
// Liste depuis la fin de l'archive seulement (`tail` = derniers octets, `size` = taille totale) :
// suffit pour connaître noms, tailles et CRC sans télécharger le contenu.
bool list_tail(const std::string& tail, std::uint64_t size, std::vector<Entry>& out);
// Contenu décompressé d'une entrée, CRC vérifié.
bool read(const std::string& host_path, const Entry& e, std::string& out, std::string& err);

// Écriture d'une archive (fichiers stockés, sans compression : les sauvegardes sont petites et
// c'est le format le plus sûr à relire par JKSV). `files` : nom dans l'archive → contenu.
std::uint32_t crc32(const std::string& data);
std::string build(const std::vector<std::pair<std::string, std::string>>& files);

}  // namespace pbc::zip
