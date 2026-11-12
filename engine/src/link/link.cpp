#include "link/link.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstring>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "link/wan.hpp"

namespace pbc::link {

Link::Link(std::string state_dir, Self self, std::uint16_t discovery_port, std::vector<std::string> announce_to)
    : state_dir_(std::move(state_dir)), self_(std::move(self)), port_(discovery_port), announce_to_(std::move(announce_to)) {
    load();
    wan::Hooks h;
    h.tokens = [this] { return self_.issued_tokens ? self_.issued_tokens() : std::vector<std::string>(); };
    h.serve_http = [this](int fd) { if (serve_http_) serve_http_(fd); else ::close(fd); };
    h.self_hello = [this] { return self_hello(); };
    h.mint_token = [this](const std::string& name) { return self_.mint_token ? self_.mint_token(name + " (à distance)") : std::string(); };
    h.on_joined = [this](const Json& hello, const std::string& their_token, const std::string& ip) {
        const std::string inst = hello["instance"].as_string();
        if (inst.empty() || inst == self_.instance || inst.size() > 64) return;
        {
            std::lock_guard<std::mutex> l(mu_);
            Peer& p = peers_[inst];
            p.instance = inst;
            p.app = hello["app"].as_string().substr(0, 64);
            p.name = hello["name"].as_string().substr(0, 64);
            p.version = hello["version"].as_string().substr(0, 32);
            p.installed = hello["installed"].as_string().substr(0, 32);
            p.caps = hello["caps"].is_array() ? hello["caps"] : Json::array();
            p.token = their_token;
            p.wan = true;
            p.seen_ip = ip;
            p.lan_ip = hello["lan_ip"].as_string().substr(0, 45);
            if (!hello["wan_host"].as_string().empty() && hello["wan_host"].as_string().find_first_of(" /@") == std::string::npos) {
                p.wan_host = hello["wan_host"].as_string().substr(0, 120);  // elle est joignable aussi : les deux peuvent appeler
                p.wan_port = static_cast<std::uint16_t>(hello["wan_port"].as_int(0));
            }
            if (p.host.empty()) p.host = ip;
            p.last_seen = now_unix();
        }
        save();
        update_park_targets();
    };
    h.on_seen = [this](const std::string& inst, const std::string& ip, const Json& hello) {
        bool targets = false, changed = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            auto it = peers_.find(inst);
            if (it == peers_.end() || it->second.token.empty()) return;
            Peer& p = it->second;
            p.last_seen = now_unix();
            if (!ip.empty()) p.seen_ip = ip;
            const std::string lan = hello["lan_ip"].as_string().substr(0, 45);
            if (!lan.empty() && lan != p.lan_ip) { p.lan_ip = lan; changed = true; }
            if (!hello["version"].as_string().empty()) p.version = hello["version"].as_string().substr(0, 32);
            if (!hello["installed"].as_string().empty()) p.installed = hello["installed"].as_string().substr(0, 32);
            // Elle est joignable (adresse publique réglée chez elle) : on peut aussi ouvrir la session de notre côté.
            const std::string wh = hello["wan_host"].as_string().substr(0, 120);
            const auto wp = static_cast<std::uint16_t>(hello["wan_port"].as_int(0));
            if (p.wan && !wh.empty() && wh.find_first_of(" /@") == std::string::npos && (wh != p.wan_host || (wp && wp != p.wan_port))) {
                p.wan_host = wh;
                if (wp) p.wan_port = wp;
                targets = changed = true;
            }
        }
        if (changed) save();
        if (targets) update_park_targets();
    };
    wan_ = std::make_unique<wan::Service>(state_dir_, std::move(h));
    update_park_targets();
}

Link::~Link() { stop(); wan_.reset(); }

namespace {
// Adresse IPv4 de cette console sur son réseau local (la première interface active, hors boucle locale).
std::string local_ipv4() {
    std::string best;
    ifaddrs* ifs = nullptr;
    if (::getifaddrs(&ifs) != 0) return best;
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, ip, sizeof ip);
        const std::string s = ip;
        if (s.empty() || s.rfind("169.254.", 0) == 0) continue;
        const bool priv = s.rfind("192.168.", 0) == 0 || s.rfind("10.", 0) == 0 || (s.rfind("172.", 0) == 0 && std::atoi(s.c_str() + 4) >= 16 && std::atoi(s.c_str() + 4) <= 31);
        if (best.empty() || priv) { best = s; if (priv) break; }
    }
    ::freeifaddrs(ifs);
    return best;
}
}  // namespace

