#include "link/wan.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "bearssl.h"
#include "core/fs.hpp"
#include "core/util.hpp"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace pbc::link::wan {

namespace {

constexpr char kMagic[4] = {'P', 'B', 'W', '1'};
constexpr std::size_t kMaxFrame = 1u << 20;

void rand_bytes(unsigned char* p, std::size_t n) {
    int fd = ::open("/dev/urandom", O_RDONLY);
    std::size_t got = 0;
    while (fd >= 0 && got < n) { const ssize_t r = ::read(fd, p + got, n - got); if (r <= 0) break; got += static_cast<std::size_t>(r); }
    if (fd >= 0) ::close(fd);
    if (got < n) {  // repli (ne devrait pas arriver) : mélange temps + adresse, jamais utilisé seul pour un secret long
        std::uint64_t x = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^ reinterpret_cast<std::uintptr_t>(p);
        for (std::size_t i = got; i < n; ++i) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; p[i] = static_cast<unsigned char>(x); }
    }
}

std::string hex(const unsigned char* p, std::size_t n) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (std::size_t i = 0; i < n; ++i) { o += h[p[i] >> 4]; o += h[p[i] & 15]; }
    return o;
}

bool unhex(const std::string& s, std::string& out) {
    if (s.size() % 2) return false;
    out.clear();
    auto v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (std::size_t i = 0; i < s.size(); i += 2) {
        const int a = v(s[i]), b = v(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out += static_cast<char>(a * 16 + b);
    }
    return true;
}

bool write_all(int fd, const char* p, std::size_t n) {
    while (n) {
        const ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && (errno == EINTR || errno == EAGAIN)) { pollfd q{fd, POLLOUT, 0}; if (::poll(&q, 1, 30000) <= 0) return false; continue; }
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

bool read_exact(int fd, unsigned char* p, std::size_t n, int timeout_ms) {
    while (n) {
        pollfd q{fd, POLLIN, 0};
        if (::poll(&q, 1, timeout_ms) <= 0) return false;
        const ssize_t r = ::recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

void nonce(std::uint64_t c, unsigned char iv[12]) {
    std::memset(iv, 0, 12);
    for (int i = 0; i < 8; ++i) iv[4 + i] = static_cast<unsigned char>(c >> (56 - 8 * i));
}

// Connexion TCP (5 s au plus) à host:port ; adresse IPv4 ou nom.
int tcp_connect(const std::string& host, std::uint16_t port, std::string& error, int* ms = nullptr) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) { error = "adresse introuvable : " + host; return -1; }
        a.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { error = "socket impossible"; return -1; }
    const auto t0 = std::chrono::steady_clock::now();
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
    if (rc != 0) { ::close(fd); error = "connexion impossible à " + host + ":" + std::to_string(port); return -1; }
    if (ms) *ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return fd;
}

// Paire de sockets locale : un côté pour HTTP en clair, l'autre relié au canal.
bool local_pair(int sv[2]) {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return false;
    timeval tv{120, 0};
    for (int i = 0; i < 2; ++i) { ::setsockopt(sv[i], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); ::setsockopt(sv[i], SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); }
#ifdef SO_NOSIGPIPE
    int one = 1;
    for (int i = 0; i < 2; ++i) ::setsockopt(sv[i], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return true;
}

// Présentation après la poignée de main : chacun envoie un JSON, l'autre répond.
bool exchange(Channel& ch, const Json& mine, Json& theirs, bool first, std::string& error) {
    std::string f;
    if (first) {
        if (!ch.send_frame(mine.dump())) { error = "envoi impossible"; return false; }
        if (ch.recv_frame(f, 15000) != 1 || !Json::parse(f, theirs) || !theirs.is_object()) { error = "réponse illisible"; return false; }
    } else {
        if (ch.recv_frame(f, 15000) != 1 || !Json::parse(f, theirs) || !theirs.is_object()) { error = "présentation illisible"; return false; }
        if (!ch.send_frame(mine.dump())) { error = "envoi impossible"; return false; }
    }
    return true;
}

const char kB32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";  // Crockford : sans I, L, O, U

}  // namespace

// --- Canal --------------------------------------------------------------------------------------------------------

Channel::~Channel() { close(); }

void Channel::close() {
    if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); }
    fd_ = -1;
}

