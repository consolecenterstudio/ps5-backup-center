#pragma once
// BitTorrent dans le moteur (docs/ETUDE-bittorrent.md). libtorrent-rasterbar (engine/third_party/libtorrent) est compilé
// avec exceptions et RTTI dans bt.cpp seulement ; cette interface n'expose aucun de ses types et ne lève rien : le reste
// du moteur (sans exceptions) l'appelle comme n'importe quel module. Sans PBC_WITH_BT (banc de test du Mac), tout répond
// « BitTorrent indisponible ».
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::bt {

struct Config {
    std::string state_dir;           // <état>/bt : données de reprise (une par torrent)
    std::string save_dir;            // dossier des téléchargements par défaut
    int port_from = 6891, port_to = 6899;  // le premier port libre de la plage est pris
    bool tcp = true, utp = true;     // protocole (Paramètres › Réseau)
    bool dht = true, upnp = false;   // DHT : liens magnet ; UPnP / NAT-PMP : ouvrir le port sur la box
    bool incoming = true;            // accepter les connexions entrantes (non : sortant seulement, rien n'écoute sur le réseau)
    int down_kbps = 0, up_kbps = 0;  // 0 : illimité
    int active = 3;                  // téléchargements actifs en même temps (les autres attendent leur tour, dans l'ordre de la file)
    int connections = 200;           // connexions au plus, pour tous les torrents
    int seed_ratio = 0;              // arrêter de partager un torrent terminé à ce ratio (×100 ; 0 : jamais)
    int seed_minutes = 0;            // ou après ce temps de partage (0 : jamais) ; jamais pour un lien créé ici
    bool prealloc = false;           // réserver tout l'espace au départ
    bool space_check = true;         // mettre en pause (avec un message) un torrent plus gros que la place libre
    int encrypt = 0;                 // chiffrement du protocole : 0 préféré, 1 obligatoire, 2 désactivé
    std::vector<std::string> blocklist;  // adresses refusées : « 1.2.3.4 », « 1.2.3.0/24 » ou « 1.2.3.4-1.2.3.9 »
    std::string move_dir;            // à la fin d'un téléchargement : déplacer ses fichiers ici (vide : non) ; il reste partagé
    std::string watch_dir;           // dossier surveillé (chemin console) : un .torrent déposé est ajouté seul (puis renommé « .ajouté »)
};

class Session {
public:
    Session();
    ~Session();
    bool available() const;          // compilé avec libtorrent
    bool start(const Config& c, std::string& err);
    void stop();
    void configure(const Config& c);                   // débits, DHT, UPnP (le port ne change qu'au prochain démarrage)
    // Ajoute un lien magnet ou un fichier .torrent (chemin sur la console) ; rend l'identifiant (empreinte hexadécimale).
    std::string add(const std::string& uri_or_path, const std::string& save_dir, std::string& err);
    bool pause(const std::string& id, std::string& err);
    bool resume(const std::string& id, std::string& err);
    bool remove(const std::string& id, bool delete_files, std::string& err);
    // [{id, name, state, total, done, down, up, peers, seeds, paused, finished, save, error, queue, forced, eta, ratio, sequential}]
    // state : checking | metadata | waiting | queued | downloading | seeding | complete (partage arrêté : ratio ou durée
    // atteints) | paused | error
    Json list() const;
    // Fiche d'un torrent : ce que rend list() + magnet, pièces, disponibilité, envoyé / reçu, limites, fichiers (avec
    // priorité), trackers, pairs. Null si le torrent est inconnu.
    Json detail(const std::string& id) const;
    // Actions : force (démarrer sans attendre son tour), reannounce (chercher des pairs), recheck (revérifier le disque),
    // queue_up | queue_down | queue_top | queue_bottom, sequential {on}, limits {down, up} en Ko/s (0 : sans),
    // file {index, priority} (0 : ne pas télécharger, 1 : basse, 4 : normale, 7 : haute), files {priority} pour tous,
    // tracker_add {url}, tracker_remove {url}, tracker_edit {url, to}, add_peer {addr: « ip:port »} (se brancher sur un
    // pair connu : une autre PS5, un PC de la maison, un ami), first_last {on} (premiers et derniers morceaux de chaque
    // fichier d'abord : aperçu d'une vidéo, archive lisible plus tôt), superseed {on} (partage initial économe : chaque
    // pair reçoit des morceaux différents), rename {name} (nouveau nom du torrent : son dossier ou son fichier).
    // Une entrée de liste de blocage valable ? (sinon : le message d'erreur)
    static bool valid_block(const std::string& entry, std::string& err);
    bool act(const std::string& id, const std::string& what, const Json& args, std::string& err);
    int port() const;                // port d'écoute pris (0 : aucun)
    bool incoming() const;           // les connexions entrantes sont acceptées (réglage)
    // À la fin d'un téléchargement (une seule fois par torrent ; jamais pour un lien créé ici ni pour un torrent déjà
    // complet à l'ajout), après l'éventuel déplacement : nom et dossier (chemin de la console) de ses fichiers.
    std::function<void(const std::string& name, const std::string& dir)> on_done;
    // Adresse publique (Internet) de la console telle que la voient les trackers, le DHT ou la box (UPnP) ; vide : inconnue.
    std::string external_ip() const;
    // Ouvrir aussi ce port TCP sur la box (UPnP / NAT-PMP), si l'option UPnP est activée : liens de partage par Internet.
    bool map_port(int port);
    // « Créer un lien » : calcule le .torrent d'un fichier ou d'un dossier de la console (en arrière-plan : quelques
    // minutes pour un gros jeu), puis le partage depuis là où il est (rien n'est copié). Rend l'identifiant du calcul.
    std::string create(const std::string& path, std::string& err);
    bool cancel_create(const std::string& job, std::string& err);
    // Les calculs en cours ou ratés : [{job, name, path, done, total, state (hashing | error | done), error, id}].
    Json creating() const;
    // Le fichier .torrent d'un torrent (pour l'enregistrer ou le donner) ; vide si les métadonnées manquent encore.
    std::string torrent_file(const std::string& id, std::string& name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace pbc::bt
