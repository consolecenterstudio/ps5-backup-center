#include "update/autostart.hpp"
#include "http/server.hpp"
#include "core/http_client.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>

#include <map>

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/json.hpp"
#include "core/notify.hpp"
#include "core/memory.hpp"
#include "core/util.hpp"
#include "accounts/accounts.hpp"
#include "engine.hpp"
#include "http/web_assets.hpp"
#include "images/images.hpp"
#include "core/image.hpp"

namespace pbc {

namespace {

constexpr std::size_t max_request = 64 * 1024;
constexpr int max_clients = 16;

struct Request {
    std::string method, path, query, body, token, client;
    std::string peer;                // adresse IP de l'appelant
    bool stream = false;             // PUT /api/v2/link/inbox : le corps reste dans la socket (body = début déjà lu)
    std::uint64_t stream_length = 0; // taille totale annoncée
    std::string param(std::string_view name) const;
};

std::string url_decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; };
            int a = hex(s[i + 1]), b = hex(s[i + 2]);
            if (a < 0 || b < 0) { out += s[i]; continue; }
            out += static_cast<char>(a * 16 + b);
            i += 2;
        } else out += s[i];
    }
    return out;
}

std::string Request::param(std::string_view name) const {
    std::string_view q(query);
    while (!q.empty()) {
        auto amp = q.find('&');
        auto pair = q.substr(0, amp);
        auto eq = pair.find('=');
        if (pair.substr(0, eq) == name) return eq == std::string_view::npos ? std::string() : url_decode(pair.substr(eq + 1));
        if (amp == std::string_view::npos) break;
        q.remove_prefix(amp + 1);
    }
    return {};
}

bool send_all(int fd, const char* p, std::size_t n) {
    // Sur la console, la socket acceptée hérite du mode non bloquant de l'écoute : une réponse plus grosse que le
    // tampon (≈ 8 Ko en local) renvoie EAGAIN. On attend que l'autre côté lise (60 s au plus), sans rien perdre.
    auto stalled = std::chrono::steady_clock::now();  // depuis le dernier octet parti
    while (n) {
        ssize_t w = ::send(fd, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && std::chrono::steady_clock::now() - stalled < std::chrono::seconds(60)) {
                pollfd pw{fd, POLLOUT, 0};
                ::poll(&pw, 1, 500);
                continue;
            }
            return false;
        }
        stalled = std::chrono::steady_clock::now();
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

thread_local int t_status = 0;          // diagnostic : dernière réponse de ce fil
thread_local std::size_t t_bytes = 0;
thread_local bool t_sent = false;
void respond(int fd, int status, const char* type, const std::string& body) {
    t_status = status; t_bytes = body.size();
    const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized"
                       : status == 404 ? "Not Found" : status == 409 ? "Conflict" : "Error";
    char head[256];
    int n = std::snprintf(head, sizeof head,
                          "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                          "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",
                          status, reason, type, body.size());
    t_sent = send_all(fd, head, static_cast<std::size_t>(n)) && send_all(fd, body.data(), body.size());
}
void json(int fd, int status, const Json& j) { respond(fd, status, "application/json; charset=utf-8", j.dump()); }
void error(int fd, int status, const std::string& message) { Json j = Json::object(); j.set("error", message); json(fd, status, j); }

bool read_request(int fd, Request& r) {
    std::string buf;
    char tmp[8192];
    std::size_t header_end = std::string::npos, content_length = 0;
    for (;;) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 5000) <= 0) return false;
        ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
        if (n <= 0) return false;
        buf.append(tmp, static_cast<std::size_t>(n));
        if (buf.size() > max_request && !(header_end != std::string::npos && r.stream)) return false;
        if (header_end == std::string::npos) {
            header_end = buf.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            std::string_view head(buf.data(), header_end);
            auto line_end = head.find("\r\n");
            std::string_view line = head.substr(0, line_end);
            auto s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
            if (s1 == std::string_view::npos || s2 == std::string_view::npos) return false;
            r.method = std::string(line.substr(0, s1));
            std::string_view target = line.substr(s1 + 1, s2 - s1 - 1);
            auto qm = target.find('?');
            r.path = url_decode(target.substr(0, qm));
            if (qm != std::string_view::npos) r.query = std::string(target.substr(qm + 1));
            std::size_t pos = line_end == std::string_view::npos ? head.size() : line_end + 2;
            while (pos < head.size()) {
                auto e = head.find("\r\n", pos);
                std::string_view h = head.substr(pos, e == std::string_view::npos ? head.size() - pos : e - pos);
                auto colon = h.find(':');
                if (colon != std::string_view::npos) {
                    std::string key(h.substr(0, colon));
                    for (auto& c : key) c = static_cast<char>(c | 32);
                    std::string_view val = h.substr(colon + 1);
                    while (!val.empty() && val.front() == ' ') val.remove_prefix(1);
                    if (key == "content-length") content_length = static_cast<std::size_t>(std::strtoull(std::string(val).c_str(), nullptr, 10));
                    else if (key == "x-pbc-token") r.token = std::string(val);
                    else if (key == "x-pbc-client") r.client = std::string(val).substr(0, 64);
                }
                if (e == std::string_view::npos) break;
                pos = e + 2;
            }
            // Réception d'un fichier d'une autre console : flux jusqu'à 4 Go, lu par le gestionnaire (jamais en mémoire).
            if (r.method == "PUT" && (r.path == "/api/v2/link/inbox" || r.path == "/api/v2/link/fs/chunk") && content_length <= (4ull << 30)) {
                r.stream = true;
                r.stream_length = content_length;
                r.body = buf.substr(header_end + 4);
                return true;
            }
            if (content_length > max_request) return false;
        }
        if (buf.size() >= header_end + 4 + content_length) {
            r.body = buf.substr(header_end + 4, content_length);
            return true;
        }
    }
}

std::vector<Part> parts_of(const Json& a) {
    std::vector<Part> v;
    for (const auto& p : a.items()) { Part x; if (parse_part(p.as_string(), x)) v.push_back(x); }
    return v;
}

}  // namespace

bool HttpServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
    // Une socket orpheline d'un ancien moteur (arrêté brutalement) ne doit jamais empêcher le suivant d'écouter.
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
    ::fcntl(listen_fd_, F_SETFD, FD_CLOEXEC);  // jamais transmise à un autre programme
    quit_ = false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port_);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(listen_fd_, 32) != 0) {
        static int reported = 0;
        if (!reported++) log("http bind busy port=%u errno=%d (waiting)", port_, errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (port_ == 0) { socklen_t len = sizeof a; ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&a), &len); port_ = ntohs(a.sin_port); }
    thread_ = std::thread([this] { accept_loop(); });
    log("http listening port=%u", port_);
    return true;
}

void HttpServer::stop() {
    if (quit_.exchange(true)) return;
    if (listen_fd_ >= 0) { ::shutdown(listen_fd_, SHUT_RDWR); ::close(listen_fd_); listen_fd_ = -1; }
    if (thread_.joinable()) thread_.join();
    // Les connexions en cours ont 3 s pour finir ; au-delà, on n'attend plus (elles mourront avec le processus).
    for (int i = 0; i < 300 && clients_.load() > 0; ++i) ::usleep(10 * 1000);
}

