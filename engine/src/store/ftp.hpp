// Connecteur FTP : NAS (Synology, QNAP…), PC, autre PS5 (ftpsrv), Switch (ftpd).
// Mode passif (EPSV, sinon PASV), binaire, une connexion de contrôle par magasin.
// FTPS explicite (AUTH TLS) : contrôle et données chiffrés, session TLS reprise pour les données.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "core/json.hpp"
#include "store/store.hpp"

namespace pbc::store {

struct FtpConfig {
    std::string host;
    std::uint16_t port = 21;
    std::string user = "anonymous", password;
    std::string domain;               // SMB : domaine / groupe de travail (souvent vide)
    std::string proto;                // Switch : « smb » pour la lire par SMB (sinon FTP, ftpd / sphaira)
    std::string root = "/";         // dossier de départ sur l'appareil
    std::string tls = "auto";
    std::string bucket, region;       // S3 : compartiment et région (user = clé d'accès, password = clé secrète)
    std::string service;              // S3 : « aws », « b2 » ou « other » (host = adresse du service)
    std::string pin;                  // WebDAV : empreinte du certificat maison du NAS (vide : certificat vérifié)
    // Rangement des backups : « pbc » (dossier « PBC Backup ») ou « platform » (rangement classique :
    // « PS5 backup/[PS5] Nom (ID)/saves/<date>/ », « Switch backup/… »).
    std::string layout = "pbc";       // « auto » : chiffré si le serveur le propose ; « on » : exigé ; « off »
    Json to_json(bool with_password) const;
    static FtpConfig from_json(const Json&);
};

// Connexion + identification ; nullptr et `err` lisible si l'appareil refuse.
std::unique_ptr<Store> ftp(const FtpConfig& cfg, std::string& err);

}  // namespace pbc::store