Json Link::self_hello() const {
    Json j = Json::object();
    j.set("proto", static_cast<std::int64_t>(kProtocol));
    j.set("instance", self_.instance);
    j.set("app", self_.app);
    j.set("name", self_.name ? self_.name() : std::string("PS5"));
    j.set("version", self_.version);
    if (self_.installed) j.set("installed", self_.installed());
    j.set("port", static_cast<std::int64_t>(self_.port));
    j.set("caps", self_.caps);
    // Pour la liaison par Internet : son adresse locale (affichée chez l'autre) et, si elle est joignable, son
    // adresse publique — l'autre peut alors ouvrir la session lui aussi.
    j.set("lan_ip", local_ipv4());
    {
        std::lock_guard<std::mutex> l(pub_mu_);
        if (!wan_public_host_.empty()) { j.set("wan_host", wan_public_host_); j.set("wan_port", static_cast<std::int64_t>(wan_port_)); }
    }
    return j;
}

bool Link::use_wan(const Peer& p) const { return p.wan && !(p.lan_seen && now_unix() - p.lan_seen < 20); }

std::string Link::base_url(const Peer& p) const {
    return use_wan(p) ? "http://wan-" + p.instance : "http://" + p.host + ":" + std::to_string(p.port);
}

void Link::update_park_targets() {
    std::vector<std::tuple<std::string, std::string, std::uint16_t, std::string>> t;
    {
        std::lock_guard<std::mutex> l(mu_);
        // On garde une session ouverte vers elle si elle est joignable, sauf si c'est elle qui doit appeler (« in »).
        for (const auto& [id, p] : peers_) if (p.wan && !p.token.empty() && !p.wan_host.empty() && p.wan_dir != "in") t.emplace_back(id, p.wan_host, p.wan_port, p.token);
    }
    if (wan_) wan_->set_park_targets(std::move(t));
}

void Link::set_serve_http(std::function<void(int)> fn) { serve_http_ = std::move(fn); }
void Link::wan_configure(bool enabled, std::uint16_t port, const std::string& public_host) {
    { std::lock_guard<std::mutex> l(pub_mu_); wan_public_host_ = public_host; wan_port_ = port; }
    if (wan_) wan_->configure(enabled, port);
}

Json Link::wan_status() const {
    Json j = Json::object();
    j.set("enabled", wan_ && wan_->enabled());
    j.set("port", static_cast<std::int64_t>(wan_ ? wan_->port() : wan::kDefaultPort));
    j.set("listening", wan_ && wan_->listening());
    j.set("error", wan_ ? wan_->listen_error() : std::string());
    j.set("observed_ip", wan_ ? wan_->observed_ip() : std::string());
    j.set("invites", wan_ ? wan_->invites() : Json::array());
    return j;
}

Json Link::wan_invite(const std::string& public_host, std::string& error) { return wan_ ? wan_->create_invite(public_host, error) : Json(); }
void Link::wan_cancel_invites() { if (wan_) wan_->cancel_invites(); }