std::string Channel::key_id(const std::string& secret) {
    br_sha256_context c;
    br_sha256_init(&c);
    static const char tag[] = "pbc-wan-id:";
    br_sha256_update(&c, tag, sizeof tag - 1);
    br_sha256_update(&c, secret.data(), secret.size());
    unsigned char d[32];
    br_sha256_out(&c, d);
    return hex(d, 16);
}

bool Channel::setup(const std::string& secret, const unsigned char* nc, const unsigned char* ns, bool is_client) {
    unsigned char salt[32], okm[64];
    std::memcpy(salt, nc, 16);
    std::memcpy(salt + 16, ns, 16);
    br_hkdf_context hc;
    br_hkdf_init(&hc, &br_sha256_vtable, salt, sizeof salt);
    br_hkdf_inject(&hc, secret.data(), secret.size());
    br_hkdf_flip(&hc);
    static const char info[] = "pbc-wan v1 keys";
    br_hkdf_produce(&hc, info, sizeof info - 1, okm, sizeof okm);
    std::memcpy(is_client ? k_send_ : k_recv_, okm, 32);       // client → serveur
    std::memcpy(is_client ? k_recv_ : k_send_, okm + 32, 32);  // serveur → client
    n_send_ = n_recv_ = 0;
    return true;
}

bool Channel::send(const char* p, std::size_t n) {
    if (fd_ < 0) return false;
    do {
        const std::size_t len = std::min<std::size_t>(n, 65536);
        std::string fr(4 + len + 16, '\0');
        unsigned char* b = reinterpret_cast<unsigned char*>(&fr[0]);
        b[0] = static_cast<unsigned char>(len >> 24); b[1] = static_cast<unsigned char>(len >> 16);
        b[2] = static_cast<unsigned char>(len >> 8); b[3] = static_cast<unsigned char>(len);
        if (len) std::memcpy(b + 4, p, len);
        unsigned char iv[12];
        nonce(n_send_++, iv);
        br_poly1305_ctmul_run(k_send_, iv, b + 4, len, b, 4, b + 4 + len, br_chacha20_ct_run, 1);
        if (!write_all(fd_, fr.data(), fr.size())) return false;
        p += len;
        n -= len;
    } while (n);
    return true;
}

bool Channel::heartbeat() { return send("", 0); }

int Channel::fill(std::size_t need, int timeout_ms) {
    char buf[65536];
    while (in_.size() < need) {
        pollfd q{fd_, POLLIN, 0};
        const int pr = ::poll(&q, 1, timeout_ms);
        if (pr == 0) return 0;
        if (pr < 0) return -1;
        const ssize_t r = ::recv(fd_, buf, sizeof buf, 0);
        if (r <= 0) return -1;
        in_.append(buf, static_cast<std::size_t>(r));
    }
    return 1;
}

int Channel::recv_frame(std::string& out, int timeout_ms) {
    if (fd_ < 0) return -1;
    int r = fill(4, timeout_ms);
    if (r <= 0) return r;
    const unsigned char* h = reinterpret_cast<const unsigned char*>(in_.data());
    const std::size_t len = (std::size_t(h[0]) << 24) | (std::size_t(h[1]) << 16) | (std::size_t(h[2]) << 8) | h[3];
    if (len > kMaxFrame) return -1;
    if ((r = fill(4 + len + 16, timeout_ms < 15000 ? 15000 : timeout_ms)) <= 0) return -1;  // trame commencée : on attend la suite
    std::string body = in_.substr(4, len), head = in_.substr(0, 4);
    unsigned char tag[16], iv[12];
    nonce(n_recv_++, iv);
    br_poly1305_ctmul_run(k_recv_, iv, len ? &body[0] : nullptr, len, head.data(), 4, tag, br_chacha20_ct_run, 0);
    unsigned diff = 0;
    for (int i = 0; i < 16; ++i) diff |= tag[i] ^ static_cast<unsigned char>(in_[4 + len + i]);
    in_.erase(0, 4 + len + 16);
    if (diff) { log("wan: trame falsifiée ou clé différente, connexion fermée"); return -1; }
    out = std::move(body);
    return 1;
}

