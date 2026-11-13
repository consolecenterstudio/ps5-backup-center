// SPDX-License-Identifier: GPL-3.0-or-later
// « Soutenir le projet » : les moyens de soutenir le développeur, avec un code QR pour chacun (la télé ne peut pas
// ouvrir de lien : on scanne avec son téléphone).
//
// La liste vient du paquet de l'appli (support.json, toujours là) et, au plus une fois par jour et seulement quand
// l'écran est ouvert, de notre site (…/ps5-backup-center/support.json, à côté des mises à jour) : un moyen peut ainsi être
// ajouté, désactivé ou remplacé sans nouvelle version. Codes QR : qrcodegen de Project Nayuki (MIT), third_party/qrcodegen.
#include <cstring>
#include <string>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "engine.hpp"

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

Json Engine::support(bool fresh) {
    Json list;
    std::string text, source = "app";
    const std::string bundled = fs::host(fs::join(app_dir_, "support.json")), cache = fs::host(fs::join(dir_, "support-remote.json"));
    if (fs::read(bundled, text, 1u << 16)) Json::parse(text, list);
    // Liste en ligne : gardée un jour ; relue seulement quand quelqu'un ouvre l'écran (aucun appel sinon)
    Json cached;
    if (fs::read(cache, text, 1u << 16) && Json::parse(text, cached) && valid(cached["list"])) {
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
            list = remote;
            source = "site";
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
        wall.push(x);
    }
    out.set("supporters", wall);
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
