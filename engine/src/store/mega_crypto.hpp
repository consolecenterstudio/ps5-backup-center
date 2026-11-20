// MEGA : tout est chiffré sur la console (la clé ne quitte jamais l'appareil). Briques de chiffrement du protocole
// MEGA, au-dessus de BearSSL : AES (ECB, CBC, CTR 64 bits), CBC-MAC des morceaux, PBKDF2-HMAC-SHA512, base64 « url »
// sans remplissage, et le déchiffrement RSA de l'identifiant de session à la connexion.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pbc::store::mega {

using Bytes = std::string;  // octets bruts

std::string b64url_encode(std::string_view in);
bool b64url_decode(std::string_view in, Bytes& out);  // accepte aussi « + / = »

// AES-128. ECB : longueur multiple de 16 (sinon faux).
bool aes_ecb_encrypt(const Bytes& key, Bytes& data);
bool aes_ecb_decrypt(const Bytes& key, Bytes& data);
// CBC avec un vecteur d'initialisation nul (attributs des fichiers et dossiers).
bool aes_cbc_encrypt0(const Bytes& key, Bytes& data);
bool aes_cbc_decrypt0(const Bytes& key, Bytes& data);
// CTR de MEGA : nonce de 8 octets, compteur de 64 bits = position / 16. Chiffrer = déchiffrer.
void aes_ctr(const Bytes& key, const Bytes& nonce8, std::uint64_t offset, char* data, std::size_t n);

// CBC-MAC d'un morceau (clé du fichier, vecteur = nonce répété deux fois), et MAC final du fichier.
struct ChunkMac {
    Bytes key, iv;    // 16 octets chacun
    Bytes mac;        // état courant (16 octets)
    Bytes pending;    // reste de moins de 16 octets
    void start(const Bytes& file_key, const Bytes& nonce8);
    void update(const char* p, std::size_t n);
    Bytes finish();   // complète de zéros le dernier bloc
};
// Bornes des morceaux de MEGA : 128 Kio, 256 Kio, … jusqu'à 1 Mio, puis 1 Mio.
std::vector<std::uint64_t> chunk_bounds(std::uint64_t size);
// MAC du fichier à partir des MAC des morceaux, puis « meta-MAC » de 8 octets.
Bytes file_meta_mac(const Bytes& file_key, const std::vector<Bytes>& chunk_macs);

Bytes pbkdf2_sha512(std::string_view password, const Bytes& salt, unsigned iterations, std::size_t len);

// Clé privée RSA de MEGA (p, q, d, u en MPI), déchiffrée avec la clé maître ; renvoie m = c^d mod pq.
bool rsa_decrypt_mpi(const Bytes& privk, const Bytes& c_mpi, Bytes& out);

// Lecture d'un MPI de MEGA : 2 octets (longueur en bits, gros-boutiste) puis la valeur.
bool read_mpi(const Bytes& in, std::size_t& at, Bytes& out);

}  // namespace pbc::store::mega