bool Link::wan_join(const std::string& code, std::string& instance, std::string& name, std::string& error) {
    if (!wan_) { error = "liaison Internet indisponible"; return false; }
    const std::string ours = self_.mint_token ? self_.mint_token("PS5 à distance") : std::string();
    if (ours.empty()) { error = "jeton impossible à créer"; return false; }
    Json hello;
    std::string their_token, host, seen;
    std::uint16_t port = 0;
    if (!wan_->join(code, ours, hello, their_token, host, port, seen, error)) return false;
    instance = hello["instance"].as_string();
    name = hello["name"].as_string();
    if (instance == self_.instance) { error = "c'est l'invitation de cette PS5"; return false; }
    {
        std::lock_guard<std::mutex> l(mu_);
        Peer& p = peers_[instance];
        p.instance = instance;
        p.app = hello["app"].as_string().substr(0, 64);
        p.name = name.substr(0, 64);
        p.version = hello["version"].as_string().substr(0, 32);
        p.installed = hello["installed"].as_string().substr(0, 32);
        p.caps = hello["caps"].is_array() ? hello["caps"] : Json::array();
        p.token = their_token;
        p.wan = true;
        p.wan_host = host;
        p.wan_port = port;
        p.host = host;
        p.port = static_cast<std::uint16_t>(hello["port"].as_int(52370));
        p.last_seen = now_unix();
    }
    save();
    update_park_targets();
    log("link: jumelée par Internet instance=%s host=%s", instance.c_str(), host.c_str());
    return true;
}

Json Link::wan_test(const std::string& instance) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty()) { Json e = Json::object(); e.set("error", "console non jumelée"); return e; }
        p = it->second;
    }
    return wan_->test(instance, p.name.empty() ? p.host : p.name, p.wan_host, p.wan_port, p.token);
}

bool Link::via_wan(const std::string& instance) const {
    std::lock_guard<std::mutex> l(mu_);
    auto it = peers_.find(instance);
    return it != peers_.end() && use_wan(it->second);
}

int Link::wan_dial(const std::string& instance, std::string& error) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty() || !it->second.wan) { error = "console à distance inconnue"; return -1; }
        p = it->second;
    }
    return wan_ ? wan_->dial(instance, p.wan_host, p.wan_port, p.token, error, p.wan_dir == "out" ? 'o' : p.wan_dir == "in" ? 'i' : 'a') : -1;
}

bool Link::wan_set_peer(const std::string& instance, const std::string& dir, const std::string& host, std::uint16_t port, bool from_peer, std::string& error) {
    if (dir != "auto" && dir != "out" && dir != "in") { error = "choix inconnu"; return false; }
    if (host.size() > 120 || host.find_first_of(" /@") != std::string::npos) { error = "adresse invalide"; return false; }
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty() || !it->second.wan) { error = "console à distance inconnue"; return false; }
        if (dir == "out" && host.empty() && it->second.wan_host.empty()) { error = "adresse de l'autre PS5 inconnue : indiquez-la"; return false; }
    }
    std::string my_host;
    std::uint16_t my_port = 0;
    { std::lock_guard<std::mutex> l(pub_mu_); my_host = wan_public_host_; my_port = wan_port_; }
    // « Elle appelle cette PS5 » : il lui faut notre adresse publique (redirection de port chez nous).
    if (dir == "in" && !from_peer && my_host.empty()) { error = "indiquez d'abord l'adresse publique de cette PS5 (Appareils › PS5 à distance)"; return false; }
    // D'abord dire à l'autre le choix inversé (sinon elles s'attendraient l'une l'autre), par le chemin encore ouvert ;
    // si elle ne répond pas (elle a changé de réseau), le choix s'applique quand même ici.
    if (!from_peer) {
        const std::string mirror = dir == "out" ? "in" : dir == "in" ? "out" : "auto";
        Json b = Json::object(), out;
        b.set("peer", self_.instance);
        b.set("dir", mirror);
        if (!my_host.empty()) { b.set("host", my_host); b.set("port", static_cast<std::int64_t>(my_port)); }
        std::string e;
        if (!call(instance, "POST", "/api/v2/link/wan-dir", b, out, e, 15)) log("link: choix non transmis à %s : %s", instance.c_str(), e.c_str());
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end()) { error = "console à distance inconnue"; return false; }
        Peer& p = it->second;
        if (!host.empty()) { p.wan_host = host; if (port) p.wan_port = port; }
        p.wan_dir = dir;
    }
    save();
    update_park_targets();
    log("link: wan %s → %s%s", instance.c_str(), dir.c_str(), from_peer ? " (demandé par elle)" : "");
    return true;
}

