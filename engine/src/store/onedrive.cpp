// OneDrive (Microsoft Graph) : tout se passe dans le dossier de l'appli (« Applications/PS5 Backup Center »), adressé
// par chemin. Petits fichiers en un envoi ; gros fichiers par session d'envoi, en morceaux de 10 Mo (taille finale
// annoncée par size_hint), rendus visibles à la fin seulement.
#include <algorithm>
#include <mutex>
#include <set>

#include "core/util.hpp"
#include "store/cloud.hpp"
#include "store/drives.hpp"

namespace pbc::store {

namespace {

constexpr std::size_t kSmall = 4u << 20;   // au-delà : session d'envoi
constexpr std::size_t kChunk = 10u << 20;  // multiple de 320 Kio, comme Graph l'exige

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

class Graph {
public:
    explicit Graph(const FtpConfig& c) : api_("onedrive", c.password), dirs_(std::make_shared<Dirs>()) {
        base_ = cloud::provider("onedrive")->api_base + "/me/drive/special/approot";
    }
    // Adresse d'un élément (vide : le dossier de l'appli), suivie de « suffix » (« /children », « /content »…).
    std::string url(const std::string& p, const std::string& suffix = "") const {
        return p.empty() ? base_ + suffix : base_ + ":/" + cloud::url_encode(p, true) + ":" + suffix;
    }
    http::Reply send(const std::string& method, const std::string& u, const std::string& body = {}, http::Headers extra = {}, std::size_t limit = 16u << 20) const {
        return api_.send(method, u, body, std::move(extra), limit);
    }
    http::Reply send_json(const std::string& method, const std::string& u, const Json& body) const {
        return api_.send(method, u, body.dump(), {{"Content-Type", "application/json"}});
    }
    const cloud::Api& api() const { return api_; }
    bool known_dir(const std::string& d) const { std::lock_guard<std::mutex> l(dirs_->mu); return dirs_->set.count(d) != 0; }
    void remember_dir(const std::string& d) const { std::lock_guard<std::mutex> l(dirs_->mu); dirs_->set.insert(d); }
    void forget_dirs(const std::string& prefix) const {
        std::lock_guard<std::mutex> l(dirs_->mu);
        for (auto it = dirs_->set.begin(); it != dirs_->set.end();) it = it->rfind(prefix, 0) == 0 ? dirs_->set.erase(it) : std::next(it);
    }

private:
    struct Dirs { std::mutex mu; std::set<std::string> set; };
    cloud::Api api_;
    std::string base_;
    std::shared_ptr<Dirs> dirs_;
};

fs::Entry entry_of(const Json& it) {
    fs::Entry e;
    e.name = it["name"].as_string();
    e.dir = it["folder"].is_object();
    e.size = static_cast<std::uint64_t>(it["size"].as_int());
    e.mtime = cloud::parse_iso_time(it["lastModifiedDateTime"].as_string());
    return e;
}

class OdWriter : public Writer {
public:
    OdWriter(const Graph& g, std::string path) : g_(g), path_(std::move(path)) {}
    ~OdWriter() override { if (!done_) abort(); }
    void size_hint(std::uint64_t n) override { total_ = n; }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        buf_.append(p, n);
        // Taille connue et grosse : envoi par morceaux au fil de l'eau (rien de gros ne reste en mémoire).
        if (total_ > kSmall) while (buf_.size() >= kChunk) if (!send_chunk(kChunk)) { failed_ = true; return false; }
        return true;
    }
    bool commit(std::string& err) override {
        done_ = true;
        if (failed_) { err = "envoi interrompu vers OneDrive" + (err_.empty() ? std::string() : " (" + err_ + ")"); cancel(); return false; }
        if (session_.empty() && buf_.size() <= kSmall) {
            const http::Reply r = g_.send("PUT", g_.url(path_, "/content"), buf_, {{"Content-Type", "application/octet-stream"}});
            if (!r.ok()) { err = "envoi refusé : " + cloud::api_error(r); return false; }
            return true;
        }
        if (!total_ || sent_ + buf_.size() != total_) total_ = sent_ + buf_.size();  // taille réelle (annonce absente ou fausse)
        while (!buf_.empty()) if (!send_chunk(std::min(kChunk, buf_.size()))) { err = "envoi refusé : " + err_; cancel(); return false; }
        if (!finished_) { err = "envoi incomplet vers OneDrive"; cancel(); return false; }
        return true;
    }
    void abort() override { done_ = true; cancel(); }

private:
    bool open_session() {
        Json item = Json::object();
        item.set("@microsoft.graph.conflictBehavior", "replace");
        Json body = Json::object();
        body.set("item", item);
        const http::Reply r = g_.send_json("POST", g_.url(path_, "/createUploadSession"), body);
        Json j;
        Json::parse(r.body, j);
        session_ = j["uploadUrl"].as_string();
        if (!r.ok() || session_.empty()) { err_ = cloud::api_error(r); return false; }
        return true;
    }
    bool send_chunk(std::size_t n) {
        if (session_.empty() && !open_session()) return false;
        http::Call c;  // adresse pré-autorisée : sans jeton
        c.method = "PUT";
        c.url = session_;
        c.headers = {{"Content-Range", "bytes " + std::to_string(sent_) + "-" + std::to_string(sent_ + n - 1) + "/" + std::to_string(total_)}};
        c.timeout_s = 120;
        const http::Reply r = http::call(c, buf_.substr(0, n));
        if (!r.ok()) { err_ = cloud::api_error(r); return false; }
        sent_ += n;
        buf_.erase(0, n);
        finished_ = r.status == 200 || r.status == 201;
        return true;
    }
    void cancel() {
        if (!session_.empty()) { http::Call c; c.method = "DELETE"; c.url = session_; http::call(c); }
        session_.clear();
        buf_.clear();
        buf_.shrink_to_fit();
    }
    Graph g_;
    std::string path_, buf_, session_, err_;
    std::uint64_t total_ = 0, sent_ = 0;
    bool failed_ = false, done_ = false, finished_ = false;
};

class OdReader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class OdStore : public Store {
public:
    explicit OdStore(const FtpConfig& c) : g_(c) {}
    std::string label() const override { return "OneDrive"; }
    bool mkdirs(const std::string& dir) override {
        const std::string p = clean(dir);
        if (p.empty() || g_.known_dir(p)) return true;
        fs::Entry e;
        if (stat(dir, e)) { if (e.dir) g_.remember_dir(p); return e.dir; }
        const std::string parent = parent_of(p);
        if (!parent.empty() && !mkdirs("/" + parent)) return false;
        Json body = Json::object();
        body.set("name", std::string(fs::basename(p)));
        body.set("folder", Json::object());
        body.set("@microsoft.graph.conflictBehavior", "fail");
        const http::Reply r = g_.send_json("POST", g_.url(parent, "/children"), body);
        if (!r.ok() && r.status != 409) return false;  // 409 : déjà là
        g_.remember_dir(p);
        return true;
    }
    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        const std::string p = clean(path);
        if (p.empty()) { err = "chemin vide"; return nullptr; }
        if (!mkdirs("/" + parent_of(p))) { err = "dossier impossible à créer sur OneDrive"; return nullptr; }
        return std::make_unique<OdWriter>(g_, p);
    }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        auto r = std::make_unique<OdReader>();
        http::Headers h;
        if (offset) h.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
        http::Reply head;
        if (!g_.api().download(g_.url(clean(path), "/content"), h, r->dl, head)) { err = "lecture refusée : " + cloud::api_error(head); return nullptr; }
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
        const http::Reply r = g_.send("GET", g_.url(clean(path), "/content"), {}, {}, static_cast<std::size_t>(limit));
        if (!r.ok()) return false;
        out = r.body;
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        const std::string p = clean(path);
        if (!mkdirs("/" + parent_of(p))) return false;
        return g_.send("PUT", g_.url(p, "/content"), std::string(data), {{"Content-Type", "application/octet-stream"}}).ok();
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        const http::Reply r = g_.send("GET", g_.url(clean(path)));
        Json j;
        if (!r.ok() || !Json::parse(r.body, j)) return false;
        out = entry_of(j);
        if (clean(path).empty()) out.dir = true;
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        std::string u = g_.url(clean(dir), "/children") + "?$top=1000&$select=name,size,folder,file,lastModifiedDateTime";
        while (!u.empty()) {
            const http::Reply r = g_.send("GET", u);
            Json j;
            if (!r.ok() || !Json::parse(r.body, j)) return false;
            for (const auto& it : j["value"].items()) out.push_back(entry_of(it));
            u = j["@odata.nextLink"].as_string();
        }
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }
    bool space(const std::string&, fs::Space& out) override {
        const http::Reply r = g_.send("GET", cloud::provider("onedrive")->api_base + "/me/drive?$select=quota");
        Json j;
        if (!r.ok() || !Json::parse(r.body, j) || !j["quota"]["total"].as_int()) return false;
        out.total = static_cast<std::uint64_t>(j["quota"]["total"].as_int());
        out.free = static_cast<std::uint64_t>(j["quota"]["remaining"].as_int());
        return true;
    }
    bool remove(const std::string& path) override {  // dossiers compris
        const std::string p = clean(path);
        if (p.empty()) return false;
        g_.forget_dirs(p);
        return g_.send("DELETE", g_.url(p)).ok();
    }
    bool rename(const std::string& from, const std::string& to) override {
        const std::string a = clean(from), b = clean(to);
        if (a.empty() || b.empty()) return false;
        g_.forget_dirs(a);
        if (!mkdirs("/" + parent_of(b))) return false;
        const http::Reply parent = g_.send("GET", g_.url(parent_of(b)) + "?$select=id");
        Json pj;
        if (!parent.ok() || !Json::parse(parent.body, pj) || pj["id"].as_string().empty()) return false;
        fs::Entry e;
        if (stat(to, e)) remove(to);  // comme MOVE avec Overwrite : la destination est remplacée
        Json ref = Json::object();
        ref.set("id", pj["id"].as_string());
        Json body = Json::object();
        body.set("name", std::string(fs::basename(b)));
        body.set("parentReference", ref);
        return g_.send_json("PATCH", g_.url(a), body).ok();
    }

private:
    Graph g_;
};

}  // namespace

std::unique_ptr<Store> onedrive(const FtpConfig& cfg, std::string& err) {
    if (cfg.password.empty()) { err = "compte OneDrive non connecté"; return nullptr; }
    auto st = std::make_unique<OdStore>(cfg);
    fs::Entry e;
    if (!st->stat("/", e)) {  // le dossier de l'appli (créé par OneDrive à la première demande)
        const Graph g(cfg);
        err = cloud::api_error(g.send("GET", g.url("")));
        log("onedrive open failed error=%s", err.c_str());
        return nullptr;
    }
    return st;
}

}  // namespace pbc::store
