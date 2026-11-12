// Client HTTP minimal : manifeste de mise à jour, fichiers, envoi au chargeur ; GET aussi en https://
// (TLS BearSSL, certificats vérifiés), réponses « chunked » et redirections suivies.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace pbc::http {

struct Url {
    std::string host, path = "/";
    std::uint16_t port = 80;
    bool tls = false;  // https://
};
bool parse_url(const std::string& url, Url& out);
// Adresses « wan-<console> » : la connexion passe par la liaison Internet (canal chiffré) au lieu de TCP. Le
// « composeur » rend une socket locale qui parle HTTP en clair, ou -1 (avec l'erreur).
void set_dialer(std::function<int(const std::string& console, std::string& error)> dialer);
// Résout une adresse relative (« files/x ») par rapport à l'URL d'un document.
std::string resolve(const std::string& base, const std::string& rel);

struct Response {
    int status = 0;
    std::uint64_t length = 0;  // Content-Length (0 si absent)
    std::string error;
};

// GET : le corps est passé morceau par morceau à `sink` (qui renvoie faux pour abandonner).
Response get(const std::string& url, const std::function<bool(const char*, std::size_t)>& sink, int timeout_s = 20);
// GET en mémoire (limité).
Response get_text(const std::string& url, std::string& body, std::size_t limit = 1u << 20);
// POST du contenu brut d'un fichier hôte (application/octet-stream), réponse en mémoire.
Response post_raw(const std::string& url, const std::string& host_file, std::string& body, int timeout_s = 120);
// Requête JSON (GET / POST / …) avec jeton facultatif (en-tête X-PBC-Token) ; corps de réponse en mémoire (16 Mo max).
Response request(const std::string& method, const std::string& url, const std::string& token, const std::string& json_body, std::string& out, int timeout_s = 20);
// Requête avec un corps binaire en mémoire (application/octet-stream) et jeton ; réponse en mémoire.
Response request_raw(const std::string& method, const std::string& url, const std::string& token, const std::string& data, std::string& out, int timeout_s = 60);
// PUT du contenu d'un fichier hôte en flux (application/octet-stream), avec jeton ; réponse en mémoire.
Response put_file(const std::string& url, const std::string& token, const std::string& host_file, std::string& out, int timeout_s = 60);
// POST multipart d'un fichier (chargeur d'ELF d'Arsenal : POST /elfldr?pipe=0&args=<nom>).
Response post_file(const std::string& url, const std::string& field, const std::string& filename, const std::string& data);

}  // namespace pbc::http