std::string Link::announce_payload() const {
    Json j = Json::object();
    j.set("proto", static_cast<std::int64_t>(kProtocol));
    j.set("app", self_.app);
    j.set("name", self_.name ? self_.name() : std::string());
    j.set("version", self_.version);
    if (self_.installed) j.set("installed", self_.installed());
    j.set("instance", self_.instance);
    j.set("port", static_cast<std::int64_t>(self_.port));
    j.set("caps", self_.caps);
    return j.dump();
}

void Link::on_announce(const std::string& host, const std::string& payload) {
    Json j;
    if (!Json::parse(payload, j) || j["proto"].as_int() != kProtocol) return;
    const std::string inst = j["instance"].as_string();
    if (inst.empty() || inst == self_.instance || inst.size() > 64) return;  // soi-même : ignoré
    std::lock_guard<std::mutex> l(mu_);
    Peer& p = peers_[inst];
    p.instance = inst;
    p.app = j["app"].as_string().substr(0, 64);
    p.name = j["name"].as_string().substr(0, 64);
    p.version = j["version"].as_string().substr(0, 32);
    p.installed = j["installed"].as_string().substr(0, 32);
    p.host = host;
    p.port = static_cast<std::uint16_t>(j["port"].as_int(52370));
    p.caps = j["caps"].is_array() ? j["caps"] : Json::array();
    p.last_seen = now_unix();
    p.lan_seen = p.last_seen;
}

void Link::start() {
    if (running_.exchange(true)) return;
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ >= 0) {
        int one = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
        ::setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
        timeval tv{1, 0};
        ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port_);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(sock_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) log("link bind failed port=%u", port_);
    }
    thread_ = std::thread([this] { loop(); });
    http::set_dialer([this](const std::string& inst, std::string& err) { return wan_dial(inst, err); });
    log("link start port=%u instance=%s", port_, self_.instance.c_str());
}

void Link::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
}

void Link::loop() {
    std::int64_t next = 0;
    char buf[2048];
    while (running_) {
        if (sock_ < 0) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        const std::int64_t now = now_unix();
        if (now >= next) {  // annonce toutes les 5 s
            next = now + 5;
            const std::string msg = announce_payload();
            auto send_to = [&](const std::string& ip) {
                sockaddr_in d{};
                d.sin_family = AF_INET;
                d.sin_port = htons(port_);
                if (::inet_pton(AF_INET, ip.c_str(), &d.sin_addr) == 1) ::sendto(sock_, msg.data(), msg.size(), 0, reinterpret_cast<sockaddr*>(&d), sizeof d);
            };
            if (announce_to_.empty()) {  // diffusion générale + adresse de diffusion de chaque réseau (certaines piles ignorent la première)
                send_to("255.255.255.255");
                ifaddrs* ifs = nullptr;
                if (::getifaddrs(&ifs) == 0) {
                    for (ifaddrs* i = ifs; i; i = i->ifa_next)
                        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && i->ifa_broadaddr && (i->ifa_flags & IFF_BROADCAST) && !(i->ifa_flags & IFF_LOOPBACK)) {
                            char ip[INET_ADDRSTRLEN] = {0};
                            ::inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_broadaddr)->sin_addr, ip, sizeof ip);
                            if (ip[0]) send_to(ip);
                        }
                    ::freeifaddrs(ifs);
                }
            }
            else for (const auto& ip : announce_to_) send_to(ip);
        }
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        const ssize_t n = ::recvfrom(sock_, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n > 0) {
            char ip[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
            on_announce(ip, std::string(buf, static_cast<std::size_t>(n)));
        }
    }
}

