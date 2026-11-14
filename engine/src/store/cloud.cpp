#include "store/cloud.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>

#include "core/http_client.hpp"
#include "core/util.hpp"
#include "store/cloud_apps.hpp"

namespace pbc::store::cloud {

namespace {

std::mutex g_mu;
std::map<std::string, std::pair<std::string, std::int64_t>> g_access;  // jeton de connexion → (accès, expiration)
std::function<void(const std::string&, const std::string&)> g_rotate;

Provider& dropbox() {
    static Provider p{"dropbox", "", "https://api.dropboxapi.com/oauth2/token", "https://api.dropboxapi.com/2", "https://content.dropboxapi.com/2",
                      kDropboxAppKey, "", "", "https://www.dropbox.com/oauth2/authorize"};
    return p;
}

std::string base64url(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) { const unsigned v = static_cast<unsigned char>(in[i]) << 16; out += t[v >> 18]; out += t[(v >> 12) & 63]; }
    else if (i + 2 == in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63];
    }
    return out;
}

std::string sha256_raw(const std::string& s) {
    self_update_sha256 h{};
    self_update_sha256_start(&h);
    self_update_sha256_add(&h, s.data(), s.size());
    unsigned char d[32];
    self_update_sha256_finish(&h, d);
    return std::string(reinterpret_cast<const char*>(d), 32);
}

Provider& google() {
    static Provider p{"gdrive", "https://oauth2.googleapis.com/device/code", "https://oauth2.googleapis.com/token",
                      "https://www.googleapis.com/drive/v3", "https://www.googleapis.com/upload/drive/v3",
                      kGoogleClientId, kGoogleClientSecret, "https://www.googleapis.com/auth/drive.file", ""};
    return p;
}
Provider& microsoft() {
    static Provider p{"onedrive", "https://login.microsoftonline.com/common/oauth2/v2.0/devicecode",
                      "https://login.microsoftonline.com/common/oauth2/v2.0/token", "https://graph.microsoft.com/v1.0", "",
                      kMicrosoftClientId, "", "Files.ReadWrite.AppFolder User.Read offline_access", ""};
    return p;
}

http::Reply post_form(const std::string& url, const std::string& form) {
    http::Call c;
    c.method = "POST";
    c.url = url;
    c.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
    c.timeout_s = 30;
    return http::call(c, form);
}

std::string why(const http::Reply& r, const Json& j) {
    if (!r.error.empty()) return r.error;
    const std::string e = j["error"].as_string();
    if (e == "invalid_client" || e == "unauthorized_client") return "l'appli n'est pas reconnue par le service (identifiant client)";
    if (e == "invalid_grant") return "accès retiré ou expiré : reconnectez le compte";
    const std::string d = j["error_description"].as_string();
    return e.empty() ? "réponse " + std::to_string(r.status) + " du service" : e + (d.empty() ? std::string() : " : " + d.substr(0, 160));
}

}  // namespace

std::string url_encode(const std::string& s, bool keep_slash) {
    static const char* x = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || (keep_slash && c == '/')) out += static_cast<char>(c);
        else { out += '%'; out += x[c >> 4]; out += x[c & 15]; }
    }
    return out;
}

