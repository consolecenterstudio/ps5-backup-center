#include "model.hpp"
#include "i18n.hpp"
#include "ui_util.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC  // privé à ce fichier : pas de conflit avec le décodeur du moteur (aperçu Mac)
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 2048
#define STBI_NO_THREAD_LOCALS
#define STBI_ASSERT(x) do { if (!(x)) __builtin_trap(); } while (0)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#include "stb_image.h"
#pragma clang diagnostic pop

namespace app {

using pbc::Json;

int http(const Endpoint& ep, const std::string& method, const std::string& path, const std::string& body, std::string& out, int timeout_ms) {
    out.clear();
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(ep.port));
    ::inet_pton(AF_INET, ep.host.c_str(), &a.sin_addr);
    auto wait = [&](short ev) { pollfd p{fd, ev, 0}; return ::poll(&p, 1, timeout_ms) > 0; };
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        int e = 0; socklen_t n = sizeof e;
        if (errno != EINPROGRESS || !wait(POLLOUT) || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &n) != 0 || e) { ::close(fd); return 0; }
    }
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + ep.host + "\r\nConnection: close\r\nContent-Length: " +
                      std::to_string(body.size()) + "\r\n";
    if (!ep.token.empty()) req += "X-PBC-Token: " + ep.token + "\r\n";
#if defined(PBC_VERSION)
    req += "X-PBC-Client: tv/" PBC_VERSION "\r\n";  // le moteur sait quelle version de l'appli télé tourne
#endif
    if (!body.empty()) req += "Content-Type: application/json\r\n";
    req += "\r\n" + body;
    for (std::size_t sent = 0; sent < req.size();) {
        if (!wait(POLLOUT)) { ::close(fd); return 0; }
        ssize_t w = ::send(fd, req.data() + sent, req.size() - sent, 0);
        if (w <= 0) { if (errno == EAGAIN || errno == EINTR) continue; ::close(fd); return 0; }
        sent += static_cast<std::size_t>(w);
    }
    std::string resp;
    char buf[16384];
    for (;;) {
        if (!wait(POLLIN)) break;
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (n <= 0) break;
        resp.append(buf, static_cast<std::size_t>(n));
        if (resp.size() > (8u << 20)) break;
    }
    ::close(fd);
    const auto sp = resp.find(' '), end = resp.find("\r\n\r\n");
    if (sp == std::string::npos || end == std::string::npos) return 0;
    out = resp.substr(end + 4);
    return std::atoi(resp.c_str() + sp + 1);
}

// ---- Modèle ----------------------------------------------------------------------------

void Model::start() { quit_ = false; thread_ = std::thread([this] { loop(); }); }
void Model::stop() {
    if (!thread_.joinable()) return;
    quit_ = true;
    cv_.notify_all();
    thread_.join();
}

void Model::command(std::string method, std::string path, std::string body, std::function<void(const std::string&, const std::string&)> done) {
    { std::lock_guard<std::mutex> l(mu_); cmds_.push_back({std::move(method), std::move(path), std::move(body), std::move(done)}); }
    cv_.notify_all();
}

namespace {
std::vector<std::string> strings(const Json& a) { std::vector<std::string> v; for (const auto& x : a.items()) v.push_back(x.as_string()); return v; }
}

