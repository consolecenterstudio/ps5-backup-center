// Appareils réseau enregistrés (FTP pour l'instant) : nom, réglages, mot de passe gardé
// uniquement sur la console (<état>/remotes.json). Les backups vont dans <racine>/PBC Backup.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "store/ftp.hpp"

namespace pbc::store {

struct Remote {
    std::string id, type = "ftp", name;
    FtpConfig ftp;
    Json to_json(bool with_secret) const;
    static Remote from_json(const Json&);
    std::string folder() const;   // dossier des backups sur l'appareil
    std::string address() const;  // ftp://hôte:port/racine (affichage)
};

class Remotes {
public:
    explicit Remotes(std::string host_path) : path_(std::move(host_path)) {}
    void load();
    std::vector<Remote> list() const;
    bool find(const std::string& id, Remote& out) const;
    // Teste la connexion (écriture d'un fichier témoin) puis enregistre ; renvoie l'id.
    bool add(Remote r, std::string& id, std::string& err);
    // Modifie un appareil (mot de passe gardé s'il n'est pas redonné), après un test de connexion.
    bool update(const std::string& id, Remote r, std::string& err);
    bool remove(const std::string& id);
    std::unique_ptr<Store> open(const std::string& id, std::string& err) const;
    // Connexion + écriture/lecture/suppression d'un témoin dans le dossier des backups.
    static bool test(const Remote& r, std::string& err);

private:
    bool save() const;
    std::string path_;
    mutable std::mutex mu_;
    std::vector<Remote> list_;
};

}  // namespace pbc::store
