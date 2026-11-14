// Comptes dans le nuage (Google Drive, OneDrive) : connexion « par code » (OAuth 2.0 pour appareils, RFC 8628).
// La télé montre un code court et une adresse (ou un code QR) ; l'utilisateur se connecte sur son téléphone, chez
// Google ou Microsoft (e-mail, mot de passe, code de validation : tout se passe chez eux). La console ne reçoit qu'un
// jeton limité au dossier de l'appli (Google : drive.file ; Microsoft : Files.ReadWrite.AppFolder), gardé dans
// remotes.json comme un mot de passe.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include <memory>

#include "core/http_client.hpp"
#include "core/json.hpp"

namespace pbc::store::cloud {

struct Provider {
    std::string name;                  // « gdrive » ou « onedrive »
    std::string device_url, token_url;  // points d'accès OAuth
    std::string api_base, upload_base;  // Google : …/drive/v3 et …/upload/drive/v3 ; Microsoft : …/v1.0
    std::string client_id, client_secret, scope;
    std::string authorize_url;          // Dropbox : page d'autorisation (pas de connexion par code court)
};

// Réglages d'un fournisseur (nullptr si inconnu). Modifiables : les tests visent de faux serveurs.
Provider* provider(const std::string& name);
// L'appli est déclarée chez ce fournisseur (identifiant client présent dans cette version).
bool configured(const std::string& name);

struct DeviceCode {
    std::string device_code, user_code, verification_url;
    int interval = 5, expires_in = 900;
};
bool start(const std::string& name, DeviceCode& out, std::string& err);

// Dropbox : pas de code court. Adresse d'autorisation (PKCE, sans redirection : Dropbox affiche un code à copier) et
// secret de vérification à garder jusqu'à l'échange du code.
bool pkce_start(const std::string& name, std::string& authorize, std::string& verifier, std::string& err);
bool exchange_code(const std::string& name, const std::string& code, const std::string& verifier, std::string& refresh, std::string& err);

// Un essai : « pending », « slow_down », « ok » (refresh rempli), « denied », « expired », sinon un message d'erreur.
std::string poll(const std::string& name, const std::string& device_code, std::string& refresh);

// Jeton d'accès pour un jeton de connexion (gardé en mémoire jusqu'à expiration). Si le fournisseur en donne un
// nouveau (Microsoft les renouvelle), le crochet de rotation est appelé pour l'enregistrer.
bool access_token(const std::string& name, const std::string& refresh, std::string& access, std::string& err);
void set_rotation_hook(std::function<void(const std::string& old_refresh, const std::string& new_refresh)> hook);
void forget(const std::string& refresh);  // jetons en mémoire de ce compte oubliés (déconnexion)

// « 2026-11-13T14:05:00.123Z » → secondes depuis 1970 (0 si illisible).
std::int64_t parse_iso_time(const std::string& s);

// Encodage pour une adresse (« / » gardé si keep_slash) et corps de formulaire.
std::string url_encode(const std::string& s, bool keep_slash = false);

// Appels à l'API d'un compte : jeton « Bearer », connexion gardée ouverte, jeton renouvelé une fois si refusé (401).
class Api {
public:
    Api(std::string provider, std::string refresh);
    http::Reply send(const std::string& method, const std::string& url, const std::string& body = {}, http::Headers extra = {},
                     std::size_t limit = 16u << 20) const;
    bool download(const std::string& url, http::Headers extra, http::Download& dl, http::Reply& head) const;
    const std::string& provider() const { return provider_; }

private:
    http::Call make(const std::string& method, const std::string& url, http::Headers extra, std::string& err) const;
    std::string provider_, refresh_;
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

// Message clair pour une réponse refusée par Google Drive ou OneDrive.
std::string api_error(const http::Reply& r);

}  // namespace pbc::store::cloud