bool Channel::client(int fd, char mode, const std::string& secret, Channel& ch, std::string& error) {
    unsigned char hello[37], nc[16], reply[21];
    rand_bytes(nc, sizeof nc);
    std::memcpy(hello, kMagic, 4);
    hello[4] = static_cast<unsigned char>(mode);
    std::string kid;
    unhex(key_id(secret), kid);
    std::memcpy(hello + 5, kid.data(), 16);
    std::memcpy(hello + 21, nc, 16);
    if (!write_all(fd, reinterpret_cast<const char*>(hello), sizeof hello) || !read_exact(fd, reply, sizeof reply, 15000)) { error = "pas de réponse (ce n'est pas une PS5 Backup Center ?)"; return false; }
    if (std::memcmp(reply, kMagic, 4) != 0) { error = "réponse inconnue (autre service sur ce port ?)"; return false; }
    if (reply[4] != 0) { error = reply[4] == 1 ? "clé refusée (jumelage inconnu ou invitation expirée)" : "refusé"; return false; }
    ch.fd_ = fd;
    ch.setup(secret, nc, reply + 5, true);
    return true;
}

bool Channel::server(int fd, const std::function<std::string(const std::string&, char)>& lookup, Channel& ch, char& mode, std::string& error) {
    unsigned char hello[37], reply[21], ns[16];
    if (!read_exact(fd, hello, sizeof hello, 10000) || std::memcmp(hello, kMagic, 4) != 0) { error = "poignée de main illisible"; return false; }
    mode = static_cast<char>(hello[4]);
    const std::string secret = lookup(hex(hello + 5, 16), mode);
    std::memcpy(reply, kMagic, 4);
    rand_bytes(ns, sizeof ns);
    std::memcpy(reply + 5, ns, 16);
    reply[4] = secret.empty() ? 1 : 0;
    write_all(fd, reinterpret_cast<const char*>(reply), sizeof reply);
    if (secret.empty()) { error = "clé inconnue"; return false; }
    ch.fd_ = fd;
    ch.setup(secret, hello + 21, ns, false);
    return true;
}

