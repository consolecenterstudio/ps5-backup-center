#include <ctime>
#include "store/ftp.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/tls.hpp"
#include "core/util.hpp"

namespace pbc::store {

Json FtpConfig::to_json(bool with_password) const {
    Json j = Json::object();
    j.set("host", host);
    j.set("port", static_cast<std::uint64_t>(port));
    j.set("user", user);
    if (with_password) j.set("password", password);
    j.set("root", root);
    j.set("tls", tls);
    j.set("layout", layout);
    if (!domain.empty()) j.set("domain", domain);
    if (!proto.empty()) j.set("proto", proto);
    return j;
}

FtpConfig FtpConfig::from_json(const Json& j) {
    FtpConfig c;
    c.host = j["host"].as_string();
    c.port = static_cast<std::uint16_t>(j["port"].as_int(21));
    if (!j["user"].as_string().empty()) c.user = j["user"].as_string();
    c.password = j["password"].as_string();
    if (!j["root"].as_string().empty()) c.root = j["root"].as_string();
    const std::string t = j["tls"].as_string();
    if (t == "on" || t == "off" || t == "auto") c.tls = t;
    if (j["layout"].as_string() == "platform") c.layout = "platform";
    c.domain = j["domain"].as_string();
    if (j["proto"].as_string() == "smb" || j["proto"].as_string() == "ftp") c.proto = j["proto"].as_string();
    return c;
}

namespace {

int dial(const std::string& host, std::uint16_t port, std::string& err) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) { err = "adresse introuvable : " + host; return -1; }
        a.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { err = "socket impossible"; return -1; }
    timeval tv{30, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (rc != 0 && errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        rc = ::poll(&p, 1, 5000) == 1 ? 0 : -1;
        int e = 0;
        socklen_t len = sizeof e;
        if (rc == 0 && (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len) != 0 || e != 0)) rc = -1;
    }
    ::fcntl(fd, F_SETFL, flags);
    if (rc != 0) { ::close(fd); err = "connexion impossible à " + host + ":" + std::to_string(port); return -1; }
    return fd;
}

