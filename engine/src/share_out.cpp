// Partager ma copie (3.1.43) : donner une copie (version d'une sauvegarde, où qu'elle soit rangée) par un lien.
//  - « local » : lien temporaire vers la console, http://<adresse>:<port>/s/<jeton>/<nom>.zip ; la console lit la
//    copie là où elle est (disque, USB, NAS, nuage) et l'envoie en .zip au fil de l'eau (rien n'est préparé avant).
//    Le jeton ne donne accès qu'à cette copie, en lecture, jusqu'à l'heure d'expiration ; on peut l'arrêter avant.
//  - « bt » : lien BitTorrent (magnet) calculé sur la copie, si elle est sur un disque de la console.
//  - « mega » : lien MEGA de la copie, si elle est sur MEGA.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <atomic>
#include <map>
#include <mutex>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "core/fs.hpp"
#include "core/http_client.hpp"
#include "core/util.hpp"
#include "core/zip.hpp"
#include "engine.hpp"
#include "link/link.hpp"
#include "store/drives.hpp"
#include "store/remotes.hpp"

namespace pbc {

namespace {
std::mutex g_out_mu;

std::string safe_name(std::string s) {
    for (char& c : s) if (c == '/' || c == '\\' || c == '"' || c == '?' || c == '#' || c == '%' || static_cast<unsigned char>(c) < 32) c = '_';
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 5, 5, ".pbck") == 0) s.resize(s.size() - 5);
    return s.empty() ? std::string("copie") : s;
}

std::string path_encode(std::string_view s) {  // chemin d'adresse : lettres, chiffres et -._~ gardés, le reste en %XX
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

void le16(std::string& o, std::uint32_t v) { o += static_cast<char>(v & 0xff); o += static_cast<char>((v >> 8) & 0xff); }
void le32(std::string& o, std::uint32_t v) { le16(o, v & 0xffff); le16(o, v >> 16); }

std::uint32_t crc_update(std::uint32_t crc, const char* p, std::size_t n) {
    static std::uint32_t table[256];
    static std::once_flag once;
    std::call_once(once, [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    });
    crc = ~crc;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ static_cast<unsigned char>(p[i])) & 0xff] ^ (crc >> 8);
    return ~crc;
}
}  // namespace

std::string Engine::shares_out_path() const { return fs::host(fs::join(dir_, "shares-out.json")); }

Json Engine::shares_out_load() const {
    std::string text;
    Json all;
    if (!fs::read(shares_out_path(), text) || !Json::parse(text, all) || !all.is_array()) all = Json::array();
    Json keep = Json::array();
    const std::int64_t now = now_unix();
    for (const auto& s : all.items()) if (s["expires"].as_int() > now) keep.push(s);  // les liens expirés s'effacent seuls
    return keep;
}

// Lien de la console pour un jeton : adresse locale ; l'adresse publique en plus si la box laisse entrer.
static std::string share_url(const std::string& ip, std::uint16_t port, const std::string& token, const std::string& name) {
    return "http://" + ip + ":" + std::to_string(port) + "/s/" + token + "/" + path_encode(name + ".zip");
}

// Adresse publique pour ceux qui sont loin : celle réglée pour « PS5 à distance » (adresse ou nom), sinon celle que
// BitTorrent a apprise (trackers, DHT, box). Le port 52370 de la console est ouvert sur la box par UPnP si l'option
// UPnP est activée ; sinon il faut le rediriger à la main (Paramètres › Réseau le dit).
#if defined(PBCK_PS5)
// Adresse publique demandée à un service « quelle est mon adresse » (comme le font les passerelles de jeu à distance),
// gardée 10 minutes ; un échec n'est pas redemandé avant une minute.
static std::string ask_public_ip(bool force) {
    static std::mutex mu;
    static std::string ip;
    static std::int64_t at = 0, failed = 0;
    std::lock_guard<std::mutex> l(mu);
    const std::int64_t now = now_unix();
    if (!force && !ip.empty() && now - at < 600) return ip;
    if (!force && now - failed < 60) return ip;
    for (const char* url : {"https://api.ipify.org", "https://ipv4.icanhazip.com"}) {
        http::Call c;
        c.url = url;
        c.timeout_s = 6;
        const http::Reply r = http::call(c, {}, 256);
        if (!r.ok()) continue;
        std::string v = r.body;
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
        in_addr probe{};
        if (::inet_pton(AF_INET, v.c_str(), &probe) == 1) { ip = v; at = now; return ip; }
    }
    failed = now;
    return ip;
}
#endif

