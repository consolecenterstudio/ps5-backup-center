// Google Drive (API v3, accès « drive.file » : seulement ce que l'appli a créé). Drive range les fichiers par
// identifiant, pas par chemin : chaque dossier traversé est cherché par son nom dans son parent (mémorisé). Envoi
// « reprenable » par morceaux de 8 Mo (taille finale inconnue jusqu'au dernier), visible à la fin seulement.
#include <algorithm>
#include <map>
#include <mutex>

#include "core/util.hpp"
#include "store/cloud.hpp"
#include "store/drives.hpp"

namespace pbc::store {

namespace {

constexpr const char* kFolder = "application/vnd.google-apps.folder";
constexpr std::size_t kChunk = 8u << 20;  // multiple de 256 Kio, comme Drive l'exige
constexpr const char* kFields = "id,name,mimeType,size,modifiedTime";

std::string clean(const std::string& path) {
    std::string p = path;
    while (!p.empty() && p.front() == '/') p.erase(0, 1);
    while (!p.empty() && p.back() == '/') p.pop_back();
    return p;
}
std::string parent_of(const std::string& p) {
    const auto s = p.rfind('/');
    return s == std::string::npos ? std::string() : p.substr(0, s);
}
std::string quote(const std::string& s) {  // valeur entre apostrophes dans une recherche Drive
    std::string out = "'";
    for (char c : s) { if (c == '\'' || c == '\\') out += '\\'; out += c; }
    return out + "'";
}

fs::Entry entry_of(const Json& f) {
    fs::Entry e;
    e.name = f["name"].as_string();
    e.dir = f["mimeType"].as_string() == kFolder;
    e.size = std::strtoull(f["size"].as_string().c_str(), nullptr, 10);  // Drive donne la taille en texte
    e.mtime = cloud::parse_iso_time(f["modifiedTime"].as_string());
    return e;
}

class Drive {
public:
    explicit Drive(const FtpConfig& c) : api_("gdrive", c.password), ids_(std::make_shared<Ids>()) {
        const cloud::Provider* p = cloud::provider("gdrive");
        base_ = p->api_base;
        upload_ = p->upload_base;
    }
    http::Reply send(const std::string& method, const std::string& u, const std::string& body = {}, http::Headers extra = {}, std::size_t limit = 16u << 20) const {
        return api_.send(method, u, body, std::move(extra), limit);
    }
    http::Reply send_json(const std::string& method, const std::string& u, const Json& body) const {
        return api_.send(method, u, body.dump(), {{"Content-Type", "application/json; charset=UTF-8"}});
    }
    const cloud::Api& api() const { return api_; }
    const std::string& base() const { return base_; }
    const std::string& upload() const { return upload_; }

