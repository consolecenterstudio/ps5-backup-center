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
// MEGA : user = e-mail, password = mot de passe du compte (chiffrement sur la console, store/mega.cpp).
std::unique_ptr<Store> mega_store(const FtpConfig& cfg, std::string& err);
// Lien de partage MEGA (lecture seule, sans compte) : « https://mega.nz/folder/<h>#<clé> » ou « …/file/<h>#<clé> ».
struct MegaLink { std::string raw, handle, key, sub; bool folder = false; };
bool parse_mega_link(const std::string& url, MegaLink& out);
std::unique_ptr<Store> mega_share(const std::string& url, std::string& err);
// Ce que contient le lien : {"service":"mega","kind":"folder"|"file","name","files","dirs","bytes","link"}.
// « Partager… » : lien public vers un fichier ou un dossier du compte MEGA (`off` : retirer le lien).
bool mega_export(const FtpConfig& cfg, const std::string& path, bool off, std::string& link, std::string& err);
// Coffre de clés du compte MEGA (^!keys) : lecture seule, forme et tailles (diagnostic, aucune clé rendue).
bool mega_keys_info(const FtpConfig& cfg, Json& out, std::string& err);
void mega_share_forget(const std::string& url);  // oublie l'arbre gardé en mémoire
bool mega_share_probe(const std::string& url, Json& out, std::string& err);
// Liens de partage Google Drive, Dropbox, OneDrive (lecture seule, sans compte ; store/links.cpp).
// link_service : « gdrive », « dropbox », « onedrive » ou vide. Le cache (archives des dossiers Dropbox) est réglé
// une fois par le moteur.
std::string link_service(const std::string& url);
void set_share_cache(const std::string& host_dir);
std::unique_ptr<Store> link_share(const std::string& url, std::string& err);
bool link_share_probe(const std::string& url, Json& out, std::string& err);
void link_share_forget(const std::string& url);
bool dropbox_about(const std::string& refresh, Json& out, std::string& err);

// Le compte et sa place : {"account", "free", "total"} (free/total à 0 si inconnus). type : « gdrive », « onedrive » ou « dropbox ».
bool drive_about(const std::string& type, const std::string& refresh, Json& out, std::string& err);

}  // namespace pbc::store
