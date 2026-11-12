#pragma once
// Liaison par Internet (« PS5 à distance ») — module de la liaison, réutilisable par PS5 Center.
// Voir docs/PROTOCOLE-link.md §11.
//
//  - Canal chiffré sur TCP (port 52373 par défaut) : ChaCha20-Poly1305, clés tirées (HKDF-SHA256) d'un secret partagé
//    — le jeton de jumelage, ou le secret d'une invitation — et de deux nombres au hasard. Personne ne lit ni ne modifie
//    ce qui passe ; sans le secret, la poignée de main échoue.
//  - Dans le canal, l'API HTTP habituelle (les mêmes appels qu'entre deux PS5 du salon), servie comme une demande venue
//    du réseau : le jeton est vérifié comme d'habitude, jamais de droits « locaux ».
//  - Une seule des deux consoles doit être joignable (LTE / Starlink : souvent pas d'IPv4 publique) : l'autre l'appelle
//    et garde une connexion « garée » ouverte, que la joignable utilise pour l'appeler en retour.
//  - Jumelage par invitation : un code (adresse, port, secret à usage unique valable 24 h) donné à l'autre personne.
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "core/json.hpp"

namespace pbc::link::wan {

constexpr std::uint16_t kDefaultPort = 52373;

// Canal chiffré au-dessus d'une socket connectée (après la poignée de main).
class Channel {
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel();
    int fd() const { return fd_; }
    bool send(const char* p, std::size_t n);            // découpé en trames de 64 Ko au plus
    bool send_frame(const std::string& s) { return send(s.data(), s.size()) || s.empty(); }
    bool heartbeat();                                    // trame vide (garde la connexion ouverte à travers le NAT)
    // Une trame déchiffrée (vide : battement de cœur). 1 : reçue ; 0 : délai dépassé ; -1 : fermé ou trame falsifiée.
    int recv_frame(std::string& out, int timeout_ms = 30000);
    bool pending() const { return !in_.empty(); }
    void close();
    // Poignées de main (mode : 'T' requête, 'R' connexion garée, 'I' invitation, 'P' test).
    static bool client(int fd, char mode, const std::string& secret, Channel& ch, std::string& error);
    // Côté serveur : `lookup` rend le secret correspondant à l'identifiant de clé (vide : inconnu).
    static bool server(int fd, const std::function<std::string(const std::string& key_id, char mode)>& lookup, Channel& ch, char& mode, std::string& error);
    static std::string key_id(const std::string& secret);  // 32 hex : identifie le secret sans le révéler

private:
    bool setup(const std::string& secret, const unsigned char* nc, const unsigned char* ns, bool is_client);
    int fill(std::size_t need, int timeout_ms);
    int fd_ = -1;
    unsigned char k_send_[32] = {}, k_recv_[32] = {};
    std::uint64_t n_send_ = 0, n_recv_ = 0;
    std::string in_;  // octets reçus pas encore déchiffrés
};

// Relie un canal et une socket locale (paire de sockets) jusqu'à la fin de l'un des deux. Bloquant.
void pump(Channel& ch, int local_fd);

// Code d'invitation : « PBC-XXXXX-XXXXX-… » (adresse, port, secret de 10 octets, contrôle).
std::string encode_invite(const std::string& host, std::uint16_t port, const std::string& secret_hex);
bool decode_invite(const std::string& code, std::string& host, std::uint16_t& port, std::string& secret_hex);

struct Hooks {
    std::function<std::vector<std::string>()> tokens;     // jetons délivrés aux pairs (ils s'en servent comme secret)
    std::function<void(int fd)> serve_http;               // sert l'API sur cette socket (non locale), dans un fil
    std::function<Json()> self_hello;                      // {instance, name, app, version, port, caps}
    std::function<std::string(const std::string& name)> mint_token;  // jeton pour un nouveau pair (invitation)
    // Invitation acceptée chez nous : le pair (sa présentation, son jeton pour nous joindre, son adresse vue d'ici).
    std::function<void(const Json& hello, const std::string& their_token, const std::string& seen_ip)> on_joined;
    // Un pair s'est présenté (connexion entrante, ou réponse à notre appel : seen_ip vide) : sa présentation sert à
    // apprendre son adresse locale et, s'il est joignable, son adresse publique (l'une ou l'autre peut ouvrir la session).
    std::function<void(const std::string& instance, const std::string& seen_ip, const Json& hello)> on_seen;
};

class Service {
public:
    explicit Service(std::string state_dir, Hooks hooks);
    ~Service();
    void configure(bool enabled, std::uint16_t port);  // démarre / arrête l'écoute
    bool enabled() const { return enabled_; }
    std::uint16_t port() const { return port_; }
    bool listening() const { return listen_fd_ >= 0; }
    std::string listen_error() const;

    // Invitations (à usage unique, 24 h).
    Json create_invite(const std::string& host, std::string& error);
    void cancel_invites();
    Json invites() const;

    // Côté qui rejoint : poignée de main avec le secret de l'invitation, échange des jetons. Rend la présentation de
    // l'autre console (instance, name…) et son jeton pour l'appeler.
    bool join(const std::string& code, const std::string& our_token_for_them, Json& their_hello, std::string& their_token, std::string& host, std::uint16_t& port, std::string& seen_ip, std::string& error);

    // Ouvre une connexion vers un pair (directe si on connaît son adresse, sinon par une connexion qu'il a garée chez
    // nous). Rend une socket locale qui parle HTTP en clair, reliée au canal chiffré par un fil. -1 : impossible.
    // dir : 'a' automatique (session déjà ouverte par l'autre, sinon appel direct, sinon attendre son appel),
    // 'o' cette PS5 appelle l'autre (appel direct d'abord), 'i' l'autre appelle cette PS5 (seulement ses sessions).
    int dial(const std::string& instance, const std::string& host, std::uint16_t port, const std::string& token, std::string& error, char dir = 'a');

    // Connexions garées : on garde une connexion ouverte vers chaque pair joignable (pour qu'il puisse nous appeler).
    void set_park_targets(std::vector<std::tuple<std::string, std::string, std::uint16_t, std::string>> targets);  // instance, host, port, jeton
    bool has_parked(const std::string& instance) const;   // il a garé une connexion chez nous
    bool parking(const std::string& instance) const;      // nous avons une connexion garée chez lui

    // Test de connexion vers un pair : étapes lisibles.
    Json test(const std::string& instance, const std::string& name, const std::string& host, std::uint16_t port, const std::string& token);

    std::string observed_ip() const;  // notre adresse publique, vue par un pair à la dernière connexion

private:
    void accept_loop();
    void handle(int fd, std::string ip);
    void park_loop();
    std::string state_dir_;
    Hooks hooks_;
    std::atomic<bool> enabled_{false};
    std::uint16_t port_ = kDefaultPort;
    int listen_fd_ = -1;
    std::thread accept_thread_, park_thread_;
    std::atomic<bool> quit_{false};
    mutable std::mutex mu_;
    std::string listen_error_, observed_ip_;
    struct Invite { std::string secret, code, host; std::int64_t expires = 0; };
    std::vector<Invite> invites_;
    std::map<std::string, std::deque<Channel*>> parked_;  // connexions garées chez nous, par pair
    std::vector<std::tuple<std::string, std::string, std::uint16_t, std::string>> park_targets_;
    std::map<std::string, int> parking_;                   // nos connexions garées chez eux (deux au plus)
    std::map<std::string, std::int64_t> retry_at_;         // prochain essai après un échec
};

}  // namespace pbc::link::wan