std::string Engine::share_public_host(bool refresh) {
    (void)refresh;  // sur le Mac : pas d'appel extérieur
    std::string h = settings().wan_public_host;
    if (h.empty() && torrents()) h = torrents()->external_ip();
#if defined(PBCK_PS5)
    if (h.empty()) h = ask_public_ip(refresh);  // sur le Mac (tests), jamais d'appel extérieur
#endif
    static std::atomic<bool> mapped{false};
    if (!h.empty() && torrents() && !mapped.exchange(true)) torrents()->map_port(http_port_);
    return h == link::local_ipv4() ? std::string() : h;
}

Json Engine::mega_keys(const std::string& remote, std::string& error) {
    store::Remote r;
    Json out = Json::object();
    if (!remotes_->find(remote, r) || r.type != "mega") { error = "compte MEGA inconnu"; return out; }
    store::mega_keys_info(r.ftp, out, error);
    return out;
}

Json Engine::public_address(bool refresh) {
    Json j = Json::object();
    const std::string h = share_public_host(refresh);
    j.set("host", h);
    j.set("set_by_hand", !settings().wan_public_host.empty());
    return j;
}

static void add_public(Json& x, const std::string& host, std::uint16_t port, const std::string& token, const std::string& file) {
    if (host.empty()) return;
    const std::string url = share_url(host, port, token, file);
    x.set("url_public", url);
    x.set("qr_public", qr_code(url));
}

Json Engine::shares_out() {
    std::lock_guard<std::mutex> l(g_out_mu);
    Json all = shares_out_load();
    const std::string ip = link::local_ipv4(), pub = share_public_host();
    Json out = Json::array();
    for (const auto& s : all.items()) {
        Json x = s;
        if (!ip.empty()) {
            const std::string url = share_url(ip, http_port_, s["token"].as_string(), s["file"].as_string());
            x.set("url", url);
            x.set("qr", qr_code(url));
        }
        add_public(x, pub, http_port_, s["token"].as_string(), s["file"].as_string());
        out.push(x);
    }
    Json j = Json::object();
    j.set("shares", out);
    return j;
}

bool Engine::share_stop(const std::string& token) {
    std::lock_guard<std::mutex> l(g_out_mu);
    Json all = shares_out_load(), keep = Json::array();
    bool found = false;
    for (const auto& s : all.items()) { if (s["token"].as_string() == token) found = true; else keep.push(s); }
    fs::write_atomic(shares_out_path(), keep.dump(true));
    return found;
}

static bool is_image_file(const std::string& p) {
    for (const char* ext : {".ffpfsc", ".exfat", ".ffpkg", ".pfs", ".img"})
        if (p.size() > std::strlen(ext) && p.compare(p.size() - std::strlen(ext), std::strlen(ext), ext) == 0) return true;
    return false;
}

std::string Engine::item_game_path(const Item& it) {
    for (const Part want : {Part::app, Part::game}) {
        for (const auto& c : it.components) {
            if (c.part != want || c.sources.empty()) continue;
            for (const auto& s : c.sources) if (!s.dir && is_image_file(s.source)) return s.source;  // jeu en image
            if (c.sources.size() == 1) return c.sources[0].source;                                    // un dossier, une ROM
            for (const auto& s : c.sources) if (s.dir) return s.source;                               // le dossier du jeu
            const std::string parent(fs::dirname(c.sources[0].source));                               // fichiers d'un même dossier
            bool same = true;
            for (const auto& s : c.sources) same = same && std::string(fs::dirname(s.source)) == parent;
            if (same && !parent.empty() && parent != "/") return parent;
        }
    }
    return {};
}

