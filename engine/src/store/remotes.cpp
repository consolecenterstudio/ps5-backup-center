#include "store/remotes.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"
#include "store/drives.hpp"
#include "store/s3.hpp"
#include "store/smb.hpp"
#include "store/webdav.hpp"

namespace pbc::store {

namespace {
// SMB : un partage (NAS, PC) ou une Switch réglée en SMB ; sinon FTP.
bool uses_smb(const Remote& r) { return r.type == "smb" || (r.type == "switch" && r.ftp.proto == "smb"); }
}  // namespace

std::unique_ptr<Store> connect(const std::string& type, const FtpConfig& cfg, std::string& err) {
    if (type == "webdav") return webdav(cfg, err);
    if (type == "s3") return s3(cfg, err);
    if (type == "gdrive") return gdrive(cfg, err);
    if (type == "onedrive") return onedrive(cfg, err);
    if (type == "dropbox") return dropbox(cfg, err);
    if (type == "smb" || (type == "switch" && cfg.proto == "smb")) return smb(cfg, err);
    return ftp(cfg, err);
}

Json Remote::to_json(bool with_secret) const {
    Json j = Json::object();
    j.set("id", id);
    j.set("type", type);
    j.set("name", name);
    if (!use.empty()) j.set("use", use);
    j.set("config", ftp.to_json(with_secret));
    j.set("address", address());
    return j;
}

Remote Remote::from_json(const Json& j) {
    Remote r;
    r.id = j["id"].as_string();
    if (!j["type"].as_string().empty()) r.type = j["type"].as_string();
    r.name = j["name"].as_string();
    if (j["use"].as_string() == "backups" || j["use"].as_string() == "sync") r.use = j["use"].as_string();
    r.ftp = FtpConfig::from_json(j["config"]);
    return r;
}

std::string Remote::folder() const {
    std::string root = ftp.root.empty() ? "/" : ftp.root;
    if (root.back() == '/') root.pop_back();
    // Dossier choisi = « PBC Backup » lui-même (avec Parcourir) : c'est déjà le dossier des sauvegardes, pas de
    // « PBC Backup/PBC Backup ».
    if (root.size() >= 11 && root.compare(root.size() - 11, 11, "/PBC Backup") == 0) return root;
    // Tout va dans « PBC Backup » : par plateforme, les sauvegardes sont rangées dessous
    // (« PBC Backup/PS5 backup/[PS5] Nom (ID)/saves/<date> »).
    return root + "/PBC Backup";
}

std::string Remote::tagged() const {
    if (type == "switch") return name;
    if (type == "s3") return name + (ftp.service == "b2" ? " (B2)" : " (S3)");
    if (type == "gdrive") return name + " (Google Drive)";
    if (type == "onedrive") return name + " (OneDrive)";
    if (type == "dropbox") return name + " (Dropbox)";
    return name + (type == "webdav" ? " (WebDAV)" : type == "smb" ? " (SMB)" : " (FTP)");
}

std::string Remote::address() const {
    if (type == "s3") return "s3://" + ftp.bucket + (ftp.root.empty() ? "/" : ftp.root);
    if (type == "gdrive" || type == "onedrive" || type == "dropbox") return type + "://" + ftp.bucket + (ftp.root.empty() ? "/" : ftp.root);
    if (type == "webdav") return std::string(ftp.port == 80 || ftp.port == 5005 ? "http://" : "https://") + ftp.host + ":" + std::to_string(ftp.port) + (ftp.root.empty() ? "/" : ftp.root);
    if (type == "smb" || (type == "switch" && ftp.proto == "smb")) return "smb://" + ftp.host + (ftp.port && ftp.port != 445 ? ":" + std::to_string(ftp.port) : std::string()) + (ftp.root.empty() ? "/" : ftp.root);
    return "ftp://" + ftp.host + ":" + std::to_string(ftp.port) + (ftp.root.empty() ? "/" : ftp.root);
}

void Remotes::load() {
    std::lock_guard<std::mutex> l(mu_);
    list_.clear();
    std::string text;
    Json j;
    if (fs::read(path_, text) && Json::parse(text, j)) for (const auto& r : j.items()) list_.push_back(Remote::from_json(r));
}

bool Remotes::save() const {
    Json a = Json::array();
    for (const auto& r : list_) a.push(r.to_json(true));
    return fs::write_atomic(path_, a.dump(true) + "\n");
}

std::vector<Remote> Remotes::list() const { std::lock_guard<std::mutex> l(mu_); return list_; }

bool Remotes::find(const std::string& id, Remote& out) const {
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& r : list_) if (r.id == id) { out = r; return true; }
    return false;
}

