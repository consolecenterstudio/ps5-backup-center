// SPDX-License-Identifier: GPL-3.0-or-later
// « Soutenir le projet » : les moyens de soutenir le développeur, avec un code QR pour chacun (la télé ne peut pas
// ouvrir de lien : on scanne avec son téléphone).
//
// La liste vient du paquet de l'appli (support.json, toujours là) et, au plus une fois par jour et seulement quand
// l'écran est ouvert, de notre site (…/ps5-backup-center/support.json, à côté des mises à jour) : un moyen peut ainsi être
// ajouté, désactivé ou remplacé sans nouvelle version. Codes QR : qrcodegen de Project Nayuki (MIT), third_party/qrcodegen.
#include <algorithm>
#include <cstring>
#include <string>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "engine.hpp"
#include "link/link.hpp"
#include "store/cloud.hpp"
#include "store/drives.hpp"

extern "C" {
#include "qrcodegen/qrcodegen.h"
}

namespace pbc {

namespace {

#if defined(PBC_UPDATE_URL)
std::string support_url() {
    std::string u = PBC_UPDATE_URL;  // …/updates/latest.json → …/support.json
    const auto p = u.rfind("/updates/");
    return p == std::string::npos ? std::string() : u.substr(0, p) + "/support.json";
}
#else
std::string support_url() { return ""; }
#endif

// Le code QR d'un texte : une ligne par rangée, « 1 » = case noire. Vide si le texte est trop long.
Json qr_rows(const std::string& text) {
    Json rows = Json::array();
    if (text.empty() || text.size() > 400) return rows;
    static thread_local std::uint8_t buf[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text.c_str(), tmp, buf, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return rows;
    const int n = qrcodegen_getSize(buf);
    for (int y = 0; y < n; ++y) {
        std::string r(static_cast<std::size_t>(n), '0');
        for (int x = 0; x < n; ++x) if (qrcodegen_getModule(buf, x, y)) r[static_cast<std::size_t>(x)] = '1';
        rows.push(r);
    }
    return rows;
}

std::string cut(const std::string& s, std::size_t n) {  // coupe sans casser un caractère UTF-8
    if (s.size() <= n) return s;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}

bool valid(const Json& j) { return j.is_object() && j["version"].as_int() >= 1 && (j["links"].is_array() || j["crypto"].is_array()); }

}  // namespace

// Ouvrir la page web au bon endroit depuis un téléphone (ex. « #cloud » : coller les clés d'un stockage S3).
Json Engine::web_link(const std::string& place) {
    Json j = Json::object();
    const std::string ip = link::local_ipv4();
    if (ip.empty()) return j;
    const std::string url = "http://" + ip + ":" + std::to_string(http_port_) + "/" + (place.empty() ? std::string() : "#" + place);
    j.set("url", url);
    j.set("qr", qr_rows(url));
    return j;
}

// ---- Google Drive, OneDrive : connexion par code ----
Json Engine::cloud_services() {
    Json j = Json::object();
    j.set("gdrive", store::cloud::configured("gdrive"));
    j.set("onedrive", store::cloud::configured("onedrive"));
    j.set("dropbox", store::cloud::configured("dropbox"));
    return j;
}

Json Engine::cloud_link_start(const std::string& provider, std::string& error) {
    if (provider == "dropbox") {  // la page de la console, sur le téléphone, fait le reste
        CloudLink l;
        l.provider = provider;
        if (!store::cloud::pkce_start(provider, l.authorize, l.verifier, error)) return Json();
        l.state = "waiting_code";
        l.expires = now_unix() + 1800;
        const std::string id = random_id(12);
        const Json page = web_link("link=" + id);
        if (page["url"].as_string().empty()) { error = "adresse de la console introuvable sur le réseau"; return Json(); }
        l.url = page["url"].as_string();
        {
            std::lock_guard<std::mutex> g(links_mu_);
            links_[id] = l;
        }
        Json j = Json::object();
        j.set("id", id);
        j.set("url", l.url);
        j.set("qr", page["qr"]);
        j.set("expires_in", static_cast<std::int64_t>(1800));
        return j;
    }
    store::cloud::DeviceCode dc;
    if (!store::cloud::start(provider, dc, error)) return Json();
    CloudLink l;
    l.provider = provider;
    l.device_code = dc.device_code;
    l.user_code = dc.user_code;
    l.url = dc.verification_url;
    l.interval = std::max(2, dc.interval);
    l.expires = now_unix() + dc.expires_in;
    l.next = now_unix() + l.interval;
    const std::string id = random_id(6);
    {
        std::lock_guard<std::mutex> g(links_mu_);
        for (auto it = links_.begin(); it != links_.end();) it = it->second.expires < now_unix() - 3600 ? links_.erase(it) : std::next(it);
        links_[id] = l;
    }
    Json j = Json::object();
    j.set("id", id);
    j.set("user_code", l.user_code);
    j.set("url", l.url);
    j.set("qr", qr_rows(l.url));
    j.set("expires_in", static_cast<std::int64_t>(dc.expires_in));
    return j;
}

Json Engine::cloud_link_status(const std::string& id) {
    CloudLink l;
    {
        std::lock_guard<std::mutex> g(links_mu_);
        const auto it = links_.find(id);
        if (it == links_.end()) { Json j = Json::object(); j.set("state", "expired"); return j; }
        l = it->second;
    }
    // Un essai auprès du service quand c'est l'heure (la télé et la page demandent toutes les deux ou trois secondes).
    if (l.state == "pending" && now_unix() >= l.next) {
        if (now_unix() > l.expires) l.state = "expired";
        else {
            std::string refresh;
            const std::string s = store::cloud::poll(l.provider, l.device_code, refresh);
            if (s == "ok") {
                Json about;
                std::string err;
                l.refresh = refresh;
                if (store::drive_about(l.provider, refresh, about, err)) {
                    l.state = "ok";
                    l.account = about["account"].as_string();
                    l.free = static_cast<std::uint64_t>(about["free"].as_int());
                    l.total = static_cast<std::uint64_t>(about["total"].as_int());
                } else { l.state = "error"; l.error = err; }
            } else if (s == "slow_down") l.interval += 5;
            else if (s == "denied" || s == "expired") l.state = s;
            else if (s != "pending") { l.state = "error"; l.error = s; }
            l.next = now_unix() + l.interval;
        }
        std::lock_guard<std::mutex> g(links_mu_);
        if (links_.count(id)) links_[id] = l;
    }
    Json j = Json::object();
    j.set("state", l.state);
    if (!l.account.empty()) j.set("account", l.account);
    j.set("free", l.free);
    j.set("total", l.total);
    if (!l.error.empty()) j.set("error", l.error);
    return j;
}

Json Engine::cloud_link_info(const std::string& id) {
    Json j = Json::object();
    std::lock_guard<std::mutex> g(links_mu_);
    const auto it = links_.find(id);
    if (it == links_.end() || it->second.expires < now_unix() || it->second.authorize.empty()) { j.set("state", "expired"); return j; }
    j.set("provider", it->second.provider);
    j.set("state", it->second.state);
    if (it->second.state == "waiting_code") j.set("authorize", it->second.authorize);
    return j;
}

bool Engine::cloud_link_code(const std::string& id, const std::string& code, std::string& error) {
    CloudLink l;
    {
        std::lock_guard<std::mutex> g(links_mu_);
        const auto it = links_.find(id);
        if (it == links_.end() || it->second.expires < now_unix() || it->second.state != "waiting_code") { error = "lien expiré : recommencez depuis la télé"; return false; }
        l = it->second;
        it->second.state = "checking";  // un seul essai à la fois
    }
    std::string refresh;
    Json about;
    const bool ok = store::cloud::exchange_code(l.provider, code, l.verifier, refresh, error) && store::drive_about(l.provider, refresh, about, error);
    std::lock_guard<std::mutex> g(links_mu_);
    auto& x = links_[id];
    if (!ok) { x.state = "waiting_code"; return false; }  // mauvais code : on peut recoller
    x.state = "ok";
    x.refresh = refresh;
    x.account = about["account"].as_string();
    x.free = static_cast<std::uint64_t>(about["free"].as_int());
    x.total = static_cast<std::uint64_t>(about["total"].as_int());
    return true;
}

bool Engine::take_link(const Json& body, store::Remote& r, std::string& error) {
    std::lock_guard<std::mutex> g(links_mu_);
    const auto it = links_.find(body["link"].as_string());
    if (it == links_.end() || it->second.state != "ok") { error = "connexion au compte pas terminée : recommencez"; return false; }
    if (it->second.provider != r.type) { error = "connexion d'un autre service"; return false; }
    r.ftp.password = it->second.refresh;
    r.ftp.bucket = it->second.account;
    if (r.name.empty()) r.name = it->second.account;
    return true;
}

Json Engine::support(bool fresh) {
    Json list;
    std::string text, source = "app";
    const std::string bundled = fs::host(fs::join(app_dir_, "support.json")), cache = fs::host(fs::join(dir_, "support-remote.json"));
    if (fs::read(bundled, text, 1u << 16)) Json::parse(text, list);
    // Liste en ligne : gardée un jour ; relue seulement quand quelqu'un ouvre l'écran (aucun appel sinon)
    Json cached;
    // La liste du site remplace celle du paquet, sauf si celle du paquet est plus récente (champ « updated »).
    auto newer = [&](const Json& site) { return site["updated"].as_int() >= list["updated"].as_int(); };
    if (fs::read(cache, text, 1u << 16) && Json::parse(text, cached) && valid(cached["list"]) && newer(cached["list"])) {
        list = cached["list"];
        source = "site";
    }
    const std::int64_t age = now_unix() - cached["fetched_at"].as_int();
    const std::string url = support_url();
    if (!url.empty() && (fresh || age > 86400)) {
        std::string body;
        const auto r = http::get_text(url, body, 1u << 16);
        Json remote;
        if (r.error.empty() && r.status == 200 && Json::parse(body, remote) && valid(remote)) {
            if (newer(remote)) { list = remote; source = "site"; }
            Json c = Json::object();
            c.set("fetched_at", now_unix()); c.set("list", remote);
            fs::write_atomic(cache, c.dump());
        }
    }
    Json out = Json::object();
    out.set("source", source);
    Json links = Json::array(), crypto = Json::array();
    for (const auto& l : list["links"].items()) {
        Json x = l;
        if (l["active"].as_bool() && !l["url"].as_string().empty()) x.set("qr", qr_rows(l["url"].as_string()));
        links.push(x);
    }
    for (const auto& c : list["crypto"].items()) {
        if (c["address"].as_string().empty()) continue;
        Json x = c;
        x.set("qr", qr_rows(c["uri"].as_string().empty() ? c["address"].as_string() : c["uri"].as_string()));
        crypto.push(x);
    }
    out.set("links", links);
    out.set("crypto", crypto);
    // Mur des remerciements : pseudo et petit message choisis par la personne, ajoutés à la main (jamais un vrai nom)
    Json wall = Json::array();
    for (const auto& s : list["supporters"].items()) {
        if (s["name"].as_string().empty()) continue;
        Json x = Json::object();
        x.set("name", cut(s["name"].as_string(), 40)); x.set("message", cut(s["message"].as_string(), 160)); x.set("via", s["via"].as_string());
        x.set("date", cut(s["date"].as_string(), 10));  // « 2026-11 » : quand le message ou le soutien est arrivé
        x.set("backer", s["backer"].as_bool());          // a vraiment soutenu le projet (don) : étiquette
        wall.push(x);
    }
    out.set("supporters", wall);
    // Laisser un message (Discussions GitHub), une étoile, les Discussions : codes QR pour la télé
    const std::string repo = "https://github.com/consolecenterstudio/ps5-backup-center";
    const std::string msg = list["messages_url"].as_string().rfind(repo, 0) == 0 ? list["messages_url"].as_string() : repo + "/discussions";
    Json q = Json::object();
    q.set("messages", msg); q.set("messages_qr", qr_rows(msg));
    q.set("discussions", repo + "/discussions"); q.set("discussions_qr", qr_rows(repo + "/discussions"));
    q.set("star", repo); q.set("star_qr", qr_rows(repo));
    q.set("issues", repo + "/issues/new/choose"); q.set("issues_qr", qr_rows(repo + "/issues/new/choose"));
    out.set("community", q);
    // Suivez-nous : profils publics du studio. La liste en ligne (follow : [{id, url, handle}]) peut changer un lien ou
    // en activer un (Discord) sans nouvelle version ; seuls les sites attendus sont acceptés.
    struct Net { const char* id; const char* name; const char* handle; const char* url; const char* host; };
    static const Net nets[] = {{"x", "X", "@CCenterStudio", "https://x.com/CCenterStudio", "https://x.com/"},
                               {"youtube", "YouTube", "@ConsoleCenterStudio", "https://www.youtube.com/@ConsoleCenterStudio", "https://www.youtube.com/"},
                               {"reddit", "Reddit", "@ConsoleCenterStudio", "https://www.reddit.com/user/ConsoleCenterStudio/", "https://www.reddit.com/"},
                               {"discord", "Discord", "", "", "https://discord.gg/"}};
    Json follow = Json::array();
    for (const auto& n : nets) {
        std::string url = n.url, handle = n.handle;
        for (const auto& f : list["follow"].items())
            if (f["id"].as_string() == n.id && f["url"].as_string().rfind(n.host, 0) == 0) { url = cut(f["url"].as_string(), 200); handle = cut(f["handle"].as_string(), 60); }
        Json x = Json::object();
        x.set("id", n.id); x.set("name", n.name); x.set("handle", handle); x.set("url", url);
        x.set("active", !url.empty());
        if (!url.empty()) x.set("qr", qr_rows(url));
        follow.push(x);
    }
    out.set("follow", follow);
    // Projets à financer (montants en dollars US, mis à jour à la main dans la liste en ligne)
    Json goals = Json::array();
    for (const auto& g : list["goals"].items()) {
        if (g["name"].as_string().empty() || g["target"].as_int() <= 0) continue;
        Json x = Json::object();
        for (const char* k : {"id", "name", "pitch", "currency"}) x.set(k, cut(g[k].as_string(), 200));
        x.set("target", g["target"].as_int()); x.set("raised", std::max<std::int64_t>(0, g["raised"].as_int())); x.set("supporters", g["supporters"].as_int());
        Json f = Json::array();
        for (const auto& s : g["features"].items()) if (f.items().size() < 5) f.push(cut(s.as_string(), 120));
        x.set("features", f);
        goals.push(x);
    }
    out.set("goals", goals);
    return out;
}

}  // namespace pbc