std::string Engine::item_location(const Item& it) {
    for (const auto& c : it.components)
        for (const auto& s : c.sources) {
            if (s.dir) return s.source;
            const std::string d(fs::dirname(s.source));
            if (!d.empty()) return d;
        }
    for (const auto& c : it.components) {
        if (c.meta["image"].is_string()) return std::string(fs::dirname(c.meta["image"].as_string()));
        if (c.meta["registration"].is_string()) return c.meta["registration"].as_string();
    }
    return {};
}

Json Engine::share_create(const Json& b, std::string& error) {
    if (b["kind"].as_string() == "game") {  // le jeu installé lui-même : lien BitTorrent sur son dossier ou son image, sans copie
        Item it;
        if (!find_item(b["item"].as_string(), it)) { error = "élément introuvable"; return Json(); }
        // Le jeu lui-même : son image ou son dossier (partie « app »), sinon le fichier ou dossier de la ROM.
        const std::string path = item_game_path(it);
        if (path.empty()) { error = "fichiers du jeu introuvables sur la console : partagez une copie"; return Json(); }
        bt::Session* t = torrents();
        if (!t || !t->available()) { error = "BitTorrent indisponible"; return Json(); }
        const std::string job = t->create(path, error);
        if (job.empty()) return Json();
        bt_name_note(path, it.title);
        Json j = Json::object();
        j.set("kind", "bt");
        j.set("job", job);
        return j;
    }
    const std::string place = b["place"].as_string(), bundle = b["bundle"].as_string(), kind0 = b["kind"].as_string();
    const std::string kind = kind0.empty() ? std::string("local") : kind0;
    std::string path;
    auto st = version_store(place, bundle, path, error);
    if (!st) return Json();
    fs::Entry e;
    if (!st->stat(path, e)) { error = "copie introuvable"; return Json(); }
    Json j = Json::object();
    j.set("kind", kind);
    if (kind == "bt") {
        if (place.rfind("remote:", 0) == 0) { error = "BitTorrent : seulement pour une copie sur un disque de la console"; return Json(); }
        bt::Session* t = torrents();
        if (!t || !t->available()) { error = "BitTorrent indisponible"; return Json(); }
        const std::string job = t->create(path, error);
        if (job.empty()) return Json();
        if (!b["title"].as_string().empty()) bt_name_note(path, b["title"].as_string());
        j.set("job", job);
        return j;
    }
    if (kind == "mega") {
        store::Remote r;
        if (!remotes_->find(place, r) || r.type != "mega") { error = "cette copie n'est pas sur MEGA"; return Json(); }
        std::string link;
        const bool off = b["off"].as_bool();  // retirer le lien (MEGA le garde ouvert tant qu'on ne le retire pas)
        if (!store::mega_export(r.ftp, path, off, link, error)) return Json();
        j.set("place", place);
        j.set("bundle", bundle);
        j.set("title", b["title"]);
        j.set("url", link);
        links_note(j, off);
        if (off) return Json::object();
        j.set("qr", qr_code(link));
        return j;
    }
    if (kind == "service") {  // Google Drive, OneDrive, Dropbox : le lien du service, « toute personne qui a le lien »
        const bool off = b["off"].as_bool();
        std::string link;
        if (!st->share_link(path, off, link, error)) return Json();
        j.set("place", place);
        j.set("bundle", bundle);
        j.set("title", b["title"]);
        j.set("url", link);
        links_note(j, off);
        if (off) return Json::object();
        j.set("qr", qr_code(link));
        return j;
    }
    if (kind != "local") { error = "sorte de lien inconnue"; return Json(); }
    const int hours = std::clamp(static_cast<int>(b["hours"].as_int(24)), 1, 24 * 7);
    Json s = Json::object();
    s.set("token", random_id(12));
    s.set("place", place);
    s.set("bundle", bundle);
    s.set("title", b["title"].as_string().empty() ? fs::basename(path) : b["title"].as_string());
    s.set("file", safe_name(std::string(fs::basename(path))));
    s.set("created", now_unix());
    s.set("expires", now_unix() + static_cast<std::int64_t>(hours) * 3600);
    s.set("downloads", 0);
    {
        std::lock_guard<std::mutex> l(g_out_mu);
        Json all = shares_out_load();
        all.push(s);
        if (!fs::write_atomic(shares_out_path(), all.dump(true))) { error = "impossible d'enregistrer le lien"; return Json(); }
    }
    const std::string ip = link::local_ipv4();
    if (ip.empty()) { error = "la console n'a pas d'adresse sur le réseau"; share_stop(s["token"].as_string()); return Json(); }
    const std::string url = share_url(ip, http_port_, s["token"].as_string(), s["file"].as_string());
    s.set("url", url);
    s.set("qr", qr_code(url));
    s.set("kind", "local");
    add_public(s, share_public_host(), http_port_, s["token"].as_string(), s["file"].as_string());
    return s;
}