    // Le fichier ou dossier `name` dans le dossier `parent` (identifiant) : faux s'il n'existe pas.
    bool child(const std::string& parent, const std::string& name, Json& out) const {
        const std::string q = quote(parent) + " in parents and name = " + quote(name) + " and trashed = false";
        const http::Reply r = send("GET", base_ + "/files?q=" + cloud::url_encode(q) + "&fields=" + cloud::url_encode(std::string("files(") + kFields + ")") + "&pageSize=10");
        Json j;
        if (!r.ok() || !Json::parse(r.body, j) || j["files"].items().empty()) return false;
        out = j["files"].items()[0];
        return true;
    }
    // Chemin → identifiant (« root » pour la racine), dossiers mémorisés. meta : le fichier trouvé (vide pour la racine).
    bool resolve(const std::string& path, std::string& id, Json* meta = nullptr) const {
        const std::string p = clean(path);
        if (p.empty()) { id = "root"; if (meta) *meta = Json(); return true; }
        if (!meta) { std::lock_guard<std::mutex> l(ids_->mu); const auto it = ids_->map.find(p); if (it != ids_->map.end()) { id = it->second; return true; } }
        std::string parent;
        if (!resolve(parent_of(p), parent)) return false;
        Json f;
        if (!child(parent, std::string(fs::basename(p)), f)) return false;
        id = f["id"].as_string();
        if (f["mimeType"].as_string() == kFolder) { std::lock_guard<std::mutex> l(ids_->mu); ids_->map[p] = id; }
        if (meta) *meta = f;
        return true;
    }
    void remember(const std::string& p, const std::string& id) const { std::lock_guard<std::mutex> l(ids_->mu); ids_->map[clean(p)] = id; }
    void forget(const std::string& path) const {
        const std::string p = clean(path);
        std::lock_guard<std::mutex> l(ids_->mu);
        for (auto it = ids_->map.begin(); it != ids_->map.end();)
            it = it->first == p || it->first.rfind(p + "/", 0) == 0 ? ids_->map.erase(it) : std::next(it);
    }

private:
    struct Ids { std::mutex mu; std::map<std::string, std::string> map; };
    cloud::Api api_;
    std::string base_, upload_;
    std::shared_ptr<Ids> ids_;
};

class GdWriter : public Writer {
public:
    GdWriter(const Drive& d, std::string key, std::string parent, std::string name, std::string existing)
        : d_(d), key_(std::move(key)), parent_(std::move(parent)), name_(std::move(name)), existing_(std::move(existing)) {}
    // Reprise d'une session gardée : Drive dit combien il a reçu, il doit en avoir exactement autant.
    bool reopen(const Kept& k, std::string& err) {
        http::Call c;
        c.method = "PUT";
        c.url = k.session;
        c.headers = {{"Content-Range", "bytes */*"}};
        c.timeout_s = 60;
        const http::Reply r = http::call(c, std::string());
        const std::string range = r.header("range");  // « bytes=0-N »
        const auto dash = range.rfind('-');
        const std::uint64_t got = r.status == 308 && dash != std::string::npos ? std::strtoull(range.c_str() + dash + 1, nullptr, 10) + 1 : 0;
        if (r.status != 308 || got != k.have || !acked_.restore(k)) { err = "session d'envoi expirée chez Google Drive"; return false; }
        session_ = k.session;
        sent_ = k.have;
        return true;
    }
    ~GdWriter() override { if (!done_) abort(); }
    void size_hint(std::uint64_t n) override { total_ = n; }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        buf_.append(p, n);
        while (buf_.size() > kChunk) if (!send_chunk(kChunk, false)) { failed_ = true; return false; }  // le dernier morceau attend commit
        return true;
    }
    bool commit(std::string& err) override {
        done_ = true;
        if (failed_ || !send_chunk(buf_.size(), true)) { err = "envoi refusé : " + err_; kept_drop(key_); cancel(); return false; }
        kept_drop(key_);
        return true;
    }
    void abort() override { done_ = true; kept_drop(key_); cancel(); }
    // Arrêt : la session reste ouverte chez Google (une semaine), ce qu'il a accepté est noté pour la reprise.
    bool keep() override {
        if (session_.empty() || sent_ == 0 || failed_) { abort(); return false; }
        Kept k;
        acked_.fill(k);
        k.session = session_;
        done_ = true;
        buf_.clear();
        buf_.shrink_to_fit();
        if (!kept_save(key_, k)) { cancel(); return false; }
        return true;
    }

private:
    bool open_session() {
        Json meta = Json::object();
        meta.set("name", name_);
        std::string u;
        if (existing_.empty()) {
            Json parents = Json::array();
            parents.push(parent_);
            meta.set("parents", parents);
            u = d_.upload() + "/files?uploadType=resumable&fields=id";
        } else u = d_.upload() + "/files/" + existing_ + "?uploadType=resumable&fields=id";  // remplace le contenu
        const http::Reply r = d_.send_json(existing_.empty() ? "POST" : "PATCH", u, meta);
        session_ = r.header("location");
        if (!r.ok() || session_.empty()) { err_ = cloud::api_error(r); return false; }
        return true;
    }
    bool send_chunk(std::size_t n, bool last) {
        if (session_.empty() && !open_session()) return false;
        http::Call c;  // adresse de session : elle vaut autorisation
        c.method = "PUT";
        c.url = session_;
        const std::string total = last ? std::to_string(sent_ + n) : "*";
        c.headers = {{"Content-Range", n ? "bytes " + std::to_string(sent_) + "-" + std::to_string(sent_ + n - 1) + "/" + total : "bytes */" + total}};
        c.timeout_s = 120;
        const http::Reply r = http::call(c, buf_.substr(0, n));
        if (last ? !(r.status == 200 || r.status == 201) : r.status != 308) { err_ = cloud::api_error(r); return false; }
        acked_.add(buf_.data(), n);
        sent_ += n;
        buf_.erase(0, n);
        return true;
    }
    void cancel() {
        if (!session_.empty()) { http::Call c; c.method = "DELETE"; c.url = session_; http::call(c); }
        session_.clear();
        buf_.clear();
        buf_.shrink_to_fit();
    }
    Drive d_;
    std::string key_, parent_, name_, existing_, buf_, session_, err_;
    Acked acked_;
    std::uint64_t total_ = 0, sent_ = 0;
    bool failed_ = false, done_ = false;
};

class GdReader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class GdStore : public Store {
public:
    explicit GdStore(const FtpConfig& c) : d_(c), account_(!c.account.empty() ? c.account : c.bucket.empty() ? c.password : c.bucket) {}
    std::string label() const override { return "Google Drive"; }
    bool mkdirs(const std::string& dir) override {
        const std::string p = clean(dir);
        if (p.empty()) return true;
        std::string id;
        if (d_.resolve(p, id)) return true;
        std::string parent;
        if (!mkdirs("/" + parent_of(p)) || !d_.resolve(parent_of(p), parent)) return false;
        Json body = Json::object();
        body.set("name", std::string(fs::basename(p)));
        body.set("mimeType", kFolder);
        Json parents = Json::array();
        parents.push(parent);
        body.set("parents", parents);
        const http::Reply r = d_.send_json("POST", d_.base() + "/files?fields=id", body);
        Json j;
        if (!r.ok() || !Json::parse(r.body, j) || j["id"].as_string().empty()) return false;
        d_.remember(p, j["id"].as_string());
        return true;
    }
    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        const std::string p = clean(path);
        std::string parent, existing;
        if (p.empty() || !mkdirs("/" + parent_of(p)) || !d_.resolve(parent_of(p), parent)) { err = "dossier impossible à créer sur Google Drive"; return nullptr; }
        Json f;
        if (d_.child(parent, std::string(fs::basename(p)), f)) existing = f["id"].as_string();  // Drive accepte deux fichiers du même nom : on remplace
        drop_partial("/" + p);  // nouvel envoi depuis le début : une session gardée pour ce fichier est refermée
        return std::make_unique<GdWriter>(d_, key(p), parent, std::string(fs::basename(p)), existing);
    }
    bool kept(const std::string& path, Kept& out) override { return kept_load(key(clean(path)), out); }
    std::unique_ptr<Writer> resume(const std::string& path, std::uint64_t& have, std::string& err) override {
        have = 0;
        const std::string p = clean(path);
        Kept k;
        if (!kept_load(key(p), k)) { err = "rien à reprendre"; return nullptr; }
        auto w = std::make_unique<GdWriter>(d_, key(p), std::string(), std::string(fs::basename(p)), std::string());
        if (!w->reopen(k, err)) { kept_drop(key(p)); return nullptr; }
        have = k.have;
        return w;
    }
    void drop_partial(const std::string& path) override {
        const std::string p = clean(path);
        Kept k;
        if (kept_load(key(p), k)) { http::Call c; c.method = "DELETE"; c.url = k.session; http::call(c); }
        kept_drop(key(p));
    }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        std::string id;
        Json meta;
        if (!d_.resolve(path, id, &meta) || id == "root") { err = "introuvable sur Google Drive"; return nullptr; }
        auto r = std::make_unique<GdReader>();
        http::Headers h;
        if (offset) h.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
        http::Reply head;
        if (!d_.api().download(d_.base() + "/files/" + id + "?alt=media", h, r->dl, head)) { err = "lecture refusée : " + cloud::api_error(head); return nullptr; }
        if (offset && head.status != 206) {
            char buf[65536];
            while (offset) {
                const long n = r->dl.read(buf, offset < sizeof buf ? static_cast<std::size_t>(offset) : sizeof buf);
                if (n <= 0) { err = "lecture interrompue"; return nullptr; }
                offset -= static_cast<std::uint64_t>(n);
            }
        }
        return r;
    }
    bool read_small(const std::string& path, std::string& out, std::uint64_t limit) override {
        std::string id;
        Json meta;
        if (!d_.resolve(path, id, &meta) || id == "root") { log("gdrive read_small: introuvable %s", path.c_str()); return false; }
        const http::Reply r = d_.send("GET", d_.base() + "/files/" + id + "?alt=media", {}, {}, static_cast<std::size_t>(limit));
        if (!r.ok()) { log("gdrive read_small: %s -> %d %s", path.c_str(), r.status, r.error.c_str()); return false; }
        out = r.body;
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        std::string err;
        auto w = create(path, err);
        return w && w->write(data.data(), data.size()) && w->commit(err);
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        std::string id;
        Json meta;
        if (!d_.resolve(path, id, &meta)) return false;
        if (id == "root") { out = fs::Entry{}; out.dir = true; out.name = "/"; return true; }
        out = entry_of(meta);
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        std::string id;
        if (!d_.resolve(dir, id)) return false;
        std::string token;
        do {
            const std::string q = quote(id) + " in parents and trashed = false";
            std::string u = d_.base() + "/files?q=" + cloud::url_encode(q) + "&pageSize=1000&fields=" +
                            cloud::url_encode(std::string("nextPageToken,files(") + kFields + ")");
            if (!token.empty()) u += "&pageToken=" + cloud::url_encode(token);
            const http::Reply r = d_.send("GET", u);
            Json j;
            if (!r.ok() || !Json::parse(r.body, j)) return false;
            for (const auto& f : j["files"].items()) {
                out.push_back(entry_of(f));
                if (out.back().dir) d_.remember(clean(dir).empty() ? f["name"].as_string() : clean(dir) + "/" + f["name"].as_string(), f["id"].as_string());
            }
            token = j["nextPageToken"].as_string();
        } while (!token.empty());
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }
    bool space(const std::string&, fs::Space& out) override {
        const http::Reply r = d_.send("GET", d_.base() + "/about?fields=storageQuota");
        Json j;
        if (!r.ok() || !Json::parse(r.body, j)) return false;
        const std::uint64_t limit = std::strtoull(j["storageQuota"]["limit"].as_string().c_str(), nullptr, 10);
        const std::uint64_t used = std::strtoull(j["storageQuota"]["usage"].as_string().c_str(), nullptr, 10);
        if (!limit) return false;  // compte sans limite
        out.total = limit;
        out.free = limit > used ? limit - used : 0;
        return true;
    }
    bool share_link(const std::string& path, bool off, std::string& link, std::string& err) override {
        std::string id;
        Json meta;
        if (!d_.resolve(path, id, &meta) || id == "root") { err = "introuvable sur Google Drive"; return false; }
        if (off) {
            const http::Reply r = d_.send("DELETE", d_.base() + "/files/" + id + "/permissions/anyoneWithLink");
            if (!r.ok() && r.status != 404) { err = "Google Drive a refusé de retirer le lien (" + std::to_string(r.status) + ")"; return false; }
            return true;
        }
        Json body = Json::object();
        body.set("role", "reader");
        body.set("type", "anyone");
        const http::Reply r = d_.send_json("POST", d_.base() + "/files/" + id + "/permissions?fields=id", body);
        if (!r.ok()) { err = "Google Drive a refusé le partage (" + std::to_string(r.status) + ") : " + r.body.substr(0, 240); return false; }
        link = meta["mimeType"].as_string() == kFolder ? "https://drive.google.com/drive/folders/" + id : "https://drive.google.com/file/d/" + id + "/view";
        return true;
    }
    bool remove(const std::string& path) override {  // dossiers compris (définitif)
        std::string id;
        Json meta;
        if (!d_.resolve(path, id, &meta) || id == "root") return false;
        d_.forget(path);
        return d_.send("DELETE", d_.base() + "/files/" + id).ok();
    }
    bool rename(const std::string& from, const std::string& to) override {
        std::string id, old_parent, new_parent;
        Json meta;
        if (!d_.resolve(from, id, &meta) || id == "root" || !d_.resolve(parent_of(clean(from)), old_parent)) return false;
        if (!mkdirs("/" + parent_of(clean(to))) || !d_.resolve(parent_of(clean(to)), new_parent)) return false;
        fs::Entry e;
        if (stat(to, e)) remove(to);  // la destination est remplacée
        d_.forget(from);
        Json body = Json::object();
        body.set("name", std::string(fs::basename(clean(to))));
        std::string u = d_.base() + "/files/" + id + "?fields=id";
        if (new_parent != old_parent) u += "&addParents=" + cloud::url_encode(new_parent) + "&removeParents=" + cloud::url_encode(old_parent);
        return d_.send_json("PATCH", u, body).ok();
    }

