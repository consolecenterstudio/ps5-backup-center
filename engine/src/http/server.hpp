// Serveur HTTP du démon : l'API de l'app télé et du web, et les pages web.
// Un fil d'acceptation, un fil par connexion (courte), réponses JSON.
#pragma once
#include <atomic>
#include <functional>
#include <cstdint>
#include <string>
#include <thread>

namespace pbc {
class Engine;

class HttpServer {
public:
    HttpServer(Engine& e, std::uint16_t port) : engine_(e), port_(port) {}
    ~HttpServer() { stop(); }
    bool start();
    void stop();
    std::uint16_t port() const { return port_; }
    std::function<void()> on_stop;  // POST /api/v2/engine/stop
    void serve(int fd);             // liaison par Internet : une requête arrivée par le canal chiffré (jeton exigé)

private:
    void accept_loop();
    void handle(int fd, bool local);
    Engine& engine_;
    std::uint16_t port_;
    int listen_fd_ = -1;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<int> clients_{0};
};

}  // namespace pbc
