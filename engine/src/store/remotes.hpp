// Appareils réseau enregistrés (FTP, SMB, WebDAV, Switch) : nom, réglages, mot de passe gardé
// uniquement sur la console (<état>/remotes.json). Les backups vont dans <racine>/PBC Backup.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "store/ftp.hpp"

namespace pbc::store {

// Ouvre un appareil réseau selon son type : « ftp », « smb », « webdav » ou « switch » (FTP ou SMB selon cfg.proto).
std::unique_ptr<Store> connect(const std::string& type, const FtpConfig& cfg, std::string& err);

struct Remote {
    std::string id, type = "ftp", name;
    std::string use;  // à quoi il sert : vide (Sauvegardes et Synchro), « backups » ou « sync » seulement
    FtpConfig ftp;
    bool for_backups() const { return use != "sync"; }
    bool for_sync() const { return use != "backups"; }
    Json to_json(bool with_secret) const;
    static Remote from_json(const Json&);
    std::string folder() const;   // dossier des backups sur l'appareil
    std::string address() const;  // ftp://hôte:port/racine (affichage)
    std::string tagged() const;   // nom + moyen, pour savoir qui est quoi : « NAS (FTP) », « NAS (WebDAV) »
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
    // Le même NAS déjà enregistré par un autre protocole (même adresse, même dossier) : son id, sinon vide. Ses
    // sauvegardes sont alors les mêmes fichiers : comptées une seule fois (copies, résumé, synchro).
    // purpose « backups » ou « sync » : seuls les appareils qui servent à cela comptent (vide : tous).
    std::string same_as(const std::string& id, const std::string& purpose = "") const;
    // Un autre appareil fait déjà la synchro dans ce même dossier du même NAS : son nom (« NAS (WebDAV) »), sinon vide.
    std::string sync_taken(const Remote& r) const;
    std::unique_ptr<Store> open(const std::string& id, std::string& err) const;
    // Google Drive, OneDrive : le service a renouvelé le jeton de connexion d'un compte, on garde le nouveau.
    void rotate_secret(const std::string& old_secret, const std::string& new_secret);
    // Connexion + écriture/lecture/suppression d'un témoin dans le dossier des backups.
    static bool test(const Remote& r, std::string& err);

private:
    bool save() const;
    std::string path_;
    mutable std::mutex mu_;
    std::vector<Remote> list_;
};

}  // namespace pbc::store