void pump(Channel& ch, int local_fd) {
    char buf[65536];
    std::string f;
    for (;;) {
        if (ch.pending()) {  // trame déjà reçue en partie
            const int r = ch.recv_frame(f, 30000);
            if (r < 0) break;
            if (r > 0 && !f.empty() && !write_all(local_fd, f.data(), f.size())) break;
            continue;
        }
        pollfd q[2] = {{local_fd, POLLIN, 0}, {ch.fd(), POLLIN, 0}};
        const int pr = ::poll(q, 2, 120000);
        if (pr <= 0) break;  // deux minutes sans rien : abandon
        if (q[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t n = ::recv(local_fd, buf, sizeof buf, 0);
            if (n <= 0) break;
            if (!ch.send(buf, static_cast<std::size_t>(n))) break;
        }
        if (q[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            const int r = ch.recv_frame(f, 30000);
            if (r < 0) break;
            if (r > 0 && !f.empty() && !write_all(local_fd, f.data(), f.size())) break;
        }
    }
    ::shutdown(local_fd, SHUT_RDWR);
    ch.close();
}

// --- Codes d'invitation ------------------------------------------------------------------------------------------

std::string encode_invite(const std::string& host, std::uint16_t port, const std::string& secret_hex) {
    std::string secret, b;
    if (!unhex(secret_hex, secret) || secret.size() != 10) return "";
    b += static_cast<char>(1);                       // version
    b += static_cast<char>(port >> 8); b += static_cast<char>(port & 255);
    b += secret;
    in_addr a{};
    if (::inet_pton(AF_INET, host.c_str(), &a) == 1) { b += '4'; b.append(reinterpret_cast<const char*>(&a), 4); }
    else { if (host.empty() || host.size() > 120) return ""; b += 'D'; b += static_cast<char>(host.size()); b += host; }
    br_sha256_context c;
    br_sha256_init(&c);
    br_sha256_update(&c, b.data(), b.size());
    unsigned char d[32];
    br_sha256_out(&c, d);
    b += static_cast<char>(d[0]);                    // contrôle (faute de frappe)
    std::string s;
    unsigned acc = 0;
    int bits = 0;
    for (unsigned char ch : b) {
        acc = (acc << 8) | ch;
        bits += 8;
        while (bits >= 5) { s += kB32[(acc >> (bits - 5)) & 31]; bits -= 5; }
    }
    if (bits) s += kB32[(acc << (5 - bits)) & 31];
    std::string out = "PBC";
    for (std::size_t i = 0; i < s.size(); i += 5) out += "-" + s.substr(i, 5);
    return out;
}

bool decode_invite(const std::string& code, std::string& host, std::uint16_t& port, std::string& secret_hex) {
    std::string s;
    std::string up;
    for (char c : code) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (up.rfind("PBC", 0) == 0) up = up.substr(3);
    for (char c : up) {
        if (c == '-' || c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (!std::strchr(kB32, c) || !c) return false;
        s += c;
    }
    std::string b;
    unsigned acc = 0;
    int bits = 0;
    for (char c : s) {
        acc = (acc << 5) | static_cast<unsigned>(std::strchr(kB32, c) - kB32);
        bits += 5;
        if (bits >= 8) { b += static_cast<char>((acc >> (bits - 8)) & 255); bits -= 8; }
    }
    if (b.size() < 1 + 2 + 10 + 1 + 4 + 1 || b[0] != 1) return false;
    const std::string body = b.substr(0, b.size() - 1);
    std::size_t at = 13;
    if (body[at] == '4' && body.size() == at + 5) {
        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, body.data() + at + 1, ip, sizeof ip);
        host = ip;
    } else if (body[at] == 'D' && body.size() > at + 2 && body.size() == at + 2 + static_cast<unsigned char>(body[at + 1])) {
        host = body.substr(at + 2);
    } else return false;
    br_sha256_context c;
    br_sha256_init(&c);
    br_sha256_update(&c, body.data(), body.size());
    unsigned char d[32];
    br_sha256_out(&c, d);
    if (static_cast<unsigned char>(b.back()) != d[0]) return false;
    port = static_cast<std::uint16_t>((static_cast<unsigned char>(body[1]) << 8) | static_cast<unsigned char>(body[2]));
    secret_hex = hex(reinterpret_cast<const unsigned char*>(body.data() + 3), 10);
    return port != 0;
}

// --- Service ------------------------------------------------------------------------------------------------------

Service::Service(std::string state_dir, Hooks hooks) : state_dir_(std::move(state_dir)), hooks_(std::move(hooks)) {
    std::string text;
    Json j;
    if (fs::read(fs::host(fs::join(state_dir_, "wan-invites.json")), text) && Json::parse(text, j))
        for (const auto& x : j.items()) {
            Invite iv{x["secret"].as_string(), x["code"].as_string(), x["host"].as_string(), x["expires"].as_int()};
            if (iv.expires > now_unix() && iv.secret.size() == 20) invites_.push_back(iv);
        }
    park_thread_ = std::thread([this] { park_loop(); });
}

Service::~Service() {
    quit_ = true;
    configure(false, port_);
    if (park_thread_.joinable()) park_thread_.join();
    std::lock_guard<std::mutex> l(mu_);
    for (auto& [k, q] : parked_) for (Channel* c : q) delete c;
    parked_.clear();
}

std::string Service::listen_error() const { std::lock_guard<std::mutex> l(mu_); return listen_error_; }
std::string Service::observed_ip() const { std::lock_guard<std::mutex> l(mu_); return observed_ip_; }

void Service::configure(bool enabled, std::uint16_t port) {
    if (!port) port = kDefaultPort;
    if (enabled == enabled_.load() && port == port_ && (!enabled || listen_fd_ >= 0)) return;
    if (listen_fd_ >= 0) {  // arrêt de l'écoute
        const int fd = listen_fd_;
        listen_fd_ = -1;
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        if (accept_thread_.joinable()) accept_thread_.join();
    }
    enabled_ = enabled;
    port_ = port;
    if (!enabled || quit_) return;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd < 0 || ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd, 16) != 0) {
        { std::lock_guard<std::mutex> l(mu_); listen_error_ = "port " + std::to_string(port) + " déjà utilisé"; }
        if (fd >= 0) ::close(fd);
        log("wan: écoute impossible sur le port %u", port);
        return;
    }
    { std::lock_guard<std::mutex> l(mu_); listen_error_.clear(); }
    listen_fd_ = fd;
    accept_thread_ = std::thread([this] { accept_loop(); });
    log("wan: écoute sur le port %u", port);
}