std::int64_t parse_iso_time(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    const int yy = y - (mo <= 2);
    const int era = (yy >= 0 ? yy : yy - 399) / 400;
    const int yoe = yy - era * 400;
    const int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + (yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468;
    return days * 86400 + h * 3600 + mi * 60 + se;
}

Provider* provider(const std::string& name) {
    if (name == "gdrive") return &google();
    if (name == "onedrive") return &microsoft();
    if (name == "dropbox") return &dropbox();
    return nullptr;
}

bool configured(const std::string& name) {
    const Provider* p = provider(name);
    return p && !p->client_id.empty();
}

bool pkce_start(const std::string& name, std::string& authorize, std::string& verifier, std::string& err) {
    const Provider* p = provider(name);
    if (!p || p->authorize_url.empty()) { err = "service inconnu"; return false; }
    if (p->client_id.empty()) { err = "pas encore disponible dans cette version (appli non déclarée chez le service)"; return false; }
    verifier = random_id(24) + random_id(24);  // 96 caractères hexadécimaux (43 à 128 permis)
    authorize = p->authorize_url + "?client_id=" + url_encode(p->client_id) + "&response_type=code&token_access_type=offline" +
                "&code_challenge_method=S256&code_challenge=" + base64url(sha256_raw(verifier));
    return true;
}

bool exchange_code(const std::string& name, const std::string& code, const std::string& verifier, std::string& refresh, std::string& err) {
    const Provider* p = provider(name);
    if (!p) { err = "service inconnu"; return false; }
    std::string c;
    for (char ch : code) if (!std::isspace(static_cast<unsigned char>(ch))) c += ch;  // collé avec des espaces autour
    const http::Reply r = post_form(p->token_url, "grant_type=authorization_code&code=" + url_encode(c) + "&code_verifier=" + url_encode(verifier) +
                                                      "&client_id=" + url_encode(p->client_id));
    Json j;
    Json::parse(r.body, j);
    if (!r.ok() || j["refresh_token"].as_string().empty()) {
        err = j["error"].as_string() == "invalid_grant" ? "code refusé ou déjà utilisé : recommencez" : why(r, j);
        return false;
    }
    refresh = j["refresh_token"].as_string();
    std::lock_guard<std::mutex> l(g_mu);
    g_access[refresh] = {j["access_token"].as_string(), now_unix() + j["expires_in"].as_int(3600) - 120};
    return true;
}

bool start(const std::string& name, DeviceCode& out, std::string& err) {
    const Provider* p = provider(name);
    if (!p) { err = "service inconnu"; return false; }
    if (p->client_id.empty()) { err = "pas encore disponible dans cette version (appli non déclarée chez le service)"; return false; }
    const http::Reply r = post_form(p->device_url, "client_id=" + url_encode(p->client_id) + "&scope=" + url_encode(p->scope));
    Json j;
    Json::parse(r.body, j);
    if (!r.ok() || j["device_code"].as_string().empty()) { err = why(r, j); return false; }
    out.device_code = j["device_code"].as_string();
    out.user_code = j["user_code"].as_string();
    out.verification_url = !j["verification_url"].as_string().empty() ? j["verification_url"].as_string() : j["verification_uri"].as_string();
    out.interval = static_cast<int>(j["interval"].as_int(5));
    out.expires_in = static_cast<int>(j["expires_in"].as_int(900));
    return true;
}

std::string poll(const std::string& name, const std::string& device_code, std::string& refresh) {
    const Provider* p = provider(name);
    if (!p) return "service inconnu";
    std::string form = "grant_type=" + url_encode("urn:ietf:params:oauth:grant-type:device_code") + "&client_id=" + url_encode(p->client_id) +
                       "&device_code=" + url_encode(device_code);
    if (!p->client_secret.empty()) form += "&client_secret=" + url_encode(p->client_secret);
    const http::Reply r = post_form(p->token_url, form);
    Json j;
    Json::parse(r.body, j);
    const std::string e = j["error"].as_string();
    if (r.ok() && !j["refresh_token"].as_string().empty()) {
        refresh = j["refresh_token"].as_string();
        std::lock_guard<std::mutex> l(g_mu);
        g_access[refresh] = {j["access_token"].as_string(), now_unix() + j["expires_in"].as_int(3600) - 120};
        return "ok";
    }
    if (e == "authorization_pending") return "pending";
    if (e == "slow_down") return "slow_down";
    if (e == "access_denied" || e == "authorization_declined") return "denied";
    if (e == "expired_token" || e == "bad_verification_code") return "expired";
    return why(r, j);
}

bool access_token(const std::string& name, const std::string& refresh, std::string& access, std::string& err) {
    const Provider* p = provider(name);
    if (!p) { err = "service inconnu"; return false; }
    if (refresh.empty()) { err = "compte non connecté"; return false; }
    {
        std::lock_guard<std::mutex> l(g_mu);
        const auto it = g_access.find(refresh);
        if (it != g_access.end() && it->second.second > now_unix()) { access = it->second.first; return true; }
    }
    std::string form = "grant_type=refresh_token&client_id=" + url_encode(p->client_id) + "&refresh_token=" + url_encode(refresh);
    if (!p->client_secret.empty()) form += "&client_secret=" + url_encode(p->client_secret);
    if (p->name == "onedrive") form += "&scope=" + url_encode(p->scope);
    const http::Reply r = post_form(p->token_url, form);
    Json j;
    Json::parse(r.body, j);
    if (!r.ok() || j["access_token"].as_string().empty()) { err = why(r, j); return false; }
    access = j["access_token"].as_string();
    const std::string fresh = j["refresh_token"].as_string();
    std::function<void(const std::string&, const std::string&)> hook;
    {
        std::lock_guard<std::mutex> l(g_mu);
        const std::int64_t until = now_unix() + j["expires_in"].as_int(3600) - 120;
        g_access[refresh] = {access, until};
        if (!fresh.empty() && fresh != refresh) { g_access[fresh] = {access, until}; hook = g_rotate; }
    }
    if (hook) hook(refresh, fresh);  // Microsoft : nouveau jeton de connexion, à enregistrer
    return true;
}

void set_rotation_hook(std::function<void(const std::string&, const std::string&)> hook) {
    std::lock_guard<std::mutex> l(g_mu);
    g_rotate = std::move(hook);
}

void forget(const std::string& refresh) {
    std::lock_guard<std::mutex> l(g_mu);
    g_access.erase(refresh);
}

struct Api::Shared {
    std::mutex mu;
    http::Conn conn;
};

Api::Api(std::string provider, std::string refresh) : provider_(std::move(provider)), refresh_(std::move(refresh)), shared_(std::make_shared<Shared>()) {}

http::Call Api::make(const std::string& method, const std::string& url, http::Headers extra, std::string& err) const {
    http::Call c;
    c.method = method;
    c.url = url;
    std::string access;
    if (!access_token(provider_, refresh_, access, err)) return c;
    c.headers = std::move(extra);
    c.headers.push_back({"Authorization", "Bearer " + access});
    c.timeout_s = 120;
    return c;
}

http::Reply Api::send(const std::string& method, const std::string& url, const std::string& body, http::Headers extra, std::size_t limit) const {
    for (int attempt = 0;; ++attempt) {
        std::string err;
        const http::Call c = make(method, url, extra, err);
        if (!err.empty()) { http::Reply r; r.error = err; return r; }
        http::Reply r;
        {
            std::lock_guard<std::mutex> l(shared_->mu);
            r = http::call(c, shared_->conn, body, limit);
        }
        if (r.status == 401 && attempt == 0) { forget(refresh_); continue; }  // jeton expiré plus tôt que prévu
        return r;
    }
}

bool Api::download(const std::string& url, http::Headers extra, http::Download& dl, http::Reply& head) const {
    for (int attempt = 0;; ++attempt) {
        std::string err;
        const http::Call c = make("GET", url, extra, err);
        if (!err.empty()) { head.error = err; return false; }
        if (dl.begin(c, head)) return true;
        if (head.status == 401 && attempt == 0) { forget(refresh_); continue; }
        return false;
    }
}

std::string api_error(const http::Reply& r) {
    if (!r.error.empty()) return r.error;
    Json j;
    Json::parse(r.body, j);
    const Json& e = j["error"];
    const std::string code = e.is_object() ? e["code"].as_string() : e.as_string();
    std::string msg = e.is_object() ? e["message"].as_string() : j["error_description"].as_string();
    if (r.status == 401) return "accès refusé : reconnectez le compte";
    if (r.status == 403 && (code == "quotaExceeded" || code == "storageQuotaExceeded" || msg.find("quota") != std::string::npos)) return "plus de place sur le compte";
    if (r.status == 403) return "accès refusé par le service" + (msg.empty() ? std::string() : " (" + msg.substr(0, 120) + ")");
    if (r.status == 404) return "introuvable sur le compte";
    if (r.status == 507) return "plus de place sur le compte";
    if (r.status == 429) return "le service demande de ralentir : réessayez dans un moment";
    return "réponse " + std::to_string(r.status) + " du service" + (msg.empty() ? std::string() : " (" + msg.substr(0, 120) + ")");
}

}  // namespace pbc::store::cloud
