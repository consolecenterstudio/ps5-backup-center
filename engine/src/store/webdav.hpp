// Connecteur WebDAV : NAS Synology (WebDAV Server, port 5006), QNAP, Nextcloud, clouds perso. Même interface que
// le FTP et le SMB. Réglages : FtpConfig (hôte, port, compte, mot de passe, dossier de départ, empreinte du
// certificat). https, sauf sur les ports http habituels (80, 5005).
#pragma once
#include <memory>
#include <string>

#include "store/ftp.hpp"

namespace pbc::store {

std::unique_ptr<Store> webdav(const FtpConfig& cfg, std::string& err);

// À l'enregistrement : si le certificat du NAS n'est pas reconnu par une autorité (certificat maison), il est
// accepté pour cet appareil et son empreinte est gardée dans cfg.pin ; sinon cfg.pin reste vide (vérifié).
bool webdav_trust(FtpConfig& cfg, std::string& err);

}  // namespace pbc::store
