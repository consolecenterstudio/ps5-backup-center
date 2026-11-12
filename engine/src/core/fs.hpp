// Accès fichiers POSIX communs à la PS5 et à macOS. Aucun chemin absolu en dur :
// tout passe par host() pour que les tests puissent simuler la console.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pbc::fs {

// Racine hôte : "" sur la PS5, un dossier temporaire dans les tests.
void set_host_root(std::string root);
std::string host(std::string_view console_path);       // "/data/x" -> "<racine>/data/x"
std::string console_path(std::string_view host_path);  // inverse (pour l'affichage)

std::string join(std::string_view a, std::string_view b);
std::string_view basename(std::string_view p);
std::string_view dirname(std::string_view p);

struct Entry {
    std::string name;
    bool dir = false;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
};

bool exists(const std::string& p);
bool is_dir(const std::string& p);
bool stat(const std::string& p, Entry& out);
bool list(const std::string& dir, std::vector<Entry>& out);  // trié par nom, sans "." ni ".."
bool mkdirs(const std::string& dir);                        // crée les parents ; ok si existe
bool read(const std::string& p, std::string& out, std::uint64_t limit = 16u << 20);
// Écrit dans <p>.tmp-<pid>, fsync, renomme, puis fsync du dossier : jamais de fichier à moitié écrit.
bool write_atomic(const std::string& p, std::string_view data);
bool remove_tree(const std::string& p);

struct Space { std::uint64_t free = 0, total = 0; };
bool space(const std::string& p, Space& out);

int last_error();  // errno du dernier échec

}  // namespace pbc::fs