bool Remotes::test(const Remote& given, std::string& err) {
    Remote r = given;
    if (r.type == "s3" && r.ftp.service != "other" && r.ftp.service != "r2") r.ftp.host = s3_endpoint(r.ftp);  // Amazon, Backblaze : d'après la région
    if (r.type == "gdrive" || r.type == "onedrive" || r.type == "dropbox") r.ftp.host = r.type;  // pas d'adresse : le service
    if (r.type == "switch") {  // source de sauvegardes : on vérifie juste le dossier JKSV (FTP ou SMB)
        if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
        auto st = connect(r.type, r.ftp, err);
        if (!st) return false;
        std::vector<fs::Entry> l;
        if (!st->list("/JKSV", l)) { err = "dossier /JKSV introuvable : lancez JKSV et faites une première sauvegarde"; return false; }
        return true;
    }
    if (r.type != "ftp" && r.type != "smb" && r.type != "webdav" && r.type != "s3" && r.type != "gdrive" && r.type != "onedrive" && r.type != "dropbox") { err = "type d'appareil inconnu"; return false; }
    if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
    auto st = connect(r.type, r.ftp, err);
    if (!st) return false;
    const std::string probe = r.folder() + "/.pbc-test-" + random_id(3);
    std::string back;
    // Message clair : la connexion marche, seul le dossier pose problème → le choisir en parcourant.
    if (!st->mkdirs(r.folder())) { err = "connexion réussie, mais votre compte ne peut pas écrire dans « " + (r.ftp.root.empty() ? std::string("/") : r.ftp.root) + " » : choisissez un dossier avec Parcourir (ex. un dossier partagé)"; return false; }
    if (!st->write_small(probe, "PBC")) { err = "connexion réussie, mais écriture refusée dans « " + r.folder() + " » : choisissez un autre dossier avec Parcourir"; return false; }
    const bool read_ok = st->read_small(probe, back) && back == "PBC";
    st->remove(probe);
    if (!read_ok) { err = "relecture impossible dans « " + r.folder() + " »"; return false; }
    return true;
}

bool Remotes::add(Remote r, std::string& id, std::string& err) {
    if (r.name.empty()) r.name = r.ftp.host;
    if (uses_smb(r) && (r.ftp.port == 21 || (r.type == "switch" && r.ftp.port == 5000))) r.ftp.port = 445;  // port SMB par défaut
    if (r.type == "webdav" && r.ftp.port == 21) r.ftp.port = 5006;  // WebDAV Server de Synology en https
    if (r.type == "s3" && r.ftp.service != "other" && r.ftp.service != "r2") r.ftp.host = s3_endpoint(r.ftp);  // Amazon, Backblaze : d'après la région
    if (r.type == "gdrive" || r.type == "onedrive" || r.type == "dropbox") r.ftp.host = r.type;
    if (r.name.empty() || r.name == "anonymous") r.name = r.type == "s3" || (r.type == "gdrive" || r.type == "onedrive" || r.type == "dropbox") ? r.ftp.bucket : r.ftp.host;
    // Deux synchros dans le même dossier se marcheraient dessus : refusé, l'écran dit lequel (« sync_taken:NAS (FTP) »).
    if (const std::string other = sync_taken(r); !other.empty()) { err = "sync_taken:" + other; return false; }
    if (r.type == "webdav" && !webdav_trust(r.ftp, err)) return false;  // certificat maison du NAS : retenu
    if (!test(r, err)) return false;
    std::lock_guard<std::mutex> l(mu_);
    r.id = "remote:" + random_id(4);
    list_.push_back(r);
    id = r.id;
    return save();
}