bool Model::refresh(bool force) {
    std::string body;
    auto next = std::make_shared<Snapshot>(*snapshot());
    const int st = http(ep_, "GET", "/api/v2/status", "", body, 1500);
    Json s;
    if (st != 200 || !Json::parse(body, s)) {
        // Un raté isolé (moteur occupé un instant) ne compte pas : injoignable après 3 échecs d'affilée.
        if (++misses_ >= 3 && next->connected) { next->connected = false; std::lock_guard<std::mutex> l(mu_); snap_ = next; ++serial_; }
        return false;
    }
    misses_ = 0;
    // Moteur occupé (verrou pris plus d'1 s) : il répond, donc il est là ; on garde ce qu'on a et on réessaie plus tard.
    if (s["busy"].as_bool() && next->connected && s["started_at"].as_int() == next->started_at) return true;
    // Reconnexion : on ne recharge tout que si les données ont changé ou si c'est un autre moteur (relancé). Avant, chaque
    // retour rechargeait le catalogue entier, ce qui chargeait le moteur, retardait sa réponse suivante… et l'appli se
    // déconnectait / reconnectait en boucle (rechargements complets répétés, données inchangées).
    const bool changed = force || s["revision"].as_int() != next->revision || s["scanning"].as_bool() != next->scanning ||
                         s["started_at"].as_int() != next->started_at;
    const bool was_connected = next->connected;
    next->connected = next->ever_connected = true;
    if (!changed) {
        if (!was_connected) { std::lock_guard<std::mutex> l(mu_); snap_ = next; ++serial_; }  // de retour, rien à recharger
        return true;
    }
    next->revision = s["revision"].as_int();
    next->version = s["version"].as_string();
    next->started_at = s["started_at"].as_int();
    {
        const Json& u = s["update"];
        Update x;
        x.state = u["state"].as_string().empty() ? "idle" : u["state"].as_string();
        x.message = u["message"].as_string();
        x.source = u["source"].as_string();
        x.installed = u["installed"].as_string();
        x.available = u["available"].as_string();
        x.build = u["build"].as_string();
        for (const auto& n : u["notes"].items()) x.notes.push_back(n.as_string());
        x.bytes_total = static_cast<std::uint64_t>(u["bytes_total"].as_int());
        x.bytes_done = static_cast<std::uint64_t>(u["bytes_done"].as_int());
        x.bytes_per_second = static_cast<std::uint64_t>(u["bytes_per_second"].as_int());
        x.eta_seconds = static_cast<std::uint64_t>(u["eta_seconds"].as_int());
        x.file_index = static_cast<unsigned>(u["file_index"].as_int());
        x.current_file = u["current_file"].as_string();
        x.previous = u["previous"].as_string();
        x.skipped = u["skipped"].as_string();
        x.files = static_cast<unsigned>(u["files"].as_int());
        x.checked_at = u["checked_at"].as_int();
        x.app_changed = u["app_changed"].as_bool();
        x.engine_restart_needed = u["engine_restart_needed"].as_bool();
        x.received_from = u["received_from"].as_string();
        for (const auto& v : u["changelog"].items()) {
            ChangelogEntry c;
            c.version = v["version"].as_string();
            c.date = v["date"].as_string();
            c.title = v["title"].as_string();
            for (const auto& n : v["new"].items()) c.added.push_back(n.as_string());
            for (const auto& n : v["improved"].items()) c.improved.push_back(n.as_string());
            for (const auto& n : v["fixed"].items()) c.fixed.push_back(n.as_string());
            x.changelog.push_back(std::move(c));
        }
        next->update = std::move(x);
    }
    next->scanning = s["scanning"].as_bool();
    next->scanned_at = s["scanned_at"].as_int();
    Json j;
    if (http(ep_, "GET", "/api/v2/items", "", body, 5000) == 200 && Json::parse(body, j)) {
        next->items.clear();
        for (const auto& x : j.items()) {
            Item it{x["key"].as_string(), x["kind"].as_string(), x["title"].as_string(), x["title_id"].as_string(), x["platform"].as_string(),
                    x["place"].as_string(), x["image_type"].as_string(), strings(x["apps"]), {}, x["has_cover"].as_bool(), x["cover"].as_string(), {}};
            it.ignored = x["ignored"].as_bool();
            for (const auto& c : x["components"].items()) {
                it.components.push_back({c["part"].as_string(), ui_util::engine_label(c["label"].as_string()), static_cast<std::uint64_t>(c["bytes"].as_int())});
                if (it.location.empty() && c["sources"].size()) {
                    const Json& s0 = c["sources"][0];
                    std::string p = s0["source"].as_string();
                    if (!s0["dir"].as_bool()) { const auto sl = p.rfind('/'); if (sl != std::string::npos && sl > 0) p.resize(sl); }
                    it.location = p;
                }
            }
            next->items.push_back(std::move(it));
        }
    }
    if (http(ep_, "GET", "/api/v2/places", "", body) == 200 && Json::parse(body, j)) {
        next->places.clear();
        for (const auto& x : j.items())
            next->places.push_back({x["id"].as_string(), x["type"].as_string(), x["mount"].as_string(), ui_util::engine_label(x["label"].as_string()),
                                    static_cast<std::uint64_t>(x["free"].as_int()), static_cast<std::uint64_t>(x["total"].as_int()), x["marked"].as_bool()});
    }
    if (http(ep_, "GET", "/api/v2/tasks?brief=1", "", body) == 200 && Json::parse(body, j)) {
        next->tasks.clear();
        for (const auto& x : j["tasks"].items()) {
            const Json& p = x["progress"];
            next->tasks.push_back({x["id"].as_string(), x["type"].as_string(), x["state"].as_string(), x["title"].as_string(), x["error"].as_string(),
                                   static_cast<std::uint64_t>(p["bytes_done"].as_int()), static_cast<std::uint64_t>(p["bytes_total"].as_int()),
                                   static_cast<unsigned>(p["files_done"].as_int()), static_cast<unsigned>(p["files_total"].as_int()),
                                   x["bytes_per_second"].as_number(), x["report"], x["item"].as_string().empty() ? x["title_id"].as_string() : x["item"].as_string(), x["step"].as_string(),
                                   p["phase"].as_string(), static_cast<std::uint64_t>(p["phase_done"].as_int()), static_cast<std::uint64_t>(p["phase_total"].as_int()),
                                   x["started"].as_int(), x["finished"].as_int()});
        }
    }
    if (http(ep_, "GET", "/api/v2/emulators", "", body) == 200 && Json::parse(body, j)) {
        next->emulators.clear();
        for (const auto& x : j.items()) {
            Emulator e;
            e.name = x["name"].as_string();
            e.installed = x["installed"].as_bool();
            for (const auto& r : x["roots"].items()) e.roots.push_back({r["path"].as_string(), r["platform"].as_string(), r["source"].as_string()});
            for (const auto& t : x["title_ids"].items()) e.title_ids.push_back(t.as_string());
            e.id = x["id"].as_string();
            e.platform = x["platform"].as_string();
            for (const auto& c : x["config_files"].items()) e.config_files.push_back(c.as_string());
            for (const auto& r : x["manual"]["roms"].items()) e.manual_roms.push_back(r.as_string());
            e.manual_config = x["manual"]["config"].as_string();
            e.manual_saves = x["manual"]["saves"].as_string();
            e.is_manual = x["is_manual"].as_bool();
            next->emulators.push_back(std::move(e));
        }
    }
    if (http(ep_, "GET", "/api/v2/settings", "", body) == 200 && Json::parse(body, j)) { next->default_place = j["default_place"].as_string(); next->update_auto_check = j["update_auto_check"].as_bool(true); next->update_auto_install = j["update_auto_install"].as_bool(false); next->update_channel = j["update_channel"].as_string() == "beta" ? "beta" : "stable"; next->tv_seen_version = j["tv_seen_version"].as_string(); next->theme = j["theme"].as_string();
        next->sound_ui = j["sound_ui"].as_bool(true); next->music = j["music"].as_bool(false); next->volume = static_cast<int>(j["volume"].as_int(70)); if (!j["language"].as_string().empty()) next->language = j["language"].as_string(); next->system_language = j["system_language"].as_string(); next->settings_loaded = true;
        for (const auto& t : j["ignored"].items()) next->ignored.push_back(t.as_string()); }
    if (http(ep_, "GET", "/api/v2/pair-code", "", body) == 200 && Json::parse(body, j)) next->pair_code = j["code"].as_string();
    std::lock_guard<std::mutex> l(mu_);
    snap_ = next;
    ++serial_;
    return true;
}

