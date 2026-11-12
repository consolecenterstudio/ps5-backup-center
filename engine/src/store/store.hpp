// Où l'on écrit un backup : disque de la console (interne, USB) ou appareil réseau (FTP pour
// commencer ; SMB, WebDAV, cloud ensuite). Le moteur de sauvegarde ne connaît que cette interface.
// Les chemins sont ceux de l'appareil : chemin console pour le disque local, chemin distant sinon.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/fs.hpp"

namespace pbc::store {

// Écriture d'un fichier : dans <chemin>.part, rendu visible seulement par commit().
struct Writer {
    virtual ~Writer() = default;
    virtual bool write(const char* p, std::size_t n) = 0;
    virtual bool commit(std::string& err) = 0;
    virtual void abort() = 0;
};

struct Reader {
    virtual ~Reader() = default;
    virtual long read(char* p, std::size_t n) = 0;  // 0 : fin ; <0 : erreur
};

class Store {
public:
    virtual ~Store() = default;
    virtual std::string label() const = 0;
    virtual bool mkdirs(const std::string& dir) = 0;
    virtual std::unique_ptr<Writer> create(const std::string& path, std::string& err) = 0;
    virtual std::unique_ptr<Reader> open(const std::string& path, std::string& err) = 0;
    // Lecture à partir d'un décalage (fin d'une archive, reprise) ; par défaut : lecture et saut.
    virtual std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) {
        auto r = open(path, err);
        char buf[65536];
        while (r && offset) { const long n = r->read(buf, offset < sizeof buf ? static_cast<std::size_t>(offset) : sizeof buf); if (n <= 0) return nullptr; offset -= static_cast<std::uint64_t>(n); }
        return r;
    }
    virtual bool read_small(const std::string& path, std::string& out, std::uint64_t limit = 16u << 20) = 0;
    virtual bool write_small(const std::string& path, std::string_view data) = 0;  // atomique
    virtual bool stat(const std::string& path, fs::Entry& out) = 0;
    virtual bool list(const std::string& dir, std::vector<fs::Entry>& out) = 0;
    virtual bool space(const std::string& path, fs::Space& out) = 0;   // faux si l'appareil ne le dit pas
    virtual bool remove(const std::string& path) = 0;
    // Renommer / déplacer sur le même appareil (sans copie). Faux si l'appareil ne sait pas le faire.
    virtual bool rename(const std::string&, const std::string&) { return false; }
};

// Disque de la console : chemins console (fs::host appliqué en interne).
std::unique_ptr<Store> local();

}  // namespace pbc::store