bool Remotes::update(const std::string& id, Remote r, std::string& err) {
    Remote old;
    if (!find(id, old)) { err = "appareil inconnu"; return false; }
    if (r.name.empty()) r.name = old.name;
    if (r.type.empty()) r.type = old.type;
    if (r.ftp.password.empty()) r.ftp.password = old.ftp.password;
    if (r.type == "s3" && r.ftp.service != "other" && r.ftp.service != "r2") r.ftp.host = s3_endpoint(r.ftp);
    if (r.type == "gdrive" || r.type == "onedrive" || r.type == "dropbox") { r.ftp.host = r.type; if (r.ftp.bucket.empty()) r.ftp.bucket = old.ftp.bucket; }
    if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
    r.id = id;
    if (const std::string other = sync_taken(r); !other.empty()) { err = "sync_taken:" + other; return false; }
    // Régler l'appareil = le reconnecter : le certificat présenté maintenant est celui qu'on retient.
    if (r.type == "webdav" && !webdav_trust(r.ftp, err)) return false;
    if (!test(r, err)) return false;
    std::lock_guard<std::mutex> l(mu_);
    r.id = id;
    for (auto& x : list_) if (x.id == id) x = r;
    return save();
}

bool Remotes::remove(const std::string& id) {
    std::lock_guard<std::mutex> l(mu_);
    const auto before = list_.size();
    std::erase_if(list_, [&](const Remote& r) { return r.id == id; });
    return list_.size() != before && save();
}

namespace {
// Dossier de base : sans « / » final ni « /PBC Backup » (les deux réglages mènent aux mêmes sauvegardes).
std::string base_root(std::string r) {
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    if (r.size() >= 11 && r.compare(r.size() - 11, 11, "/PBC Backup") == 0) r.resize(r.size() - 11);
    return r.empty() ? std::string("/") : r;
}
bool same_folder(const Remote& a, const Remote& b) {
    return a.type != "switch" && b.type != "switch" && a.ftp.host == b.ftp.host && a.ftp.bucket == b.ftp.bucket && base_root(a.ftp.root) == base_root(b.ftp.root);
}
bool serves(const Remote& r, const std::string& purpose) {
    return purpose.empty() || (purpose == "sync" ? r.for_sync() : r.for_backups());
}
}  // namespace

std::string Remotes::same_as(const std::string& id, const std::string& purpose) const {
    std::lock_guard<std::mutex> l(mu_);
    for (std::size_t i = 0; i < list_.size(); ++i) {
        if (list_[i].id != id) continue;
        if (!serves(list_[i], purpose)) return {};
        for (std::size_t j = 0; j < i; ++j)  // le premier enregistré reste la référence
            if (serves(list_[j], purpose) && same_folder(list_[j], list_[i])) return list_[j].id;
        return {};
    }
    return {};
}

std::string Remotes::sync_taken(const Remote& r) const {
    if (r.type == "switch" || !r.for_sync()) return {};
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& x : list_)
        if (x.id != r.id && x.for_sync() && same_folder(x, r)) return x.tagged();
    return {};
}

std::unique_ptr<Store> Remotes::open(const std::string& id, std::string& err) const {
    Remote r;
    if (!find(id, r)) { err = "appareil inconnu"; return nullptr; }
    return connect(r.type, r.ftp, err);
}

void Remotes::rotate_secret(const std::string& old_secret, const std::string& new_secret) {
    if (old_secret.empty() || new_secret.empty()) return;
    std::lock_guard<std::mutex> l(mu_);
    bool changed = false;
    for (auto& x : list_) if (x.ftp.password == old_secret) { x.ftp.password = new_secret; changed = true; }
    if (changed) save();
}

}  // namespace pbc::store
