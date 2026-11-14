#include "store/s3.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <set>

#include "core/http_client.hpp"
#include "core/util.hpp"

namespace pbc::store {

namespace {

// ---- Empreintes : SHA-256 brut et HMAC-SHA256 (signature v4) ----
std::string sha_raw(std::string_view s) {
    self_update_sha256 h{};
    self_update_sha256_start(&h);
    self_update_sha256_add(&h, s.data(), s.size());
    unsigned char d[32];
    self_update_sha256_finish(&h, d);
    return std::string(reinterpret_cast<const char*>(d), 32);
}

std::string hex(const std::string& raw) {
    static const char* x = "0123456789abcdef";
    std::string out;
    for (unsigned char c : raw) { out += x[c >> 4]; out += x[c & 15]; }
    return out;
}

std::string hmac(std::string key, std::string_view msg) {
    if (key.size() > 64) key = sha_raw(key);
    key.resize(64, '\0');
    std::string in(64, '\0'), out(64, '\0');
    for (int i = 0; i < 64; ++i) { in[i] = static_cast<char>(key[i] ^ 0x36); out[i] = static_cast<char>(key[i] ^ 0x5c); }
    return sha_raw(out + sha_raw(in + std::string(msg)));
}

const char* kEmptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// Encodage URI d'AWS : tout sauf [A-Za-z0-9-._~] ; « / » gardé dans les chemins, encodé dans les paramètres.
std::string uri(const std::string& s, bool keep_slash) {
    static const char* x = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || (keep_slash && c == '/')) out += static_cast<char>(c);
        else { out += '%'; out += x[c >> 4]; out += x[c & 15]; }
    }
    return out;
}

