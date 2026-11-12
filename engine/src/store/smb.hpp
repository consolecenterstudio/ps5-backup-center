// SPDX-License-Identifier: GPL-3.0-or-later
// Connecteur SMB 2 / 3 (libsmb2) : partages Windows, Synology, QNAP… Même interface que le FTP.
// Réglages : FtpConfig (hôte, port 445, compte, mot de passe, domaine, dossier de départ « /<partage>/… »).
#pragma once
#include <memory>
#include <string>

#include "store/ftp.hpp"

namespace pbc::store {

std::unique_ptr<Store> smb(const FtpConfig& cfg, std::string& err);

}  // namespace pbc::store
