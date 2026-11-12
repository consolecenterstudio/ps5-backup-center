#include "store/remotes.hpp"

#include "core/fs.hpp"
#include "core/util.hpp"
#include "store/smb.hpp"

namespace pbc::store {

namespace {
// SMB : un partage (NAS, PC) ou une Switch réglée en SMB ; sinon FTP.
bool uses_smb(const Remote& r) { return r.type == "smb" || (r.type == "switch" && r.ftp.proto == "smb"); }
}  // namespace

Json Remote::to_json(bool with_secret) const {
    Json j = Json::object();
    j.set("id", id);
    j.set("type", type);
    j.set("name", name);
    j.set("config", ftp.to_json(with_secret));
    j.set("address", address());
    return j;
}

Remote Remote::from_json(const Json& j) {
    Remote r;
    r.id = j["id"].as_string();
    if (!j["type"].as_string().empty()) r.type = j["type"].as_string();
    r.name = j["name"].as_string();
    r.ftp = FtpConfig::from_json(j["config"]);
    return r;
}

std::string Remote::folder() const {
    std::string root = ftp.root.empty() ? "/" : ftp.root;
    if (root.back() == '/') root.pop_back();
    // Tout va dans « PBC Backup » : par plateforme, les sauvegardes sont rangées dessous
    // (« PBC Backup/PS5 backup/[PS5] Nom (ID)/saves/<date> »).
    return root + "/PBC Backup";
}

std::string Remote::address() const {
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

bool Remotes::test(const Remote& r, std::string& err) {
    if (r.type == "switch") {  // source de sauvegardes : on vérifie juste le dossier JKSV (FTP ou SMB)
        if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
        auto st = uses_smb(r) ? smb(r.ftp, err) : ftp(r.ftp, err);
        if (!st) return false;
        std::vector<fs::Entry> l;
        if (!st->list("/JKSV", l)) { err = "dossier /JKSV introuvable : lancez JKSV et faites une première sauvegarde"; return false; }
        return true;
    }
    if (r.type != "ftp" && r.type != "smb") { err = "type d'appareil inconnu"; return false; }
    if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
    auto st = uses_smb(r) ? smb(r.ftp, err) : ftp(r.ftp, err);
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
    if (r.ftp.host.empty()) { err = "adresse manquante"; return false; }
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

std::unique_ptr<Store> Remotes::open(const std::string& id, std::string& err) const {
    Remote r;
    if (!find(id, r)) { err = "appareil inconnu"; return nullptr; }
    return uses_smb(r) ? smb(r.ftp, err) : ftp(r.ftp, err);
}

}  // namespace pbc::store