void Service::accept_loop() {
    while (listen_fd_ >= 0) {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&a), &al);
        if (fd < 0) { if (listen_fd_ < 0) break; std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        std::thread([this, fd, s = std::string(ip)] { handle(fd, s); }).detach();
    }
}

void Service::handle(int fd, std::string ip) {
    auto* ch = new Channel();
    char mode = 0;
    std::string error;
    std::string invite_secret;
    const bool ok = Channel::server(fd, [&](const std::string& kid, char m) -> std::string {
        if (m == 'I') {  // invitation : son secret, une seule fois, avant expiration
            std::lock_guard<std::mutex> l(mu_);
            for (const auto& iv : invites_) {
                std::string sec;
                if (iv.expires > now_unix() && unhex(iv.secret, sec) && Channel::key_id(sec) == kid) { invite_secret = iv.secret; return sec; }
            }
            return "";
        }
        if (!hooks_.tokens) return "";
        for (const auto& t : hooks_.tokens()) if (!t.empty() && Channel::key_id(t) == kid) return t;
        return "";
    }, *ch, mode, error);
    if (!ok) { if (ch->fd() < 0) ::close(fd); delete ch; return; }
    Json theirs, mine = hooks_.self_hello ? hooks_.self_hello() : Json::object();
    std::string f;
    if (ch->recv_frame(f, 15000) != 1 || !Json::parse(f, theirs) || !theirs.is_object()) { delete ch; return; }
    const std::string inst = theirs["instance"].as_string();
    mine.set("seen_ip", ip);
    if (mode == 'I') {
        const std::string their_token = theirs["token"].as_string();
        const std::string token = hooks_.mint_token ? hooks_.mint_token(theirs["name"].as_string().empty() ? ip : theirs["name"].as_string()) : std::string();
        if (inst.empty() || their_token.size() < 16 || token.empty()) { mine.set("error", "invitation refusée"); ch->send_frame(mine.dump()); delete ch; return; }
        mine.set("token", token);
        if (hooks_.on_joined) hooks_.on_joined(theirs, their_token, ip);  // enregistrée avant la réponse : elle peut appeler aussitôt
        ch->send_frame(mine.dump());
        {
            std::lock_guard<std::mutex> l(mu_);
            for (auto it = invites_.begin(); it != invites_.end();) it = it->secret == invite_secret ? invites_.erase(it) : it + 1;  // à usage unique
        }
        Json arr = Json::array();
        { std::lock_guard<std::mutex> l(mu_); for (const auto& iv : invites_) { Json x = Json::object(); x.set("secret", iv.secret); x.set("code", iv.code); x.set("host", iv.host); x.set("expires", iv.expires); arr.push(x); } }
        fs::write_atomic(fs::host(fs::join(state_dir_, "wan-invites.json")), arr.dump() + "\n");
        log("wan: invitation acceptée par %s (%s)", theirs["name"].as_string().c_str(), ip.c_str());
        delete ch;
        return;
    }
    if (mode == 'P') {  // test : l'autre veut savoir s'il est joignable depuis ici
        Json probe = Json::object();
        const std::int64_t pp = theirs["probe_port"].as_int();
        if (pp > 0 && pp < 65536) {
            std::string e2;
            int ms = 0;
            const int t = tcp_connect(ip, static_cast<std::uint16_t>(pp), e2, &ms);
            probe.set("reachable", t >= 0);
            if (t >= 0) ::close(t);
        }
        mine.set("probe", probe);
        ch->send_frame(mine.dump());
        if (hooks_.on_seen && !inst.empty()) hooks_.on_seen(inst, ip, theirs);
        delete ch;
        return;
    }
    if (!ch->send_frame(mine.dump())) { delete ch; return; }
    if (hooks_.on_seen && !inst.empty()) hooks_.on_seen(inst, ip, theirs);
    if (mode == 'R') {  // connexion garée : gardée pour l'appeler en retour (deux au plus)
        std::lock_guard<std::mutex> l(mu_);
        auto& q = parked_[inst];
        q.push_back(ch);
        while (q.size() > 2) { delete q.front(); q.pop_front(); }
        return;
    }
    // 'T' : une requête HTTP dans le canal, servie comme une demande du réseau (jeton vérifié, jamais « locale »).
    int sv[2];
    if (!hooks_.serve_http || !local_pair(sv)) { delete ch; return; }
    hooks_.serve_http(sv[1]);
    pump(*ch, sv[0]);
    ::close(sv[0]);
    delete ch;
}