private:
    std::string key(const std::string& p) const { return kept_key("gdrive", account_, p); }
    Drive d_;
    std::string account_;  // l'appareil (son identifiant ; sinon le nom du compte ou son jeton) : une session n'est reprise que sur le même compte
};

}  // namespace

std::unique_ptr<Store> gdrive(const FtpConfig& cfg, std::string& err) {
    if (cfg.password.empty()) { err = "compte Google non connecté"; return nullptr; }
    const Drive d(cfg);
    const http::Reply r = d.send("GET", d.base() + "/about?fields=user(emailAddress)");
    if (!r.ok()) { err = cloud::api_error(r); log("gdrive open failed error=%s", err.c_str()); return nullptr; }
    return std::make_unique<GdStore>(cfg);
}

bool drive_about(const std::string& type, const std::string& refresh, Json& out, std::string& err) {
    if (type == "dropbox") return dropbox_about(refresh, out, err);
    const cloud::Provider* p = cloud::provider(type);
    if (!p) { err = "service inconnu"; return false; }
    const cloud::Api api(type, refresh);
    out = Json::object();
    if (type == "gdrive") {
        const http::Reply r = api.send("GET", p->api_base + "/about?fields=" + cloud::url_encode("user(emailAddress,displayName),storageQuota"));
        Json j;
        if (!r.ok() || !Json::parse(r.body, j)) { err = cloud::api_error(r); return false; }
        out.set("account", j["user"]["emailAddress"].as_string());
        const std::uint64_t limit = std::strtoull(j["storageQuota"]["limit"].as_string().c_str(), nullptr, 10);
        const std::uint64_t used = std::strtoull(j["storageQuota"]["usage"].as_string().c_str(), nullptr, 10);
        out.set("total", limit);
        out.set("free", limit > used ? limit - used : 0);
        return true;
    }
    const http::Reply me = api.send("GET", p->api_base + "/me?$select=mail,userPrincipalName,displayName");
    Json m;
    if (!me.ok() || !Json::parse(me.body, m)) { err = cloud::api_error(me); log("%s profil refusé : %d %s %s", type.c_str(), me.status, me.error.c_str(), me.body.substr(0, 300).c_str()); return false; }
    out.set("account", !m["mail"].as_string().empty() ? m["mail"].as_string() : !m["userPrincipalName"].as_string().empty() ? m["userPrincipalName"].as_string() : m["displayName"].as_string());
    const http::Reply d = api.send("GET", p->api_base + "/me/drive?$select=quota");
    Json q;
    Json::parse(d.body, q);
    out.set("total", static_cast<std::uint64_t>(q["quota"]["total"].as_int()));
    out.set("free", static_cast<std::uint64_t>(q["quota"]["remaining"].as_int()));
    return true;
}

}  // namespace pbc::store
