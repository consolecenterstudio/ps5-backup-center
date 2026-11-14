// Google Drive et OneDrive : même interface que le FTP, le WebDAV et le S3. Réglages : FtpConfig — password = jeton
// de connexion (store/cloud.hpp), bucket = compte (e-mail), root = dossier de départ. L'appli ne voit que son dossier :
// OneDrive « Applications/PS5 Backup Center », Google Drive les fichiers qu'elle a créés (« PBC Backup »).
#pragma once
#include <memory>
#include <string>

#include "core/json.hpp"
#include "store/ftp.hpp"

namespace pbc::store {

std::unique_ptr<Store> onedrive(const FtpConfig& cfg, std::string& err);
std::unique_ptr<Store> gdrive(const FtpConfig& cfg, std::string& err);
std::unique_ptr<Store> dropbox(const FtpConfig& cfg, std::string& err);
bool dropbox_about(const std::string& refresh, Json& out, std::string& err);

// Le compte et sa place : {"account", "free", "total"} (free/total à 0 si inconnus). type : « gdrive », « onedrive » ou « dropbox ».
bool drive_about(const std::string& type, const std::string& refresh, Json& out, std::string& err);

}  // namespace pbc::store
