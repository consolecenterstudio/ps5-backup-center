// HTTPS : une session TLS (BearSSL) au-dessus d'une socket déjà connectée. Certificats vérifiés
// contre les autorités racines embarquées (engine/third_party/bearssl_tas.c).
#pragma once
#include <cstddef>
#include <string>
#include <sys/types.h>

namespace pbc::tls {

struct Session;
struct Options {
    // false : appareil du réseau local (NAS au certificat auto-signé ou expiré) — la clé du serveur est
    // acceptée sans autorité, son empreinte est gardée (fingerprint) pour l'épingler plus tard.
    bool verify = true;
    const Session* resume = nullptr;  // reprendre la session TLS d'une autre connexion (canal de données FTPS)
};
Session* open(int fd, const std::string& host, std::string& error, const Options& opt = {});  // nullptr en cas d'échec
std::string fingerprint(const Session* s);  // SHA-256 du certificat du serveur (hex), si verify=false
ssize_t read(Session* s, char* buf, std::size_t n);                    // 0 : fin, < 0 : erreur
bool write_all(Session* s, const char* buf, std::size_t n);
void close(Session* s);                                                // ne ferme pas la socket

}  // namespace pbc::tls