std::string xml_text(std::string s) {
    const std::pair<const char*, const char*> ents[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#13;", "\r"}, {"&#10;", "\n"}, {"&amp;", "&"}};
    for (const auto& [e, r] : ents)
        for (std::size_t p = 0; (p = s.find(e, p)) != std::string::npos; p += std::strlen(r)) s.replace(p, std::strlen(e), r);
    return s;
}

// Contenu de la prochaine balise <name>…</name> à partir de pos (le XML de S3 n'a pas de préfixe).
bool block(const std::string& x, const std::string& name, std::size_t& pos, std::string& inner) {
    const std::string open = "<" + name + ">", close = "</" + name + ">";
    const std::size_t a = x.find(open, pos);
    if (a == std::string::npos) return false;
    const std::size_t b = x.find(close, a);
    if (b == std::string::npos) return false;
    inner = x.substr(a + open.size(), b - a - open.size());
    pos = b + close.size();
    return true;
}

std::string value(const std::string& x, const std::string& name) {
    std::size_t p = 0;
    std::string v;
    return block(x, name, p, v) ? xml_text(v) : std::string();
}

std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return static_cast<std::int64_t>(era) * 146097 + (yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
}

// « 2026-11-13T14:05:00.000Z » (listes) ou « Thu, 13 Nov 2026 14:05:00 GMT » (en-têtes) → secondes (0 si illisible).
std::int64_t parse_time(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6) return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {};
    const auto comma = s.find(',');
    if (std::sscanf(s.c_str() + (comma == std::string::npos ? 0 : comma + 1), " %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &se) != 6) return 0;
    const char* f = mon[0] ? std::strstr(months, mon) : nullptr;
    if (!f) return 0;
    return days_from_civil(y, static_cast<int>(f - months) / 3 + 1, d) * 86400 + h * 3600 + mi * 60 + se;
}

// Écart entre l'heure de la console et celle du service (S3 refuse une requête datée à plus de 15 min).
std::atomic<std::int64_t> g_skew{0};

std::string amz_date() {
    const std::time_t t = static_cast<std::time_t>(now_unix() + g_skew.load());
    std::tm tmv{};
    ::gmtime_r(&t, &tmv);
    char b[32];
    std::strftime(b, sizeof b, "%Y%m%dT%H%M%SZ", &tmv);
    return b;
}

std::string why(const http::Reply& r) {
    if (!r.error.empty()) return r.error;
    const std::string code = value(r.body, "Code");
    if (code == "InvalidAccessKeyId") return "clé d'accès inconnue du service";
    if (code == "SignatureDoesNotMatch") return "clé secrète refusée";
    if (code == "NoSuchBucket") return "compartiment introuvable";
    if (code == "AuthorizationHeaderMalformed" || code == "PermanentRedirect" || code == "IllegalLocationConstraintException")
        return "ce compartiment est dans une autre région : vérifiez la région";
    if (code == "RequestTimeTooSkewed") return "l'heure de la PS5 est trop différente de celle du service : réglez la date et l'heure de la console";
    if (code == "AccessDenied" || r.status == 403) return "accès refusé : la clé n'a pas les droits sur ce compartiment";
    if (code == "QuotaExceeded" || r.status == 507) return "plus de place sur le stockage";
    if (r.status == 404) return "introuvable sur le stockage";
    if (r.status == 301) return "ce compartiment est dans une autre région : vérifiez la région";
    return "réponse " + std::to_string(r.status) + " du service" + (code.empty() ? std::string() : " (" + code + ")");
}

using Query = std::vector<std::pair<std::string, std::string>>;

struct Shared {
    std::mutex mu;
    http::Conn conn;
    std::set<std::string> dirs;
};

class Client {
public:
    explicit Client(FtpConfig c) : cfg_(std::move(c)), shared_(std::make_shared<Shared>()) {
        std::string ep = s3_endpoint(cfg_);
        scheme_ = "https://";
        if (ep.rfind("http://", 0) == 0) { scheme_ = "http://"; ep = ep.substr(7); }
        else if (ep.rfind("https://", 0) == 0) ep = ep.substr(8);
        while (!ep.empty() && ep.back() == '/') ep.pop_back();
        // Compartiment dans le nom d'hôte (Amazon, Backblaze) ; dans le chemin pour les autres services (tous l'acceptent),
        // et quand son nom a un point (le certificat *.s3… ne le couvrirait pas).
        virtual_ = (cfg_.service == "aws" || cfg_.service == "b2") && cfg_.bucket.find('.') == std::string::npos;
        host_ = virtual_ ? cfg_.bucket + "." + ep : ep;
        region_ = cfg_.service == "r2" ? "auto" : cfg_.region.empty() ? "us-east-1" : cfg_.region;
    }
    // Chemin de l'appareil (« /PBC Backup/… ») → clé dans le compartiment, sans « / » au début ni à la fin. Le
    // dossier choisi (root) est un chemin comme un autre, comme pour le FTP et le WebDAV.
    static std::string key(const std::string& path) {
        std::string p = path;
        while (!p.empty() && p.front() == '/') p.erase(0, 1);
        while (!p.empty() && p.back() == '/') p.pop_back();
        return p;
    }
    std::string resource(const std::string& k) const {  // chemin d'URL encodé
        if (virtual_) return "/" + uri(k, true);
        return "/" + uri(cfg_.bucket, false) + (k.empty() ? std::string() : "/" + uri(k, true));
    }
    std::string copy_source(const std::string& k) const { return "/" + uri(cfg_.bucket, false) + "/" + uri(k, true); }

    http::Call make(const char* method, const std::string& k, Query q, http::Headers extra, const std::string& payload_hash) const {
        std::sort(q.begin(), q.end());
        std::string qs;
        for (const auto& [n, v] : q) qs += (qs.empty() ? "" : "&") + uri(n, false) + "=" + uri(v, false);
        S3Sign s;
        s.method = method;
        s.host = host_;
        s.path = resource(k);
        s.query = qs;
        s.amz_date = amz_date();
        s.region = region_;
        s.payload_hash = payload_hash;
        for (const auto& h : extra) s.headers.push_back({h.name, h.value});
        http::Call c;
        c.method = method;
        c.url = scheme_ + host_ + s.path + (qs.empty() ? "" : "?" + qs);
        c.headers = std::move(extra);
        c.headers.push_back({"x-amz-date", s.amz_date});
        c.headers.push_back({"x-amz-content-sha256", payload_hash});
        c.headers.push_back({"Authorization", s3_authorization(s, cfg_.user, cfg_.password)});
        c.timeout_s = 120;
        c.redirects = 0;
        return c;
    }
    // Requête complète sur la connexion gardée ; une seconde chance si l'heure de la console est décalée.
    http::Reply send(const char* method, const std::string& k, const Query& q = {}, const http::Headers& extra = {},
                     const std::string& body = {}, std::size_t limit = 16u << 20) const {
        const std::string ph = body.empty() ? std::string(kEmptyHash) : sha256_hex(body);
        for (int attempt = 0;; ++attempt) {
            http::Reply r;
            {
                std::lock_guard<std::mutex> l(shared_->mu);
                r = http::call(make(method, k, q, extra, ph), shared_->conn, body, limit);
            }
            if (attempt == 0 && fix_clock(r)) continue;
            return r;
        }
    }
    bool fix_clock(const http::Reply& r) const {
        if (r.status != 403 || r.body.find("RequestTimeTooSkewed") == std::string::npos) return false;
        const std::int64_t server = parse_time(r.header("date"));
        if (!server) return false;
        g_skew = server - now_unix();
        log("s3: heure de la console décalée de %lld s, corrigée", static_cast<long long>(g_skew.load()));
        return true;
    }
    bool known_dir(const std::string& d) const { std::lock_guard<std::mutex> l(shared_->mu); return shared_->dirs.count(d) != 0; }
    void remember_dir(const std::string& d) const { std::lock_guard<std::mutex> l(shared_->mu); shared_->dirs.insert(d); }
    void forget_dirs(const std::string& prefix) const {
        std::lock_guard<std::mutex> l(shared_->mu);
        for (auto it = shared_->dirs.begin(); it != shared_->dirs.end();)
            it = it->rfind(prefix, 0) == 0 ? shared_->dirs.erase(it) : std::next(it);
    }
    const FtpConfig& cfg() const { return cfg_; }

    // Liste d'un « dossier » (délimiteur « / ») ou de tout ce qui est dessous (delimiter vide). found : quelque
    // chose existe sous ce préfixe (fichier, sous-dossier ou repère de dossier).
    bool list(const std::string& pfx, bool shallow, std::vector<fs::Entry>* entries, std::vector<std::pair<std::string, std::uint64_t>>* keys,
              bool& found, std::string& err, int max_keys = 1000) const {
        found = false;
        std::string token;
        for (;;) {
            Query q = {{"list-type", "2"}, {"prefix", pfx}, {"max-keys", std::to_string(max_keys)}};
            if (shallow) q.push_back({"delimiter", "/"});
            if (!token.empty()) q.push_back({"continuation-token", token});
            const http::Reply r = send("GET", "", q);
            if (r.status != 200) { err = why(r); return false; }
            std::size_t pos = 0;
            std::string b;
            while (block(r.body, "Contents", pos, b)) {
                const std::string k = value(b, "Key");
                found = true;
                if (k == pfx) continue;  // repère du dossier lui-même
                const std::uint64_t size = std::strtoull(value(b, "Size").c_str(), nullptr, 10);
                if (keys) keys->push_back({k, size});
                if (entries) {
                    fs::Entry e;
                    e.name = k.substr(pfx.size());
                    if (e.name.empty() || e.name.find('/') != std::string::npos) continue;
                    e.size = size;
                    e.mtime = parse_time(value(b, "LastModified"));
                    entries->push_back(e);
                }
            }
            pos = 0;
            while (block(r.body, "CommonPrefixes", pos, b)) {
                found = true;
                if (!entries) continue;
                std::string name = value(b, "Prefix").substr(pfx.size());
                while (!name.empty() && name.back() == '/') name.pop_back();
                if (name.empty()) continue;
                fs::Entry e;
                e.name = name;
                e.dir = true;
                entries->push_back(e);
            }
            if (max_keys < 1000 || value(r.body, "IsTruncated") != "true") return true;
            token = value(r.body, "NextContinuationToken");
            if (token.empty()) return true;
        }
    }
    bool head(const std::string& k, fs::Entry& e) const {
        if (k.empty()) return false;
        const http::Reply r = send("HEAD", k);
        if (r.status != 200) return false;
        e = fs::Entry{};
        e.size = std::strtoull(r.header("content-length").c_str(), nullptr, 10);
        e.mtime = parse_time(r.header("last-modified"));
        return true;
    }

    // Copie côté service (rien ne passe par la console) ; au-delà de 5 Go, par morceaux d'1 Go.
    bool copy(const std::string& from, const std::string& to, std::uint64_t size) const {
        if (size <= (5ull << 30)) {
            const http::Reply r = send("PUT", to, {}, {{"x-amz-copy-source", copy_source(from)}});
            return r.ok() && r.body.find("<Error>") == std::string::npos;
        }
        const http::Reply init = send("POST", to, {{"uploads", ""}});
        const std::string id = value(init.body, "UploadId");
        if (!init.ok() || id.empty()) return false;
        std::string parts;
        int n = 0;
        for (std::uint64_t at = 0; at < size; at += (1ull << 30)) {
            const std::uint64_t end = std::min<std::uint64_t>(size, at + (1ull << 30)) - 1;
            const http::Reply r = send("PUT", to, {{"partNumber", std::to_string(++n)}, {"uploadId", id}},
                                       {{"x-amz-copy-source", copy_source(from)}, {"x-amz-copy-source-range", "bytes=" + std::to_string(at) + "-" + std::to_string(end)}});
            const std::string etag = value(r.body, "ETag");
            if (!r.ok() || etag.empty()) { send("DELETE", to, {{"uploadId", id}}); return false; }
            parts += "<Part><PartNumber>" + std::to_string(n) + "</PartNumber><ETag>" + etag + "</ETag></Part>";
        }
        const http::Reply done = send("POST", to, {{"uploadId", id}}, {}, "<CompleteMultipartUpload>" + parts + "</CompleteMultipartUpload>");
        return done.ok() && done.body.find("<Error>") == std::string::npos;
    }

private:
    FtpConfig cfg_;
    std::string scheme_, host_, region_;
    bool virtual_ = true;
    std::shared_ptr<Shared> shared_;
};

// Écriture : un petit fichier part en une fois ; au-delà d'un morceau, envoi en plusieurs parties (multipart),
// rendu visible seulement à la fin (commit), effacé par abort. Morceaux de 16 Mo, plus gros pour les très gros
// fichiers (S3 accepte 10 000 morceaux au plus).
class S3Writer : public Writer {
public:
    S3Writer(const Client& c, std::string key) : c_(c), key_(std::move(key)) {}
    ~S3Writer() override { if (!done_) abort(); }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        buf_.append(p, n);
        if (buf_.size() >= part_size() && !send_part()) { failed_ = true; return false; }
        return true;
    }
    bool commit(std::string& err) override {
        done_ = true;
        if (failed_) { err = "envoi interrompu vers le stockage" + (err_.empty() ? std::string() : " (" + err_ + ")"); cancel(); return false; }
        if (id_.empty()) {
            const http::Reply r = c_.send("PUT", key_, {}, {}, buf_);
            if (!r.ok()) { err = "envoi refusé : " + why(r); return false; }
            return true;
        }
        if (!buf_.empty() && !send_part()) { err = "envoi refusé : " + err_; cancel(); return false; }
        const http::Reply r = c_.send("POST", key_, {{"uploadId", id_}}, {}, "<CompleteMultipartUpload>" + parts_ + "</CompleteMultipartUpload>");
        if (!r.ok() || r.body.find("<Error>") != std::string::npos) { err = "envoi refusé : " + why(r); cancel(); return false; }
        return true;
    }
    void abort() override {
        done_ = true;
        cancel();
    }

private:
    std::size_t part_size() const { return (16u << 20) * static_cast<std::size_t>(1 + n_ / 1000); }
    bool send_part() {
        if (id_.empty()) {
            const http::Reply r = c_.send("POST", key_, {{"uploads", ""}});
            id_ = value(r.body, "UploadId");
            if (!r.ok() || id_.empty()) { err_ = why(r); return false; }
        }
        const http::Reply r = c_.send("PUT", key_, {{"partNumber", std::to_string(n_ + 1)}, {"uploadId", id_}}, {}, buf_);
        const std::string etag = r.header("etag");
        if (!r.ok() || etag.empty()) { err_ = why(r); return false; }
        ++n_;
        parts_ += "<Part><PartNumber>" + std::to_string(n_) + "</PartNumber><ETag>" + etag + "</ETag></Part>";
        buf_.clear();
        return true;
    }
    void cancel() {
        if (!id_.empty()) c_.send("DELETE", key_, {{"uploadId", id_}});
        id_.clear();
        buf_.clear();
        buf_.shrink_to_fit();
    }
    Client c_;  // copie (partage la connexion) : l'écriture peut finir après le magasin
    std::string key_, buf_, id_, parts_, err_;
    int n_ = 0;
    bool failed_ = false, done_ = false;
};

class S3Reader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class S3Store : public Store {
public:
    explicit S3Store(FtpConfig c) : c_(std::move(c)) {}
    std::string label() const override { return "S3 " + c_.cfg().bucket; }
    // Pas de vrais dossiers en S3 : un repère vide « dossier/ » le fait exister (liste, stat) avant ses fichiers.
    bool mkdirs(const std::string& dir) override {
        const std::string k = c_.key(dir);
        if (k.empty() || c_.known_dir(k)) return true;
        if (!c_.send("PUT", k + "/").ok()) return false;
        c_.remember_dir(k);
        return true;
    }
    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        if (c_.key(path).empty()) { err = "chemin vide"; return nullptr; }
        return std::make_unique<S3Writer>(c_, c_.key(path));
    }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        auto r = std::make_unique<S3Reader>();
        http::Headers h;
        if (offset) h.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
        http::Reply head;
        bool ok = r->dl.begin(c_.make("GET", c_.key(path), {}, h, kEmptyHash), head);
        if (!ok && c_.fix_clock(head)) ok = r->dl.begin(c_.make("GET", c_.key(path), {}, h, kEmptyHash), head);
        if (!ok) { err = "lecture refusée : " + why(head); return nullptr; }
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
        const http::Reply r = c_.send("GET", c_.key(path), {}, {}, {}, static_cast<std::size_t>(limit));
        if (!r.ok()) return false;
        out = r.body;
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        return c_.send("PUT", c_.key(path), {}, {}, std::string(data)).ok();
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        const std::string k = c_.key(path);
        if (k.empty()) { out = fs::Entry{}; out.dir = true; out.name = "/"; return true; }
        if (c_.head(k, out)) { out.name = std::string(fs::basename(path)); return true; }
        bool found = false;
        std::string err;
        if (!c_.list(k + "/", true, nullptr, nullptr, found, err, 1) || !found) return false;
        out = fs::Entry{};
        out.dir = true;
        out.name = std::string(fs::basename(path));
        return true;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        const std::string k = c_.key(dir);
        bool found = false;
        std::string err;
        if (!c_.list(k.empty() ? std::string() : k + "/", true, &out, nullptr, found, err)) return false;
        if (!found && !k.empty()) return false;  // dossier absent
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }
    bool space(const std::string&, fs::Space&) override { return false; }  // S3 ne donne pas de limite
    bool remove(const std::string& path) override {  // dossiers compris
        const std::string k = c_.key(path);
        if (k.empty()) return false;
        c_.forget_dirs(k);
        std::vector<std::pair<std::string, std::uint64_t>> keys;
        bool found = false;
        std::string err;
        fs::Entry e;
        if (c_.head(k, e)) keys.push_back({k, e.size});
        if (!c_.list(k + "/", false, nullptr, &keys, found, err)) return false;
        if (found) keys.push_back({k + "/", 0});  // repère du dossier (rendu par la liste mais pas dans keys)
        if (keys.empty()) return false;
        bool ok = true;
        for (const auto& [key, size] : keys) ok = c_.send("DELETE", key).ok() && ok;
        return ok;
    }
    bool rename(const std::string& from, const std::string& to) override {
        const std::string a = c_.key(from), b = c_.key(to);
        if (a.empty() || b.empty()) return false;
        c_.forget_dirs(a);
        fs::Entry e;
        if (c_.head(a, e)) return c_.copy(a, b, e.size) && c_.send("DELETE", a).ok();
        std::vector<std::pair<std::string, std::uint64_t>> keys;
        bool found = false;
        std::string err;
        if (!c_.list(a + "/", false, nullptr, &keys, found, err) || !found) return false;
        keys.push_back({a + "/", 0});
        for (const auto& [key, size] : keys) {
            const std::string dst = b + key.substr(a.size());
            if (key.back() == '/' ? !c_.send("PUT", dst).ok() : !c_.copy(key, dst, size)) return false;
        }
        bool ok = true;
        for (const auto& [key, size] : keys) ok = c_.send("DELETE", key).ok() && ok;
        return ok;
    }

