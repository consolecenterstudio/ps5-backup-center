// Dropbox (API v2, appli limitée à son dossier « Applications/PS5 Backup Center »). Appels JSON ; les chemins partent
// du dossier de l'appli (« » pour sa racine). Envoi en une fois jusqu'à 8 Mo, sinon par session (morceaux de 8 Mo),
// visible à la fin seulement.
#include <algorithm>

#include "core/util.hpp"
#include "store/cloud.hpp"
#include "store/drives.hpp"

namespace pbc::store {

namespace {

constexpr std::size_t kChunk = 8u << 20;

std::string dbx_path(const std::string& path) {  // « /PBC Backup/x/ » → « /PBC Backup/x » ; racine → « »
    std::string p = path;
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (!p.empty() && p.front() != '/') p = "/" + p;
    return p;
}

// En-tête « Dropbox-API-Arg » : JSON en ASCII seulement (accents en \uXXXX), comme Dropbox l'exige.
std::string header_json(const Json& j) {
    const std::string s = j.dump();
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { out += static_cast<char>(c); ++i; continue; }
        unsigned cp = 0;
        int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        cp = c & (n == 3 ? 0x07 : n == 2 ? 0x0F : 0x1F);
        for (int k = 1; k <= n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += static_cast<std::size_t>(n) + 1;
        char b[16];
        if (cp >= 0x10000) {
            cp -= 0x10000;
            std::snprintf(b, sizeof b, "\\u%04x\\u%04x", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
        } else std::snprintf(b, sizeof b, "\\u%04x", cp);
        out += b;
    }
    return out;
}

fs::Entry entry_of(const Json& m) {
    fs::Entry e;
    e.name = m["name"].as_string();
    e.dir = m[".tag"].as_string() == "folder";
    e.size = static_cast<std::uint64_t>(m["size"].as_int());
    e.mtime = cloud::parse_iso_time(m["server_modified"].as_string());
    return e;
}

class Dbx {
public:
    explicit Dbx(const FtpConfig& c) : api_("dropbox", c.password) {
        const cloud::Provider* p = cloud::provider("dropbox");
        rpc_ = p->api_base;
        content_ = p->upload_base;
    }
    http::Reply rpc(const std::string& endpoint, const Json& body) const {
        return api_.send("POST", rpc_ + endpoint, body.is_null() ? std::string("null") : body.dump(), {{"Content-Type", "application/json"}});
    }
    http::Reply upload(const std::string& endpoint, const Json& arg, const std::string& data) const {
        return api_.send("POST", content_ + endpoint, data, {{"Content-Type", "application/octet-stream"}, {"Dropbox-API-Arg", header_json(arg)}});
    }
    std::string download_url(const std::string& path) const {
        Json a = Json::object();
        a.set("path", path);
        return content_ + "/files/download?arg=" + cloud::url_encode(a.dump());
    }
    const cloud::Api& api() const { return api_; }

private:
    cloud::Api api_;
    std::string rpc_, content_;
};

Json path_arg(const std::string& p) { Json j = Json::object(); j.set("path", p); return j; }

std::string users_endpoint(const char* what) { return std::string("/users") + '/' + what; }

class DbxWriter : public Writer {
public:
    DbxWriter(const Dbx& d, std::string path) : d_(d), path_(std::move(path)) {}
    ~DbxWriter() override { if (!done_) abort(); }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        buf_.append(p, n);
        while (buf_.size() > kChunk) if (!send(kChunk)) { failed_ = true; return false; }  // le dernier morceau attend commit
        return true;
    }
    bool commit(std::string& err) override {
        done_ = true;
        Json c = Json::object();
        c.set("path", path_);
        c.set("mode", "overwrite");
        c.set("mute", true);
        if (failed_) { err = "envoi interrompu vers Dropbox" + (err_.empty() ? std::string() : " (" + err_ + ")"); return false; }
        http::Reply r;
        if (session_.empty()) r = d_.upload("/files/upload", c, buf_);  // petit fichier : une seule requête
        else {
            Json cursor = Json::object();
            cursor.set("session_id", session_);
            cursor.set("offset", sent_);
            Json a = Json::object();
            a.set("cursor", cursor);
            a.set("commit", c);
            r = d_.upload("/files/upload_session/finish", a, buf_);
        }
        if (!r.ok()) { err = "envoi refusé : " + cloud::api_error(r); return false; }
        return true;
    }
    void abort() override { done_ = true; buf_.clear(); buf_.shrink_to_fit(); }  // une session non terminée expire seule

private:
    bool send(std::size_t n) {
        http::Reply r;
        if (session_.empty()) {
            Json a = Json::object();
            a.set("close", false);
            r = d_.upload("/files/upload_session/start", a, buf_.substr(0, n));
            Json j;
            Json::parse(r.body, j);
            session_ = j["session_id"].as_string();
            if (!r.ok() || session_.empty()) { err_ = cloud::api_error(r); return false; }
        } else {
            Json cursor = Json::object();
            cursor.set("session_id", session_);
            cursor.set("offset", sent_);
            Json a = Json::object();
            a.set("cursor", cursor);
            a.set("close", false);
            r = d_.upload("/files/upload_session/append_v2", a, buf_.substr(0, n));
            if (!r.ok()) { err_ = cloud::api_error(r); return false; }
        }
        sent_ += n;
        buf_.erase(0, n);
        return true;
    }
    Dbx d_;
    std::string path_, buf_, session_, err_;
    std::uint64_t sent_ = 0;
    bool failed_ = false, done_ = false;
};

class DbxReader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class DbxStore : public Store {
public:
    explicit DbxStore(const FtpConfig& c) : d_(c) {}
    std::string label() const override { return "Dropbox"; }
    bool mkdirs(const std::string& dir) override {
        const std::string p = dbx_path(dir);
        if (p.empty()) return true;
        Json a = path_arg(p);
        a.set("autorename", false);
        const http::Reply r = d_.rpc("/files/create_folder_v2", a);  // crée aussi les dossiers parents
        if (r.ok()) return true;
        fs::Entry e;
        return r.status == 409 && stat(p, e) && e.dir;  // déjà là
    }
    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        const std::string p = dbx_path(path);
        if (p.empty()) { err = "chemin vide"; return nullptr; }
        return std::make_unique<DbxWriter>(d_, p);  // Dropbox crée les dossiers en enregistrant le fichier
    }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        auto r = std::make_unique<DbxReader>();
        http::Headers h;
        if (offset) h.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
        http::Reply head;
        if (!d_.api().download(d_.download_url(dbx_path(path)), h, r->dl, head)) { err = "lecture refusée : " + cloud::api_error(head); return nullptr; }
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
        const http::Reply r = d_.api().send("GET", d_.download_url(dbx_path(path)), {}, {}, static_cast<std::size_t>(limit));
        if (!r.ok()) return false;
        out = r.body;
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        Json c = Json::object();
        c.set("path", dbx_path(path));
        c.set("mode", "overwrite");
        c.set("mute", true);
        return d_.upload("/files/upload", c, std::string(data)).ok();
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        const std::string p = dbx_path(path);
        if (p.empty()) { out = fs::Entry{}; out.dir = true; out.name = "/"; return true; }
        const http::Reply r = d_.rpc("/files/get_metadata", path_arg(p));
        Json j;
        if (!r.ok() || !Json::parse(r.body, j)) return false;
        out = entry_of(j);
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        Json a = path_arg(dbx_path(dir));
        a.set("limit", static_cast<std::int64_t>(2000));
        http::Reply r = d_.rpc("/files/list_folder", a);
        for (;;) {
            Json j;
            if (!r.ok() || !Json::parse(r.body, j)) return false;
            for (const auto& m : j["entries"].items()) if (m[".tag"].as_string() != "deleted") out.push_back(entry_of(m));
            if (!j["has_more"].as_bool()) break;
            Json c = Json::object();
            c.set("cursor", j["cursor"].as_string());
            r = d_.rpc("/files/list_folder/continue", c);
        }
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }
    bool space(const std::string&, fs::Space& out) override {
        const http::Reply r = d_.rpc(users_endpoint("get_space_usage"), Json());
        Json j;
        if (!r.ok() || !Json::parse(r.body, j)) return false;
        const std::uint64_t total = static_cast<std::uint64_t>(j["allocation"]["allocated"].as_int()), used = static_cast<std::uint64_t>(j["used"].as_int());
        if (!total) return false;
        out.total = total;
        out.free = total > used ? total - used : 0;
        return true;
    }
    bool remove(const std::string& path) override {  // dossiers compris
        const std::string p = dbx_path(path);
        return !p.empty() && d_.rpc("/files/delete_v2", path_arg(p)).ok();
    }
    bool rename(const std::string& from, const std::string& to) override {
        const std::string a = dbx_path(from), b = dbx_path(to);
        if (a.empty() || b.empty()) return false;
        fs::Entry e;
        if (stat(b, e)) remove(b);  // la destination est remplacée
        Json m = Json::object();
        m.set("from_path", a);
        m.set("to_path", b);
        m.set("autorename", false);
        return d_.rpc("/files/move_v2", m).ok();
    }

private:
    Dbx d_;
};

}  // namespace

