#include "core/http_client.hpp"

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

}  // namespace pbc::http