std::string Engine::links_path() const { return fs::host(fs::join(dir_, "links-out.json")); }

void Engine::links_note(const Json& rec, bool off) {
    std::lock_guard<std::mutex> l(g_out_mu);
    std::string text;
    Json all;
    if (!fs::read(links_path(), text) || !Json::parse(text, all) || !all.is_array()) all = Json::array();
    Json keep = Json::array();
    for (const auto& x : all.items())
        if (!(x["kind"].as_string() == rec["kind"].as_string() && x["place"].as_string() == rec["place"].as_string() && x["bundle"].as_string() == rec["bundle"].as_string()))
            keep.push(x);
    if (!off) {
        Json x = Json::object();
        for (const char* k : {"kind", "place", "bundle", "title", "url"}) x.set(k, rec[k]);
        x.set("created", now_unix());
        keep.push(x);
    }
    fs::write_atomic(links_path(), keep.dump(true));
    ::chmod(links_path().c_str(), 0600);  // les liens donnent accès aux copies : lisibles par la console seulement
}

// Le nom à montrer pour un lien BitTorrent : celui du jeu (« 007 First Light »), pas celui du fichier partagé.
void Engine::bt_name_note(const std::string& path, const std::string& title) {
    Json rec = Json::object();
    rec.set("kind", "btname");
    rec.set("place", std::string(fs::basename(path)));
    rec.set("bundle", "");
    rec.set("title", title);
    rec.set("url", "");
    links_note(rec, false);
}

