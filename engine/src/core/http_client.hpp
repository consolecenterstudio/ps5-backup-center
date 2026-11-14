// Client HTTP minimal : manifeste de mise à jour, fichiers, envoi au chargeur ; GET aussi en https://
// (TLS BearSSL, certificats vérifiés), réponses « chunked » et redirections suivies.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

// --- Requêtes générales (connecteurs cloud : WebDAV, S3, Google Drive, OneDrive…) --------------------------------
// N'importe quelle méthode, en http:// ou https://, en-têtes libres, en-têtes de la réponse rendus.
struct Header { std::string name, value; };
using Headers = std::vector<Header>;

struct Call {
    std::string method = "GET", url;
    Headers headers;          // Host, Connection et la longueur du corps sont ajoutés ici
    bool verify_tls = true;   // false : appareil du réseau local au certificat auto-signé (NAS)
    std::string pin;          // avec verify_tls=false : empreinte SHA-256 attendue (vide : acceptée, rendue dans Reply)
    int timeout_s = 30;
    int redirects = 3;        // suivies pour GET et HEAD seulement
};

struct Reply {
    int status = 0;           // 0 : pas de réponse (voir error)
    Headers headers;          // noms en minuscules
    std::string body;
    std::string error;        // erreur réseau / TLS ; un statut HTTP ≠ 2xx n'en est pas une
    std::string fingerprint;  // certificat du serveur (verify_tls=false)
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    std::string header(const std::string& lower_name) const;
};

// Corps en mémoire ; réponse gardée en mémoire jusqu'à `limit` octets (au-delà : erreur).
Reply call(const Call& c, const std::string& body = {}, std::size_t limit = 16u << 20);

// Connexion gardée ouverte entre plusieurs requêtes au même serveur (keep-alive) : une seule poignée de main TLS
// pour toute une série (WebDAV : création des dossiers, envoi, renommage de chaque fichier). Une connexion à la fois.
class Conn {
public:
    Conn();
    ~Conn();
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    void close();
    struct State;
    State* s_;
};
Reply call(const Call& c, Conn& conn, const std::string& body = {}, std::size_t limit = 16u << 20);
// Réponse passée morceau par morceau à `sink` (téléchargement d'un gros fichier) ; Reply.body reste vide si le
// statut est 2xx (sinon il contient le message d'erreur du serveur, 64 Ko au plus).
Reply fetch(const Call& c, const std::function<bool(const char*, std::size_t)>& sink);

// Envoi d'un corps en flux (gros fichier, taille connue ou non).
class Upload {
public:
    Upload();
    ~Upload();
    Upload(const Upload&) = delete;
    Upload& operator=(const Upload&) = delete;
    // length < 0 : taille inconnue, envoi « chunked ». Faux : erreur dans error().
    bool begin(const Call& c, std::int64_t length);
    bool write(const char* p, std::size_t n);
    Reply finish(std::size_t limit = 1u << 20);  // termine le corps et lit la réponse
    void abort();
    const std::string& error() const;

private:
    struct State;
    State* s_;
};

// Réception d'un corps en flux, à la demande (lecture d'un gros fichier par morceaux).
class Download {
public:
    Download();
    ~Download();
    Download(const Download&) = delete;
    Download& operator=(const Download&) = delete;
    // Envoie la requête et lit les en-têtes (redirections suivies pour GET). Reply.body n'est rempli que si le
    // statut n'est pas 2xx (message du serveur, 64 Ko au plus) ; alors begin rend faux.
    bool begin(const Call& c, Reply& head);
    long read(char* p, std::size_t n);  // 0 : fin ; < 0 : erreur
    void close();

private:
    struct State;
    State* s_;
};

}  // namespace pbc::http
