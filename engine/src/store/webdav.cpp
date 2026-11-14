#include "store/webdav.hpp"

#include <algorithm>
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

bool plain_http(const FtpConfig& c) { return c.port == 80 || c.port == 5005; }

std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

// Chemin → URL : chaque octet hors [A-Za-z0-9-._~/] est encodé (« [PS5] Jeu (ID) » compris).
std::string encode(const std::string& path) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

std::string decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    // Entités XML de base
    const std::pair<const char*, const char*> ents[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    for (const auto& [e, r] : ents)
        for (std::size_t p; (p = out.find(e)) != std::string::npos;) out.replace(p, std::strlen(e), r);
    return out;
}

std::string trim_slash(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p.empty() ? "/" : p;
}

// Contenu de la prochaine balise de nom local `name` (préfixe d'espace de noms ignoré : <d:href>, <D:href>, <href>).
bool tag(const std::string& x, const char* name, std::size_t& pos, std::string& inner) {
    for (std::size_t i = x.find('<', pos); i != std::string::npos; i = x.find('<', i + 1)) {
        if (i + 1 >= x.size() || x[i + 1] == '/' || x[i + 1] == '?' || x[i + 1] == '!') continue;
        const std::size_t e = x.find_first_of(" \t\r\n/>", i + 1);
        if (e == std::string::npos) return false;
        const std::string q = x.substr(i + 1, e - i - 1);
        const auto colon = q.find(':');
        if ((colon == std::string::npos ? q : q.substr(colon + 1)) != name) continue;
        const std::size_t gt = x.find('>', e);
        if (gt == std::string::npos) return false;
        if (x[gt - 1] == '/') { inner.clear(); pos = gt + 1; return true; }
        const std::size_t close = x.find("</" + q, gt);
        if (close == std::string::npos) return false;
        inner = x.substr(gt + 1, close - gt - 1);
        pos = close + q.size() + 3;
        return true;
    }
    return false;
}

std::string value(const std::string& block, const char* name) {
    std::size_t pos = 0;
    std::string v;
    return tag(block, name, pos, v) ? v : std::string();
}