void Model::loop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now() - std::chrono::seconds(10);
    while (!quit_) {
        Cmd cmd;
        bool have = false;
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait_until(l, last + std::chrono::milliseconds(700), [&] { return quit_.load() || !cmds_.empty(); });
            if (quit_) return;
            if (!cmds_.empty()) { cmd = std::move(cmds_.front()); cmds_.pop_front(); have = true; }
        }
        // Demandes longues : comparaison de synchro, et tout ce qui passe par un appareil réseau (Switch qui
        // sort de veille, NAS…) — à part, avec un délai large, pour ne pas bloquer le reste de l'app.
        const bool slow = have && (cmd.path.rfind("/api/v2/sync/", 0) == 0 || cmd.path.rfind("/api/v2/remotes", 0) == 0 ||
                                    cmd.path.rfind("/api/v2/backups", 0) == 0 ||        // listes, tailles, protéger, supprimer (NAS lent)
                                    cmd.path.rfind("/api/v2/trash", 0) == 0 ||
                                    cmd.path.rfind("/api/v2/ignore", 0) == 0 ||         // ignorer un jeu (ses copies vont à la corbeille, NAS compris)          // corbeille d'un disque ou du NAS
                                    cmd.path.rfind("/api/v2/saves/", 0) == 0 ||         // vue par compte, comparaisons de contenu
                                    cmd.path.rfind("/api/v2/images", 0) == 0);          // images de jeux (mesures)
        if (slow) {
            std::thread([ep = ep_, c = std::move(cmd)] {
                std::string out;
                const int st = http(ep, c.method, c.path, c.body, out, c.path.rfind("/api/v2/sync/", 0) == 0 ? 90000 : 60000);
                std::string err;
                if (st < 200 || st >= 300) {
                    Json e;
                    err = Json::parse(out, e) && !e["error"].as_string().empty() ? e["error"].as_string()
                          : st == 0 ? std::string(tr("l'appareil ne répond pas (délai dépassé)")) : trf("erreur %d", st);
                }
                if (c.done) c.done(err, out);
            }).detach();
            continue;
        }
        if (have) {
            std::string out, err;
            const int st = http(ep_, cmd.method, cmd.path, cmd.body, out, 5000);
            if (st < 200 || st >= 300) {
                Json e;
                err = Json::parse(out, e) && !e["error"].as_string().empty() ? e["error"].as_string()
                      : st == 0 ? std::string(tr("le moteur ne répond pas")) : trf("erreur %d", st);
                auto next = std::make_shared<Snapshot>(*snapshot());
                next->last_error = err;
                ++next->error_serial;
                std::lock_guard<std::mutex> l(mu_);
                snap_ = next;
                ++serial_;
            }
            if (cmd.done) cmd.done(err, out);
            if (cmd.method != "GET") refresh(true);
            last = clock::now();
            continue;
        }
        refresh(false);
        last = clock::now();
    }
}