Json Engine::torrent_link(const std::string& id) {
    bt::Session* bt = torrents();
    Json t = bt ? bt->detail(id) : Json();
    if (t.is_null()) return t;
        // Code QR du lien magnet : la version courte (empreinte, nom, quelques trackers) pour rester lisible au téléphone.
        const std::string full = t["magnet"].as_string();
        std::string shortm;
        if (full.rfind("magnet:?", 0) == 0) {
            std::string rest = full.substr(8);
            std::vector<std::string> parts;
            for (std::size_t a = 0; a <= rest.size();) { const std::size_t b = std::min(rest.find('&', a), rest.size()); parts.push_back(rest.substr(a, b - a)); a = b + 1; }
            shortm = "magnet:?";
            bool first = true;
            for (const char* key : {"xt=", "dn=", "tr="})
                for (const auto& p : parts) {
                    if (p.rfind(key, 0) != 0) continue;
                    std::string add = p;
                    if (add.rfind("dn=", 0) == 0 && add.size() > 80) { add = add.substr(0, 80); const auto pc = add.rfind('%'); if (pc != std::string::npos && pc + 3 > add.size()) add.resize(pc); }
                    if (shortm.size() + add.size() + 1 > 380) continue;
                    shortm += (first ? "" : "&") + add;
                    first = false;
                }
        }
        // Notre adresse sur le réseau local dans le lien (x.pe) : une autre PS5 ou un PC de la maison se branche
        // directement sur cette console, sans attendre le DHT.
        // Et l'adresse publique (Internet) : pour ceux qui sont loin, si le port BitTorrent est ouvert sur la box (ou UPnP).
        if (!shortm.empty() && bt->port() > 0 && bt->incoming()) {  // sortant seulement : personne ne peut se brancher ici
            const std::string ip = link::local_ipv4(), port = std::to_string(bt->port());
            std::string pe;
            if (!ip.empty()) pe += "&x.pe=" + ip + ":" + port;
            std::string pub = bt->external_ip();
            if (pub.empty()) {  // sinon la même adresse publique que le lien .zip : celle réglée pour « PS5 à distance »,
                // ou celle demandée à l'extérieur comme le font les passerelles de jeu à distance (gardée 10 minutes)
                const std::string wh = share_public_host();
                in_addr probe{};
                if (::inet_pton(AF_INET, wh.c_str(), &probe) == 1) pub = wh;
            }
#if defined(PBCK_PS5)
            if (pub.empty()) pub = ask_public_ip(false);  // l'adresse réglée est un nom (DDNS) : un lien magnet veut une adresse
#endif
            // Deux liens, comme le .zip : « de la maison » (notre adresse locale seulement) et « Internet » (l'adresse
            // publique seulement : l'adresse locale n'a rien à faire chez quelqu'un de loin — son logiciel l'essaierait
            // sur son propre réseau). Le magnet complet de la fiche du torrent garde les deux adresses.
            const std::string home_short = shortm + pe;
            if (!pub.empty() && pub != ip) {
                const std::string pub_pe = "&x.pe=" + pub + ":" + port;
                t.set("public", pub + ":" + port);
                t.set("magnet_public", full + pub_pe);
                t.set("magnet_public_qr", qr_code(shortm + pub_pe));
                pe += pub_pe;
            }
            if (!ip.empty()) t.set("local", ip + ":" + port);
            t.set("magnet_home", full + (ip.empty() ? std::string() : "&x.pe=" + ip + ":" + port));
            t.set("magnet_home_qr", qr_code(home_short));
            t.set("magnet", full + pe);
            shortm += pe;  // la fiche du torrent garde un seul code QR, avec les deux adresses
        }
        t.set("magnet_short", shortm);
        t.set("magnet_qr", qr_code(shortm));
    return t;
}

Json Engine::links() {
    Json out = Json::array();
    std::string text;
    Json all;
    {
        std::lock_guard<std::mutex> l(g_out_mu);
        if (!fs::read(links_path(), text) || !Json::parse(text, all) || !all.is_array()) all = Json::array();
    }
    std::map<std::string, std::string> names;  // fichier partagé → nom du jeu
    for (const auto& r : all.items()) if (r["kind"].as_string() == "btname") names[r["place"].as_string()] = r["title"].as_string();
    auto named = [&](Json& x) {
        const auto f = names.find(x["title"].as_string());
        if (f != names.end() && !f->second.empty()) { x.set("file", x["title"]); x.set("title", f->second); }
    };
    // 1. Liens BitTorrent en préparation (empreintes en cours), puis ceux créés ici et partagés.
    if (bt::Session* bt = torrents()) {
        const Json creating = bt->creating(), list = bt->list();  // gardées : parcourir un temporaire lirait une liste détruite
        for (const auto& c : creating.items()) {
            Json x = Json::object();
            x.set("kind", "bt");
            x.set("id", c["job"]);
            x.set("title", c["name"]);
            x.set("path", c["path"]);
            x.set("state", c["error"].is_string() ? "error" : "preparing");
            x.set("error", c["error"]);
            x.set("done", c["done"]);
            x.set("total", c["total"]);
            named(x);
            out.push(x);
        }
        for (const auto& t : list.items()) {
            if (!t["created"].as_bool()) continue;
            const Json d = torrent_link(t["id"].as_string());
            if (d.is_null()) continue;
            Json x = Json::object();
            x.set("kind", "bt");
            x.set("id", t["id"]);
            x.set("title", t["name"]);
            // Un lien créé ici partage ce qu'il a déjà : s'il cherche encore à télécharger, ses fichiers ne sont plus là
            // où ils étaient (déplacés ou retirés) et il ne partage rien — on le dit au lieu de l'afficher « partagé ».
            const std::string ts = t["state"].as_string();
            x.set("state", t["error"].is_string() ? "error" : ts == "paused" ? "paused" : ts == "checking" ? "checking"
                          : ts == "seeding" ? "ready" : "missing");
            x.set("error", t["error"]);
            x.set("total", t["total"]);
            x.set("up", t["up"]);
            x.set("peers", t["peers"]);
            x.set("uploaded", d["uploaded"]);
            x.set("url", d["magnet_home"].is_string() ? d["magnet_home"] : d["magnet"]);  // le lien de la maison
            x.set("qr", d["magnet_home_qr"].is_array() ? d["magnet_home_qr"] : d["magnet_qr"]);
            if (d["magnet_public"].is_string()) { x.set("url_public", d["magnet_public"]); x.set("qr_public", d["magnet_public_qr"]); }
            x.set("local", d["local"]);
            x.set("public", d["public"]);
            named(x);
            out.push(x);
        }
    }
    // 2. Liens de la console (.zip), avec leur fin et le nombre de téléchargements.
    const Json zips = shares_out();
    for (const auto& s : zips["shares"].items()) {
        Json x = s;
        x.set("kind", "local");
        x.set("id", s["token"]);
        x.set("state", "ready");
        out.push(x);
    }
    // 3. Liens MEGA et liens du service (OneDrive…) : ils restent ouverts tant qu'on ne les retire pas.
    for (const auto& r : all.items()) {
        if (r["kind"].as_string() == "btname") continue;
        Json x = r;
        x.set("id", r["kind"].as_string() + ":" + r["place"].as_string() + ":" + r["bundle"].as_string());
        x.set("state", "ready");
        x.set("qr", qr_code(r["url"].as_string()));
        store::Remote rm;
        if (remotes_->find(r["place"].as_string(), rm)) x.set("place_label", rm.name);
        out.push(x);
    }
    Json j = Json::object();
    j.set("links", out);
    return j;
}