Json Service::create_invite(const std::string& host, std::string& error) {
    if (host.empty()) { error = "adresse publique inconnue : indiquez-la (ou « Détecter »)"; return Json(); }
    if (!enabled_) { error = "activez d'abord la liaison par Internet"; return Json(); }
    unsigned char s[10];
    rand_bytes(s, sizeof s);
    Invite iv{hex(s, sizeof s), "", host, now_unix() + 86400};
    iv.code = encode_invite(host, port_, iv.secret);
    if (iv.code.empty()) { error = "adresse invalide"; return Json(); }
    Json arr = Json::array();
    {
        std::lock_guard<std::mutex> l(mu_);
        invites_.push_back(iv);
        for (const auto& x : invites_) { Json o = Json::object(); o.set("secret", x.secret); o.set("code", x.code); o.set("host", x.host); o.set("expires", x.expires); arr.push(o); }
    }
    fs::write_atomic(fs::host(fs::join(state_dir_, "wan-invites.json")), arr.dump() + "\n");
    Json out = Json::object();
    out.set("code", iv.code);
    out.set("host", host);
    out.set("port", static_cast<std::int64_t>(port_));
    out.set("expires", iv.expires);
    return out;
}

void Service::cancel_invites() {
    { std::lock_guard<std::mutex> l(mu_); invites_.clear(); }
    fs::write_atomic(fs::host(fs::join(state_dir_, "wan-invites.json")), "[]\n");
}

Json Service::invites() const {
    Json arr = Json::array();
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& iv : invites_) {
        if (iv.expires <= now_unix()) continue;
        Json o = Json::object();
        o.set("code", iv.code); o.set("host", iv.host); o.set("expires", iv.expires);
        arr.push(o);
    }
    return arr;
}

bool Service::join(const std::string& code, const std::string& our_token_for_them, Json& their_hello, std::string& their_token, std::string& host, std::uint16_t& port, std::string& seen_ip, std::string& error) {
    std::string secret_hex, secret;
    if (!decode_invite(code, host, port, secret_hex) || !unhex(secret_hex, secret)) { error = "code d'invitation illisible (faute de frappe ?)"; return false; }
    const int fd = tcp_connect(host, port, error);
    if (fd < 0) { error += " — le port est-il ouvert chez l'autre ?"; return false; }
    Channel ch;
    if (!Channel::client(fd, 'I', secret, ch, error)) { ::close(fd); return false; }
    Json mine = hooks_.self_hello ? hooks_.self_hello() : Json::object();
    mine.set("token", our_token_for_them);
    if (!exchange(ch, mine, their_hello, true, error)) return false;
    if (!their_hello["error"].as_string().empty()) { error = their_hello["error"].as_string(); return false; }
    their_token = their_hello["token"].as_string();
    seen_ip = their_hello["seen_ip"].as_string();
    if (their_token.empty() || their_hello["instance"].as_string().empty()) { error = "réponse incomplète"; return false; }
    { std::lock_guard<std::mutex> l(mu_); observed_ip_ = seen_ip; }
    return true;
}

