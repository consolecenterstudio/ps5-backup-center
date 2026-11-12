#pragma once
// Liaison entre consoles (module réutilisable : PS5 Backup Center, et plus tard PS5 Center).
// Voir docs/PROTOCOLE-link.md. Sans dépendance au reste du moteur : seulement core/json, core/fs, core/http_client.
//
//  - Découverte : chaque application s'annonce en UDP sur le réseau local (port 52371, toutes les 5 s) avec un
//    petit JSON {proto, app, name, version, instance, port, caps} ; les annonces reçues forment la liste des pairs.
//  - Jumelage : l'utilisateur lit le code à 6 chiffres affiché par l'autre console ; on l'envoie à son
//    POST /api/v2/pair, qui rend un jeton. Le jeton est gardé dans <état>/link-peers.json (jamais ailleurs).
//  - Appels : HTTP vers l'autre console avec l'en-tête X-PBC-Token.
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/json.hpp"

namespace pbc::link {
namespace wan { class Service; }

constexpr int kProtocol = 1;
constexpr std::uint16_t kDiscoveryPort = 52371;

struct Self {
    std::string app;        // « PS5 Backup Center », « PS5 Center »…
    std::string version;
    std::string instance;   // identifiant stable de cette installation
    std::uint16_t port = 52370;  // port HTTP de l'API
    Json caps = Json::array();   // ce que l'application sait faire (« backups », « saves », « games », « tasks »…)
    std::function<std::string()> installed;  // version installée (peut différer de « version », en marche, avant relance)
    std::function<std::string()> name;  // nom de la console, lu à chaque annonce (modifiable par l'utilisateur)
    // Crée un jeton d'accès À CETTE console pour un pair (jumelage mutuel : un seul code suffit). Vide : pas mutuel.
    std::function<std::string(const std::string& peer_name)> mint_token;
    std::function<std::vector<std::string>()> issued_tokens;  // jetons délivrés (liaison par Internet : secrets des canaux)
};

struct Peer {
    std::string instance, app, name, version, host;
    std::string installed;  // version installée chez elle (vide : ancien moteur qui ne la donne pas)
    std::uint16_t port = 0;
    Json caps = Json::array();
    std::int64_t last_seen = 0;   // dernière annonce reçue (0 : jamais vue sur le réseau, seulement jumelée)
    std::string token;            // vide : pas jumelée
    // Liaison par Internet (« PS5 à distance ») : jumelée par invitation. wan_host vide : pas joignable directement
    // (elle nous appelle et garde une connexion ouverte chez nous).
    bool wan = false;
    std::string wan_host, seen_ip;
    std::string lan_ip;           // son adresse sur son réseau local (elle la donne en se présentant)
    std::string wan_dir = "auto"; // qui ouvre la session : auto, out (cette PS5 l'appelle), in (elle appelle cette PS5)
    std::uint16_t wan_port = 0;
    std::int64_t lan_seen = 0;    // dernière annonce sur le réseau local (le chemin local est préféré)
    int rtt_ms = -1;              // temps de réponse de son état (GET /api/v2/status), -1 : pas encore mesuré
    std::int64_t rtt_at = 0;
};

class Link {
public:
    // state_dir : dossier d'état (link-peers.json) ; discovery_port : 52371 (autre valeur pour les tests) ;
    // announce_to : adresses où envoyer l'annonce (vide : diffusion 255.255.255.255).
    Link(std::string state_dir, Self self, std::uint16_t discovery_port = kDiscoveryPort, std::vector<std::string> announce_to = {});
    ~Link();
    void start();
    void stop();

    Json peers() const;  // pairs vus et jumelés, sans les jetons
    // Jumelage avec le code affiché par l'autre console (adresse vue dans l'annonce, ou saisie).
    bool pair(const std::string& host, std::uint16_t port, const std::string& code, std::string& instance, std::string& error);
    bool forget(const std::string& instance);
    // Un pair qui vient de se jumeler à nous avec notre code se présente (appel authentifié par le jeton qu'on lui a
    // donné) et nous donne le sien : on le garde, le lien marche alors dans les deux sens.
    bool accept(const std::string& host, const Json& hello, std::string& error);
    // Appel de l'API d'un pair jumelé ; réponse JSON dans out. Faux si injoignable ou refusé.
    bool call(const std::string& instance, const std::string& method, const std::string& path, const Json& body, Json& out, std::string& error, int timeout_s = 0);  // 0 : délai habituel

    // Envoie un fichier hôte dans la boîte de réception d'un pair (PUT /api/v2/link/inbox, empreinte vérifiée là-bas).
    bool upload(const std::string& instance, const std::string& transfer, const std::string& name, const std::string& host_file, std::string& error);

    // Fichiers dans le dossier « PBC Backup » d'un disque du pair (autre PS5 = destination de backup).
    bool put_chunk(const std::string& instance, const std::string& place, const std::string& rel, std::uint64_t offset, const std::string& data, std::string& error);
    bool get_chunk(const std::string& instance, const std::string& place, const std::string& rel, std::uint64_t offset, std::uint64_t limit, std::string& out, std::string& error);
    // Version installée d'un pair, lue dans son état (/status → update.installed) : marche aussi avec un ancien moteur.
    void note_installed(const std::string& instance, const std::string& installed);
    // GET brut (image…) sur l'API d'un pair jumelé, délai court. Vrai seulement sur 200.
    bool get_raw(const std::string& instance, const std::string& path, std::string& out, int timeout_s = 5);

    // Exposé pour les tests : traite une annonce reçue de « host ».
    void on_announce(const std::string& host, const std::string& payload);

    // --- Liaison par Internet (link/wan.hpp) ---
    void set_serve_http(std::function<void(int fd)> fn);       // le serveur HTTP de l'application (requêtes du canal)
    // public_host : notre adresse publique si on est joignable (redirection de port) ; donnée à l'autre en se présentant.
    void wan_configure(bool enabled, std::uint16_t port, const std::string& public_host = std::string());
    Json wan_status() const;                                     // activée, port, écoute, adresse vue, invitations
    Json wan_invite(const std::string& public_host, std::string& error);
    void wan_cancel_invites();
    bool wan_join(const std::string& code, std::string& instance, std::string& name, std::string& error);
    Json wan_test(const std::string& instance);
    bool via_wan(const std::string& instance) const;            // l'appel passerait par Internet
    int wan_dial(const std::string& instance, std::string& error);
    // Choisir qui ouvre la session avec une PS5 à distance (et son adresse, si on la connaît) ; changeable à tout moment.
    // from_peer : demandé par l'autre PS5 (son choix inversé) ; sinon on lui envoie le choix inversé.
    bool wan_set_peer(const std::string& instance, const std::string& dir, const std::string& host, std::uint16_t port, bool from_peer, std::string& error);

private:
    std::string announce_payload() const;
    std::string base_url(const Peer& p) const;  // http://hôte:port (local) ou http://wan-<console> (Internet)
    bool use_wan(const Peer& p) const;
    void update_park_targets();
    Json self_hello() const;
    void loop();
    void load();
    void save() const;

    std::string state_dir_;
    Self self_;
    std::uint16_t port_;
    std::vector<std::string> announce_to_;
    mutable std::mutex mu_;
    mutable std::mutex pub_mu_;
    std::string wan_public_host_;
    std::uint16_t wan_port_ = 0;
    std::map<std::string, Peer> peers_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int sock_ = -1;
    std::function<void(int)> serve_http_;
    std::unique_ptr<wan::Service> wan_;
};

}  // namespace pbc::link
