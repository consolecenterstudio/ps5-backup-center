// Point d'entrée du démon. Sur la PS5 : racine hôte vide, état dans /data/pbc-backup-center,
// port 52370. Sur le Mac : `engine --root <dossier> --port <n>` pour les essais.
#include <csignal>
#include <thread>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <signal.h>
#include <sys/time.h>
#include <sys/file.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>

#include "core/fs.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "update/self_launch.hpp"
#include "http/server.hpp"

namespace {
volatile std::sig_atomic_t g_stop = 0;
// Premier signal : arrêt propre. Second signal : sortie immédiate (on ne reste jamais coincé).
void on_signal(int) { if (g_stop) ::_exit(0); g_stop = 1; }

// Une seule instance : verrou exclusif sur <état>/engine.lock (libéré par le système à la fin du processus,
// même brutale). Le fichier contient le PID, la version et l'instance du moteur qui le tient.
bool try_lock(int fd) { return ::flock(fd, LOCK_EX | LOCK_NB) == 0; }
int lock_holder(int fd) {
    char buf[64] = {0};
    if (::pread(fd, buf, sizeof buf - 1, 0) <= 0) return -1;
    return std::atoi(buf);
}
bool wait_lock(int fd, int ms) {
    for (int waited = 0; waited < ms; waited += 200) { if (try_lock(fd)) return true; ::usleep(200 * 1000); }
    return try_lock(fd);
}
}  // namespace

// Le moteur en place répond-il ? (GET /api/v2/status en local, 3 s au plus) ; `version` : la sienne.
bool engine_alive(std::uint16_t port, std::string& version) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    timeval tv{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        const std::string req = "GET /api/v2/status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        std::string resp;
        if (::send(fd, req.data(), req.size(), 0) == static_cast<ssize_t>(req.size())) {
            char buf[4096];
            for (ssize_t n; resp.size() < 65536 && (n = ::recv(fd, buf, sizeof buf, 0)) > 0;) resp.append(buf, static_cast<std::size_t>(n));
        }
        ok = resp.size() > 12 && resp.compare(9, 3, "200") == 0;
        const auto v = resp.find("\"version\":\"");
        if (v != std::string::npos) { const auto e2 = resp.find('"', v + 11); if (e2 != std::string::npos) version = resp.substr(v + 11, e2 - v - 11); }
    }
    ::close(fd);
    return ok;
}

// Une instance tourne déjà sur ce port : on lui demande de s'arrêter proprement
// (ses tâches sont journalisées et reprendront ici), puis on attend que le port se libère.
bool take_over(std::uint16_t port, const std::string& token) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    // Jamais bloqué ici : 2 s pour se connecter, envoyer et lire la réponse.
    timeval tv{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool sent = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
        std::string req = "POST /api/v2/engine/stop HTTP/1.1\r\nHost: 127.0.0.1\r\nX-PBC-Token: " + token +
                          "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sent = ::send(fd, req.data(), req.size(), 0) == static_cast<ssize_t>(req.size());
        char buf[256];
        (void)::recv(fd, buf, sizeof buf, 0);
    }
    ::close(fd);
    pbc::log("takeover port=%u request_sent=%d", port, sent ? 1 : 0);
    return sent;
}