// Lectures sur disque lancées par des demandes (état des images, compatibilité…) : UNE SEULE à la fois par sorte.
// Si une lecture traîne (disque ou montage coincé), les demandes suivantes reçoivent tout de suite le dernier résultat
// connu, marqué « stale », au lieu de se coincer à leur tour — au pire un seul fil reste bloqué, jamais tout le serveur.
namespace {
struct Flight {
    std::mutex mu;
    std::condition_variable cv;
    bool running = false;
    std::uint64_t gen = 0;
    std::map<std::string, Json> last;  // dernier résultat par clé (ex. élément)
};
Json single_flight(Flight& f, const std::string& key, int wait_ms, std::function<Json()> work) {
    std::unique_lock<std::mutex> l(f.mu);
    if (!f.running) {
        f.running = true;
        const std::uint64_t my = ++f.gen;
        std::thread([&f, key, work, my] {
            Json r = work();
            std::lock_guard<std::mutex> g(f.mu);
            f.last[key] = r;
            if (f.gen == my) f.running = false;
            f.cv.notify_all();
        }).detach();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    while (f.running && f.cv.wait_until(l, deadline) != std::cv_status::timeout) {}
    auto it = f.last.find(key);
    if (!f.running && it != f.last.end()) return it->second;
    // Toujours en cours (ou rien encore pour cette clé) : ce qu'on a, marqué comme pas à jour.
    Json r = it != f.last.end() ? it->second : Json::object();
    if (!r.is_object()) r = Json::object();
    r.set("stale", true);
    r.set("busy", true);
    return r;
}
Flight g_images_flight, g_compat_flight;
}  // namespace

void HttpServer::accept_loop() {
    while (!quit_) {
        pollfd p{listen_fd_, POLLIN, 0};
        if (::poll(&p, 1, 250) <= 0) continue;
        sockaddr_in peer{};
        socklen_t plen = sizeof peer;
        int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (fd < 0) continue;
        // L'app télé tourne sur la console : ses requêtes arrivent par la boucle locale.
        const bool local = peer.sin_family == AF_INET && (ntohl(peer.sin_addr.s_addr) >> 24) == 127;
        if (clients_.load() >= max_clients) {
            // Saturé : passent quand même la demande d'arrêt (relais d'un nouveau moteur), l'état (« es-tu là ? », jamais
            // refusé : sinon l'appli télé croit le moteur parti) et, dans une petite marge, les demandes de l'appli télé.
            timeval ptv{0, 300 * 1000};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &ptv, sizeof ptv);
            char head[48] = {0};
            const ssize_t n = ::recv(fd, head, sizeof head - 1, MSG_PEEK);
            const bool stop = n > 0 && std::strncmp(head, "POST /api/v2/engine/stop", 24) == 0;
            const bool status = n > 0 && std::strncmp(head, "GET /api/v2/status", 18) == 0;
            if (!stop && !status && !(local && clients_.load() < max_clients + 8)) { error(fd, 503, "serveur occupé"); ::close(fd); continue; }
        }
        // Une connexion muette ou très lente ne garde pas sa place indéfiniment : 20 s pour envoyer sa demande,
        // 60 s pour recevoir la réponse.
        timeval rtv{20, 0}, stv{60, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof rtv);
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof stv);
        ++clients_;
        std::thread([this, fd, local] { handle(fd, local); ::close(fd); --clients_; }).detach();
    }
}

void HttpServer::serve(int fd) {
    timeval rtv{20, 0}, stv{60, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof rtv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof stv);
    ++clients_;
    std::thread([this, fd] { handle(fd, false); ::shutdown(fd, SHUT_RDWR); ::close(fd); --clients_; }).detach();
}

namespace {
// Demandes en cours (diagnostic des saturations : « serveur occupé » sans savoir par qui).
std::mutex g_inflight_mu;
std::map<int, std::pair<std::string, std::int64_t>> g_inflight;  // descripteur → (« GET /api/v2/… [ip] », début)
struct Inflight {
    int fd;
    Inflight(int f, const std::string& what) : fd(f) { std::lock_guard<std::mutex> l(g_inflight_mu); g_inflight[f] = {what, now_unix()}; }
    ~Inflight() { std::lock_guard<std::mutex> l(g_inflight_mu); g_inflight.erase(fd); }
};
Json inflight_json() {
    std::vector<std::pair<std::int64_t, std::string>> v;
    { std::lock_guard<std::mutex> l(g_inflight_mu); for (const auto& [fd, x] : g_inflight) v.push_back({x.second, x.first}); }
    std::sort(v.begin(), v.end());
    Json a = Json::array();
    const std::int64_t now = now_unix();
    for (std::size_t i = 0; i < v.size() && i < 24; ++i) { Json j = Json::object(); j.set("what", v[i].second); j.set("age", now - v[i].first); a.push(j); }
    return a;
}
}  // namespace

