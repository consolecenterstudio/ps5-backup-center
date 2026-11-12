// Scanner unique : un seul parcours de chaque emplacement, des briques de
// reconnaissance branchées dessus, une seule liste d'éléments dédoublonnée.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/fs.hpp"
#include "model/model.hpp"

namespace pbc::scan {

// Ce qu'une brique voit d'un dossier : jamais d'accès disque à faire elle-même
// pour le parcours (elle peut lire un petit fichier de description qu'elle reconnaît).
struct DirView {
    std::string path;                      // chemin console
    const Place* place = nullptr;
    const std::vector<fs::Entry>* entries = nullptr;
    int depth = 0;
    bool has(std::string_view name, bool dir) const;
    const fs::Entry* find(std::string_view name) const;
};

enum class Verdict : std::uint8_t { descend, claimed };  // claimed : ne pas descendre dedans

class Recognizer {
public:
    virtual ~Recognizer() = default;
    virtual const char* name() const = 0;
    virtual Verdict visit(const DirView& dir, std::vector<Item>& out) = 0;
};

struct Root { std::string path; int max_depth = 4; };

struct Stats { unsigned dirs = 0, reused = 0, items = 0; std::int64_t ms = 0; };

class Scanner {
public:
    void add(std::unique_ptr<Recognizer> r) { recognizers_.push_back(std::move(r)); }
    // Racines par défaut d'un emplacement : interne → /data/homebrew et /user/app ; USB → sa racine.
    static std::vector<Root> default_roots(const Place& p);

    // Parcourt tous les emplacements et renvoie la liste dédoublonnée.
    std::vector<Item> run(const std::vector<Place>& places, Stats* stats = nullptr) { return run(places, {}, stats); }
    // `extra` : dossiers de jeux des émulateurs (revisités à chaque scan : leurs sauvegardes changent sans
    // que la date du dossier de jeux change).
    std::vector<Item> run(const std::vector<Place>& places, const std::vector<Root>& extra, Stats* stats = nullptr);
    // Cache des dossiers sur disque : après un redémarrage, seuls les dossiers modifiés sont relus.
    bool save_cache(const std::string& host_file) const;
    bool load_cache(const std::string& host_file);

private:
    struct Cached { std::int64_t mtime = 0; std::size_t count = 0; std::vector<Item> items; std::vector<std::string> children; bool claimed = false; };
    void walk(const Place& place, const std::string& path, int depth, int max_depth, std::vector<Item>& out, Stats& st, bool no_cache = false);
    std::vector<std::unique_ptr<Recognizer>> recognizers_;
    std::map<std::string, Cached> cache_;
};

// Fusion : même clé → un seul élément ; les applications qui l'utilisent sont réunies.
void merge(std::vector<Item>& items);

bool is_ignored_dir(std::string_view name);

}  // namespace pbc::scan