Json Link::peers() const {
    std::lock_guard<std::mutex> l(mu_);
    Json out = Json::array();
    const std::int64_t now = now_unix();
    for (const auto& [id, p] : peers_) {
        Json j = Json::object();
        j.set("instance", p.instance);
        j.set("app", p.app);
        j.set("name", p.name);
        j.set("version", p.version);
        if (!p.installed.empty()) j.set("installed", p.installed);
        j.set("host", p.host);
        j.set("port", static_cast<std::int64_t>(p.port));
        j.set("caps", p.caps);
        j.set("last_seen", p.last_seen);
        const bool lan = p.lan_seen > 0 && now - p.lan_seen < 20;
        const bool wan_up = p.wan && wan_ && (wan_->has_parked(id) || wan_->parking(id) || (p.last_seen > 0 && now - p.last_seen < 90));
        j.set("online", (!p.wan && p.last_seen > 0 && now - p.last_seen < 20) || lan || wan_up);
        j.set("paired", !p.token.empty());
        j.set("remote", p.wan);  // PS5 à distance (par Internet)
        j.set("via", lan ? "lan" : wan_up ? "wan" : "");
        j.set("wan_channel", p.wan && wan_ && (wan_->has_parked(id) || wan_->parking(id)));  // connexion ouverte en continu
        j.set("wan_host", p.wan_host);
        j.set("seen_ip", p.seen_ip);
        j.set("lan_ip", p.lan_ip);
        if (p.wan) j.set("wan_dir", p.wan_dir);
        j.set("wan_port", static_cast<std::int64_t>(p.wan_port));
        if (p.rtt_ms >= 0 && now - p.rtt_at < 120) j.set("rtt_ms", static_cast<std::int64_t>(p.rtt_ms));
        out.push(j);
    }
    return out;
}

bool Link::pair(const std::string& host, std::uint16_t port, const std::string& code, std::string& instance, std::string& error) {
    if (host.empty() || host.find_first_not_of("0123456789.") != std::string::npos) { error = "adresse invalide"; return false; }
    const std::string base = "http://" + host + ":" + std::to_string(port ? port : 52370);
    // Qui répond ? (accès libre : /api/v2/status)
    std::string text;
    Json st;
    if (http::get_text(base + "/api/v2/status", text, 1 << 16).status != 200 || !Json::parse(text, st)) { error = "console injoignable"; return false; }
    Json b = Json::object(), r;
    b.set("code", code);
    b.set("name", (self_.name ? self_.name() : std::string("PS5")) + " (" + self_.app + ")");
    std::string out;
    const http::Response resp = http::request("POST", base + "/api/v2/pair", "", b.dump(), out);
    if (resp.status != 200 || !Json::parse(out, r) || r["token"].as_string().empty()) { error = resp.status == 401 ? "code refusé" : "jumelage impossible"; return false; }
    // Identifiant STABLE de l'autre (link_id) ; « instance » du moteur change à chaque démarrage (ancien moteur seulement).
    instance = !st["link_id"].as_string().empty() ? st["link_id"].as_string() : st["instance"].as_string();
    if (instance.empty()) { error = "réponse incomplète"; return false; }
    {
        std::lock_guard<std::mutex> l(mu_);
        Peer& p = peers_[instance];
        p.instance = instance;
        p.host = host;
        p.port = port ? port : 52370;
        if (p.app.empty()) p.app = st["engine"].as_string();
        if (p.version.empty()) p.version = st["version"].as_string();
        p.token = r["token"].as_string();
        p.last_seen = now_unix();  // on vient de lui parler : en ligne
    }
    save();
    log("link paired instance=%s host=%s", instance.c_str(), host.c_str());
    // Jumelage mutuel : on donne à l'autre un jeton pour nous joindre (un seul code pour les deux sens).
    if (self_.mint_token) {
        std::string other;
        { std::lock_guard<std::mutex> l(mu_); other = peers_[instance].name; }
        const std::string back = self_.mint_token(other.empty() ? host : other);
        if (!back.empty()) {
            Json hello = Json::object(), ack;
            hello.set("proto", static_cast<std::int64_t>(kProtocol));
            hello.set("instance", self_.instance);
            hello.set("app", self_.app);
            hello.set("name", self_.name ? self_.name() : std::string("PS5"));
            hello.set("version", self_.version);
            hello.set("port", static_cast<std::int64_t>(self_.port));
            hello.set("caps", self_.caps);
            hello.set("token", back);
            std::string e2;
            if (!call(instance, "POST", "/api/v2/link/hello", hello, ack, e2)) log("link hello to %s failed: %s", instance.c_str(), e2.c_str());
        }
    }
    return true;
}