std::unique_ptr<Store> dropbox(const FtpConfig& cfg, std::string& err) {
    if (cfg.password.empty()) { err = "compte Dropbox non connecté"; return nullptr; }
    const Dbx d(cfg);
    const http::Reply r = d.rpc(users_endpoint("get_current_account"), Json());
    if (!r.ok()) { err = cloud::api_error(r); log("dropbox open failed error=%s", err.c_str()); return nullptr; }
    return std::make_unique<DbxStore>(cfg);
}

bool dropbox_about(const std::string& refresh, Json& out, std::string& err) {
    FtpConfig c;
    c.password = refresh;
    const Dbx d(c);
    const http::Reply a = d.rpc(users_endpoint("get_current_account"), Json());
    Json j;
    if (!a.ok() || !Json::parse(a.body, j)) { err = cloud::api_error(a); return false; }
    out = Json::object();
    out.set("account", j["email"].as_string());
    const http::Reply s = d.rpc(users_endpoint("get_space_usage"), Json());
    Json q;
    Json::parse(s.body, q);
    const std::uint64_t total = static_cast<std::uint64_t>(q["allocation"]["allocated"].as_int()), used = static_cast<std::uint64_t>(q["used"].as_int());
    out.set("total", total);
    out.set("free", total > used ? total - used : 0);
    return true;
}

}  // namespace pbc::store