int main(int argc, char** argv) {
    // Avant tout le reste (et avant tout fil) : nos droits élevés, quel que soit celui qui nous a lancés.
    const std::string privileges = pbc::raise_own_privileges();
    std::string root, state = "/data/pbc-backup-center", app_dir = "/data/homebrew/PPSA99950";
    std::uint16_t port = 52370;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--root")) root = argv[++i];
        else if (!std::strcmp(argv[i], "--state")) state = argv[++i];
        else if (!std::strcmp(argv[i], "--app-dir")) app_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--port")) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    }
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    pbc::fs::set_host_root(root);

    pbc::fs::mkdirs(pbc::fs::host(state));
    const int lock_fd = ::open(pbc::fs::host(pbc::fs::join(state, "engine.lock")).c_str(), O_RDWR | O_CREAT, 0666);
    std::string token;
    pbc::fs::read(pbc::fs::host(pbc::fs::join(state, "token")), token, 256);
    // Remplacement d'une instance en cours (mise à jour, redémarrage) avant de toucher à l'état.
    // On ne démarre que lorsque l'ancien moteur est réellement terminé : jamais deux moteurs actifs.
    if (lock_fd >= 0 && !try_lock(lock_fd)) {
        const int old = lock_holder(lock_fd);
        pbc::log("takeover lock held by pid=%d", old);
        // Remplacer le moteur en place SEULEMENT pour une mise à jour (marqueur posé par l'updater juste avant), ou s'il
        // ne répond plus. Sinon (l'appli télé l'a relancé parce qu'il tardait à répondre…) : on s'en va tout de suite —
        // des moteurs en double restaient en mémoire et empêchaient le lancement des jeux (CE-15773-3).
        const std::string marker = pbc::fs::host(pbc::fs::join(state, "takeover-request"));
        std::string mk;
        const bool asked = pbc::fs::read(marker, mk, 64) && std::time(nullptr) - std::atoll(mk.c_str()) < 120;
        if (asked) ::unlink(marker.c_str());
        std::string their;
        const bool alive = !asked && engine_alive(port, their);
        // Une version différente de la nôtre en place = une mise à jour (même lancée par un moteur d'avant le marqueur).
        if (alive && their == pbc::engine_version) {
            pbc::log("engine already running (pid=%d, same version) and answering: this copy exits", old);
            ::_exit(0);
        }
        take_over(port, token.substr(0, 32));
        if (!wait_lock(lock_fd, 15000) && old > 1 && old != ::getpid()) {
            pbc::log("takeover pid=%d still alive after stop request: SIGTERM", old);
            ::kill(old, SIGTERM);
            if (!wait_lock(lock_fd, 5000)) { pbc::log("takeover pid=%d: SIGKILL", old); ::kill(old, SIGKILL); wait_lock(lock_fd, 3000); }
        }
        if (!try_lock(lock_fd)) {
            pbc::log("takeover failed: pid=%d still holds the lock (blocked in the kernel?) — not starting", old);
            ::_exit(3);
        }
    } else if (!token.empty()) {
        take_over(port, token.substr(0, 32));  // ancien moteur d'avant le verrou (2.28 et avant)
    }
    if (lock_fd >= 0) {
        char info[160];
        const int n = std::snprintf(info, sizeof info, "%d\n%s\n", static_cast<int>(::getpid()), pbc::engine_version);
        ::ftruncate(lock_fd, 0);
        if (n > 0) (void)::pwrite(lock_fd, info, static_cast<std::size_t>(n), 0);
    }
    pbc::Engine engine(state, app_dir);
    engine.set_http_port(port);  // annoncé aux autres consoles (liaison)
    pbc::HttpServer http(engine, port);
    // État lisible en FTP (<état>/engine-status.json) : on sait toujours d'où vient un moteur muet.
    auto status = [&](const char* st, const std::string& detail) {
        char b[512];
        std::snprintf(b, sizeof b, "{\"state\":\"%s\",\"process\":\"pbc-engine2.elf\",\"pid\":%d,\"version\":\"%s\",\"port\":%u,\"time\":%lld,\"detail\":\"%s\"}\n", st,
                      static_cast<int>(::getpid()), pbc::engine_version, port, static_cast<long long>(::time(nullptr)), detail.c_str());
        pbc::fs::write_atomic(pbc::fs::host(pbc::fs::join(state, "engine-status.json")), b);
    };
    // Le port peut rester pris un moment par l'ancien moteur : on réessaie 2 minutes, en le disant, avant d'abandonner.
    bool bound = false;
    for (int i = 0; i < 600 && !bound; ++i) {
        bound = http.start();
        if (!bound) { if (i % 25 == 0) status("waiting_port", "port 52370 encore pris (ancien moteur ?)"); ::usleep(200 * 1000); }
    }
    if (!bound) { pbc::log("port busy, giving up port=%u", port); status("port_busy", "port toujours pris apres 2 min : redemarrer la console"); return 2; }
    status("running", "");
    http.on_stop = [] { g_stop = 1; };
    if (!engine.start()) { pbc::log("engine start failed"); http.stop(); return 1; }
    if (engine.link()) engine.link()->set_serve_http([&http](int fd) { http.serve(fd); });  // liaison par Internet (créée au démarrage)
    {   // Environnement donné par celui qui nous a lancés (droits, pile des fils) : pour diagnostiquer un lancement.
        pthread_attr_t at;
        std::size_t stack = 0;
        if (pthread_attr_init(&at) == 0) { pthread_attr_getstacksize(&at, &stack); pthread_attr_destroy(&at); }
        pbc::log("engine env uid=%d thread_stack=%zu (au lancement : %s)", static_cast<int>(::getuid()), stack, privileges.c_str());
    }
    pbc::log("engine start pid=%d ppid=%d version=%s instance=%s port=%u", static_cast<int>(::getpid()), static_cast<int>(::getppid()),
             pbc::engine_version, engine.instance().c_str(), http.port());
    while (!g_stop) ::usleep(200 * 1000);
    // Arrêt borné : quoi qu'il arrive (connexion, tâche ou montage qui traîne), on sort en 20 s au plus.
    engine.set_stopping();
    std::thread([] { ::sleep(20); pbc::log("engine stop timeout: forced exit"); ::_exit(0); }).detach();
    const auto t0 = std::chrono::steady_clock::now();
    auto ms = [&] { return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()); };
    http.stop();
    pbc::log("engine stop phase=http elapsed_ms=%ld", ms());
    engine.stop();
    pbc::log("engine stop phase=exit elapsed_ms=%ld", ms());
    ::_exit(0);  // sortie immédiate : aucun destructeur ni fil qui traîne ne peut garder le processus (et sa mémoire)
}