bool Link::accept(const std::string& host, const Json& hello, std::string& error) {
    const std::string inst = hello["instance"].as_string(), token = hello["token"].as_string();
    if (hello["proto"].as_int() != kProtocol || inst.empty() || inst.size() > 64 || inst == self_.instance || token.size() < 16 || token.size() > 64) { error = "présentation invalide"; return false; }
    {
        std::lock_guard<std::mutex> l(mu_);
        Peer& p = peers_[inst];
        p.instance = inst;
        p.app = hello["app"].as_string().substr(0, 64);
        p.name = hello["name"].as_string().substr(0, 64);
        p.version = hello["version"].as_string().substr(0, 32);
        p.installed = hello["installed"].as_string().substr(0, 32);
        p.host = host;
        p.port = static_cast<std::uint16_t>(hello["port"].as_int(52370));
        p.caps = hello["caps"].is_array() ? hello["caps"] : Json::array();
        p.token = token;
        if (!p.last_seen) p.last_seen = now_unix();
    }
    save();
    log("link accepted instance=%s host=%s", inst.c_str(), host.c_str());
    return true;
}

bool Link::forget(const std::string& instance) {
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end()) return false;
        it->second.token.clear();
        if (!it->second.last_seen || it->second.wan) peers_.erase(it);  // à distance : oubliée entièrement
    }
    save();
    update_park_targets();
    return true;
}

bool Link::call(const std::string& instance, const std::string& method, const std::string& path, const Json& body, Json& out, std::string& error, int timeout_s) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty()) { error = "console non jumelée"; return false; }
        p = it->second;
    }
    if (path.rfind("/api/", 0) != 0) { error = "chemin invalide"; return false; }
    std::string text;
    const auto t0 = std::chrono::steady_clock::now();
    const http::Response r = timeout_s > 0 ? http::request(method, base_url(p) + path, p.token, method == "GET" ? std::string() : body.dump(), text, timeout_s)
                                           : http::request(method, base_url(p) + path, p.token, method == "GET" ? std::string() : body.dump(), text);
    // 2xx = réussi (202 : accepté, une tâche démarre là-bas). Sinon, le message de l'autre console s'il y en a un.
    if (r.status < 200 || r.status >= 300) {
        Json e;
        error = Json::parse(text, e) && !e["error"].as_string().empty() ? e["error"].as_string() : r.error.empty() ? "réponse " + std::to_string(r.status) : r.error;
        return false;
    }
    if (!Json::parse(text, out)) { error = "réponse illisible"; return false; }
    {   // a répondu : en ligne ; son état sert aussi à mesurer le temps de réponse (ms affichées sur sa carte)
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it != peers_.end()) {
            it->second.last_seen = now_unix();
            if (method == "GET" && path == "/api/v2/status") {
                it->second.rtt_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
                it->second.rtt_at = now_unix();
            }
        }
    }
    return true;
}

bool Link::upload(const std::string& instance, const std::string& transfer, const std::string& name, const std::string& host_file, std::string& error) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty()) { error = "console non jumelée"; return false; }
        p = it->second;
    }
    // Empreinte du fichier (vérifiée par la console qui reçoit).
    FILE* f = std::fopen(host_file.c_str(), "rb");
    if (!f) { error = "fichier illisible"; return false; }
    self_update_sha256 ctx;
    self_update_sha256_start(&ctx);
    char buf[65536];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) self_update_sha256_add(&ctx, buf, n);
    std::fclose(f);
    unsigned char dg[32];
    self_update_sha256_finish(&ctx, dg);
    std::string hex;
    for (unsigned char c : dg) { static const char* h = "0123456789abcdef"; hex += h[c >> 4]; hex += h[c & 15]; }
    std::string out;
    const http::Response r = http::put_file(base_url(p) + "/api/v2/link/inbox?transfer=" + transfer + "&name=" + name + "&sha256=" + hex, p.token, host_file, out, 120);
    if (r.status != 200) { error = r.error.empty() ? "envoi refusé" : r.error; return false; }
    return true;
}

namespace {
std::string qenc(const std::string& s) {  // encodage d'un paramètre d'URL
    static const char* h = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') o += static_cast<char>(c);
        else { o += '%'; o += h[c >> 4]; o += h[c & 15]; }
    }
    return o;
}
}  // namespace