bool send_all(int fd, const char* p, std::size_t n) {
    while (n) {
        const ssize_t w = ::send(fd, p, n, 0);
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; return false; }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

// Une socket, nue ou chiffrée (TLS).
struct Conn {
    int fd = -1;
    tls::Session* s = nullptr;
    ssize_t recv(char* p, std::size_t n) { return s ? tls::read(s, p, n) : ::recv(fd, p, n, 0); }
    bool send(const char* p, std::size_t n) { return s ? tls::write_all(s, p, n) : send_all(fd, p, n); }
    void close() { if (s) tls::close(s); s = nullptr; if (fd >= 0) ::close(fd); fd = -1; }
    bool ok() const { return fd >= 0; }
};

// Une connexion de contrôle FTP.
class Ftp {
public:
    ~Ftp() { if (ctl_.ok()) { cmd("QUIT"); ctl_.close(); } }

    bool open(const FtpConfig& c, std::string& err) {
        host_ = c.host;
        ctl_.fd = dial(c.host, c.port, err);
        if (ctl_.fd < 0) return false;
        if (reply() / 100 != 2) { err = "l'appareil ne répond pas en FTP (" + last_ + ")"; return false; }
        // FTPS explicite : on chiffre avant d'envoyer le mot de passe. Certificat des NAS souvent
        // auto-signé : accepté sur le réseau local, empreinte journalisée.
        if (c.tls != "off") {
            if (cmd("AUTH TLS") == 234) {
                tls::Options o;
                o.verify = false;
                ctl_.s = tls::open(ctl_.fd, c.host, err, o);
                if (!ctl_.s) return false;
                buf_.clear();
                secure_ = cmd("PBSZ 0") / 100 == 2 && cmd("PROT P") / 100 == 2;
                if (!secure_) { err = "chiffrement des données refusé (" + last_ + ")"; return false; }
                log("ftp tls host=%s fingerprint=%s", c.host.c_str(), tls::fingerprint(ctl_.s).c_str());
            } else if (c.tls == "on") { err = "l'appareil ne propose pas le FTP chiffré (FTPS)"; return false; }
        }
        int code = cmd("USER " + c.user);
        if (code == 331) code = cmd("PASS " + c.password);
        if (code / 100 != 2) { err = "identifiant ou mot de passe refusé (" + last_ + ")"; return false; }
        cmd("TYPE I");
        mlsd_ = true;
        return true;
    }

    int cmd(const std::string& line) {
        const std::string l = line + "\r\n";
        if (!ctl_.send(l.data(), l.size())) { last_ = "connexion perdue"; return 0; }
        return reply();
    }

    // Réponse (éventuellement sur plusieurs lignes « 123- … 123 fin »).
    int reply() {
        std::string line;
        int code = 0;
        for (;;) {
            if (!read_line(line)) { last_ = "connexion perdue"; return 0; }
            if (line.size() >= 3 && std::isdigit(static_cast<unsigned char>(line[0]))) {
                const int c = std::atoi(line.substr(0, 3).c_str());
                if (!code) code = c;
                if (c == code && (line.size() == 3 || line[3] == ' ')) { last_ = line; return code; }
            }
        }
    }

    // Connexion de données en mode passif (chiffrée ensuite par secure(), après la réponse 150).
    Conn data(std::string& err) {
        Conn c;
        if (cmd("EPSV") == 229) {
            const auto a = last_.find("(|||"), b = last_.find('|', a + 4);
            if (a != std::string::npos && b != std::string::npos) {
                const int port = std::atoi(last_.substr(a + 4, b - a - 4).c_str());
                c.fd = dial(host_, static_cast<std::uint16_t>(port), err);
                if (c.fd >= 0) return c;
            }
        }
        if (cmd("PASV") != 227) { err = "mode passif refusé (" + last_ + ")"; return c; }
        const auto p = last_.find('(');
        int h1, h2, h3, h4, p1, p2;
        if (p == std::string::npos || std::sscanf(last_.c_str() + p + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) { err = "réponse PASV illisible"; return c; }
        // Certains serveurs annoncent une adresse interne : on garde l'hôte de la connexion de contrôle.
        c.fd = dial(host_, static_cast<std::uint16_t>(p1 * 256 + p2), err);
        return c;
    }
    // FTPS : la poignée de main TLS du canal de données se fait après « 150 », en reprenant la session
    // du contrôle (exigé par la plupart des serveurs, dont ceux des NAS Synology).
    bool secure(Conn& c) {
        if (!secure_ || c.s) return true;
        std::string err;
        tls::Options o;
        o.verify = false;
        o.resume = ctl_.s;
        c.s = tls::open(c.fd, host_, err, o);
        if (!c.s) log("ftp tls data failed host=%s error=%s", host_.c_str(), err.c_str());
        return c.s != nullptr;
    }

    bool mkdirs(const std::string& dir) {
        std::string cur;
        std::size_t start = 0;
        while (start < dir.size()) {
            auto end = dir.find('/', start);
            if (end == std::string::npos) end = dir.size();
            if (end > start) {
                cur += "/" + dir.substr(start, end - start);
                if (cmd("MKD " + cur) / 100 != 2 && cmd("CWD " + cur) / 100 != 2) return false;
            }
            start = end + 1;
        }
        return true;
    }

    bool list(const std::string& dir, std::vector<fs::Entry>& out) {
        out.clear();
        std::string text, err;
        // On se place dans le dossier puis on liste sans argument : certains serveurs (Synology)
        // prennent les crochets d'un chemin pour un motif (« [PS5] … » ne se listait pas).
        if (cmd("CWD " + dir) / 100 != 2) return false;  // dossier absent
        if (mlsd_) {
            Conn d = data(err);
            if (!d.ok()) return false;
            const int code = cmd("MLSD");
            if (code / 100 == 1) {
                if (!secure(d)) { d.close(); reply(); return false; }
                read_all(d, text);
                d.close();
                if (reply() / 100 != 2) return false;
                return parse_mlsd(text, out);
            }
            d.close();
            if (code == 550) return false;  // dossier absent
            mlsd_ = false;                   // serveur sans MLSD : LIST
        }
        Conn d = data(err);
        if (!d.ok()) return false;
        if (cmd("LIST") / 100 != 1) { d.close(); return false; }
        if (!secure(d)) { d.close(); reply(); return false; }
        read_all(d, text);
        d.close();
        if (reply() / 100 != 2) return false;
        return parse_list(text, out);
    }

    bool size(const std::string& p, std::uint64_t& out) {
        if (cmd("SIZE " + p) != 213) return false;
        out = std::strtoull(last_.c_str() + 4, nullptr, 10);
        return true;
    }

    const std::string& last() const { return last_; }
    bool secure() const { return secure_; }

private:
    bool read_line(std::string& line) {
        line.clear();
        for (;;) {
            const auto nl = buf_.find('\n');
            if (nl != std::string::npos) {
                line = buf_.substr(0, nl);
                buf_.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            char tmp[2048];
            const ssize_t n = ctl_.recv(tmp, sizeof tmp);
            if (n <= 0) return false;
            buf_.append(tmp, static_cast<std::size_t>(n));
        }
    }
    static void read_all(Conn& d, std::string& out) {
        char tmp[16384];
        for (ssize_t n; (n = d.recv(tmp, sizeof tmp)) > 0;) out.append(tmp, static_cast<std::size_t>(n));
    }
    static bool parse_mlsd(const std::string& text, std::vector<fs::Entry>& out) {
        std::size_t s = 0;
        while (s < text.size()) {
            auto e = text.find('\n', s);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(s, e - s);
            s = e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto sp = line.find(' ');
            if (sp == std::string::npos) continue;
            fs::Entry en;
            en.name = line.substr(sp + 1);
            const std::string facts = line.substr(0, sp);
            std::string type;
            for (std::size_t f = 0; f < facts.size();) {
                auto g = facts.find(';', f);
                if (g == std::string::npos) g = facts.size();
                const std::string kv = facts.substr(f, g - f);
                f = g + 1;
                const auto eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string k = kv.substr(0, eq);
                for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const std::string v = kv.substr(eq + 1);
                if (k == "type") { type = v; for (auto& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
                else if (k == "size") en.size = std::strtoull(v.c_str(), nullptr, 10);
                else if (k == "modify" && v.size() >= 14) {  // AAAAMMJJHHMMSS, en UTC
                    std::tm tm{};
                    tm.tm_year = std::atoi(v.substr(0, 4).c_str()) - 1900;
                    tm.tm_mon = std::atoi(v.substr(4, 2).c_str()) - 1;
                    tm.tm_mday = std::atoi(v.substr(6, 2).c_str());
                    tm.tm_hour = std::atoi(v.substr(8, 2).c_str());
                    tm.tm_min = std::atoi(v.substr(10, 2).c_str());
                    tm.tm_sec = std::atoi(v.substr(12, 2).c_str());
                    en.mtime = static_cast<std::int64_t>(timegm(&tm));
                }
            }
            if (type == "cdir" || type == "pdir" || en.name == "." || en.name == "..") continue;
            en.dir = type == "dir";
            out.push_back(std::move(en));
        }
        return true;
    }
    // LIST façon Unix : « drwxr-xr-x 1 owner group 4096 Nov 07 12:00 nom ».
    static bool parse_list(const std::string& text, std::vector<fs::Entry>& out) {
        std::size_t s = 0;
        while (s < text.size()) {
            auto e = text.find('\n', s);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(s, e - s);
            s = e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() < 10 || (line[0] != 'd' && line[0] != '-')) continue;
            int field = 0;
            std::size_t i = 0;
            std::string size;
            while (i < line.size() && field < 8) {
                while (i < line.size() && line[i] == ' ') ++i;
                const auto st = i;
                while (i < line.size() && line[i] != ' ') ++i;
                if (field == 4) size = line.substr(st, i - st);
                ++field;
            }
            while (i < line.size() && line[i] == ' ') ++i;
            fs::Entry en;
            en.name = line.substr(i);
            if (en.name == "." || en.name == ".." || en.name.empty()) continue;
            en.dir = line[0] == 'd';
            en.size = std::strtoull(size.c_str(), nullptr, 10);
            out.push_back(std::move(en));
        }
        return true;
    }

    Conn ctl_;
    std::string host_, buf_, last_;
    bool mlsd_ = true, secure_ = false;
};

// Le .part est renommé par commit() : un fichier à moitié envoyé ne porte jamais le nom final.
class FtpWriter : public Writer {
public:
    FtpWriter(Ftp& f, std::string path, Conn data) : f_(f), path_(std::move(path)), d_(data) {}
    ~FtpWriter() override { abort(); }
    bool write(const char* p, std::size_t n) override { return d_.ok() && d_.send(p, n); }
    bool commit(std::string& err) override {
        if (!d_.ok()) { err = "déjà fermé"; return false; }
        d_.close();
        if (f_.reply() / 100 != 2) { err = "envoi refusé (" + f_.last() + ")"; f_.cmd("DELE " + path_ + ".part"); return false; }
        f_.cmd("DELE " + path_);  // RNTO n'écrase pas partout
        if (f_.cmd("RNFR " + path_ + ".part") != 350 || f_.cmd("RNTO " + path_) / 100 != 2) { err = "renommage refusé (" + f_.last() + ")"; return false; }
        return true;
    }
    void abort() override {
        if (!d_.ok()) return;
        d_.close();
        f_.reply();
        f_.cmd("DELE " + path_ + ".part");
    }
private:
    Ftp& f_;
    std::string path_;
    Conn d_;
};

class FtpReader : public Reader {
public:
    FtpReader(Ftp& f, Conn data) : f_(f), d_(data) {}
    ~FtpReader() override { if (d_.ok()) { d_.close(); f_.reply(); } }
    long read(char* p, std::size_t n) override {
        const ssize_t r = d_.recv(p, n);
        return static_cast<long>(r);
    }
private:
    Ftp& f_;
    Conn d_;
};

class FtpStore : public Store {
public:
    FtpStore(FtpConfig c, std::unique_ptr<Ftp> f) : cfg_(std::move(c)), f_(std::move(f)) {}
    std::string label() const override { return (f_->secure() ? "FTPS " : "FTP ") + cfg_.host; }
    bool mkdirs(const std::string& dir) override { return f_->mkdirs(dir); }
    std::unique_ptr<Writer> create(const std::string& p, std::string& err) override {
        if (!f_->mkdirs(std::string(fs::dirname(p)))) { err = "dossier impossible à créer (" + f_->last() + ")"; return nullptr; }
        Conn d = f_->data(err);
        if (!d.ok()) return nullptr;
        if (f_->cmd("STOR " + p + ".part") / 100 != 1) { d.close(); err = "envoi refusé (" + f_->last() + ")"; return nullptr; }
        if (!f_->secure(d)) { d.close(); f_->reply(); err = "chiffrement du transfert impossible"; return nullptr; }
        return std::make_unique<FtpWriter>(*f_, p, d);
    }
    std::unique_ptr<Reader> open(const std::string& p, std::string& err) override {
        Conn d = f_->data(err);
        if (!d.ok()) return nullptr;
        if (f_->cmd("RETR " + p) / 100 != 1) { d.close(); err = "lecture refusée (" + f_->last() + ")"; return nullptr; }
        if (!f_->secure(d)) { d.close(); f_->reply(); err = "chiffrement du transfert impossible"; return nullptr; }
        return std::make_unique<FtpReader>(*f_, d);
    }
    std::unique_ptr<Reader> open_at(const std::string& p, std::uint64_t offset, std::string& err) override {
        Conn d = f_->data(err);
        if (!d.ok()) return nullptr;
        if (offset && f_->cmd("REST " + std::to_string(offset)) != 350) { d.close(); return Store::open_at(p, offset, err); }
        if (f_->cmd("RETR " + p) / 100 != 1) { d.close(); err = "lecture refusée (" + f_->last() + ")"; return nullptr; }
        if (!f_->secure(d)) { d.close(); f_->reply(); err = "chiffrement du transfert impossible"; return nullptr; }
        return std::make_unique<FtpReader>(*f_, d);
    }
    bool read_small(const std::string& p, std::string& out, std::uint64_t limit) override {
        out.clear();
        std::string err;
        auto r = open(p, err);
        if (!r) return false;
        char buf[16384];
        for (long n; (n = r->read(buf, sizeof buf)) > 0;) { out.append(buf, static_cast<std::size_t>(n)); if (out.size() > limit) return false; }
        return true;
    }
    bool write_small(const std::string& p, std::string_view data) override {
        std::string err;
        auto w = create(p, err);
        return w && w->write(data.data(), data.size()) && w->commit(err);
    }
    bool stat(const std::string& p, fs::Entry& out) override {
        out = {};
        out.name = std::string(fs::basename(p));
        if (f_->size(p, out.size)) return true;
        std::vector<fs::Entry> l;
        if (f_->list(std::string(fs::dirname(p)), l))
            for (const auto& e : l) if (e.name == out.name) { out = e; return true; }
        return false;
    }
    bool list(const std::string& dir, std::vector<fs::Entry>& out) override { return f_->list(dir, out); }
    bool space(const std::string&, fs::Space&) override { return false; }  // FTP ne le dit pas de façon standard
    bool remove(const std::string& p) override {
        std::vector<fs::Entry> l;
        if (f_->list(p, l) && f_->cmd("CWD " + p) / 100 == 2) {
            for (const auto& e : l) remove(p + "/" + e.name);
            return f_->cmd("RMD " + p) / 100 == 2;
        }
        return f_->cmd("DELE " + p) / 100 == 2;
    }
    bool rename(const std::string& from, const std::string& to) override {
        return f_->cmd("RNFR " + from) == 350 && f_->cmd("RNTO " + to) / 100 == 2;
    }
private:
    FtpConfig cfg_;
    std::unique_ptr<Ftp> f_;
};

}  // namespace

std::unique_ptr<Store> ftp(const FtpConfig& cfg, std::string& err) {
    auto f = std::make_unique<Ftp>();
    if (!f->open(cfg, err)) { log("ftp open failed host=%s error=%s", cfg.host.c_str(), err.c_str()); return nullptr; }
    return std::make_unique<FtpStore>(cfg, std::move(f));
}

}  // namespace pbc::store
