#include "core/http_client.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/tls.hpp"

namespace pbc::http {

bool parse_url(const std::string& url, Url& out) {
    out = Url{};
    std::string rest;
    if (url.rfind("http://", 0) == 0) rest = url.substr(7);
    else if (url.rfind("https://", 0) == 0) { rest = url.substr(8); out.tls = true; out.port = 443; }
    else return false;
    const auto slash = rest.find('/');
    std::string hostport = rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out.port = static_cast<std::uint16_t>(std::atoi(hostport.c_str() + colon + 1));
        hostport.resize(colon);
    }
    out.host = hostport;
    return !out.host.empty() && out.port != 0;
}

std::string resolve(const std::string& base, const std::string& rel) {
    if (rel.rfind("http://", 0) == 0 || rel.rfind("https://", 0) == 0) return rel;
    const auto q = base.find('?');
    const std::string b = base.substr(0, q);
    if (!rel.empty() && rel[0] == '/') {
        Url u;
        if (!parse_url(b, u)) return rel;
        return std::string(u.tls ? "https://" : "http://") + u.host + ":" + std::to_string(u.port) + rel;
    }
    return b.substr(0, b.rfind('/') + 1) + rel;
}

namespace {

std::function<int(const std::string&, std::string&)> g_dialer;

int connect_to(const Url& u, int timeout_s, std::string& error) {
    if (u.host.rfind("wan-", 0) == 0) {  // console à distance : par la liaison Internet
        if (!g_dialer) { error = "liaison Internet indisponible"; return -1; }
        const int fd = g_dialer(u.host.substr(4), error);
        if (fd >= 0) {
            timeval tv{timeout_s > 0 ? timeout_s : 20, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        }
        return fd;
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(u.port);
    if (::inet_pton(AF_INET, u.host.c_str(), &a.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (::getaddrinfo(u.host.c_str(), nullptr, &hints, &res) != 0 || !res) { error = "adresse introuvable : " + u.host; return -1; }
        a.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { error = "socket impossible"; return -1; }
    timeval tv{timeout_s, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    // Connexion non bloquante limitée à 5 s : sinon le système attend plus d'une minute une machine injoignable.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (rc != 0 && errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        rc = ::poll(&p, 1, 5000) == 1 ? 0 : -1;
        int err = 0;
        socklen_t len = sizeof err;
        if (rc == 0 && (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0)) rc = -1;
    }
    ::fcntl(fd, F_SETFL, flags);
    if (rc != 0) {
        error = "connexion impossible à " + u.host + ":" + std::to_string(u.port);
        ::close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const char* p, std::size_t n) {
    while (n) {
        const ssize_t w = ::send(fd, p, n, 0);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

// Flux : socket nue ou session TLS.
struct Stream {
    int fd = -1;
    tls::Session* tls = nullptr;
    ssize_t recv(char* p, std::size_t n) { return tls ? tls::read(tls, p, n) : ::recv(fd, p, n, 0); }
    bool send(const char* p, std::size_t n) { return tls ? tls::write_all(tls, p, n) : send_all(fd, p, n); }
    void close() { if (tls) tls::close(tls); tls = nullptr; if (fd >= 0) ::close(fd); fd = -1; }
};

struct Head { bool chunked = false; std::string location; };

// Lit les en-têtes ; ce qui suit déjà reçu est laissé dans `extra`.
bool read_head(Stream& st, Response& r, std::string& extra, Head* h = nullptr) {
    std::string buf;
    char tmp[8192];
    std::size_t end;
    while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 65536) { r.error = "en-têtes trop longs"; return false; }
        const ssize_t n = st.recv(tmp, sizeof tmp);
        if (n <= 0) { r.error = "réponse interrompue"; return false; }
        buf.append(tmp, static_cast<std::size_t>(n));
    }
    const std::string head = buf.substr(0, end);
    extra = buf.substr(end + 4);
    const auto sp = head.find(' ');
    r.status = sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
    for (std::size_t pos = head.find("\r\n"); pos != std::string::npos;) {
        const auto next = head.find("\r\n", pos + 2);
        std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        pos = next;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value[0] == ' ') value.erase(0, 1);
        if (name == "content-length") r.length = std::strtoull(value.c_str(), nullptr, 10);
        if (h && name == "transfer-encoding" && value.find("chunked") != std::string::npos) h->chunked = true;
        if (h && name == "location") h->location = value;
    }
    return true;
}

bool open_stream(const Url& u, int timeout_s, Stream& st, std::string& error) {
    st.fd = connect_to(u, timeout_s, error);
    if (st.fd < 0) return false;
    if (u.tls) {
        st.tls = tls::open(st.fd, u.host, error);
        if (!st.tls) { ::close(st.fd); st.fd = -1; return false; }
    }
    return true;
}

}  // namespace

void set_dialer(std::function<int(const std::string&, std::string&)> dialer) { g_dialer = std::move(dialer); }

Response get(const std::string& url, const std::function<bool(const char*, std::size_t)>& sink, int timeout_s) {
    Response r;
    std::string current = url;
    for (int hop = 0; hop < 4; ++hop) {  // redirections suivies (3 au plus)
        r = Response{};
        Url u;
        if (!parse_url(current, u)) { r.error = "adresse non prise en charge"; return r; }
        Stream st;
        if (!open_stream(u, timeout_s, st, r.error)) return r;
        const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nUser-Agent: Mozilla/5.0 (PBC-Engine)\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        std::string extra;
        Head h;
        if (!st.send(req.data(), req.size())) { r.error = "envoi impossible"; st.close(); return r; }
        if (!read_head(st, r, extra, &h)) { st.close(); return r; }
        if ((r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308) && !h.location.empty()) {
            st.close();
            current = h.location.rfind("http", 0) == 0 ? h.location : resolve(current, h.location);
            continue;
        }
        if (r.status != 200) { r.error = "réponse " + std::to_string(r.status); st.close(); return r; }
        char buf[65536];
        bool ok = true;
        if (h.chunked) {
            // « chunked » : taille en hexadécimal, ligne, données, ligne vide… jusqu'au bloc de taille 0.
            std::string pending = extra;
            auto fill = [&](std::size_t need) {
                while (pending.size() < need) {
                    const ssize_t n = st.recv(buf, sizeof buf);
                    if (n <= 0) return false;
                    pending.append(buf, static_cast<std::size_t>(n));
                }
                return true;
            };
            for (;;) {
                std::size_t eol;
                while ((eol = pending.find("\r\n")) == std::string::npos) if (!fill(pending.size() + 1)) { r.error = "réception interrompue"; break; }
                if (!r.error.empty()) break;
                const std::size_t size = std::strtoull(pending.c_str(), nullptr, 16);
                pending.erase(0, eol + 2);
                if (size == 0) break;
                if (!fill(size + 2)) { r.error = "réception interrompue"; break; }
                r.length += size;
                ok = sink(pending.data(), size);
                pending.erase(0, size + 2);
                if (!ok) break;
            }
        } else {
            std::uint64_t got = extra.size();
            ok = extra.empty() || sink(extra.data(), extra.size());
            while (ok && (!r.length || got < r.length)) {
                const ssize_t n = st.recv(buf, sizeof buf);
                if (n < 0) { if (!r.length) break; r.error = "réception interrompue"; break; }
                if (n == 0) break;
                got += static_cast<std::uint64_t>(n);
                ok = sink(buf, static_cast<std::size_t>(n));
            }
            if (r.error.empty() && r.length && got < r.length && ok) r.error = "fichier incomplet";
        }
        if (!ok && r.error.empty()) r.error = "abandon";
        st.close();
        return r;
    }
    r.error = "trop de redirections";
    return r;
}

Response get_text(const std::string& url, std::string& body, std::size_t limit) {
    body.clear();
    return get(url, [&](const char* p, std::size_t n) { if (body.size() + n > limit) return false; body.append(p, n); return true; });
}

Response post_raw(const std::string& url, const std::string& host_file, std::string& body, int timeout_s) {
    Response r;
    body.clear();
    Url u;
    if (!parse_url(url, u)) { r.error = "adresse non prise en charge"; return r; }
    FILE* f = std::fopen(host_file.c_str(), "rb");
    if (!f) { r.error = "fichier illisible"; return r; }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    const int fd = connect_to(u, timeout_s, r.error);
    if (fd < 0) { std::fclose(f); return r; }
    const std::string head = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                             std::to_string(size) + "\r\n\r\n";
    bool ok = send_all(fd, head.data(), head.size());
    char buf[65536];
    for (std::size_t n; ok && (n = std::fread(buf, 1, sizeof buf, f)) > 0;) ok = send_all(fd, buf, n);
    std::fclose(f);
    std::string extra;
    Stream st;
    st.fd = fd;
    if (!ok) r.error = "envoi impossible";
    else if (read_head(st, r, extra)) {
        body = extra;
        for (ssize_t n; (!r.length || body.size() < r.length) && (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) body.append(buf, static_cast<std::size_t>(n));
        if (r.status != 200) r.error = "réponse " + std::to_string(r.status);
    }
    ::close(fd);
    return r;
}

Response request(const std::string& method, const std::string& url, const std::string& token, const std::string& json_body, std::string& out, int timeout_s) {
    Response r;
    out.clear();
    Url u;
    if (!parse_url(url, u) || u.tls) { r.error = "adresse non prise en charge"; return r; }
    const int fd = connect_to(u, timeout_s, r.error);
    if (fd < 0) return r;
    std::string head = method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\n";
    if (!token.empty()) head += "X-PBC-Token: " + token + "\r\n";
    if (!json_body.empty() || method != "GET") head += "Content-Type: application/json\r\nContent-Length: " + std::to_string(json_body.size()) + "\r\n";
    head += "\r\n";
    bool ok = send_all(fd, head.data(), head.size()) && (json_body.empty() || send_all(fd, json_body.data(), json_body.size()));
    std::string extra;
    Stream st;
    st.fd = fd;
    if (!ok) r.error = "envoi impossible";
    else if (read_head(st, r, extra)) {
        out = extra;
        char buf[65536];
        for (ssize_t n; (!r.length || out.size() < r.length) && out.size() < (16u << 20) && (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) out.append(buf, static_cast<std::size_t>(n));
        if (r.status != 200 && r.error.empty()) r.error = "réponse " + std::to_string(r.status);
    } else if (r.error.empty()) r.error = "réponse illisible";
    ::close(fd);
    return r;
}

Response request_raw(const std::string& method, const std::string& url, const std::string& token, const std::string& data, std::string& out, int timeout_s) {
    Response r;
    out.clear();
    Url u;
    if (!parse_url(url, u) || u.tls) { r.error = "adresse non prise en charge"; return r; }
    const int fd = connect_to(u, timeout_s, r.error);
    if (fd < 0) return r;
    std::string head = method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\nContent-Type: application/octet-stream\r\n";
    if (!token.empty()) head += "X-PBC-Token: " + token + "\r\n";
    head += "Content-Length: " + std::to_string(data.size()) + "\r\n\r\n";
    bool ok = send_all(fd, head.data(), head.size()) && (data.empty() || send_all(fd, data.data(), data.size()));
    std::string extra;
    Stream st;
    st.fd = fd;
    if (!ok) r.error = "envoi impossible";
    else if (read_head(st, r, extra)) {
        out = extra;
        char buf[65536];
        for (ssize_t n; (!r.length || out.size() < r.length) && out.size() < (1u << 20) && (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) out.append(buf, static_cast<std::size_t>(n));
        if (r.status != 200 && r.error.empty()) r.error = "réponse " + std::to_string(r.status);
    } else if (r.error.empty()) r.error = "réponse illisible";
    ::close(fd);
    return r;
}

Response put_file(const std::string& url, const std::string& token, const std::string& host_file, std::string& out, int timeout_s) {
    Response r;
    out.clear();
    Url u;
    if (!parse_url(url, u) || u.tls) { r.error = "adresse non prise en charge"; return r; }
    FILE* f = std::fopen(host_file.c_str(), "rb");
    if (!f) { r.error = "fichier illisible"; return r; }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    const int fd = connect_to(u, timeout_s, r.error);
    if (fd < 0) { std::fclose(f); return r; }
    std::string head = "PUT " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\nContent-Type: application/octet-stream\r\n";
    if (!token.empty()) head += "X-PBC-Token: " + token + "\r\n";
    head += "Content-Length: " + std::to_string(size) + "\r\n\r\n";
    bool ok = send_all(fd, head.data(), head.size());
    char buf[65536];
    for (std::size_t n; ok && (n = std::fread(buf, 1, sizeof buf, f)) > 0;) ok = send_all(fd, buf, n);
    std::fclose(f);
    std::string extra;
    Stream st;
    st.fd = fd;
    if (!ok) r.error = "envoi impossible";
    else if (read_head(st, r, extra)) {
        out = extra;
        for (ssize_t n; (!r.length || out.size() < r.length) && out.size() < (1u << 20) && (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) out.append(buf, static_cast<std::size_t>(n));
        if (r.status != 200 && r.error.empty()) r.error = "réponse " + std::to_string(r.status);
    } else if (r.error.empty()) r.error = "réponse illisible";
    ::close(fd);
    return r;
}

Response post_file(const std::string& url, const std::string& field, const std::string& filename, const std::string& data) {
    Response r;
    Url u;
    if (!parse_url(url, u)) { r.error = "adresse non prise en charge"; return r; }
    const int fd = connect_to(u, 20, r.error);
    if (fd < 0) return r;
    const std::string b = "PBCENGINE_" + std::to_string(::getpid());
    const std::string head = "--" + b + "\r\nContent-Disposition: form-data; name=\"" + field + "\"; filename=\"" + filename +
                             "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    const std::string tail = "\r\n--" + b + "--\r\n";
    const std::string req = "POST " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\nContent-Type: multipart/form-data; boundary=" + b +
                            "\r\nContent-Length: " + std::to_string(head.size() + data.size() + tail.size()) + "\r\n\r\n";
    std::string extra;
    if (!send_all(fd, req.data(), req.size()) || !send_all(fd, head.data(), head.size()) || !send_all(fd, data.data(), data.size()) ||
        !send_all(fd, tail.data(), tail.size()))
        r.error = "envoi impossible";
    else if (Stream st{fd, nullptr}; read_head(st, r, extra) && r.status != 200)
        r.error = "réponse " + std::to_string(r.status);
    ::close(fd);
    return r;
}

// --- Requêtes générales ------------------------------------------------------------------------------------------

std::string Reply::header(const std::string& lower_name) const {
    for (const auto& h : headers) if (h.name == lower_name) return h.value;
    return {};
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

bool open_call(const Url& u, const Call& c, Stream& st, std::string& error, std::string& fp) {
    st.fd = connect_to(u, c.timeout_s, error);
    if (st.fd < 0) return false;
    if (!u.tls) return true;
    tls::Options o;
    o.verify = c.verify_tls;
    st.tls = tls::open(st.fd, u.host, error, o);
    if (!st.tls) { ::close(st.fd); st.fd = -1; return false; }
    if (!c.verify_tls) {
        fp = tls::fingerprint(st.tls);
        if (!c.pin.empty() && lower(c.pin) != fp) {
            error = "le certificat de l'appareil a changé (empreinte différente) : vérifiez l'appareil puis reconnectez-le";
            st.close();
            return false;
        }
    }
    return true;
}

// En-tête de la requête. body_length < 0 : corps « chunked » ; -2 : pas de corps.
std::string request_head(const Call& c, const Url& u, std::int64_t body_length, bool keep_alive = false) {
    std::string h = c.method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host;
    if (u.port != (u.tls ? 443 : 80)) h += ":" + std::to_string(u.port);
    h += keep_alive ? "\r\nUser-Agent: PBC-Engine\r\nConnection: keep-alive\r\n" : "\r\nUser-Agent: PBC-Engine\r\nConnection: close\r\n";
    for (const auto& x : c.headers) {
        const std::string n = lower(x.name);
        if (n == "host" || n == "connection" || n == "content-length" || n == "transfer-encoding") continue;
        h += x.name + ": " + x.value + "\r\n";
    }
    if (body_length == -1) h += "Transfer-Encoding: chunked\r\n";
    else if (body_length >= 0) h += "Content-Length: " + std::to_string(body_length) + "\r\n";
    return h + "\r\n";
}

// En-têtes de la réponse (les « 100 Continue » sont sautés) ; ce qui suit déjà reçu est laissé dans `extra`.
bool reply_head(Stream& st, Reply& r, std::string& extra) {
    std::string buf = std::move(extra);
    char tmp[8192];
    for (;;) {
        std::size_t end;
        while ((end = buf.find("\r\n\r\n")) == std::string::npos) {
            if (buf.size() > 65536) { r.error = "en-têtes trop longs"; return false; }
            const ssize_t n = st.recv(tmp, sizeof tmp);
            if (n <= 0) { r.error = "réponse interrompue"; return false; }
            buf.append(tmp, static_cast<std::size_t>(n));
        }
        const std::string head = buf.substr(0, end);
        buf.erase(0, end + 4);
        const auto sp = head.find(' ');
        r.status = sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
        r.headers.clear();
        for (std::size_t pos = head.find("\r\n"); pos != std::string::npos;) {
            const auto next = head.find("\r\n", pos + 2);
            const std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
            pos = next;
            const auto colon = line.find(':');
            if (colon != std::string::npos) r.headers.push_back({lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1))});
        }
        if (r.status >= 100 && r.status < 200) continue;  // réponse intermédiaire
        extra = std::move(buf);
        return true;
    }
}

// Corps de la réponse : « chunked », Content-Length, ou jusqu'à la fermeture.
bool reply_body(Stream& st, std::string pending, const Reply& r, bool head_request,
                const std::function<bool(const char*, std::size_t)>& sink, std::string& error) {
    if (head_request || r.status == 204 || r.status == 304) return true;
    char buf[65536];
    if (lower(r.header("transfer-encoding")).find("chunked") != std::string::npos) {
        auto fill = [&](std::size_t need) {
            while (pending.size() < need) {
                const ssize_t n = st.recv(buf, sizeof buf);
                if (n <= 0) return false;
                pending.append(buf, static_cast<std::size_t>(n));
            }
            return true;
        };
        for (;;) {
            std::size_t eol;
            while ((eol = pending.find("\r\n")) == std::string::npos)
                if (!fill(pending.size() + 1)) { error = "réception interrompue"; return false; }
            const std::size_t size = std::strtoull(pending.c_str(), nullptr, 16);
            pending.erase(0, eol + 2);
            if (size == 0) return true;
            if (!fill(size + 2)) { error = "réception interrompue"; return false; }
            if (!sink(pending.data(), size)) { error = "abandon"; return false; }
            pending.erase(0, size + 2);
        }
    }
    const std::string cl = r.header("content-length");
    const bool known = !cl.empty();
    const std::uint64_t want = known ? std::strtoull(cl.c_str(), nullptr, 10) : 0;
    std::uint64_t got = 0;
    if (!pending.empty()) {
        const std::size_t n = known && pending.size() > want ? static_cast<std::size_t>(want) : pending.size();
        if (!sink(pending.data(), n)) { error = "abandon"; return false; }
        got = n;
    }
    while (!known || got < want) {
        const std::size_t room = known && want - got < sizeof buf ? static_cast<std::size_t>(want - got) : sizeof buf;
        const ssize_t n = st.recv(buf, room);
        if (n == 0) break;
        if (n < 0) { if (known) { error = "réception interrompue"; return false; } break; }
        if (!sink(buf, static_cast<std::size_t>(n))) { error = "abandon"; return false; }
        got += static_cast<std::uint64_t>(n);
    }
    if (known && got < want) { error = "réponse incomplète"; return false; }
    return true;
}

bool is_redirect(int s) { return s == 301 || s == 302 || s == 303 || s == 307 || s == 308; }

// Requête avec corps en mémoire. `sink` nul : réponse gardée dans Reply.body (limit) ; sinon les réponses 2xx
// partent dans `sink`, les autres restent dans Reply.body (64 Ko au plus).
// Le corps de cette réponse a-t-il une fin connue (la connexion peut alors resservir) ?
bool delimited(const Reply& r, bool head_request) {
    if (head_request || r.status == 204 || r.status == 304) return true;
    return lower(r.header("transfer-encoding")).find("chunked") != std::string::npos || !r.header("content-length").empty();
}

}  // namespace

struct Conn::State {
    Stream st;
    std::string key;  // hôte:port:tls:épinglage
    std::string fingerprint;
};

Conn::Conn() : s_(new State) {}
Conn::~Conn() { close(); delete s_; }
void Conn::close() { s_->st.close(); s_->key.clear(); }

namespace {

std::string conn_key(const Url& u, const Call& c) {
    return u.host + ":" + std::to_string(u.port) + (u.tls ? ":s:" : ":p:") + (c.verify_tls ? "v" : "n") + c.pin;
}

Reply run(const Call& first, const std::string& body, std::size_t limit, const std::function<bool(const char*, std::size_t)>* sink,
          Conn::State* keep = nullptr) {
    Call c = first;
    for (int hop = 0;; ++hop) {
        Reply r;
        Url u;
        if (!parse_url(c.url, u)) { r.error = "adresse non prise en charge"; return r; }
        Stream own;
        Stream* stp = &own;
        const bool reuse = keep && keep->st.fd >= 0 && keep->key == conn_key(u, c);
        if (keep && !reuse) keep->st.close();
        const bool has_body = !body.empty() || c.method == "POST" || c.method == "PUT" || c.method == "PATCH";
        const std::string head = request_head(c, u, has_body ? static_cast<std::int64_t>(body.size()) : -2, keep != nullptr);
        std::string extra;
        // Une connexion gardée a pu être fermée par le serveur entre deux requêtes : un seul nouvel essai, sur une neuve.
        for (int attempt = 0;; ++attempt) {
            if (keep) {
                stp = &keep->st;
                if (stp->fd < 0) {
                    if (!open_call(u, c, *stp, r.error, keep->fingerprint)) return r;
                    keep->key = conn_key(u, c);
                }
                r.fingerprint = keep->fingerprint;
            } else if (!open_call(u, c, own, r.error, r.fingerprint)) {
                return r;
            }
            const bool sent = stp->send(head.data(), head.size()) && (body.empty() || stp->send(body.data(), body.size()));
            extra.clear();
            if (sent && reply_head(*stp, r, extra)) break;
            const bool retry = keep && reuse && attempt == 0;
            stp->close();
            if (!retry) { if (r.error.empty()) r.error = "envoi impossible"; return r; }
            r = Reply{};
        }
        Stream& st = *stp;
        const std::string location = r.header("location");
        if (is_redirect(r.status) && !location.empty() && hop < c.redirects && (c.method == "GET" || c.method == "HEAD")) {
            st.close();
            const std::string next = location.rfind("http", 0) == 0 ? location : resolve(c.url, location);
            Url nu;
            if (parse_url(next, nu) && nu.host != u.host)  // autre serveur : on ne lui donne pas nos identifiants
                for (auto it = c.headers.begin(); it != c.headers.end();) it = lower(it->name) == "authorization" ? c.headers.erase(it) : it + 1;
            c.url = next;
            continue;
        }
        const bool to_sink = sink && r.status >= 200 && r.status < 300;
        const std::size_t cap = to_sink ? 0 : (sink ? 65536 : limit);
        bool over = false;
        std::string err;
        const bool done = reply_body(st, std::move(extra), r, c.method == "HEAD",
            [&](const char* p, std::size_t n) {
                if (to_sink) return (*sink)(p, n);
                if (r.body.size() + n > cap) { over = true; return false; }
                r.body.append(p, n);
                return true;
            }, err);
        if (over) r.error = "réponse trop longue";
        else if (!done) r.error = err;
        const bool reusable = keep && done && !over && delimited(r, c.method == "HEAD") && lower(r.header("connection")) != "close";
        if (!reusable) st.close();
        return r;
    }
}

}  // namespace

Reply call(const Call& c, const std::string& body, std::size_t limit) { return run(c, body, limit, nullptr); }

Reply call(const Call& c, Conn& conn, const std::string& body, std::size_t limit) { return run(c, body, limit, nullptr, conn.s_); }

Reply fetch(const Call& c, const std::function<bool(const char*, std::size_t)>& sink) { return run(c, {}, 0, &sink); }

struct Upload::State {
    Call call;
    Url url;
    Stream st;
    bool chunked = false, broken = false, open = false;
    std::int64_t length = 0;
    std::uint64_t sent = 0;
    std::string error, fingerprint, frame;
};

Upload::Upload() : s_(new State) {}
Upload::~Upload() { abort(); delete s_; }
const std::string& Upload::error() const { return s_->error; }

bool Upload::begin(const Call& c, std::int64_t length) {
    abort();
    *s_ = State{};
    s_->call = c;
    s_->chunked = length < 0;
    s_->length = length;
    if (!parse_url(c.url, s_->url)) { s_->error = "adresse non prise en charge"; return false; }
    if (!open_call(s_->url, c, s_->st, s_->error, s_->fingerprint)) return false;
    s_->open = true;
    const std::string head = request_head(c, s_->url, s_->chunked ? -1 : length);
    if (!s_->st.send(head.data(), head.size())) { s_->error = "envoi impossible"; abort(); return false; }
    return true;
}

bool Upload::write(const char* p, std::size_t n) {
    if (!s_->open || s_->broken) return false;
    if (!n) return true;
    if (!s_->chunked && s_->sent + n > static_cast<std::uint64_t>(s_->length)) { s_->error = "plus de données que prévu"; s_->broken = true; return false; }
    bool ok;
    if (s_->chunked) {
        char size[24];
        const int k = std::snprintf(size, sizeof size, "%zx\r\n", n);
        s_->frame.assign(size, static_cast<std::size_t>(k));  // un seul envoi par morceau (un seul paquet TLS)
        s_->frame.append(p, n);
        s_->frame.append("\r\n", 2);
        ok = s_->st.send(s_->frame.data(), s_->frame.size());
    } else {
        ok = s_->st.send(p, n);
    }
    if (!ok) { s_->error = "envoi interrompu"; s_->broken = true; return false; }
    s_->sent += n;
    return true;
}

Reply Upload::finish(std::size_t limit) {
    Reply r;
    r.fingerprint = s_->fingerprint;
    if (!s_->open) { r.error = s_->error.empty() ? "envoi non commencé" : s_->error; return r; }
    if (!s_->broken) {
        if (s_->chunked && !s_->st.send("0\r\n\r\n", 5)) { s_->error = "envoi interrompu"; s_->broken = true; }
        else if (!s_->chunked && s_->sent != static_cast<std::uint64_t>(s_->length)) { s_->error = "envoi incomplet"; s_->broken = true; }
    }
    // Même après un envoi interrompu, le serveur a souvent répondu (401, 413…) : on lit sa réponse.
    std::string extra;
    if (reply_head(s_->st, r, extra)) {
        bool over = false;
        std::string err;
        const bool done = reply_body(s_->st, std::move(extra), r, false,
            [&](const char* p, std::size_t n) { if (r.body.size() + n > limit) { over = true; return false; } r.body.append(p, n); return true; }, err);
        if (over) r.error = "réponse trop longue";
        else if (!done) r.error = err;
    }
    if (s_->broken && (r.error.empty() || r.status == 0)) r.error = s_->error;
    else if (s_->broken && r.ok()) { r.status = 0; r.error = s_->error; }  // jamais « réussi » si tout n'est pas parti
    abort();
    return r;
}

void Upload::abort() {
    if (s_->open) s_->st.close();
    s_->open = false;
}

struct Download::State {
    Stream st;
    std::string pending;
    bool open = false, chunked = false, known = false, done = false, crlf = false;
    std::uint64_t want = 0, got = 0, chunk_left = 0;
    // Prochains octets : d'abord ceux déjà reçus, sinon la socket.
    long take(char* p, std::size_t n) {
        if (!pending.empty()) {
            const std::size_t k = std::min(n, pending.size());
            std::memcpy(p, pending.data(), k);
            pending.erase(0, k);
            return static_cast<long>(k);
        }
        return static_cast<long>(st.recv(p, n));
    }
    bool fill_line() {
        char buf[4096];
        while (pending.find("\r\n") == std::string::npos) {
            const ssize_t n = st.recv(buf, sizeof buf);
            if (n <= 0) return false;
            pending.append(buf, static_cast<std::size_t>(n));
        }
        return true;
    }
};

Download::Download() : s_(new State) {}
Download::~Download() { close(); delete s_; }

bool Download::begin(const Call& first, Reply& r) {
    close();
    *s_ = State{};
    Call c = first;
    for (int hop = 0;; ++hop) {
        r = Reply{};
        Url u;
        if (!parse_url(c.url, u)) { r.error = "adresse non prise en charge"; return false; }
        if (!open_call(u, c, s_->st, r.error, r.fingerprint)) return false;
        const std::string head = request_head(c, u, -2);
        if (!s_->st.send(head.data(), head.size())) { r.error = "envoi impossible"; s_->st.close(); return false; }
        std::string extra;
        if (!reply_head(s_->st, r, extra)) { s_->st.close(); return false; }
        const std::string location = r.header("location");
        if (is_redirect(r.status) && !location.empty() && hop < c.redirects && c.method == "GET") {
            s_->st.close();
            const std::string next = location.rfind("http", 0) == 0 ? location : resolve(c.url, location);
            Url nu;
            if (parse_url(next, nu) && nu.host != u.host)
                for (auto it = c.headers.begin(); it != c.headers.end();) it = lower(it->name) == "authorization" ? c.headers.erase(it) : it + 1;
            c.url = next;
            continue;
        }
        if (r.status < 200 || r.status >= 300) {
            std::string err;
            reply_body(s_->st, std::move(extra), r, false, [&](const char* p, std::size_t n) {
                if (r.body.size() + n > 65536) return false;
                r.body.append(p, n);
                return true;
            }, err);
            s_->st.close();
            return false;
        }
        s_->open = true;
        s_->pending = std::move(extra);
        s_->chunked = lower(r.header("transfer-encoding")).find("chunked") != std::string::npos;
        const std::string cl = r.header("content-length");
        s_->known = !s_->chunked && !cl.empty();
        s_->want = s_->known ? std::strtoull(cl.c_str(), nullptr, 10) : 0;
        s_->done = r.status == 204 || (s_->known && s_->want == 0);
        return true;
    }
}

long Download::read(char* p, std::size_t n) {
    State& s = *s_;
    if (!s.open) return -1;
    if (s.done || !n) return 0;
    if (s.chunked) {
        if (s.chunk_left == 0) {
            if (s.crlf) {  // fin du morceau précédent
                if (!s.fill_line()) return -1;
                s.pending.erase(0, s.pending.find("\r\n") + 2);
                s.crlf = false;
            }
            if (!s.fill_line()) return -1;
            const auto eol = s.pending.find("\r\n");
            const std::uint64_t size = std::strtoull(s.pending.c_str(), nullptr, 16);
            s.pending.erase(0, eol + 2);
            if (size == 0) { s.done = true; return 0; }
            s.chunk_left = size;
        }
        const long k = s.take(p, static_cast<std::size_t>(std::min<std::uint64_t>(n, s.chunk_left)));
        if (k <= 0) return -1;
        s.chunk_left -= static_cast<std::uint64_t>(k);
        if (s.chunk_left == 0) s.crlf = true;
        return k;
    }
    if (s.known) {
        if (s.got >= s.want) { s.done = true; return 0; }
        const long k = s.take(p, static_cast<std::size_t>(std::min<std::uint64_t>(n, s.want - s.got)));
        if (k <= 0) return -1;  // coupé avant la fin annoncée
        s.got += static_cast<std::uint64_t>(k);
        return k;
    }
    const long k = s.take(p, n);
    if (k <= 0) { s.done = true; return k < 0 ? -1 : 0; }
    return k;
}

void Download::close() {
    if (s_->open) s_->st.close();
    s_->open = false;
}

}  // namespace pbc::http