private:
    Client c_;
};

}  // namespace

std::string s3_endpoint(const FtpConfig& cfg) {
    const std::string& r = cfg.region;
    if (cfg.service == "aws") return "s3." + (r.empty() ? std::string("us-east-1") : r) + ".amazonaws.com";
    if (cfg.service == "b2") return "s3." + r + ".backblazeb2.com";
    if (cfg.service == "wasabi") return "s3." + (r.empty() ? std::string("us-east-1") : r) + ".wasabisys.com";
    if (cfg.service == "scaleway") return "s3." + (r.empty() ? std::string("fr-par") : r) + ".scw.cloud";
    if (cfg.service == "ovh") return "s3." + (r.empty() ? std::string("gra") : r) + ".io.cloud.ovh.net";
    if (cfg.service == "c2") return (r.empty() ? std::string("eu-002") : r) + ".s3.synologyc2.net";
    if (cfg.service == "r2") return "https://" + cfg.host + ".r2.cloudflarestorage.com";  // host = n° de compte Cloudflare
    return cfg.host;
}

bool s3_known_service(const std::string& s) {
    return s == "aws" || s == "b2" || s == "wasabi" || s == "scaleway" || s == "ovh" || s == "c2" || s == "r2" || s == "other";
}

std::string s3_authorization(const S3Sign& s, const std::string& access_key, const std::string& secret_key) {
    std::vector<std::pair<std::string, std::string>> h = {{"host", s.host}, {"x-amz-content-sha256", s.payload_hash}, {"x-amz-date", s.amz_date}};
    for (const auto& [n, v] : s.headers) {
        std::string ln = n;
        for (auto& ch : ln) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        std::string tv = v;
        while (!tv.empty() && tv.front() == ' ') tv.erase(0, 1);
        while (!tv.empty() && tv.back() == ' ') tv.pop_back();
        h.push_back({ln, tv});
    }
    std::sort(h.begin(), h.end());
    std::string canon_headers, signed_headers;
    for (const auto& [n, v] : h) {
        canon_headers += n + ":" + v + "\n";
        signed_headers += (signed_headers.empty() ? "" : ";") + n;
    }
    const std::string creq = s.method + "\n" + s.path + "\n" + s.query + "\n" + canon_headers + "\n" + signed_headers + "\n" + s.payload_hash;
    const std::string day = s.amz_date.substr(0, 8);
    const std::string scope = day + "/" + s.region + "/s3/aws4_request";
    const std::string to_sign = "AWS4-HMAC-SHA256\n" + s.amz_date + "\n" + scope + "\n" + hex(sha_raw(creq));
    const std::string k = hmac(hmac(hmac(hmac("AWS4" + secret_key, day), s.region), "s3"), "aws4_request");
    return "AWS4-HMAC-SHA256 Credential=" + access_key + "/" + scope + ",SignedHeaders=" + signed_headers + ",Signature=" + hex(hmac(k, to_sign));
}

std::unique_ptr<Store> s3(const FtpConfig& cfg, std::string& err) {
    if (cfg.bucket.empty()) { err = "compartiment manquant"; return nullptr; }
    if (cfg.user.empty() || cfg.user == "anonymous" || cfg.password.empty()) { err = "clé d'accès ou clé secrète manquante"; return nullptr; }
    if (cfg.service == "b2" && cfg.region.empty()) { err = "région manquante"; return nullptr; }
    if (cfg.service == "other" && cfg.host.empty()) { err = "adresse du service manquante"; return nullptr; }
    if (cfg.service == "r2" && cfg.host.empty()) { err = "n° de compte Cloudflare manquant"; return nullptr; }
    const Client c(cfg);
    bool found = false;
    const std::string root = Client::key(cfg.root);
    if (!c.list(root.empty() ? std::string() : root + "/", true, nullptr, nullptr, found, err, 1)) {
        log("s3 open failed bucket=%s error=%s", cfg.bucket.c_str(), err.c_str());
        return nullptr;
    }
    return std::make_unique<S3Store>(cfg);
}

}  // namespace pbc::store