bool Link::put_chunk(const std::string& instance, const std::string& place, const std::string& rel, std::uint64_t offset, const std::string& data, std::string& error) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty()) { error = "console non jumelée"; return false; }
        p = it->second;
    }
    std::string out;
    const http::Response r = http::request_raw("PUT", base_url(p) + "/api/v2/link/fs/chunk?place=" + qenc(place) + "&rel=" + qenc(rel) + "&offset=" + std::to_string(offset), p.token, data, out, 120);
    if (r.status != 200) { error = r.error.empty() ? "envoi refusé" : r.error; return false; }
    return true;
}

bool Link::get_chunk(const std::string& instance, const std::string& place, const std::string& rel, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty()) { error = "console non jumelée"; return false; }
        p = it->second;
    }
    const http::Response r = http::request("GET", base_url(p) + "/api/v2/link/fs/read?place=" + qenc(place) + "&rel=" + qenc(rel) +
                                           "&offset=" + std::to_string(offset) + "&limit=" + std::to_string(limit), p.token, "", out, 120);
    if (r.status != 200) { error = r.error.empty() ? "lecture refusée" : r.error; return false; }
    return true;
}

bool Link::get_raw(const std::string& instance, const std::string& path, std::string& out, int timeout_s) {
    Peer p;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = peers_.find(instance);
        if (it == peers_.end() || it->second.token.empty() || path.rfind("/api/", 0) != 0) return false;
        p = it->second;
    }
    const http::Response r = http::request("GET", base_url(p) + path, p.token, "", out, timeout_s);
    return r.status == 200 && !out.empty();
}

void Link::load() {
    std::string text;
    Json j;
    if (!fs::read(fs::host(fs::join(state_dir_, "link-peers.json")), text) || !Json::parse(text, j)) return;
    std::lock_guard<std::mutex> l(mu_);
    for (const auto& x : j["peers"].items()) {
        Peer p;
        p.instance = x["instance"].as_string();
        if (p.instance.empty()) continue;
        p.app = x["app"].as_string();
        p.name = x["name"].as_string();
        p.version = x["version"].as_string();
        p.host = x["host"].as_string();
        p.port = static_cast<std::uint16_t>(x["port"].as_int(52370));
        p.token = x["token"].as_string();
        p.wan = x["wan"].as_bool();
        p.wan_host = x["wan_host"].as_string();
        p.wan_port = static_cast<std::uint16_t>(x["wan_port"].as_int(0));
        p.seen_ip = x["seen_ip"].as_string();
        p.lan_ip = x["lan_ip"].as_string();
        p.wan_dir = x["wan_dir"].as_string().empty() ? "auto" : x["wan_dir"].as_string();
        peers_[p.instance] = p;
    }
}

void Link::save() const {
    Json arr = Json::array();
    {
        std::lock_guard<std::mutex> l(mu_);
        for (const auto& [id, p] : peers_) {
            if (p.token.empty()) continue;  // seuls les pairs jumelés sont gardés
            Json x = Json::object();
            x.set("instance", p.instance);
            x.set("app", p.app);
            x.set("name", p.name);
            x.set("version", p.version);
            x.set("host", p.host);
            x.set("port", static_cast<std::int64_t>(p.port));
            x.set("token", p.token);
            if (p.wan) {
                x.set("wan", true);
                x.set("wan_host", p.wan_host);
                x.set("wan_port", static_cast<std::int64_t>(p.wan_port));
                x.set("seen_ip", p.seen_ip);
                x.set("lan_ip", p.lan_ip);
                x.set("wan_dir", p.wan_dir);
            }
            arr.push(x);
        }
    }
    Json j = Json::object();
    j.set("format", "pbc-link-peers");
    j.set("peers", arr);
    fs::write_atomic(fs::host(fs::join(state_dir_, "link-peers.json")), j.dump(true) + "\n");
}

void Link::note_installed(const std::string& instance, const std::string& installed) {
    if (installed.empty()) return;
    std::lock_guard<std::mutex> l(mu_);
    const auto it = peers_.find(instance);
    if (it != peers_.end()) it->second.installed = installed.substr(0, 32);
}

}  // namespace pbc::link