// ---- Jaquettes -------------------------------------------------------------------------

void Covers::start() { table_.assign(static_cast<std::size_t>(slots_), {}); quit_ = false; thread_ = std::thread([this] { loop(); }); }
void Covers::stop() {
    if (!thread_.joinable()) return;
    quit_ = true;
    cv_.notify_all();
    thread_.join();
}

Image Covers::get(const std::string& key) {
    std::lock_guard<std::mutex> l(mu_);
    ++tick_;
    for (std::size_t i = 0; i < table_.size(); ++i)
        if (table_[i].key == key) {
            table_[i].used = tick_;
            if (table_[i].ready) return {size, size, mem_ + i * size * size};
            // Échec (jaquette pas encore téléchargée par le moteur…) : on redemande un peu plus tard.
            if (!table_[i].failed || tick_ - table_[i].at < 30000) return {};
            table_[i].key.clear();
            break;
        }
    if (std::find(wanted_.begin(), wanted_.end(), key) == wanted_.end()) {
        wanted_.push_front(key);  // le plus récent d'abord : ce qui est à l'écran maintenant
        if (wanted_.size() > 48) wanted_.pop_back();
        cv_.notify_all();
    }
    return {};
}

// Une jaquette dans un carré n×n, toujours pleine (même format partout, comme les tuiles PS5 /
// Switch). Une boîte en hauteur est recadrée depuis le haut : c'est là que sont le titre et le
// numéro d'une suite (« Earthworm Jim 2 »).
static void compose_cover(const std::uint32_t* src, int w, int h, std::uint32_t* dst, int n) {
    const float ratio = static_cast<float>(w) / h;
    scale_rgba(src, w, h, dst, n, n, true, ratio < 0.92f ? 0.08f : 0.5f);
}

Image Covers::get_large(const std::string& key) {
    std::lock_guard<std::mutex> l(mu_);
    ++tick_;
    for (int i = 0; i < 2; ++i)
        if (big_[i].key == key) {
            big_[i].used = tick_;
            return big_[i].ready ? Image{large, large, large_mem_ + static_cast<std::size_t>(i) * large * large} : Image{};
        }
    const std::string want = "L:" + key;
    if (large_mem_ && std::find(wanted_.begin(), wanted_.end(), want) == wanted_.end()) {
        wanted_.push_front(want);
        cv_.notify_all();
    }
    return {};
}

void Covers::loop() {
    while (!quit_) {
        std::string key;
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait(l, [&] { return quit_.load() || !wanted_.empty(); });
            if (quit_) return;
            key = wanted_.front();
            wanted_.pop_front();
        }
        const bool big = key.rfind("L:", 0) == 0;  // grande image (Accueil, bandeau, fiche)
        const std::string real = big ? key.substr(2) : key;
        const std::string request = real.substr(0, real.find('\x1f'));  // « clé␟source » : seule la clé part au moteur
        std::string enc;
        for (char c : request) {
            const auto u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '/') enc += c;
            else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", u); enc += b; }
        }
        std::string body;
        int w = 0, h = 0, n = 0;
        stbi_uc* px = nullptr;
        if (http(ep_, "GET", "/api/v2/cover?key=" + enc + "&size=" + std::to_string(big ? large : size), "", body, 5000) == 200)
            px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(body.data()), static_cast<int>(body.size()), &w, &h, &n, 4);
        std::lock_guard<std::mutex> l(mu_);
        if (big) {
            const int slot = big_[0].used <= big_[1].used ? 0 : 1;
            big_[slot] = {real, px != nullptr, tick_};
            if (px) { compose_cover(reinterpret_cast<const std::uint32_t*>(px), w, h, large_mem_ + static_cast<std::size_t>(slot) * large * large, large); stbi_image_free(px); }
            ++serial_;
            continue;
        }
        // Emplacement : libre, sinon le moins récemment affiché.
        std::size_t slot = 0;
        for (std::size_t i = 0; i < table_.size(); ++i) {
            if (table_[i].key.empty()) { slot = i; break; }
            if (table_[i].used < table_[slot].used) slot = i;
        }
        table_[slot] = {key, px != nullptr, px == nullptr, tick_, tick_};
        if (px) {
            compose_cover(reinterpret_cast<const std::uint32_t*>(px), w, h, mem_ + slot * size * size, size);
            stbi_image_free(px);
        }
        ++serial_;
    }
}

}  // namespace app