int Service::dial(const std::string& instance, const std::string& host, std::uint16_t port, const std::string& token, std::string& error, char dir) {
    auto* ch = new Channel();
    bool ready = false;
    // D'abord une session que l'autre a déjà ouverte vers nous (elle passe son pare-feu en sortie : rien à ouvrir),
    // sinon on l'appelle directement si elle est joignable, sinon on attend qu'elle nous appelle.
    auto take_parked = [&] {
        for (;;) {
            Channel* p = nullptr;
            {
                std::lock_guard<std::mutex> l(mu_);
                auto it = parked_.find(instance);
                if (it != parked_.end() && !it->second.empty()) { p = it->second.front(); it->second.pop_front(); }
            }
            if (!p) return false;
            if (p->heartbeat()) { delete ch; ch = p; return true; }
            delete p;  // morte entre-temps : la suivante
        }
    };
    if (dir != 'o') ready = take_parked();
    if (!ready && dir == 'i') error = "elle doit appeler cette PS5 (choix « " + std::string("l'autre appelle") + " ») et aucune session de sa part";
    if (!ready && dir != 'i' && !host.empty()) {  // directe
        const int fd = tcp_connect(host, port ? port : kDefaultPort, error);
        if (fd >= 0) {
            if (Channel::client(fd, 'T', token, *ch, error)) {
                Json theirs;
                if (exchange(*ch, hooks_.self_hello ? hooks_.self_hello() : Json::object(), theirs, true, error)) {
                    ready = true;
                    { std::lock_guard<std::mutex> l(mu_); if (!theirs["seen_ip"].as_string().empty()) observed_ip_ = theirs["seen_ip"].as_string(); }
                    if (hooks_.on_seen) hooks_.on_seen(instance, "", theirs);
                }
            } else ::close(fd);
        }
    }
    if (!ready) {  // sinon, une connexion qu'il va garer chez nous ; 3 s pour qu'elle arrive
        for (int i = 0; i < 30 && !has_parked(instance); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ready = take_parked();
        if (!ready && error.empty()) error = "injoignable : pas d'adresse et aucune connexion de sa part";
        if (ready) error.clear();
    }
    if (!ready) { delete ch; return -1; }
    int sv[2];
    if (!local_pair(sv)) { delete ch; error = "socket locale impossible"; return -1; }
    std::thread([ch, s = sv[0]] { pump(*ch, s); ::close(s); delete ch; }).detach();
    return sv[1];
}

void Service::set_park_targets(std::vector<std::tuple<std::string, std::string, std::uint16_t, std::string>> targets) {
    std::lock_guard<std::mutex> l(mu_);
    park_targets_ = std::move(targets);
}

bool Service::has_parked(const std::string& instance) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = parked_.find(instance);
    return it != parked_.end() && !it->second.empty();
}

bool Service::parking(const std::string& instance) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = parking_.find(instance);
    return it != parking_.end() && it->second > 0;
}

// Une connexion garée vers chaque pair joignable : il s'en sert pour nous appeler (nous sommes peut-être derrière un
// CGNAT). Battement toutes les 20 s ; dès qu'une requête arrive, elle est servie et une nouvelle connexion est garée.
void Service::park_loop() {
    while (!quit_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!enabled_) continue;
        decltype(park_targets_) targets;
        { std::lock_guard<std::mutex> l(mu_); targets = park_targets_; }
        for (const auto& [inst, host, port, token] : targets) {
            {
                std::lock_guard<std::mutex> l(mu_);
                // Deux connexions garées : pendant que l'une sert une requête, l'autre attend la suivante.
                if (host.empty() || token.empty() || parking_[inst] >= 2 || retry_at_[inst] > now_unix()) continue;
                ++parking_[inst];
            }
            std::thread([this, inst = inst, host = host, port = port, token = token] {
                std::string error;
                bool served = false;
                const int fd = tcp_connect(host, port ? port : kDefaultPort, error);
                Channel ch;
                Json theirs;
                if (fd >= 0 && Channel::client(fd, 'R', token, ch, error) && exchange(ch, hooks_.self_hello ? hooks_.self_hello() : Json::object(), theirs, true, error)) {
                    { std::lock_guard<std::mutex> l(mu_); if (!theirs["seen_ip"].as_string().empty()) observed_ip_ = theirs["seen_ip"].as_string(); }
                    if (hooks_.on_seen) hooks_.on_seen(inst, "", theirs);
                    std::string f;
                    while (!quit_ && enabled_) {
                        const int r = ch.recv_frame(f, 20000);
                        if (r < 0) break;
                        if (r == 0) {
                            bool still = false;  // le choix a changé (c'est l'autre qui appelle maintenant) : on raccroche
                            { std::lock_guard<std::mutex> l(mu_); for (const auto& t : park_targets_) if (std::get<0>(t) == inst) still = true; }
                            if (!still || !ch.heartbeat()) break;
                            continue;
                        }
                        if (f.empty()) continue;
                        int sv[2];  // une requête arrive : servie ici, la suite du canal reliée à notre API
                        if (!hooks_.serve_http || !local_pair(sv)) break;
                        hooks_.serve_http(sv[1]);
                        if (write_all(sv[0], f.data(), f.size())) pump(ch, sv[0]);
                        ::close(sv[0]);
                        served = true;
                        break;
                    }
                } else {
                    if (fd >= 0 && ch.fd() < 0) ::close(fd);
                    log("wan: connexion garée vers %s impossible : %s", inst.c_str(), error.c_str());
                }
                std::lock_guard<std::mutex> l(mu_);
                --parking_[inst];
                if (!served) retry_at_[inst] = now_unix() + 5;  // échec ou coupure : on réessaie dans 5 s
            }).detach();
        }
    }
}