// « Thu, 13 Nov 2026 14:05:00 GMT » → secondes depuis 1970 (0 si illisible).
std::int64_t http_date(const std::string& s) {
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto comma = s.find(',');
    int d = 0, y = 0, h = 0, mi = 0, se = 0;
    char mon[4] = {};
    if (std::sscanf(s.c_str() + (comma == std::string::npos ? 0 : comma + 1), " %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &se) != 6) return 0;
    const char* f = std::strstr(months, mon);
    if (!f || !mon[0]) return 0;
    const int m = static_cast<int>(f - months) / 3 + 1;
    // Jours depuis le 1er janvier 1970 (calendrier grégorien)
    const int yy = y - (m <= 2);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const int yoe = yy - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

const char* kProps =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/>"
    "<d:getlastmodified/></d:prop></d:propfind>";
const char* kQuota =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:quota-available-bytes/>"
    "<d:quota-used-bytes/></d:prop></d:propfind>";

std::string why(const http::Reply& r) {
    if (!r.error.empty()) return r.error;
    if (r.status == 401 || r.status == 403) return "identifiant ou mot de passe refusé par le NAS";
    if (r.status == 404) return "dossier introuvable sur le NAS";
    if (r.status == 405) return "le NAS refuse WebDAV à cette adresse : vérifiez que WebDAV Server est activé";
    if (r.status == 411) return "ce serveur WebDAV refuse les envois sans taille annoncée";
    if (r.status == 507) return "plus de place sur le NAS";
    return "réponse " + std::to_string(r.status) + " du NAS";
}

// Accès au serveur : une connexion gardée ouverte (keep-alive) partagée par le magasin et ses écritures, et le mémo
// des dossiers déjà créés ou vus (une sauvegarde ne recrée pas « PBC Backup/PS5 backup/… » pour chaque fichier).
struct Shared {
    std::mutex mu;
    http::Conn conn;
    std::set<std::string> dirs;
};

class Dav {
public:
    explicit Dav(FtpConfig c) : cfg_(std::move(c)), shared_(std::make_shared<Shared>()) {
        auth_ = "Basic " + base64(cfg_.user + ":" + cfg_.password);
        base_ = std::string(plain_http(cfg_) ? "http://" : "https://") + cfg_.host + ":" + std::to_string(cfg_.port);
    }
    std::string url(const std::string& path) const { return base_ + encode(path.empty() || path[0] != '/' ? "/" + path : path); }
    http::Call call(const char* method, const std::string& path, http::Headers extra = {}) const {
        http::Call c;
        c.method = method;
        c.url = url(path);
        c.headers = {{"Authorization", auth_}};
        for (auto& h : extra) c.headers.push_back(std::move(h));
        c.verify_tls = cfg_.pin.empty();
        c.pin = cfg_.pin;
        c.timeout_s = 60;
        return c;
    }
    // Requête courte sur la connexion gardée.
    http::Reply send(const http::Call& c, const std::string& body = {}, std::size_t limit = 16u << 20) const {
        std::lock_guard<std::mutex> l(shared_->mu);
        return http::call(c, shared_->conn, body, limit);
    }
    http::Reply propfind(const std::string& path, const char* depth, const char* body = kProps) const {
        return send(call("PROPFIND", path, {{"Depth", depth}, {"Content-Type", "application/xml; charset=utf-8"}}), body);
    }
    bool move(const std::string& from, const std::string& to) const {
        return send(call("MOVE", from, {{"Destination", url(to)}, {"Overwrite", "T"}})).ok();
    }
    bool known_dir(const std::string& d) const { std::lock_guard<std::mutex> l(shared_->mu); return shared_->dirs.count(d) != 0; }
    void remember_dir(const std::string& d) const { std::lock_guard<std::mutex> l(shared_->mu); shared_->dirs.insert(d); }
    void forget_dirs(const std::string& prefix) const {
        std::lock_guard<std::mutex> l(shared_->mu);
        for (auto it = shared_->dirs.begin(); it != shared_->dirs.end();)
            it = it->rfind(prefix, 0) == 0 ? shared_->dirs.erase(it) : std::next(it);
    }
    const FtpConfig& cfg() const { return cfg_; }

private:
    FtpConfig cfg_;
    std::string auth_, base_;
    std::shared_ptr<Shared> shared_;
};

bool parse_entry(const std::string& block, fs::Entry& e, std::string& href) {
    href = decode(value(block, "href"));
    if (href.empty()) return false;
    if (href.rfind("http", 0) == 0) {  // adresse complète : on garde le chemin
        const auto s = href.find('/', href.find("//") + 2);
        href = s == std::string::npos ? "/" : href.substr(s);
    }
    href = trim_slash(href);
    std::size_t p = 0;
    std::string rt;
    e.dir = tag(block, "resourcetype", p, rt) && rt.find("collection") != std::string::npos;
    e.size = std::strtoull(value(block, "getcontentlength").c_str(), nullptr, 10);
    e.mtime = http_date(value(block, "getlastmodified"));
    e.name = std::string(fs::basename(href));
    return true;
}

// Écriture : les 8 premiers Mo restent en mémoire. Un petit fichier part en une fois avec sa taille (accepté par
// tous les serveurs) ; au-delà, envoi en flux (« chunked »). Toujours dans <chemin>.part, renommé par commit().
class DavWriter : public Writer {
public:
    DavWriter(const Dav& d, std::string path) : d_(d), path_(std::move(path)) {}
    ~DavWriter() override { if (!done_) abort(); }
    bool write(const char* p, std::size_t n) override {
        if (failed_) return false;
        if (!streaming_) {
            buf_.append(p, n);
            if (buf_.size() <= kHold) return true;
            if (!up_.begin(d_.call("PUT", path_ + ".part"), -1) || !up_.write(buf_.data(), buf_.size())) { failed_ = true; return false; }
            streaming_ = true;
            buf_.clear();
            buf_.shrink_to_fit();
            return true;
        }
        if (!up_.write(p, n)) { failed_ = true; return false; }
        return true;
    }
    bool commit(std::string& err) override {
        done_ = true;
        if (failed_) { err = "envoi interrompu vers le NAS" + (up_.error().empty() ? std::string() : " (" + up_.error() + ")"); up_.abort(); remove_part(); return false; }
        const http::Reply r = streaming_ ? up_.finish() : d_.send(d_.call("PUT", path_ + ".part"), buf_);
        if (!r.ok()) { err = "envoi refusé : " + why(r); remove_part(); return false; }
        if (!d_.move(path_ + ".part", path_)) { err = "le NAS n'a pas pu renommer le fichier envoyé"; remove_part(); return false; }
        return true;
    }
    void abort() override {
        done_ = true;
        up_.abort();
        if (streaming_) remove_part();
    }

private:
    void remove_part() { d_.send(d_.call("DELETE", path_ + ".part")); }
    static constexpr std::size_t kHold = 8u << 20;
    Dav d_;  // copie (partage la connexion) : l'écriture peut finir après le magasin
    std::string path_, buf_;
    http::Upload up_;
    bool streaming_ = false, failed_ = false, done_ = false;
};

class DavReader : public Reader {
public:
    long read(char* p, std::size_t n) override { return dl.read(p, n); }
    http::Download dl;
};

class DavStore : public Store {
public:
    explicit DavStore(FtpConfig c) : d_(std::move(c)) {}
    std::string label() const override { return "WebDAV " + d_.cfg().host; }
    // Crée le dossier (et ses parents si besoin). D'abord le dossier lui-même : en général ses parents existent déjà,
    // une seule requête suffit. Une réponse « déjà là » (405) est vérifiée : à la racine d'un Synology, 405 veut dire
    // « interdit ici » (seuls les dossiers partagés y sont permis), pas « existe ».
    bool mkdirs(const std::string& dir) override {
        const std::string d = trim_slash(dir);
        if (d == "/" || d_.known_dir(d)) return true;
        http::Reply r = d_.send(d_.call("MKCOL", d + "/"));
        if (r.status == 409 || r.status == 404) {  // parent absent
            if (!mkdirs(std::string(fs::dirname(d)))) return false;
            r = d_.send(d_.call("MKCOL", d + "/"));
        }
        bool ok = r.status == 201;
        if (!ok && (r.status == 405 || r.status == 301 || r.status == 302)) {
            fs::Entry e;
            ok = stat(d, e) && e.dir;
        }
        if (ok) d_.remember_dir(d);
        return ok;
    }
    std::unique_ptr<Writer> create(const std::string& path, std::string& err) override {
        if (!mkdirs(std::string(fs::dirname(path)))) { err = "dossier impossible à créer sur le NAS"; return nullptr; }
        return std::make_unique<DavWriter>(d_, path);
    }
    std::unique_ptr<Reader> open(const std::string& path, std::string& err) override { return open_at(path, 0, err); }
    std::unique_ptr<Reader> open_at(const std::string& path, std::uint64_t offset, std::string& err) override {
        auto r = std::make_unique<DavReader>();
        http::Headers h;
        if (offset) h.push_back({"Range", "bytes=" + std::to_string(offset) + "-"});
        http::Reply head;
        if (!r->dl.begin(d_.call("GET", path, h), head)) { err = "lecture refusée : " + why(head); return nullptr; }
        if (offset && head.status != 206) {  // le serveur ignore la reprise : on saute jusqu'au décalage
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
        const http::Reply r = d_.send(d_.call("GET", path), {}, static_cast<std::size_t>(limit));
        if (!r.ok()) return false;
        out = r.body;
        return true;
    }
    bool write_small(const std::string& path, std::string_view data) override {
        const std::string part = path + ".part";
        return d_.send(d_.call("PUT", part), std::string(data)).ok() && d_.move(part, path);
    }
    bool stat(const std::string& path, fs::Entry& out) override {
        const http::Reply r = d_.propfind(path, "0");
        if (r.status != 207) return false;
        std::size_t pos = 0;
        std::string block, href;
        return tag(r.body, "response", pos, block) && parse_entry(block, out, href);
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override {
        out.clear();
        const std::string self = trim_slash(dir);
        const http::Reply r = d_.propfind(self == "/" ? "/" : self + "/", "1");
        if (r.status != 207) return false;
        std::size_t pos = 0;
        std::string block, href;
        while (tag(r.body, "response", pos, block)) {
            fs::Entry e;
            if (!parse_entry(block, e, href) || href == self || e.name.empty()) continue;
            out.push_back(e);
        }
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) { return a.name < b.name; });
        return true;
    }
    bool space(const std::string& path, fs::Space& out) override {
        const http::Reply r = d_.propfind(path, "0", kQuota);
        const std::string avail = value(r.body, "quota-available-bytes");
        if (r.status != 207 || avail.empty()) return false;  // le NAS ne le dit pas
        out.free = std::strtoull(avail.c_str(), nullptr, 10);
        out.total = out.free + std::strtoull(value(r.body, "quota-used-bytes").c_str(), nullptr, 10);
        return true;
    }
    bool remove(const std::string& path) override {  // dossiers compris
        d_.forget_dirs(trim_slash(path));
        return d_.send(d_.call("DELETE", path)).ok();
    }
    bool rename(const std::string& from, const std::string& to) override {
        d_.forget_dirs(trim_slash(from));
        return d_.move(from, to);
    }

private:
    Dav d_;
};

// Un appareil qui répond : PROPFIND du dossier de départ.
bool reach(const FtpConfig& cfg, std::string& err) {
    const Dav d(cfg);
    const http::Reply r = d.propfind(cfg.root.empty() ? "/" : cfg.root, "0");
    if (r.status == 207) return true;
    err = why(r);
    if (r.status == 404) err = "dossier « " + cfg.root + " » introuvable sur le NAS";
    return false;
}

}  // namespace

std::unique_ptr<Store> webdav(const FtpConfig& cfg, std::string& err) {
    if (cfg.host.empty()) { err = "adresse manquante"; return nullptr; }
    if (!reach(cfg, err)) { log("webdav open failed host=%s error=%s", cfg.host.c_str(), err.c_str()); return nullptr; }
    return std::make_unique<DavStore>(cfg);
}

bool webdav_trust(FtpConfig& cfg, std::string& err) {
    cfg.pin.clear();
    if (plain_http(cfg)) return reach(cfg, err);
    if (reach(cfg, err)) return true;  // certificat reconnu
    if (err.find("certificat") == std::string::npos && err.find("TLS") == std::string::npos) return false;
    // Certificat maison du NAS : accepté pour cet appareil, empreinte retenue.
    http::Call c;
    c.method = "OPTIONS";
    c.url = Dav(cfg).url(cfg.root.empty() ? "/" : cfg.root);
    c.verify_tls = false;
    const http::Reply probe = http::call(c);
    if (probe.fingerprint.empty()) {
        std::time_t now = std::time(nullptr);
        std::tm tmv{};
        ::gmtime_r(&now, &tmv);
        if (tmv.tm_year + 1900 < 2025) err += " (l'heure de la PS5 semble fausse : réglez la date et l'heure de la console)";
        return false;
    }
    cfg.pin = probe.fingerprint;
    err.clear();
    if (!reach(cfg, err)) { cfg.pin.clear(); return false; }
    log("webdav self-signed certificate accepted host=%s", cfg.host.c_str());
    return true;
}

}  // namespace pbc::store