void HttpServer::handle(int fd, bool local) {
    Request r;
    if (!read_request(fd, r)) return;
    {
        sockaddr_in a{};
        socklen_t al = sizeof a;
        char ip[INET_ADDRSTRLEN] = {0};
        if (::getpeername(fd, reinterpret_cast<sockaddr*>(&a), &al) == 0) ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        r.peer = ip;
    }
    Engine& e = engine_;
    const std::string what_q = r.path.size() > 90 ? r.path.substr(0, 90) : r.path;
    const Inflight inflight(fd, r.method + " " + what_q + " [" + r.peer + "]");
    struct PeakNote {  // la requête qui fait battre le record de mémoire est notée dans le journal (diagnostic CE-15773-3)
        std::string what; std::uint64_t p0 = memory::peak_inuse();
        ~PeakNote() { if (!what.empty()) memory::note_peak(what.c_str(), p0); }
    } peak_note;
    peak_note.what = r.method + " " + r.path;

    // Pages web (libres : elles ne contiennent aucune donnée ; les données passent par l'API).
    if (r.method == "GET" && r.path.rfind("/api/", 0) != 0) {
        const std::string want = r.path == "/" ? "/index.html" : r.path;
        for (unsigned i = 0; i < web::asset_count; ++i)
            if (want == web::assets[i].path) {
                respond(fd, 200, web::assets[i].type, std::string(reinterpret_cast<const char*>(web::assets[i].data), web::assets[i].size));
                return;
            }
        error(fd, 404, "page inconnue");
        return;
    }
    // Accès libre : état du moteur (version, santé) et appairage par code. Tout le reste exige un jeton.
    if (r.method == "GET" && r.path == "/api/v2/status") {
        Json st = e.status();
        st.set("inflight", inflight_json());
        st.set("clients", clients_.load());
        json(fd, 200, st);
        return;
    }
    if (r.method == "OPTIONS") { respond(fd, 200, "text/plain", ""); return; }
    if (e.token().empty()) { error(fd, 503, "moteur en cours de démarrage"); return; }

    Json body;
    if (!r.stream && !r.body.empty() && !Json::parse(r.body, body)) { error(fd, 400, "JSON invalide"); return; }

    if (r.method == "POST" && r.path == "/api/v2/pair") {
        std::string token, err;
        if (!e.pair(body["code"].as_string(), body["name"].as_string(), token, err)) { error(fd, 401, err); return; }
        Json j = Json::object();
        j.set("token", token);
        json(fd, 200, j);
        return;
    }
    // Une autre PS5 télécharge notre version installée : jeton à usage limité dans le chemin (le client de mise à jour
    // ne garde pas les paramètres d'adresse). Lecture seule, seulement les fichiers de l'app.
    if (r.method == "GET" && r.path.rfind("/api/v2/update/self/", 0) == 0) {
        const std::string rest = r.path.substr(20);
        const auto sl = rest.find('/');
        if (sl == std::string::npos || !e.update_token_ok(rest.substr(0, sl))) { error(fd, 401, "jeton de mise à jour invalide"); return; }
        const std::string what = rest.substr(sl + 1);
        if (what == "manifest.json") { json(fd, 200, e.updater().self_manifest()); return; }
        std::string data;
        if (what.rfind("files/", 0) == 0 && e.updater().self_file(url_decode(what.substr(6)), data)) { respond(fd, 200, "application/octet-stream", data); return; }
        error(fd, 404, "fichier inconnu");
        return;
    }
    if (!local && !e.authorized(r.token) && !e.authorized(r.param("token"))) { error(fd, 401, "appareil non associé"); return; }
    if (r.client.rfind("tv/", 0) == 0) e.note_tv_app(r.client.substr(3));
    struct Done {  // diagnostic : réponse envoyée à l'appli de la console (code, taille, durée, envoi complet)
        Engine& e; std::string what; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~Done() {
            if (what.empty()) return;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            e.note_local_request("  → " + std::to_string(t_status) + " " + std::to_string(t_bytes) + " o " + std::to_string(ms) + " ms" + (t_sent ? "" : " ENVOI INCOMPLET") + " · " + what);
        }
    } done_note{e, ""};
    t_status = 0; t_bytes = 0; t_sent = false;
    if (r.peer == "127.0.0.1" && r.path.rfind("/api/v2/", 0) == 0 && r.path != "/api/v2/status") {
        const std::string w = r.method + " " + r.path.substr(8) + (r.query.empty() ? "" : "?" + r.query.substr(0, 60));
        e.note_local_request(w);
        if (r.path.rfind("/api/v2/sync/", 0) == 0) done_note.what = r.path.substr(8);
    }
    if (r.method == "GET" && r.path == "/api/v2/pair-code") {  // affiché par l'app télé (boucle locale seulement)
        if (!local) { error(fd, 403, "réservé à la console"); return; }
        Json j = Json::object();
        j.set("code", e.pair_code());
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/devices") { json(fd, 200, e.devices_json()); return; }
    if (r.method == "POST" && r.path.rfind("/api/v2/devices/", 0) == 0 && r.path.size() > 23 && r.path.substr(r.path.size() - 7) == "/forget") {
        const std::string id = r.path.substr(16, r.path.size() - 16 - 7);
        if (!e.forget_device(id)) { error(fd, 404, "appareil inconnu"); return; }
        json(fd, 200, Json::object());
        return;
    }

    if (r.method == "POST" && r.path == "/api/v2/engine/stop") {
        json(fd, 202, Json::object());
        if (on_stop) on_stop();
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/plans") { json(fd, 200, e.plans_json()); return; }
    if (r.method == "POST" && r.path == "/api/v2/plans") {
        std::string err;
        const std::string id = e.save_plan(body, err);
        if (id.empty()) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("id", id);
        json(fd, 200, j);
        return;
    }
    // /api/v2/plans/<id>/run | delete
    if (r.method == "POST" && r.path.rfind("/api/v2/plans/", 0) == 0) {
        const std::string rest = r.path.substr(14);
        const auto slash = rest.rfind('/');
        if (slash == std::string::npos) { error(fd, 404, "action manquante"); return; }
        const std::string id = rest.substr(0, slash), action = rest.substr(slash + 1);
        std::string err;
        if (action == "run") {
            const int n = e.run_plan(id, err);
            if (!n && !err.empty()) { error(fd, 409, err); return; }
            Json j = Json::object();
            j.set("queued", static_cast<std::int64_t>(n));
            json(fd, 200, j);
            return;
        }
        if (action == "delete") { if (!e.delete_plan(id)) { error(fd, 404, "plan inconnu"); return; } json(fd, 200, Json::object()); return; }
        error(fd, 404, "action inconnue");
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/sync/compare") {
        std::string err;
        const Json j = e.sync_compare(r.param("a"), r.param("b"), err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/eden/profiles") { json(fd, 200, e.eden_profiles_json()); return; }
    if (r.method == "GET" && r.path == "/api/v2/remotes") { json(fd, 200, e.remotes_json()); return; }
    if (r.method == "POST" && r.path == "/api/v2/remotes") {
        std::string id, err;
        if (!e.add_remote(body, id, err)) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("id", id);
        json(fd, 201, j);
        return;
    }
    // POST /api/v2/remotes/test-config {mêmes réglages} : tester avant d'enregistrer (rien n'est gardé)
    if (r.method == "POST" && r.path == "/api/v2/remotes/test-config") {
        std::string err;
        if (!e.test_config(body, err)) { error(fd, 409, err); return; }
        json(fd, 200, Json::object());
        return;
    }
    // POST /api/v2/remotes/browse {type, host, port, user, password, tls, path, id?} : parcourir avant d'enregistrer
    if (r.method == "POST" && r.path == "/api/v2/remotes/browse") {
        std::string err;
        const Json j = e.browse_config(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    // /api/v2/remotes/<id>/list?path= : explorateur d'un appareil réseau
    if (r.method == "GET" && r.path.rfind("/api/v2/remotes/", 0) == 0 && r.path.size() > 21 && r.path.substr(r.path.size() - 5) == "/list") {
        std::string err;
        const Json j = e.browse_remote(r.path.substr(16, r.path.size() - 16 - 5), r.param("path"), err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    // /api/v2/remotes/<id>/saves : sauvegardes d'une Switch (JKSV)
    if (r.method == "GET" && r.path.rfind("/api/v2/remotes/", 0) == 0 && r.path.size() > 22 && r.path.substr(r.path.size() - 6) == "/saves") {
        std::string err;
        const Json j = e.switch_saves(r.path.substr(16, r.path.size() - 16 - 6), err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    // /api/v2/remotes/<id>/test | forget
    if (r.method == "POST" && r.path.rfind("/api/v2/remotes/", 0) == 0) {
        const std::string rest = r.path.substr(16);
        const auto slash = rest.rfind('/');
        if (slash == std::string::npos) { error(fd, 404, "action manquante"); return; }
        const std::string id = rest.substr(0, slash), action = rest.substr(slash + 1);
        std::string err;
        if (action == "adopt") { const Json j = e.adopt_backups(id, err); if (!err.empty()) { error(fd, 409, err); return; } json(fd, 200, j); return; }
        if (action == "test") { if (!e.test_remote(id, err)) { error(fd, 409, err); return; } json(fd, 200, Json::object()); return; }
        if (action == "update") { if (!e.update_remote(id, body, err)) { error(fd, 409, err); return; } json(fd, 200, Json::object()); return; }
        if (action == "forget") { if (!e.forget_remote(id)) { error(fd, 404, "appareil inconnu"); return; } json(fd, 200, Json::object()); return; }
        error(fd, 404, "action inconnue");
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/saves/overview") { json(fd, 200, e.saves_overview()); return; }
    if (r.method == "GET" && r.path == "/api/v2/playtime") { json(fd, 200, e.playtime()); return; }  // notre compteur, par jeu
    // Versions d'une sauvegarde déjà comparées par leur contenu (cache de la tâche « inspect_save »).
    if (r.method == "GET" && r.path == "/api/v2/saves/inspect") { json(fd, 200, e.save_inspections(r.param("title_id"), r.param("user_id"))); return; }
    if (r.method == "GET" && r.path == "/api/v2/accounts") { json(fd, 200, accounts::to_json(accounts::list())); return; }
    if (r.method == "GET" && r.path == "/api/v2/emulators") { json(fd, 200, e.emulators_json()); return; }
    // /api/v2/emulators/<id> : réglages manuels ({roms, config, saves}) ; /api/v2/emulators/<id>/reset : automatique.
    if (r.method == "POST" && r.path.rfind("/api/v2/emulators/", 0) == 0) {
        std::string id = r.path.substr(18);
        const bool reset = id.size() > 6 && id.compare(id.size() - 6, 6, "/reset") == 0;
        if (reset) id.resize(id.size() - 6);
        if (!e.set_emulator_settings(id, reset ? Json::object() : body)) { error(fd, 404, "émulateur inconnu"); return; }
        json(fd, 200, e.emulators_json());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/fs/list") {
        std::string err;
        const Json j = e.browse(r.param("path"), err);
        if (!err.empty()) { error(fd, 404, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/places") { json(fd, 200, e.places_json()); return; }
    if (r.method == "GET" && r.path == "/api/v2/items") { json(fd, 200, e.items_json(r.param("kind"), r.param("place"))); return; }
    if (r.method == "POST" && r.path == "/api/v2/scan") { e.request_scan(); json(fd, 202, Json::object()); return; }
    if (r.method == "GET" && r.path == "/api/v2/cover" && !r.param("size").empty()) {
        const int max = std::max(64, std::min(512, std::atoi(r.param("size").c_str())));
        std::string png, key = r.param("key");
        if (r.param("local") == "1") { key = e.local_cover_key(key); if (key.empty()) { error(fd, 404, "pas de jaquette ici"); return; } }
        if (!e.cover_thumbnail(key, max, png)) { error(fd, 404, "pas de jaquette"); return; }
        respond(fd, 200, "image/png", png);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/cover") {
        std::string p = e.cover_path(r.param("key")), data;
        if (p.empty() || !fs::read(p, data, 8u << 20)) { error(fd, 404, "pas de jaquette"); return; }
        const bool tga = p.size() > 4 && p.compare(p.size() - 4, 4, ".tga") == 0;
        const bool webp = p.size() > 5 && p.compare(p.size() - 5, 5, ".webp") == 0;
        std::string png;
        if (tga && !image::tga_to_png(data, png)) { error(fd, 415, "format d'image non pris en charge"); return; }
        if (webp) {  // le navigateur lit le WebP, on l'envoie tel quel
            respond(fd, 200, "image/webp", data);
            return;
        }
        const bool jpg = p.size() > 4 && (p.compare(p.size() - 4, 4, ".jpg") == 0 || p.compare(p.size() - 5, 5, ".jpeg") == 0);
        respond(fd, 200, jpg ? "image/jpeg" : "image/png", tga ? png : data);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/backups") { json(fd, 200, e.backups_json(r.param("place"))); return; }
    // Liaison par Internet (« PS5 à distance ») : état, invitation, rejoindre, test. Réglages : wan_enabled, wan_port,
    // wan_public_host dans /api/v2/settings.
    if (r.path.rfind("/api/v2/wan", 0) == 0 && e.link()) {
        auto* lk = e.link();
        if (r.method == "GET" && r.path == "/api/v2/wan") {
            Json j = lk->wan_status();
            j.set("public_host", e.settings().wan_public_host);
            json(fd, 200, j);
            return;
        }
        if (r.method == "POST" && r.path == "/api/v2/wan/invite") {
            std::string err, host = body["host"].as_string().empty() ? e.settings().wan_public_host : body["host"].as_string();
            if (host.empty()) host = lk->wan_status()["observed_ip"].as_string();
            const Json j = lk->wan_invite(host, err);
            if (!err.empty()) { error(fd, 409, err); return; }
            json(fd, 200, j);
            return;
        }
        if (r.method == "POST" && r.path == "/api/v2/wan/invite/cancel") { lk->wan_cancel_invites(); json(fd, 200, Json::object()); return; }
        if (r.method == "POST" && r.path == "/api/v2/wan/join") {
            std::string inst, name, err;
            if (!lk->wan_join(body["code"].as_string(), inst, name, err)) { error(fd, 409, err); return; }
            Json j = Json::object();
            j.set("instance", inst);
            j.set("name", name);
            json(fd, 200, j);
            return;
        }
        if (r.method == "POST" && r.path == "/api/v2/wan/test") { json(fd, 200, lk->wan_test(body["instance"].as_string())); return; }
        if (r.method == "POST" && r.path == "/api/v2/wan/detect-ip") {  // à la demande : adresse publique vue d'Internet
            std::string text;
            const auto resp = http::get_text("https://api.ipify.org", text, 256);
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
            if (resp.status != 200 || text.empty() || text.size() > 64) { error(fd, 409, resp.error.empty() ? "adresse publique introuvable" : resp.error); return; }
            Json j = Json::object();
            j.set("ip", text);
            json(fd, 200, j);
            return;
        }
    }
    // Démarrage du moteur : chargeurs présents sur la console (ordre choisi) et option « Démarrer avec la console ».
    if (r.method == "GET" && r.path == "/api/v2/launch") { json(fd, 200, autostart::status(e.app_dir(), e.settings().launchers)); return; }
    if (r.method == "POST" && r.path == "/api/v2/launch/autostart") {
        std::string err;
        if (!autostart::set_pldmgr(body["enabled"].as_bool(), e.app_dir(), e.state_dir(), err)) { error(fd, 409, err); return; }
        json(fd, 200, autostart::status(e.app_dir(), e.settings().launchers));
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/settings") { json(fd, 200, e.settings().to_json()); return; }
    if (r.method == "POST" && r.path == "/api/v2/settings") {
        // Réglages partiels : on part des réglages actuels et on remplace seulement les clés envoyées.
        Json merged = e.settings().to_json();
        for (const auto& [k, v] : body.members()) merged.set(k, v);
        const std::string src = merged["update_source"].as_string();
        if (!src.empty() && src.rfind("http://", 0) != 0 && src.rfind("https://", 0) != 0 && src[0] != '/') { error(fd, 400, "la source doit être une adresse http:// ou https://, ou un dossier de la console (/data/…, /mnt/usb0/…)"); return; }
        const std::string channel_before = e.settings().update_channel;
        e.set_settings(Settings::from_json(merged));
        if (e.settings().update_channel != channel_before && e.settings().update_auto_check) e.updater().check_auto(0);  // nouveau canal : regarder tout de suite
        json(fd, 200, e.settings().to_json());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/changelog") { json(fd, 200, e.updater().changelog()); return; }
    if (r.method == "GET" && r.path == "/api/v2/update") { json(fd, 200, e.updater().status()); return; }
    if (r.method == "GET" && r.path == "/api/v2/update/push") { json(fd, 200, e.update_peer_status()); return; }
    if (r.method == "POST" && r.path == "/api/v2/update/push") {  // mettre à jour une autre PS5 avec notre version
        std::string err;
        std::string peer = body["peer"].as_string();
        // Demandé par une PS5 jumelée qui se présente mal (« Récupérer » des versions 2.88.8–2.88.9 envoyait l'instance de
        // son moteur au lieu de son identifiant de liaison) : on la reconnaît à son adresse.
        if (e.link()) {
            bool known = false;
            const Json peers = e.link()->peers();
            for (const auto& p : peers.items()) if (p["instance"].as_string() == peer && p["paired"].as_bool()) known = true;
            if (!known) {
                sockaddr_in a{};
                socklen_t al = sizeof a;
                char ip[INET_ADDRSTRLEN] = {0};
                if (::getpeername(fd, reinterpret_cast<sockaddr*>(&a), &al) == 0) ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
                for (const auto& p : peers.items()) if (ip[0] && p["host"].as_string() == ip && p["paired"].as_bool()) peer = p["instance"].as_string();
            }
        }
        if (!e.update_peer(peer, err)) { error(fd, 409, err); return; }
        json(fd, 202, e.update_peer_status());
        return;
    }
    // À distance, sur une PS5 jumelée : relancer son moteur (version installée pas encore en marche), ouvrir Backup Center.
    // Le miroir de « mettre à jour une autre PS5 » : récupérer ici la version (plus récente) d'une PS5 jumelée. On lui
    // demande de nous la pousser — même mécanisme, mêmes vérifications (taille + SHA-256 de chaque fichier, ancienne gardée).
    // Qui ouvre la session avec une PS5 à distance : choisi ici (POST /wan/peer), ou reçu de l'autre (son choix inversé).
    if (r.method == "POST" && (r.path == "/api/v2/wan/peer" || r.path == "/api/v2/link/wan-dir")) {
        if (!e.link()) { error(fd, 409, "liaison indisponible"); return; }
        const bool from_peer = r.path == "/api/v2/link/wan-dir";
        std::string err;
        const std::string inst = from_peer ? body["peer"].as_string() : body["instance"].as_string();
        if (!e.link()->wan_set_peer(inst, body["dir"].as_string(), body["host"].as_string(),
                                    static_cast<std::uint16_t>(body["port"].as_int(0)), from_peer, err)) { error(fd, 409, err); return; }
        Json o = Json::object(); o.set("ok", true);
        json(fd, 200, o);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/peer-update-me") {
        if (!e.link()) { error(fd, 409, "liaison indisponible"); return; }
        Json out, b = Json::object();
        b.set("peer", e.link_id());  // l'identifiant de liaison (celui que l'autre PS5 connaît), pas l'instance du moteur
        std::string err;
        if (!e.link()->call(body["instance"].as_string(), "POST", "/api/v2/update/push", b, out, err, 20)) { error(fd, 409, err.empty() ? "autre PS5 injoignable" : err); return; }
        json(fd, 202, out);
        return;
    }
    if (r.method == "POST" && (r.path == "/api/v2/link/peer-restart-engine" || r.path == "/api/v2/link/peer-open-app")) {
        if (!e.link()) { error(fd, 409, "liaison indisponible"); return; }
        Json out;
        std::string err;
        const bool restart = r.path == "/api/v2/link/peer-restart-engine";
        Json b = Json::object();
        if (!restart) b.set("delay", 0);
        if (!e.link()->call(body["instance"].as_string(), "POST", restart ? "/api/v2/update/restart-engine" : "/api/v2/update/relaunch-app", b, out, err, 20)) {
            // Relance : la connexion coupée pendant le remplacement est normale ; un refus clair est rapporté.
            if (!(restart && (err.find("connexion") != std::string::npos || err.find("délai") != std::string::npos))) { error(fd, 409, err); return; }
        }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/update/from") {  // une autre PS5 nous propose sa version
        std::string err;
        if (!e.update_from(body["source"].as_string(), err)) { error(fd, 409, err); return; }
        e.updater().note_received_from(body["from"].as_string());
        json(fd, 202, e.updater().status());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/update/versions") {  // « Versions disponibles »
        std::string err;
        const Json j = e.updater().versions(err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/update/version") {  // {"version":"2.90.7"}
        std::string err;
        if (!e.updater().install_version(body["version"].as_string(), err)) { error(fd, 409, err); return; }
        json(fd, 202, e.updater().status());
        return;
    }
    // /api/v2/update/check | install | restart-engine | relaunch-app
    if (r.method == "POST" && r.path.rfind("/api/v2/update/", 0) == 0) {
        const std::string action = r.path.substr(15);
        std::string err;
        bool ok = false;
        if (action == "check" && body["auto"].as_bool(false)) {  // ouverture de l'appli (télé, web), entrée dans Paramètres
            if (e.settings().update_auto_check) e.updater().check_auto(60);
            json(fd, 202, e.updater().status());
            return;
        }
        if (action == "check") ok = e.updater().check(err);
        else if (action == "install") ok = e.updater().install(err);
        else if (action == "rollback") ok = e.updater().rollback(err);
        else if (action == "restart-engine") ok = e.updater().restart_engine(err);
        else if (action == "relaunch-app") ok = e.updater().relaunch_app(static_cast<int>(body["delay"].as_int(3)), err);
        else err = "action inconnue";
        if (!ok) { error(fd, 409, err); return; }
        json(fd, 202, e.updater().status());
        return;
    }

    // Images de jeux (compression .ffpfsc, images exFAT / PFS, validation, transferts) : d'après PS5 Game Compressor.
    if (r.method == "GET" && r.path == "/api/v2/images") {
        const bool measure = r.param("measure") != "0";
        json(fd, 200, single_flight(g_images_flight, measure ? "m" : "q", 20000, [&e, measure] { return e.images_json(measure); }));
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/images/plan") {
        std::string err;
        const Json j = e.image_plan(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }

    if (r.method == "GET" && r.path == "/api/v2/tasks") {
        // ?brief=1 (app télé, tas de 13 Mo) : un gros rapport (comparaison de contenu…) est réduit à ses valeurs
        // simples ; le détail reste dans la liste complète (web) et dans GET /api/v2/saves/inspect.
        const bool brief = r.param("brief") == "1";
        Json a = Json::array();
        for (const auto& t : e.queue().list()) {
            Json x = t.to_json();
            if (brief && x["report"].is_object() && x["report"].dump().size() > 32768) {
                Json small = Json::object();
                for (const auto& [k, v] : x["report"].members())
                    if (!v.is_object() && !v.is_array()) small.set(k, v);
                small.set("truncated", true);
                x.set("report", small);
            }
            a.push(std::move(x));
        }
        const Json sends = e.link_sends();  // copie gardée en vie pendant la boucle
        for (const auto& [id, s] : sends.members()) {  // envois vers une autre PS5 : des tâches comme les autres
            Json x = Json::object(), p = Json::object();
            x.set("id", "send-" + id);
            x.set("type", "link_send");
            x.set("state", s["state"].as_string() == "running" ? "running" : s["state"].as_string() == "done" ? "done" : "failed");
            x.set("title", s["game"].as_string() + " → " + s["peer"].as_string());
            x.set("item", s["item"]);
            x.set("title_id", s["title_id"]);
            x.set("step", s["step"]);
            x.set("error", s["error"]);
            p.set("files_done", s["files_done"]); p.set("files_total", s["files_total"]);
            p.set("bytes_done", s["bytes_done"]); p.set("bytes_total", s["bytes_total"]);
            x.set("progress", p);
            x.set("created", s["started"]);
            Json rep = Json::object();
            rep.set("instance", s["instance"]);
            rep.set("peer", s["peer"]);
            if (s["foreground"].is_object()) rep.set("foreground", s["foreground"]);
            x.set("report", rep);
            a.push(std::move(x));
        }
        {   // désinstallation en cours ou dernière faite : une tâche comme les autres (Tâches, Accueil)
            const Json u = e.uninstall_status();
            if (u["state"].is_string()) {
                Json x = Json::object();
                x.set("id", "uninstall-" + u["title_id"].as_string() + "-" + std::to_string(u["started"].as_int()));
                x.set("type", "game_uninstall");
                x.set("state", u["state"]);
                x.set("title", u["title"]);
                x.set("title_id", u["title_id"]);
                x.set("item", "title:" + u["title_id"].as_string());
                x.set("step", u["step"]);
                x.set("error", u["error"]);
                x.set("created", u["started"]);
                x.set("started", u["started"]);
                x.set("finished", u["finished"]);
                Json rep = Json::object();
                rep.set("removed", u["removed"]);
                rep.set("backup_task", u["backup_task"]);
                if (u["note"].is_string()) rep.set("note", u["note"]);
                x.set("report", rep);
                x.set("progress", Json::object());
                a.push(std::move(x));
            }
        }
        Json j = Json::object();
        j.set("revision", e.revision());
        j.set("tasks", a);
        json(fd, 200, j);
        return;
    }
    // Corbeille : GET /api/v2/trash?place= (liste), POST /api/v2/trash/restore {place, path} (remettre en place).
    if (r.method == "GET" && r.path == "/api/v2/players") { json(fd, 200, e.players()); return; }
    // Boîte de réception de la liaison : un fichier envoyé par une console jumelée (jeton), écrit au fil de l'eau
    // dans <état>/inbox/<transfert>/<nom>, puis vérifié (SHA-256 annoncé). Rien n'est installé ici.
    if (r.method == "PUT" && r.path == "/api/v2/link/inbox") {
        const std::string tr = r.param("transfer"), name = r.param("name"), want = r.param("sha256");
        auto ok_id = [](const std::string& x, std::size_t max) { return !x.empty() && x.size() <= max && x.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.") == std::string::npos && x[0] != '.'; };
        if (!r.stream || !ok_id(tr, 40) || !ok_id(name, 120) || want.size() != 64) { error(fd, 400, "envoi invalide"); return; }
        const std::string dir = fs::join(fs::join(e.state_dir(), "inbox"), tr);
        fs::mkdirs(fs::host(dir));
        const std::string path = fs::join(dir, name);
        FILE* f = std::fopen(fs::host(path).c_str(), "wb");
        if (!f) { error(fd, 500, "écriture impossible"); return; }
        self_update_sha256 ctx;
        self_update_sha256_start(&ctx);
        std::uint64_t got = 0;
        bool ok = true;
        auto put = [&](const char* p, std::size_t n) { self_update_sha256_add(&ctx, p, n); ok = ok && std::fwrite(p, 1, n, f) == n; got += n; };
        put(r.body.data(), std::min<std::size_t>(r.body.size(), r.stream_length));
        char buf[65536];
        while (ok && got < r.stream_length) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 30000) <= 0) { ok = false; break; }
            const ssize_t n = ::recv(fd, buf, static_cast<std::size_t>(std::min<std::uint64_t>(sizeof buf, r.stream_length - got)), 0);
            if (n <= 0) { ok = false; break; }
            put(buf, static_cast<std::size_t>(n));
        }
        std::fclose(f);
        unsigned char dg[32];
        self_update_sha256_finish(&ctx, dg);
        std::string hex;
        for (unsigned char c : dg) { static const char* h = "0123456789abcdef"; hex += h[c >> 4]; hex += h[c & 15]; }
        if (!ok || got != r.stream_length || hex != want) { fs::remove_tree(fs::host(path)); error(fd, 409, "fichier incomplet ou empreinte différente"); return; }
        Json j = Json::object();
        j.set("bytes", static_cast<std::int64_t>(got));
        json(fd, 200, j);
        return;
    }
    // Autre PS5 = destination de backup : morceau d'un fichier écrit dans <dossier PBC Backup du disque>/<rel>.part
    if (r.method == "PUT" && r.path == "/api/v2/link/fs/chunk") {
        std::string path, err;
        if (!r.stream || !e.peer_fs_path(r.param("place"), r.param("rel"), path, err)) { error(fd, 400, err.empty() ? "chemin invalide" : err); return; }
        const std::uint64_t offset = std::strtoull(r.param("offset").c_str(), nullptr, 10);
        fs::mkdirs(std::string(fs::dirname(fs::host(path))));
        FILE* f = std::fopen(fs::host(path + ".part").c_str(), offset ? "r+b" : "wb");
        if (!f) { error(fd, 500, "écriture impossible"); return; }
        bool ok = ::fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
        std::uint64_t got = 0;
        auto put = [&](const char* p, std::size_t n) { ok = ok && std::fwrite(p, 1, n, f) == n; got += n; };
        put(r.body.data(), std::min<std::size_t>(r.body.size(), r.stream_length));
        char buf[65536];
        while (ok && got < r.stream_length) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 30000) <= 0) { ok = false; break; }
            const ssize_t n = ::recv(fd, buf, static_cast<std::size_t>(std::min<std::uint64_t>(sizeof buf, r.stream_length - got)), 0);
            if (n <= 0) { ok = false; break; }
            put(buf, static_cast<std::size_t>(n));
        }
        ok = ok && std::fflush(f) == 0;
        std::fclose(f);
        if (!ok || got != r.stream_length) { error(fd, 409, "morceau incomplet"); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.stream) { error(fd, 400, "flux inattendu"); return; }
    if (r.method == "GET" && r.path == "/api/v2/link/fs/read") {
        std::string path, err, data;
        if (!e.peer_fs_path(r.param("place"), r.param("rel"), path, err)) { error(fd, 400, err); return; }
        const std::uint64_t offset = std::strtoull(r.param("offset").c_str(), nullptr, 10);
        const std::uint64_t limit = std::min<std::uint64_t>(std::strtoull(r.param("limit").c_str(), nullptr, 10), 8u << 20);
        FILE* f = std::fopen(fs::host(path).c_str(), "rb");
        if (!f) { error(fd, 404, "fichier introuvable"); return; }
        if (::fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0) { data.resize(limit); data.resize(std::fread(data.data(), 1, limit, f)); }
        std::fclose(f);
        respond(fd, 200, "application/octet-stream", data);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/fs") {
        Json out;
        std::string err;
        if (!e.peer_fs_op(body, out, err)) { error(fd, 409, err); return; }
        json(fd, 200, out);
        return;
    }
    // Liaison entre consoles : pairs vus / jumelés, jumelage par code, oubli, lecture chez un pair.
    if (r.method == "POST" && r.path == "/api/v2/link/hello") {  // une console jumelée avec notre code se présente
        std::string err;
        if (!e.link() || !e.link()->accept(r.peer, body, err)) { error(fd, 409, err.empty() ? "liaison indisponible" : err); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/peers") { json(fd, 200, e.link() ? e.link()->peers() : Json::array()); return; }
    if (r.method == "POST" && r.path == "/api/v2/link/pair") {
        std::string inst, err;
        if (!e.link() || !e.link()->pair(body["host"].as_string(), static_cast<std::uint16_t>(body["port"].as_int(52370)), body["code"].as_string(), inst, err)) { error(fd, 409, err.empty() ? "liaison indisponible" : err); return; }
        e.refresh_peer_places();  // ses disques deviennent tout de suite des destinations
        Json j = Json::object();
        j.set("instance", inst);
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/forget") {
        if (!e.link() || !e.link()->forget(body["instance"].as_string())) { error(fd, 404, "console inconnue"); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/send-save") {  // envoyer une copie à une console jumelée
        std::string id, err;
        if (!e.send_save(body["instance"].as_string(), body["place"].as_string(), body["bundle"].as_string(), body["title_id"].as_string(), body["user_id"].as_string(), id, err)) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("send", id);
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/sends") { json(fd, 200, e.link_sends()); return; }
    if (r.method == "POST" && r.path == "/api/v2/backups/install") {  // installer un jeu depuis une copie (ici ou demandé par une autre PS5)
        Json tasks;
        std::string err;
        if (!e.install_backup(body["place"].as_string(), body["bundle"].as_string(), body["target"].as_string(), body["format"].as_string(), body["user_id"].as_string(), tasks, err, body["drop_copy"].as_bool(), body["replace"].as_bool())) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("tasks", tasks);
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/compat/console") { json(fd, 200, e.compat_console()); return; }  // aussi lu par une PS5 jumelée
    if (r.method == "GET" && r.path == "/api/v2/compat") {
        const std::string item = r.param("item");
        json(fd, 200, single_flight(g_compat_flight, item, 15000, [&e, item] { return e.compat_item(item); }));
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/send-check") { json(fd, 200, e.send_check(body["place"].as_string(), body["item"].as_string())); return; }
    if (r.method == "POST" && r.path == "/api/v2/backups/install-check") { json(fd, 200, e.install_check(body["place"].as_string(), body["bundle"].as_string())); return; }
    if (r.method == "GET" && r.path == "/api/v2/link/peer-list") {  // parcourir un dossier de l'autre PS5 (lecture seule)
        Json out;
        std::string err;
        std::string enc;
        for (unsigned char ch : r.param("path")) {
            if (std::isalnum(ch) || ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') enc += static_cast<char>(ch);
            else { char h[4]; std::snprintf(h, sizeof h, "%%%02X", ch); enc += h; }
        }
        if (!e.link() || !e.link()->call(r.param("instance"), "GET", "/api/v2/fs/list?path=" + enc, Json::object(), out, err)) { error(fd, 409, err.empty() ? "liaison indisponible" : err); return; }
        json(fd, 200, out);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/app/foreground") { json(fd, 200, e.foreground_app()); return; }
    // « Fichiers » (Transferts) : endroits, contenu d'un dossier, nouveau dossier, copier / déplacer (tâche).
    if (r.method == "GET" && r.path == "/api/v2/files/places") { json(fd, 200, e.files_places()); return; }
    if (r.method == "GET" && r.path == "/api/v2/files/list") {
        Json out;
        std::string err;
        if (!e.files_list(r.param("place"), r.param("path"), out, err)) { error(fd, 409, err); return; }
        json(fd, 200, out);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/files/mkdir") {
        std::string err;
        if (!e.files_mkdir(body["place"].as_string(), body["path"].as_string(), err)) { error(fd, 409, err); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/games/uninstall") {  // {title_id, delete_source, confirm:"DESINSTALLER"}
        std::string err;
        if (!e.uninstall_game(body, err)) { error(fd, 409, err); return; }
        json(fd, 202, e.uninstall_status());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/games/uninstall") { json(fd, 200, e.uninstall_status()); return; }
    if (r.method == "POST" && r.path == "/api/v2/link/peer-uninstall") {  // désinstaller un jeu sur une PS5 jumelée (locale ou à distance)
        Json out, req = Json::object();
        std::string err;
        req.set("title_id", body["title_id"]); req.set("delete_source", body["delete_source"]); req.set("confirm", body["confirm"]);
        if (!e.link() || !e.link()->call(body["instance"].as_string(), "POST", "/api/v2/games/uninstall", req, out, err)) { error(fd, 409, err.empty() ? "autre PS5 injoignable" : err); return; }
        json(fd, 202, out);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/files/delete") {  // {place, dir, names, confirm:"SUPPRIMER"}
        std::string err;
        const std::string id = e.files_delete(body, err);
        if (id.empty()) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("task", id);
        json(fd, 202, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/files/copy") {
        std::string err;
        const std::string id = e.files_copy(body, err);
        if (id.empty()) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("task", id);
        json(fd, 202, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/power") {  // {"action":"reboot"|"rest"|"off","force":false}
        std::string err;
        if (!e.power(body["action"].as_string(), body["force"].as_bool(false), err)) { error(fd, 409, err); return; }
        json(fd, 202, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/power") {  // la même chose, demandée à une PS5 jumelée
        Json out, b = Json::object();
        b.set("action", body["action"]);
        b.set("force", body["force"]);
        std::string err;
        if (!e.link() || !e.link()->call(body["instance"].as_string(), "POST", "/api/v2/power", b, out, err)) { error(fd, 409, err.empty() ? "liaison indisponible" : err); return; }
        json(fd, 202, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/app/close") {  // fermer l'application ouverte (celle confirmée, sinon rien)
        std::string err;
        if (!e.close_foreground(body["title_id"].as_string(), err)) { error(fd, 409, err); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/close-app") {  // la même chose, demandée à une PS5 jumelée
        Json out, b = Json::object();
        b.set("title_id", body["title_id"]);
        std::string err;
        if (!e.link() || !e.link()->call(body["instance"].as_string(), "POST", "/api/v2/app/close", b, out, err)) { error(fd, 409, err.empty() ? "liaison indisponible" : err); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/send-current-save") {  // la sauvegarde actuelle d'un jeu → autre PS5, sur un compte
        std::string id, err;
        if (!e.send_item(body["place"].as_string(), body["item"].as_string(), true, "", body["account"].as_string(), body["user_id"].as_string(), id, err)) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("send", id);
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/send-game") {  // envoyer un jeu à une autre PS5 et l'y installer
        std::string id, err;
        if (!e.send_game(body["place"].as_string(), body["item"].as_string(), body["format"].as_string(), body["user_id"].as_string(), id, err, body["keep_copy"].as_bool(), body["replace"].as_bool())) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("send", id);
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/inbox/restore") {  // appelé par la console qui envoie
        std::string id, err;
        if (!e.restore_inbox(body["transfer"].as_string(), body["title_id"].as_string(), body["user_id"].as_string(), id, err)) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("task", id);
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/peer") {  // lecture seule chez un pair jumelé : état, endroits, bibliothèque, parties
        static const std::map<std::string, std::string> what = {{"status", "/api/v2/status"}, {"places", "/api/v2/places"}, {"items", "/api/v2/items"}, {"overview", "/api/v2/saves/overview"}, {"accounts", "/api/v2/accounts"}, {"foreground", "/api/v2/app/foreground"}};
        const auto w = what.find(r.param("what"));
        if (!e.link() || w == what.end()) { error(fd, 400, "demande inconnue"); return; }
        Json out;
        std::string err;
        if (!e.link()->call(r.param("instance"), "GET", w->second, Json(), out, err)) { error(fd, 502, err); return; }
        if (w->first == "status") e.link()->note_installed(r.param("instance"), out["update"]["installed"].as_string());
        json(fd, 200, out);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/accounts/registry") { json(fd, 200, accounts::registry_slots()); return; }  // lecture seule
    if (r.path == "/api/v2/players/takeover" && (r.method == "GET" || r.method == "POST")) {  // aperçu (GET) ou reprise (POST)
        std::string err;
        const Json j = r.method == "GET" ? e.takeover(r.param("from"), r.param("user_id"), Json::array(), err)
                                         : e.takeover(body["from"].as_string(), body["user_id"].as_string(), body["title_ids"], err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && (r.path == "/api/v2/players/link" || r.path == "/api/v2/players/name")) {
        std::string err;
        const Json j = e.player_action(r.path.substr(r.path.rfind('/') + 1), body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/peer-games") { json(fd, 200, e.peer_games()); return; }
    if (r.method == "GET" && r.path == "/api/v2/link/peer-library") { json(fd, 200, e.peer_library(r.param("instance"), r.param("fresh") == "1")); return; }
    if (r.method == "POST" && r.path == "/api/v2/link/peer-action") {
        std::string err;
        const Json j = e.peer_action(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 202, j);
        return;
    }
    if (r.method != "GET" && r.path.rfind("/api/v2/sync/", 0) == 0) e.plans_invalidate();  // un choix ou une mise à niveau : plans à refaire
    if (r.method == "GET" && r.path == "/api/v2/sync/copies/plan") { json(fd, 200, e.plan_cached("copies", r.param("peer"), r.param("full") == "1", r.param("fresh") == "1")); return; }
    if (r.method == "GET" && r.path == "/api/v2/sync/copies/status") { json(fd, 200, e.sync_copies_status()); return; }
    if (r.method == "GET" && r.path == "/api/v2/sync/copies/prefs") { json(fd, 200, e.sync_prefs(r.param("peer"))); return; }
    if (r.method == "POST" && r.path == "/api/v2/sync/copies/prefs") {
        std::string err;
        const Json j = e.set_sync_prefs(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/sync/copies/cancel") { e.sync_copies_cancel(); json(fd, 200, Json::object()); return; }
    // Parties actives : plan (lecture), réglages (jeux choisis), départ (toujours demandé par l'utilisateur), état.
    if (r.method == "GET" && r.path == "/api/v2/sync/parties/plan") { json(fd, 200, e.plan_cached("parties", r.param("peer"), false, r.param("fresh") == "1")); return; }
    // Applis : versions installées ici (lues par l'autre PS5), plan, départ ; réglages : /sync/parties/prefs avec section « apps ».
    if (r.method == "GET" && r.path == "/api/v2/apps/versions") { json(fd, 200, e.apps_versions()); return; }
    if (r.method == "GET" && r.path == "/api/v2/dlc/list") { json(fd, 200, e.dlc_list()); return; }
    if (r.method == "GET" && r.path == "/api/v2/dlc/library") { json(fd, 200, e.pkg_library(r.param("fresh") == "1")); return; }
    if (r.method == "POST" && r.path == "/api/v2/diag/tv") {  // l'appli télé signale une réponse inutilisable
        e.note_local_request("ERREUR " + body["what"].as_string() + " : " + body["error"].as_string().substr(0, 160));
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/sync/dlc/plan") { json(fd, 200, e.plan_cached("dlc", r.param("peer"), false, r.param("fresh") == "1")); return; }
    if (r.method == "POST" && r.path == "/api/v2/sync/dlc/start") {
        std::string err;
        const Json j = e.dlc_start(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/dlc/read") {  // une PS5 jumelée vient chercher un DLC déclaré
        std::string data, err;
        if (!e.dlc_read(r.param("title_id"), r.param("id"), std::strtoull(r.param("offset").c_str(), nullptr, 10), std::strtoull(r.param("limit").c_str(), nullptr, 10), data, err)) { error(fd, 404, err); return; }
        respond(fd, 200, "application/octet-stream", data);
        return;
    }
    // Même progression des émulateurs : sauvegardes d'ici, plan, mise à niveau ; l'autre PS5 lit les fichiers par clé.
    if (r.method == "GET" && r.path == "/api/v2/support") { json(fd, 200, e.support(r.param("fresh") == "1")); return; }
    if (r.method == "GET" && r.path == "/api/v2/emu/saves") { json(fd, 200, e.emu_saves()); return; }
    if (r.method == "GET" && r.path == "/api/v2/sync/emu/plan") { json(fd, 200, e.plan_cached("emu", r.param("peer"), false, r.param("fresh") == "1")); return; }
    if (r.method == "POST" && r.path == "/api/v2/sync/emu/start") {
        std::string err;
        const Json j = e.emu_start(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/emu/files") {
        std::string err;
        const Json j = e.emu_files(r.param("key"), err);
        if (!err.empty()) { error(fd, 404, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/link/emu/read") {
        std::string data, err;
        if (!e.emu_read(r.param("key"), r.param("path"), std::strtoull(r.param("offset").c_str(), nullptr, 10), std::strtoull(r.param("limit").c_str(), nullptr, 10), data, err)) { error(fd, 404, err); return; }
        respond(fd, 200, "application/octet-stream", data);
        return;
    }
    // Réglages d'une appli : lus (avec leur contenu, pour l'autre PS5 jumelée) ; reçus (profil d'ici, copie de sécurité).
    if (r.method == "GET" && r.path == "/api/v2/apps/config") { json(fd, 200, e.app_config(r.param("title_id"), r.param("title"), r.param("data") != "0")); return; }
    if (r.method == "POST" && r.path == "/api/v2/apps/config/receive") {
        std::string err;
        if (!e.app_config_receive(body, err)) { error(fd, 409, err); return; }
        json(fd, 200, Json::object());
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/sync/apps/plan") { json(fd, 200, e.plan_cached("apps", r.param("peer"), false, r.param("fresh") == "1")); return; }
    if (r.method == "POST" && r.path == "/api/v2/sync/apps/start") {
        std::string err;
        const Json j = e.apps_start(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/sync/parties/status") { json(fd, 200, e.parties_status()); return; }
    if (r.method == "POST" && (r.path == "/api/v2/sync/parties/prefs" || r.path == "/api/v2/sync/parties/start")) {
        std::string err;
        const Json j = r.path.back() == 's' ? e.set_parties_prefs(body, err) : e.parties_start(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/sync/copies/start") {
        std::string err;
        const Json j = e.sync_copies_start(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/pull-game") {  // recevoir ici un jeu de l'autre PS5
        std::string err;
        const Json j = e.pull_peer_game(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/link/pull-save") {  // {peer, place, bundle, title_id, user_id} : récupérer ici une copie de l'autre PS5
        std::string err;
        const Json j = e.pull_peer_save(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/saves/delete") {  // {title_id, user_id}
        std::string err;
        const Json j = e.delete_console_save(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/ignore") {  // {title_id, on, trash}
        std::string err;
        const Json j = e.ignore_title(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/backups/compare-all") {
        std::string err;
        const Json j = e.compare_all(body["account"].as_string(), err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/backups/summary") { json(fd, 200, e.backups_summary(r.param("account"), r.param("local") != "1")); return; }
    if (r.method == "GET" && r.path == "/api/v2/trash") {
        std::string err;
        const Json j = e.trash_list(r.param("place"), err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "POST" && r.path == "/api/v2/trash/restore") {
        std::string err;
        const Json j = e.trash_restore(body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    // POST /api/v2/backups/protect|measure|delete {place, bundle, on?, confirm?} : gestion d'une version
    if (r.method == "POST" && r.path.rfind("/api/v2/backups/", 0) == 0) {
        std::string err;
        const Json j = e.version_action(r.path.substr(16), body, err);
        if (!err.empty()) { error(fd, 409, err); return; }
        json(fd, 200, j);
        return;
    }
    if (r.method == "GET" && r.path == "/api/v2/fullbackup") { json(fd, 200, e.full_plan()); return; }
    if (r.method == "POST" && r.path == "/api/v2/tasks") {
        std::string id, err;
        bool ok = false;
        const std::string type = body["type"].as_string();
        if (type == "backup") ok = e.backup(body["item"].as_string(), parts_of(body["parts"]), body["place"].as_string(), id, err, body["account"].as_string());
        else if (type == "import_switch") ok = e.import_switch(body, id, err);
        else if (type == "export_switch") ok = e.export_switch(body, id, err);
        else if (type == "inspect_save") ok = e.inspect_save(body, id, err);
        else if (type == "restore_save") ok = e.restore_save(body["place"].as_string(), body["bundle"].as_string(), body["user_id"].as_string(), body["title_id"].as_string(), body["allow_resign"].as_bool(), id, err, body["check_only"].as_bool());
        else if (type == "full_backup") {
            std::vector<std::string> cats;
            for (const auto& c : body["categories"].items()) cats.push_back(c.as_string());
            ok = e.full_backup(cats, body["place"].as_string(), id, err);
        }
        else if (type == "restore") ok = e.restore(body["place"].as_string(), body["bundle"].as_string(), parts_of(body["parts"]), body["target"].as_string(), id, err, body["overwrite"].as_bool());
        else if (images::Op op; images::parse_task_type(type, op)) ok = e.image_task(body, id, err);
        else err = "type inconnu";
        if (!ok) { error(fd, 409, err); return; }
        Json j = Json::object();
        j.set("id", id);
        json(fd, 202, j);
        return;
    }
    // /api/v2/tasks/<id>/<action>
    if (r.method == "POST" && r.path == "/api/v2/tasks/clear") {  // « Vider la liste » (terminées, échecs ou annulées)
        int removed = 0, kept = 0;
        if (!e.queue().clear(body["state"].as_string(), removed, kept)) { error(fd, 400, "état inconnu"); return; }
        Json out = Json::object();
        out.set("removed", removed);
        out.set("kept", kept);
        json(fd, 200, out);
        return;
    }
    if (r.method == "POST" && r.path.rfind("/api/v2/tasks/", 0) == 0) {
        std::string rest = r.path.substr(14);
        auto slash = rest.find('/');
        if (slash == std::string::npos) { error(fd, 404, "action manquante"); return; }
        std::string id = rest.substr(0, slash), action = rest.substr(slash + 1);
        if (action == "cleanup") {  // « Fichiers » : retirer ce que la copie a créé
            std::string err;
            if (!e.queue().cleanup_files(id, err)) { error(fd, 409, err); return; }
            json(fd, 202, Json::object());
            return;
        }
        bool ok = action == "pause" ? e.queue().pause(id) : action == "resume" ? e.queue().resume(id)
                : action == "cancel" ? e.queue().cancel(id) : action == "remove" ? e.queue().remove(id) : false;
        if (!ok) { error(fd, 409, "action impossible dans l'état actuel"); return; }
        json(fd, 200, Json::object());
        return;
    }
    error(fd, 404, "route inconnue");
}

}  // namespace pbc
