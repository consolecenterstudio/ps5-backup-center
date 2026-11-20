// Reprise d'un envoi vers le nuage après un arrêt (pause, coupure, redémarrage). Google Drive, OneDrive, Dropbox et
// S3 envoient par morceaux dans une « session d'envoi » qui reste ouverte plusieurs jours chez eux : on la garde sur
// la console (adresse ou identifiant de session, octets acceptés, état de l'empreinte de ce début, empreinte des
// 64 derniers Kio acceptés). À la reprise, l'appelant compare cette fin avec la source avant de continuer : rien
// n'est relu depuis le nuage.
#pragma once
#include <cstdint>
#include <string>

#include "core/json.hpp"
#include "core/util.hpp"

namespace pbc::store {

// Ce qui est gardé d'un envoi arrêté.
struct Kept {
    std::uint64_t have = 0;     // octets acceptés par le service
    std::uint64_t total = 0;    // taille annoncée (OneDrive)
    std::string sha_state;      // état de l'empreinte SHA-256 de ces octets (Sha256::state)
    std::string tail_sha;       // empreinte des 64 derniers Kio acceptés (au plus)
    std::string session;        // adresse ou identifiant de la session chez le service
    Json extra;                 // propre au service (S3 : morceaux déjà envoyés)
};

// Dossier où sont gardées les sessions (dans l'état du moteur ; vide : pas de reprise).
void set_kept_dir(const std::string& host_dir);
std::string kept_dir();  // dossier (hôte) des envois gardés ; vide s'il n'est pas réglé
// Clé d'un envoi : le compte (son secret n'est jamais écrit, seulement mêlé à l'empreinte) et le chemin.
std::string kept_key(const std::string& service, const std::string& account_secret, const std::string& path);
bool kept_save(const std::string& key, const Kept& k);
bool kept_load(const std::string& key, Kept& k);  // faux si absente ou trop vieille (6 jours : avant l'expiration chez eux)
void kept_drop(const std::string& key);

// Suivi de ce que le service a accepté : empreinte du début et fin (64 Kio) pour la comparaison à la reprise.
class Acked {
public:
    void add(const char* p, std::size_t n);
    void fill(Kept& k) const;
    bool restore(const Kept& k);  // reprend le suivi d'une session gardée
    std::uint64_t bytes() const { return n_; }
private:
    Sha256 sha_;
    std::string tail_, tail_sha_;
    std::uint64_t n_ = 0;
};

}  // namespace pbc::store