Json Service::test(const std::string& instance, const std::string& name, const std::string& host, std::uint16_t port, const std::string& token) {
    Json steps = Json::array();
    auto step = [&](const char* state, const std::string& key, std::vector<std::string> args, const std::string& detail_key = "", std::vector<std::string> dargs = {}) {
        Json s = Json::object();
        s.set("state", state);
        s.set("key", key);
        Json a = Json::array();
        for (auto& x : args) a.push(x);
        s.set("args", a);
        if (!detail_key.empty()) {
            s.set("detail_key", detail_key);
            Json d = Json::array();
            for (auto& x : dargs) d.push(x);
            s.set("detail_args", d);
        }
        steps.push(s);
    };
    const std::string p = std::to_string(port ? port : kDefaultPort), mine = std::to_string(port_);
    if (!enabled_) step("warn", "Liaison par Internet désactivée ici", {}, "Appareils › PS5 à distance : activez-la pour que l'autre PS5 puisse vous joindre.");
    else if (listen_fd_ < 0) step("bad", "Écoute impossible sur le port %s", {mine}, "Un autre programme utilise ce port : choisissez-en un autre dans Appareils › PS5 à distance.");
    else step("ok", "Liaison par Internet active ici (port %s)", {mine});
    bool direct = false;
    if (host.empty()) {
        step("warn", "Adresse de « %s » inconnue", {name}, "Elle n'a pas d'adresse joignable (LTE, Starlink…) : c'est elle qui appelle cette PS5.");
    } else {
        std::string error;
        int ms = 0;
        const int fd = tcp_connect(host, port ? port : kDefaultPort, error, &ms);
        if (fd < 0) {
            step("bad", "Port %s fermé ou injoignable chez « %s » (%s)", {p, name, host},
                 "Il faut une redirection du port %s (TCP) vers sa PS5 dans son routeur ou son pare-feu (Fortinet : « Virtual IP »), ou que son adresse soit la bonne.", {p});
        } else {
            step("ok", "« %s » répond : %s, %s ms", {name, host, std::to_string(ms)});
            Channel ch;
            if (!Channel::client(fd, 'P', token, ch, error)) {
                ::close(fd);
                step("bad", "Canal chiffré refusé par « %s »", {name}, "Le jumelage n'est plus reconnu là-bas : oubliez-la et invitez-la de nouveau.");
            } else {
                Json mine_hello = hooks_.self_hello ? hooks_.self_hello() : Json::object(), theirs;
                mine_hello.set("probe_port", static_cast<std::int64_t>(port_));
                if (exchange(ch, mine_hello, theirs, true, error)) {
                    direct = true;
                    step("ok", "Canal chiffré établi avec « %s »", {name});
                    const std::string seen = theirs["seen_ip"].as_string();
                    if (!seen.empty()) { std::lock_guard<std::mutex> l(mu_); observed_ip_ = seen; }
                    if (theirs["probe"]["reachable"].as_bool()) step("ok", "Cette PS5 est joignable depuis Internet (%s:%s)", {seen, mine});
                    else step("warn", "Cette PS5 n'est pas joignable depuis Internet (%s)", {seen.empty() ? std::string("?") : seen},
                              "Pas grave : elle garde une connexion ouverte vers « %s », qui s'en sert pour l'appeler.", {name});
                } else step("bad", "Pas de réponse dans le canal chiffré", {}, error);
            }
        }
    }
    if (has_parked(instance)) step("ok", "« %s » garde une connexion ouverte vers cette PS5", {name});
    else if (parking(instance)) step("ok", "Cette PS5 garde une connexion ouverte vers « %s »", {name});
    else if (!direct) step("bad", "Aucun chemin vers « %s »", {name}, "Il faut qu'au moins une des deux PS5 soit joignable depuis Internet (redirection de port).");
    Json out = Json::object();
    out.set("steps", steps);
    out.set("ok", direct || has_parked(instance) || parking(instance));
    return out;
}

}  // namespace pbc::link::wan