bool Engine::link_stop(const Json& b, std::string& error) {
    const std::string kind = b["kind"].as_string();
    if (kind == "bt") {
        bt::Session* bt = torrents();
        if (!bt) { error = "BitTorrent indisponible"; return false; }
        const std::string id = b["id"].as_string();
        if (!id.empty() && id[0] == 'c' && id.size() < 12) return bt->cancel_create(id, error);  // encore en préparation
        return bt->remove(id, false, error);  // le partage s'arrête ; le jeu et ses fichiers restent où ils sont
    }
    if (kind == "mega" || kind == "service") {
        Json q = Json::object();
        q.set("kind", kind);
        q.set("place", b["place"]);
        q.set("bundle", b["bundle"]);
        q.set("off", true);
        const Json r = share_create(q, error);
        if (r.is_object()) return true;
        // la copie n'existe plus (ou le service refuse) : on l'oublie quand même de la liste
        links_note(q, true);
        error.clear();
        return true;
    }
    error = "sorte de lien inconnue";
    return false;
}

// La copie d'un jeton en .zip (fichiers stockés tels quels, empreinte CRC après chaque fichier). Taille connue d'avance.
bool Engine::share_zip(const std::string& token, ShareZip& z, std::string& error) {
    Json s;
    {
        std::lock_guard<std::mutex> l(g_out_mu);
        const Json all = shares_out_load();  // gardée dans une variable : parcourir un temporaire lirait une liste détruite
        for (const auto& x : all.items()) if (x["token"].as_string() == token) s = x;
    }
    if (!s.is_object()) { error = "lien expiré ou arrêté"; return false; }
    std::string path;
    std::shared_ptr<store::Store> st(version_store(s["place"].as_string(), s["bundle"].as_string(), path, error).release());
    if (!st) return false;
    const std::string root = s["file"].as_string();
    struct F { std::string src, name; std::uint64_t size; };
    auto files = std::make_shared<std::vector<F>>();
    fs::Entry top;
    if (!st->stat(path, top)) { error = "copie introuvable"; return false; }
    if (!top.dir) files->push_back({path, root + "/" + std::string(fs::basename(path)), top.size});
    else {
        std::vector<std::pair<std::string, std::string>> todo{{path, root}};
        while (!todo.empty()) {
            auto [dir, rel] = todo.back();
            todo.pop_back();
            std::vector<fs::Entry> es;
            if (!st->list(dir, es)) { error = "lecture impossible : " + dir; return false; }
            for (const auto& e : es) {
                if (e.name == "." || e.name == "..") continue;
                if (e.dir) todo.push_back({fs::join(dir, e.name), rel + "/" + e.name});
                else files->push_back({fs::join(dir, e.name), rel + "/" + e.name, e.size});
                if (files->size() > 20000) { error = "trop de fichiers"; return false; }
            }
        }
    }
    std::sort(files->begin(), files->end(), [](const F& a, const F& b) { return a.name < b.name; });
    std::uint64_t total = 22;
    for (const auto& f : *files) total += 30 + f.name.size() + f.size + 16 + 46 + f.name.size();
    if (total >= 0xFFFFFFFFull || files->size() >= 0xFFFF) { error = "copie trop grosse pour un .zip (4 Go au plus) : utilisez BitTorrent"; return false; }
    z.name = root + ".zip";
    z.size = total;
    const std::string tok = token;
    z.write = [this, st, files, tok](const std::function<bool(const char*, std::size_t)>& out) {
        std::string central;
        std::uint32_t off = 0;
        std::vector<char> buf(256 * 1024);
        for (const auto& f : *files) {
            std::string h;
            le32(h, 0x04034b50); le16(h, 20); le16(h, 0x0808); le16(h, 0); le16(h, 0); le16(h, 0x21);
            le32(h, 0); le32(h, 0); le32(h, 0); le16(h, static_cast<std::uint32_t>(f.name.size())); le16(h, 0);
            h += f.name;
            if (!out(h.data(), h.size())) return false;
            std::string err;
            auto rd = st->open(f.src, err);
            if (!rd) return false;
            std::uint32_t crc = 0;
            std::uint64_t got = 0;
            for (;;) {
                const long n = rd->read(buf.data(), buf.size());
                if (n < 0) return false;
                if (n == 0) break;
                crc = crc_update(crc, buf.data(), static_cast<std::size_t>(n));
                got += static_cast<std::uint64_t>(n);
                if (got > f.size || !out(buf.data(), static_cast<std::size_t>(n))) return false;
            }
            if (got != f.size) return false;  // le fichier a changé depuis la liste : la taille annoncée serait fausse
            std::string d;
            le32(d, 0x08074b50); le32(d, crc); le32(d, static_cast<std::uint32_t>(f.size)); le32(d, static_cast<std::uint32_t>(f.size));
            if (!out(d.data(), d.size())) return false;
            le32(central, 0x02014b50); le16(central, 20); le16(central, 20); le16(central, 0x0808); le16(central, 0); le16(central, 0); le16(central, 0x21);
            le32(central, crc); le32(central, static_cast<std::uint32_t>(f.size)); le32(central, static_cast<std::uint32_t>(f.size));
            le16(central, static_cast<std::uint32_t>(f.name.size())); le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0);
            le32(central, 0); le32(central, off);
            central += f.name;
            off += static_cast<std::uint32_t>(30 + f.name.size() + f.size + 16);
        }
        std::string end;
        le32(end, 0x06054b50); le16(end, 0); le16(end, 0);
        le16(end, static_cast<std::uint32_t>(files->size())); le16(end, static_cast<std::uint32_t>(files->size()));
        le32(end, static_cast<std::uint32_t>(central.size())); le32(end, off); le16(end, 0);
        central += end;
        if (!out(central.data(), central.size())) return false;
        std::lock_guard<std::mutex> l(g_out_mu);  // un téléchargement complet de plus
        Json all = Json::array();
        const Json old = shares_out_load();
        for (Json x : old.items()) {
            if (x["token"].as_string() == tok) x.set("downloads", x["downloads"].as_int() + 1);
            all.push(x);
        }
        fs::write_atomic(shares_out_path(), all.dump(true));
        return true;
    };
    return true;
}

}  // namespace pbc
