// BitTorrent dans le moteur — voir bt.hpp. Compilé avec exceptions et RTTI (build-system/build.py) quand PBC_WITH_BT est
// défini ; toutes les exceptions de libtorrent sont attrapées ici et rendues en texte d'erreur.
#include "bt/bt.hpp"

#if !defined(PBC_WITH_BT)

namespace pbc::bt {
struct Session::Impl {};
Session::Session() = default;
Session::~Session() = default;
bool Session::available() const { return false; }
bool Session::start(const Config&, std::string& err) { err = "BitTorrent indisponible dans cette version"; return false; }
void Session::stop() {}
void Session::configure(const Config&) {}
std::string Session::add(const std::string&, const std::string&, std::string& err) { err = "BitTorrent indisponible dans cette version"; return {}; }
bool Session::pause(const std::string&, std::string& err) { err = "BitTorrent indisponible"; return false; }
bool Session::resume(const std::string&, std::string& err) { err = "BitTorrent indisponible"; return false; }
bool Session::remove(const std::string&, bool, std::string& err) { err = "BitTorrent indisponible"; return false; }
Json Session::list() const { return Json::array(); }
Json Session::detail(const std::string&) const { return Json(); }
bool Session::act(const std::string&, const std::string&, const Json&, std::string& err) { err = "BitTorrent indisponible"; return false; }
int Session::port() const { return 0; }
bool Session::incoming() const { return false; }
std::string Session::create(const std::string&, std::string& err) { err = "BitTorrent indisponible dans cette version"; return {}; }
bool Session::cancel_create(const std::string&, std::string& err) { err = "BitTorrent indisponible"; return false; }
Json Session::creating() const { return Json::array(); }
bool Session::valid_block(const std::string&, std::string& err) { err = "BitTorrent indisponible"; return false; }
std::string Session::torrent_file(const std::string&, std::string&) const { return {}; }
std::string Session::external_ip() const { return {}; }
bool Session::map_port(int) { return false; }
}  // namespace pbc::bt

#else

#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/peer_class.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/storage_defs.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/address.hpp>
#include <libtorrent/socket.hpp>
#include <set>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "core/fs.hpp"
#include "core/util.hpp"

namespace pbc::bt {

namespace {
std::string hex_id(const lt::info_hash_t& ih) {
    const lt::sha1_hash h = ih.get_best();
    return lt::aux::to_hex(h);
}
bool port_free(int port) {  // TCP libre sur toutes les adresses
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool ok = ::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
    ::close(s);
    return ok;
}
lt::settings_pack pack(const Config& c, int port) {
    lt::settings_pack sp;
    // Sortant seulement : on n'écoute que sur la console elle-même (personne ne peut s'y brancher depuis le réseau).
    if (port > 0) sp.set_str(lt::settings_pack::listen_interfaces, (c.incoming ? "0.0.0.0:" : "127.0.0.1:") + std::to_string(port));
    sp.set_bool(lt::settings_pack::enable_dht, c.dht);
    sp.set_bool(lt::settings_pack::enable_lsd, c.dht);
    sp.set_bool(lt::settings_pack::enable_upnp, c.upnp);
    sp.set_bool(lt::settings_pack::enable_natpmp, c.upnp);
    sp.set_bool(lt::settings_pack::enable_outgoing_utp, c.utp);
    sp.set_bool(lt::settings_pack::enable_incoming_utp, c.utp);
    sp.set_bool(lt::settings_pack::enable_outgoing_tcp, c.tcp || !c.utp);
    sp.set_bool(lt::settings_pack::enable_incoming_tcp, c.tcp || !c.utp);
    sp.set_int(lt::settings_pack::download_rate_limit, c.down_kbps > 0 ? c.down_kbps * 1024 : 0);
    sp.set_int(lt::settings_pack::upload_rate_limit, c.up_kbps > 0 ? c.up_kbps * 1024 : 0);
    sp.set_int(lt::settings_pack::active_downloads, c.active > 0 ? c.active : -1);
    sp.set_int(lt::settings_pack::active_seeds, c.active > 0 ? std::max(c.active, 3) : -1);
    sp.set_int(lt::settings_pack::active_limit, c.active > 0 ? std::max(2 * c.active + 2, 8) : -1);
    sp.set_int(lt::settings_pack::connections_limit, c.connections > 0 ? c.connections : 200);
    // Chiffrement du protocole (MSE/PE) : préféré (on chiffre si l'autre sait), obligatoire, ou désactivé.
    const int pol = c.encrypt == 1 ? lt::settings_pack::pe_forced : c.encrypt == 2 ? lt::settings_pack::pe_disabled : lt::settings_pack::pe_enabled;
    sp.set_int(lt::settings_pack::out_enc_policy, pol);
    sp.set_int(lt::settings_pack::in_enc_policy, pol);
    sp.set_int(lt::settings_pack::allowed_enc_level, lt::settings_pack::pe_both);
    sp.set_bool(lt::settings_pack::prefer_rc4, c.encrypt == 1);
    sp.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage);  // storage : déplacements
    return sp;
}
// libtorrent ne limite pas, par défaut, les pairs du réseau local (classe « local ») : on y applique les mêmes débits
// que pour Internet, pour que « Débit maximal » et « Débit pendant une partie » valent partout.
// Une entrée de la liste de blocage en plage d'adresses IPv4 : « a.b.c.d », « a.b.c.d/n » ou « a.b.c.d-e.f.g.h ».
bool block_range(const std::string& in, lt::address& first, lt::address& last, std::string& err) {
    std::string e = in;
    while (!e.empty() && (e.back() == ' ' || e.back() == '\r')) e.pop_back();
    while (!e.empty() && e.front() == ' ') e.erase(0, 1);
    lt::error_code ec;
    if (e.empty()) { err = "adresse vide"; return false; }
    const auto dash = e.find('-'), slash = e.find('/');
    if (dash != std::string::npos) {
        first = lt::make_address(e.substr(0, dash), ec);
        if (!ec) last = lt::make_address(e.substr(dash + 1), ec);
        if (ec || !first.is_v4() || !last.is_v4() || first.to_v4().to_uint() > last.to_v4().to_uint()) { err = "plage invalide : " + e; return false; }
        return true;
    }
    if (slash != std::string::npos) {
        const int bits = std::atoi(e.substr(slash + 1).c_str());
        const lt::address a = lt::make_address(e.substr(0, slash), ec);
        if (ec || !a.is_v4() || bits < 8 || bits > 32) { err = "masque invalide (de /8 à /32) : " + e; return false; }
        const std::uint32_t mask = bits == 32 ? 0xffffffffu : ~((1u << (32 - bits)) - 1u), base = a.to_v4().to_uint() & mask;
        first = lt::address_v4(base);
        last = lt::address_v4(base | ~mask);
        return true;
    }
    first = last = lt::make_address(e, ec);
    if (ec || !first.is_v4()) { err = "adresse invalide : " + e; return false; }
    return true;
}
void apply_blocklist(lt::session& ses, const Config& c) {
    lt::ip_filter f;
    for (const auto& e : c.blocklist) {
        lt::address a, b;
        std::string err;
        if (block_range(e, a, b, err)) f.add_rule(a, b, lt::ip_filter::blocked);
    }
    ses.set_ip_filter(f);
}
void limit_local(lt::session& ses, const Config& c) {
    lt::peer_class_info pc = ses.get_peer_class(lt::session::local_peer_class_id);
    pc.download_limit = c.down_kbps > 0 ? c.down_kbps * 1024 : 0;
    pc.upload_limit = c.up_kbps > 0 ? c.up_kbps * 1024 : 0;
    ses.set_peer_class(lt::session::local_peer_class_id, pc);
}
}  // namespace

// « Créer un lien » : un calcul tourne dans son propre fil ; il survit à la session (arrêt du moteur) sans y toucher :
// la session n'est jointe que sous `mu`, tant que `ses` n'est pas nul.
struct CreateJob {
    std::string job, name, path, state = "hashing", error, id;
    std::int64_t done = 0, total = 0;
    std::atomic<bool> cancel{false};
};
struct Shared {
    std::mutex mu;
    lt::session* ses = nullptr;
    std::vector<std::shared_ptr<CreateJob>> jobs;
    int next = 1;
};
struct Cancelled {};

struct Session::Impl {
    Config cfg;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    std::unique_ptr<lt::session> ses;
    std::thread loop;
    std::atomic<bool> quit{false};
    mutable std::mutex mu;
    int port = 0;
    std::map<std::string, std::string> errors;  // id → dernière erreur
    std::set<std::string> done, keep;           // partage arrêté (ratio / durée) ; reparti à la main (plus d'arrêt seul)
    std::set<std::string> moving;               // déplacement « à la fin » en cours
    std::string ext_ip;                         // adresse publique (external_ip_alert)
    std::function<void(const std::string&, const std::string&)> on_done;

    // À la fin d'un vrai téléchargement (une seule fois : marque « .after ») : déplacer, puis prévenir le moteur (copie).
    void after_download(lt::torrent_handle h) {
        if (!h.is_valid()) return;
        const lt::torrent_status st = h.status(lt::torrent_handle::query_name | lt::torrent_handle::query_save_path);
        const std::string id = hex_id(st.info_hashes);
        if (st.all_time_download <= 0 || created(id) || fs::exists(mark(id, ".after"))) return;
        fs::write_atomic(mark(id, ".after"), "1");
        if (!cfg.move_dir.empty() && fs::host(cfg.move_dir) != st.save_path) {
            fs::mkdirs(fs::host(cfg.move_dir));
            { std::lock_guard<std::mutex> l(mu); moving.insert(id); }
            h.move_storage(fs::host(cfg.move_dir), lt::move_flags_t::dont_replace);
            return;  // la suite à l'arrivée de storage_moved_alert
        }
        if (on_done) on_done(st.name, fs::console_path(st.save_path));
    }
    std::string mark(const std::string& id, const char* ext) const { return fs::host(fs::join(cfg.state_dir, id + ext)); }
    bool created(const std::string& id) const { return fs::exists(mark(id, ".created")); }
    // Ce que list() et detail() ajoutent : lien créé ici, premiers / derniers morceaux d'abord, nom choisi.
    void decorate(Json& t) const {
        const std::string id = t["id"].as_string();
        t.set("created", created(id));
        t.set("first_last", fs::exists(mark(id, ".firstlast")));
        std::string n;
        if (fs::read(mark(id, ".name"), n, 1024) && !n.empty()) t.set("name", n);
    }
    // Dossier surveillé : chaque .torrent déposé est ajouté, puis renommé (« .ajouté », ou « .refusé » s'il est illisible).
    void scan_watch(Session& self) {
        if (cfg.watch_dir.empty()) return;
        std::vector<fs::Entry> es;
        if (!fs::list(fs::host(cfg.watch_dir), es)) return;
        for (const auto& e : es) {
            if (e.dir || e.name.size() < 9) continue;
            std::string low = e.name.substr(e.name.size() - 8);
            for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (low != ".torrent") continue;
            const std::string p = fs::join(cfg.watch_dir, e.name);
            std::string err;
            const bool ok = !self.add(p, cfg.save_dir, err).empty();
            log("bt: dossier surveillé : %s %s %s", e.name.c_str(), ok ? "ajouté" : "refusé :", err.c_str());
            std::rename(fs::host(p).c_str(), fs::host(p + (ok ? ".ajouté" : ".refusé")).c_str());
        }
    }

    // Place : un torrent plus gros que la place libre de son disque est mis en pause, avec un message (on garde 512 Mo).
    void check_space(lt::torrent_handle h) {
        if (!cfg.space_check || !h.is_valid()) return;
        const lt::torrent_status st = h.status(lt::torrent_handle::query_save_path);
        if (!st.has_metadata || st.is_finished) return;
        fs::Space sp;
        if (!fs::space(st.save_path, sp)) return;
        const std::int64_t need = st.total_wanted - st.total_wanted_done, margin = 512ll << 20;
        if (need <= 0 || static_cast<std::int64_t>(sp.free) >= need + margin) return;
        h.unset_flags(lt::torrent_flags::auto_managed);
        h.pause(lt::torrent_handle::graceful_pause);
        char msg[200];
        std::snprintf(msg, sizeof msg, "pas assez de place : il faut %.1f Go, il reste %.1f Go (libérez de la place puis reprenez)",
                      static_cast<double>(need) / 1e9, static_cast<double>(sp.free) / 1e9);
        std::lock_guard<std::mutex> l(mu);
        errors[hex_id(h.info_hashes())] = msg;
    }

    // Arrêter de partager : un torrent terminé (pas un lien créé ici, pas un torrent reparti à la main) qui a atteint le
    // ratio ou la durée réglés est mis en pause et marqué « terminé ».
    void check_seed_limits() {
        if (!ses || (cfg.seed_ratio <= 0 && cfg.seed_minutes <= 0)) return;
        for (lt::torrent_handle& h : ses->get_torrents()) {
            if (!h.is_valid()) continue;
            const lt::torrent_status st = h.status();
            if (!st.is_finished || (st.flags & lt::torrent_flags::paused)) continue;
            const std::string id = hex_id(st.info_hashes);
            { std::lock_guard<std::mutex> l(mu); if (done.count(id) || keep.count(id)) continue; }
            if (created(id)) continue;
            const std::int64_t size = st.total_wanted > 0 ? st.total_wanted : 1;
            const std::int64_t down = std::max<std::int64_t>(st.all_time_download, size);
            const bool by_ratio = cfg.seed_ratio > 0 && st.all_time_upload * 100 / down >= cfg.seed_ratio;
            const bool by_time = cfg.seed_minutes > 0 && st.seeding_duration.count() >= static_cast<std::int64_t>(cfg.seed_minutes) * 60;
            if (!by_ratio && !by_time) continue;
            h.unset_flags(lt::torrent_flags::auto_managed);
            h.pause(lt::torrent_handle::graceful_pause);
            h.save_resume_data(lt::torrent_handle::save_info_dict);
            fs::write_atomic(mark(id, ".done"), by_ratio ? "ratio" : "time");
            std::lock_guard<std::mutex> l(mu);
            done.insert(id);
            log("bt: partage arrêté pour %s (%s)", id.c_str(), by_ratio ? "ratio atteint" : "durée atteinte");
        }
    }

    std::string resume_path(const std::string& id) const { return fs::join(cfg.state_dir, id + ".resume"); }

    void save_all(bool wait) {
        if (!ses) return;
        int pending = 0;
        for (lt::torrent_handle& h : ses->get_torrents()) {
            if (!h.is_valid()) continue;
            if (h.need_save_resume_data() || wait) { h.save_resume_data(lt::torrent_handle::save_info_dict); ++pending; }
        }
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(wait ? 5 : 0);
        do { handle_alerts(pending); } while (wait && pending > 0 && std::chrono::steady_clock::now() < end);
    }

    void handle_alerts(int& pending) {
        if (!ses) return;
        ses->wait_for_alert(std::chrono::milliseconds(200));
        std::vector<lt::alert*> alerts;
        ses->pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* r = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                const std::vector<char> buf = lt::write_resume_data_buf(r->params);
                const std::string id = hex_id(r->params.info_hashes);
                fs::write_atomic(fs::host(resume_path(id)), std::string(buf.begin(), buf.end()));
                --pending;
            } else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                --pending;
            } else if (auto* e = lt::alert_cast<lt::torrent_error_alert>(a)) {
                std::lock_guard<std::mutex> l(mu);
                errors[hex_id(e->handle.info_hashes())] = e->error.message();
            } else if (auto* f = lt::alert_cast<lt::file_error_alert>(a)) {
                std::lock_guard<std::mutex> l(mu);
                errors[hex_id(f->handle.info_hashes())] = f->error.message();
            } else if (auto* m = lt::alert_cast<lt::metadata_received_alert>(a)) {
                m->handle.save_resume_data(lt::torrent_handle::save_info_dict);  // le nom et la liste des fichiers sont connus
                ++pending;
                check_space(m->handle);
            } else if (auto* d = lt::alert_cast<lt::torrent_finished_alert>(a)) {
                d->handle.save_resume_data(lt::torrent_handle::save_info_dict);
                ++pending;
                after_download(d->handle);
            } else if (auto* mv = lt::alert_cast<lt::storage_moved_alert>(a)) {
                mv->handle.save_resume_data(lt::torrent_handle::save_info_dict);
                ++pending;
                const std::string id = hex_id(mv->handle.info_hashes());
                bool ours = false;
                { std::lock_guard<std::mutex> l(mu); ours = moving.erase(id) > 0; }
                if (ours) {
                    log("bt: déplacé vers %s : %s", mv->storage_path(), id.c_str());
                    if (on_done) on_done(mv->handle.status(lt::torrent_handle::query_name).name, fs::console_path(mv->storage_path()));
                }
            } else if (auto* xi = lt::alert_cast<lt::external_ip_alert>(a)) {
                std::lock_guard<std::mutex> l(mu);
                if (xi->external_address.is_v4()) ext_ip = xi->external_address.to_string();
            } else if (auto* mf = lt::alert_cast<lt::storage_moved_failed_alert>(a)) {
                const std::string id = hex_id(mf->handle.info_hashes());
                std::lock_guard<std::mutex> l(mu);
                moving.erase(id);
                errors[id] = "déplacement impossible : " + mf->error.message();
            }
        }
    }
};

Session::Session() : d_(std::make_unique<Impl>()) {}
Session::~Session() { stop(); }
bool Session::available() const { return true; }

bool Session::start(const Config& c, std::string& err) {
    try {
        d_->cfg = c;
        fs::mkdirs(fs::host(c.state_dir));
        int port = 0;
        for (int p = c.port_from; p <= c.port_to && p > 0; ++p) if (port_free(p)) { port = p; break; }
        if (!port) { err = "aucun port libre dans la plage " + std::to_string(c.port_from) + "–" + std::to_string(c.port_to); return false; }
        d_->port = port;
        // Lecture et écriture simples (pread / pwrite) : la projection en mémoire par défaut échoue sur la PS5 pour les gros
        // fichiers (image d'un jeu de 49 Gio partagée : « Cannot allocate memory »).
        lt::session_params sp(pack(c, port));
        sp.disk_io_constructor = lt::posix_disk_io_constructor;
        d_->ses = std::make_unique<lt::session>(std::move(sp));
        limit_local(*d_->ses, c);
        apply_blocklist(*d_->ses, c);
        // Reprise : chaque torrent connu repart de ses données de reprise (déjà téléchargé vérifié sur le disque).
        std::vector<fs::Entry> ents;
        if (fs::list(fs::host(c.state_dir), ents))
            for (const auto& e : ents) {
                if (!e.dir && e.name.size() > 5 && e.name.substr(e.name.size() - 5) == ".done") d_->done.insert(e.name.substr(0, e.name.size() - 5));
                if (!e.dir && e.name.size() > 5 && e.name.substr(e.name.size() - 5) == ".keep") d_->keep.insert(e.name.substr(0, e.name.size() - 5));
                if (e.dir || e.name.size() < 8 || e.name.substr(e.name.size() - 7) != ".resume") continue;
                std::string text;
                if (!fs::read(fs::host(fs::join(c.state_dir, e.name)), text)) continue;
                lt::error_code ec;
                lt::add_torrent_params atp = lt::read_resume_data({text.data(), static_cast<int>(text.size())}, ec);
                if (ec) { log("bt: reprise illisible %s : %s", e.name.c_str(), ec.message().c_str()); continue; }
                d_->ses->async_add_torrent(std::move(atp));
            }
        d_->on_done = on_done;
        d_->quit = false;
        d_->loop = std::thread([this] {
            auto last_save = std::chrono::steady_clock::now(), last_limits = last_save;
            while (!d_->quit) {
                try {
                    int pending = 0;
                    d_->handle_alerts(pending);
                    if (std::chrono::steady_clock::now() - last_save > std::chrono::seconds(30)) { d_->save_all(false); last_save = std::chrono::steady_clock::now(); }
                    if (std::chrono::steady_clock::now() - last_limits > std::chrono::seconds(20)) { d_->check_seed_limits(); d_->scan_watch(*this); last_limits = std::chrono::steady_clock::now(); }
                } catch (const std::exception& e) {
                    log("bt: boucle : %s", e.what());
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        });
        { std::lock_guard<std::mutex> l(d_->shared->mu); d_->shared->ses = d_->ses.get(); }
        log("bt: session prête (libtorrent %s), port %d", lt::version(), port);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        d_->ses.reset();
        return false;
    }
}

void Session::stop() {
    if (!d_ || !d_->ses) return;
    { std::lock_guard<std::mutex> l(d_->shared->mu); d_->shared->ses = nullptr; }
    d_->quit = true;
    if (d_->loop.joinable()) d_->loop.join();
    try { d_->save_all(true); } catch (...) {}
    try { d_->ses.reset(); } catch (...) {}
}

void Session::configure(const Config& c) {
    if (!d_->ses) return;
    try {
        Config keep = c;
        keep.state_dir = d_->cfg.state_dir;
        d_->cfg = keep;
        d_->ses->apply_settings(pack(keep, d_->port));  // le même port ; seule l'écoute change (entrantes oui / non)
        limit_local(*d_->ses, keep);
        apply_blocklist(*d_->ses, keep);
    } catch (const std::exception& e) { log("bt: réglages : %s", e.what()); }
}

std::string Session::add(const std::string& src, const std::string& save_dir, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return {}; }
    try {
        lt::add_torrent_params atp;
        lt::error_code ec;
        if (src.rfind("magnet:", 0) == 0) {
            atp = lt::parse_magnet_uri(src, ec);
            if (ec) { err = "lien magnet illisible : " + ec.message(); return {}; }
        } else {
            atp.ti = std::make_shared<lt::torrent_info>(fs::host(src), ec);
            if (ec) { err = "fichier .torrent illisible : " + ec.message(); return {}; }
        }
        atp.save_path = fs::host(save_dir.empty() ? d_->cfg.save_dir : save_dir);
        fs::mkdirs(atp.save_path);
        if (d_->cfg.prealloc) atp.storage_mode = lt::storage_mode_allocate;  // tout l'espace réservé au départ
        lt::torrent_handle h = d_->ses->add_torrent(std::move(atp), ec);
        if (ec) { err = ec.message(); return {}; }
        h.save_resume_data(lt::torrent_handle::save_info_dict);
        d_->check_space(h);  // .torrent : la taille est connue tout de suite (magnet : à l'arrivée des métadonnées)
        return hex_id(h.info_hashes());
    } catch (const std::exception& e) {
        err = e.what();
        return {};
    }
}

namespace {
lt::torrent_handle find(lt::session& s, const std::string& id) {
    for (lt::torrent_handle& h : s.get_torrents()) if (h.is_valid() && hex_id(h.info_hashes()) == id) return h;
    return {};
}
}  // namespace

bool Session::pause(const std::string& id, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return false; }
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) { err = "torrent introuvable"; return false; }
        h.unset_flags(lt::torrent_flags::auto_managed);
        h.pause(lt::torrent_handle::graceful_pause);
        h.save_resume_data(lt::torrent_handle::save_info_dict);
        return true;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

bool Session::resume(const std::string& id, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return false; }
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) { err = "torrent introuvable"; return false; }
        {
            std::lock_guard<std::mutex> l(d_->mu);
            d_->errors.erase(id);
            if (d_->done.erase(id)) {  // il avait fini de partager : il repart pour de bon (plus d'arrêt seul)
                ::unlink(d_->mark(id, ".done").c_str());
                d_->keep.insert(id);
                fs::write_atomic(d_->mark(id, ".keep"), "1");
            }
        }
        h.set_flags(lt::torrent_flags::auto_managed);
        h.resume();
        return true;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

bool Session::remove(const std::string& id, bool delete_files, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return false; }
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) { err = "torrent introuvable"; return false; }
        const std::string mark = fs::host(fs::join(d_->cfg.state_dir, id + ".created"));
        if (delete_files && fs::exists(mark)) { err = "ce lien partage vos fichiers d'origine : ils ne sont jamais effacés d'ici"; return false; }
        d_->ses->remove_torrent(h, delete_files ? lt::session::delete_files : lt::remove_flags_t{});
        ::unlink(fs::host(d_->resume_path(id)).c_str());
        ::unlink(mark.c_str());
        ::unlink(d_->mark(id, ".done").c_str());
        ::unlink(d_->mark(id, ".keep").c_str());
        ::unlink(d_->mark(id, ".after").c_str());
        { std::lock_guard<std::mutex> l(d_->mu); d_->done.erase(id); d_->keep.erase(id); d_->errors.erase(id); }
        return true;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

namespace {
// Ce que list() et detail() ont en commun.
Json summary(const lt::torrent_status& st, const std::map<std::string, std::string>& errors, std::mutex& mu, const std::set<std::string>& done) {
            const std::string id = hex_id(st.info_hashes);
            Json t = Json::object();
            t.set("id", id);
            t.set("name", st.name.empty() ? id.substr(0, 12) : st.name);
            const bool managed = static_cast<bool>(st.flags & lt::torrent_flags::auto_managed);
            const bool paused = (st.flags & lt::torrent_flags::paused) && !managed;
            std::string state = st.state == lt::torrent_status::checking_files || st.state == lt::torrent_status::checking_resume_data ? "checking"
                              : st.state == lt::torrent_status::seeding || st.state == lt::torrent_status::finished ? "seeding"
                              : (st.flags & lt::torrent_flags::paused) && managed ? "queued"  // attend son tour (file)
                              : st.state == lt::torrent_status::downloading_metadata ? "metadata"
                              : st.num_peers == 0 ? "waiting" : "downloading";
            {
                std::lock_guard<std::mutex> l(mu);
                const auto e = errors.find(id);
                if (st.errc || e != errors.end()) { state = "error"; t.set("error", e != errors.end() ? e->second : st.errc.message()); }
            }
            if (paused) state = "paused";
            {
                std::lock_guard<std::mutex> l(mu);
                if (paused && st.is_finished && done.count(id)) state = "complete";  // partage arrêté : ratio ou durée atteints
            }
            t.set("state", state);
            t.set("seeding_time", static_cast<std::int64_t>(st.seeding_duration.count()));
            t.set("total", static_cast<std::int64_t>(st.total_wanted));
            t.set("done", static_cast<std::int64_t>(st.total_wanted_done));
            t.set("down", static_cast<std::int64_t>(st.download_payload_rate));
            t.set("up", static_cast<std::int64_t>(st.upload_payload_rate));
            t.set("peers", static_cast<std::int64_t>(st.num_peers));
            t.set("seeds", static_cast<std::int64_t>(st.num_seeds));
            t.set("paused", paused);
            t.set("finished", st.is_finished);
            t.set("save", st.save_path);
            t.set("queue", static_cast<std::int64_t>(static_cast<int>(st.queue_position)));  // -1 : plus dans la file (terminé)
            t.set("forced", !managed && !paused);
            const std::int64_t left = st.total_wanted - st.total_wanted_done;
            t.set("eta", static_cast<std::int64_t>(st.download_payload_rate > 0 && left > 0 ? left / st.download_payload_rate : -1));
            t.set("ratio", st.all_time_download > 0 ? static_cast<double>(st.all_time_upload) / static_cast<double>(st.all_time_download) : 0.0);
            t.set("sequential", static_cast<bool>(st.flags & lt::torrent_flags::sequential_download));
            t.set("superseed", static_cast<bool>(st.flags & lt::torrent_flags::super_seeding));
            return t;
}
}  // namespace

Json Session::list() const {
    Json out = Json::array();
    if (!d_->ses) return out;
    try {
        std::vector<lt::torrent_status> all;
        for (const lt::torrent_handle& h : d_->ses->get_torrents()) if (h.is_valid()) all.push_back(h.status());
        // Dans l'ordre de la file (celui qui passe en premier en haut), les terminés à la fin.
        std::stable_sort(all.begin(), all.end(), [](const lt::torrent_status& a, const lt::torrent_status& b) {
            const int qa = static_cast<int>(a.queue_position), qb = static_cast<int>(b.queue_position);
            return (qa < 0 ? 1 << 30 : qa) < (qb < 0 ? 1 << 30 : qb);
        });
        for (const auto& st : all) {
            Json t = summary(st, d_->errors, d_->mu, d_->done);
            d_->decorate(t);
            out.push(t);
        }
    } catch (const std::exception& e) { log("bt: liste : %s", e.what()); }
    return out;
}

Json Session::detail(const std::string& id) const {
    if (!d_->ses) return Json();
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) return Json();
        const lt::torrent_status st = h.status(lt::torrent_handle::query_accurate_download_counters | lt::torrent_handle::query_name | lt::torrent_handle::query_save_path | lt::torrent_handle::query_distributed_copies);
        Json t = summary(st, d_->errors, d_->mu, d_->done);
        d_->decorate(t);
        t.set("magnet", lt::make_magnet_uri(h));
        t.set("pieces", static_cast<std::int64_t>(st.num_pieces));
        t.set("availability", static_cast<double>(st.distributed_copies));  // copies complètes chez les pairs (-1 : inconnu)
        t.set("uploaded", static_cast<std::int64_t>(st.all_time_upload));
        t.set("downloaded", static_cast<std::int64_t>(st.all_time_download));
        t.set("limit_down", static_cast<std::int64_t>(h.download_limit() / 1024));
        t.set("limit_up", static_cast<std::int64_t>(h.upload_limit() / 1024));
        t.set("connections", static_cast<std::int64_t>(st.num_connections));
        t.set("swarm_seeds", static_cast<std::int64_t>(st.num_complete));
        t.set("swarm_peers", static_cast<std::int64_t>(st.num_incomplete));
        Json files = Json::array();
        if (const auto ti = h.torrent_file()) {
            const lt::file_storage& fsr = ti->files();
            std::vector<std::int64_t> prog;
            h.file_progress(prog, lt::torrent_handle::piece_granularity);
            const std::vector<lt::download_priority_t> pr = h.get_file_priorities();
            for (const lt::file_index_t i : fsr.file_range()) {
                if (fsr.pad_file_at(i)) continue;
                const int k = static_cast<int>(i);
                Json f = Json::object();
                f.set("index", static_cast<std::int64_t>(k));
                f.set("path", fsr.file_path(i));
                f.set("size", static_cast<std::int64_t>(fsr.file_size(i)));
                f.set("done", static_cast<std::int64_t>(k < static_cast<int>(prog.size()) ? prog[static_cast<std::size_t>(k)] : 0));
                f.set("priority", static_cast<std::int64_t>(k < static_cast<int>(pr.size()) ? static_cast<int>(static_cast<std::uint8_t>(pr[static_cast<std::size_t>(k)])) : 4));
                files.push(f);
                if (files.size() >= 500) break;  // une fiche lisible : les 500 premiers
            }
        }
        t.set("files", files);
        Json trackers = Json::array();
        for (const lt::announce_entry& a : h.trackers()) {
            Json tr = Json::object();
            tr.set("url", a.url);
            tr.set("tier", static_cast<std::int64_t>(a.tier));
            std::string status = "waiting", message;
            int seeds = -1, peers = -1;
            for (const auto& ep : a.endpoints)
                for (const auto& ih : ep.info_hashes) {
                    if (ih.updating) status = "updating";
                    else if (ih.fails > 0 && status != "ok") { status = "error"; message = ih.last_error ? ih.last_error.message() : ih.message; }
                    else if (ih.start_sent) status = "ok";
                    if (ih.scrape_complete >= 0) seeds = std::max(seeds, ih.scrape_complete);
                    if (ih.scrape_incomplete >= 0) peers = std::max(peers, ih.scrape_incomplete);
                }
            tr.set("status", status);
            if (!message.empty()) tr.set("message", message);
            tr.set("seeds", static_cast<std::int64_t>(seeds));
            tr.set("peers", static_cast<std::int64_t>(peers));
            trackers.push(tr);
        }
        t.set("trackers", trackers);
        Json peers = Json::array();
        std::vector<lt::peer_info> pi;
        h.get_peer_info(pi);
        std::sort(pi.begin(), pi.end(), [](const lt::peer_info& a, const lt::peer_info& b) { return a.payload_down_speed > b.payload_down_speed; });
        for (const lt::peer_info& p : pi) {
            Json q = Json::object();
            q.set("ip", p.ip.address().to_string());
            q.set("client", p.client);
            q.set("down", static_cast<std::int64_t>(p.payload_down_speed));
            q.set("up", static_cast<std::int64_t>(p.payload_up_speed));
            q.set("progress", static_cast<double>(p.progress));
            q.set("seed", static_cast<bool>(p.flags & lt::peer_info::seed));
            q.set("utp", static_cast<bool>(p.flags & lt::peer_info::utp_socket));
            peers.push(q);
            if (peers.size() >= 50) break;
        }
        t.set("peer_list", peers);
        return t;
    } catch (const std::exception& e) { log("bt: fiche : %s", e.what()); return Json(); }
}

bool Session::act(const std::string& id, const std::string& what, const Json& a, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return false; }
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) { err = "torrent introuvable"; return false; }
        if (what == "force") { h.unset_flags(lt::torrent_flags::auto_managed); h.resume(); }
        else if (what == "reannounce") { h.force_reannounce(); h.force_dht_announce(); h.scrape_tracker(); }
        else if (what == "recheck") h.force_recheck();
        else if (what == "queue_up") h.queue_position_up();
        else if (what == "queue_down") h.queue_position_down();
        else if (what == "queue_top") h.queue_position_top();
        else if (what == "queue_bottom") h.queue_position_bottom();
        else if (what == "sequential") { if (a["on"].as_bool(false)) h.set_flags(lt::torrent_flags::sequential_download); else h.unset_flags(lt::torrent_flags::sequential_download); }
        else if (what == "limits") {
            const auto kb = [](std::int64_t v) { return v > 0 ? static_cast<int>(std::min<std::int64_t>(v, 1000000) * 1024) : -1; };
            h.set_download_limit(kb(a["down"].as_int(0)));
            h.set_upload_limit(kb(a["up"].as_int(0)));
        } else if (what == "file" || what == "files") {
            const std::int64_t p = a["priority"].as_int(4);
            if (p != 0 && p != 1 && p != 4 && p != 7) { err = "priorité inconnue"; return false; }
            const auto ti = h.torrent_file();
            if (!ti) { err = "liste des fichiers pas encore connue"; return false; }
            const lt::download_priority_t prio{static_cast<std::uint8_t>(p)};
            if (what == "files") {
                h.prioritize_files(std::vector<lt::download_priority_t>(static_cast<std::size_t>(ti->num_files()), prio));
            } else {
                const std::int64_t i = a["index"].as_int(-1);
                if (i < 0 || i >= ti->num_files()) { err = "fichier inconnu"; return false; }
                h.file_priority(lt::file_index_t{static_cast<int>(i)}, prio);
            }
        } else if (what == "tracker_add" || what == "tracker_remove" || what == "tracker_edit") {
            // Trackers : en ajouter un, en retirer un, ou changer l'adresse d'un (quand le tracker a déménagé).
            const std::string url = a["url"].as_string(), to = a["to"].as_string();
            auto ok_url = [](const std::string& u) { return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0 || u.rfind("udp://", 0) == 0; };
            std::vector<lt::announce_entry> v = h.trackers();
            if (what == "tracker_add") {
                if (!ok_url(url)) { err = "adresse de tracker invalide (http://, https:// ou udp://)"; return false; }
                for (const auto& e : v) if (e.url == url) { err = "ce tracker est déjà là"; return false; }
                lt::announce_entry e(url);
                e.tier = static_cast<std::uint8_t>(v.empty() ? 0 : v.back().tier + 1);
                v.push_back(e);
            } else {
                auto it = std::find_if(v.begin(), v.end(), [&](const lt::announce_entry& e) { return e.url == url; });
                if (it == v.end()) { err = "tracker introuvable"; return false; }
                if (what == "tracker_remove") v.erase(it);
                else {
                    if (!ok_url(to)) { err = "adresse de tracker invalide (http://, https:// ou udp://)"; return false; }
                    const std::uint8_t tier = it->tier;
                    *it = lt::announce_entry(to);
                    it->tier = tier;
                }
            }
            h.replace_trackers(v);
            h.force_reannounce();
        } else if (what == "add_peer") {  // « ip:port » (port 6891 si absent)
            std::string s = a["addr"].as_string();
            while (!s.empty() && s.back() == ' ') s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            int port = 6891;
            const auto colon = s.rfind(':');
            if (colon != std::string::npos) { port = std::atoi(s.substr(colon + 1).c_str()); s = s.substr(0, colon); }
            lt::error_code ec;
            const lt::address ip = lt::make_address(s, ec);
            if (ec || port <= 0 || port > 65535) { err = "adresse invalide : écrivez ip:port, par exemple 192.168.1.20:6891"; return false; }
            h.connect_peer(lt::tcp::endpoint(ip, static_cast<std::uint16_t>(port)));
        } else if (what == "superseed") {
            if (a["on"].as_bool(false)) h.set_flags(lt::torrent_flags::super_seeding); else h.unset_flags(lt::torrent_flags::super_seeding);
        } else if (what == "first_last") {
            const auto ti = h.torrent_file();
            if (!ti) { err = "les informations du torrent ne sont pas encore arrivées"; return false; }
            const bool on = a["on"].as_bool(false);
            const lt::file_storage& fst = ti->files();
            for (const lt::file_index_t i : fst.file_range()) {
                if (fst.file_size(i) <= 0) continue;
                const lt::piece_index_t first = fst.map_file(i, 0, 0).piece, last = fst.map_file(i, fst.file_size(i) - 1, 0).piece;
                for (const lt::piece_index_t p : {first, last}) h.piece_priority(p, on ? lt::top_priority : lt::default_priority);
            }
            if (on) fs::write_atomic(d_->mark(id, ".firstlast"), "1"); else std::remove(d_->mark(id, ".firstlast").c_str());
        } else if (what == "rename") {
            std::string n = a["name"].as_string();
            while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
            while (!n.empty() && n.front() == ' ') n.erase(0, 1);
            if (n.empty() || n.size() > 200 || n.find_first_of("/\\:*?\"<>|") != std::string::npos) { err = "nom invalide (pas de / \\ : * ? \" < > |)"; return false; }
            const auto ti = h.torrent_file();
            if (!ti) { err = "les informations du torrent ne sont pas encore arrivées"; return false; }
            const lt::file_storage& fst = ti->files();
            for (const lt::file_index_t i : fst.file_range()) {
                const std::string p = fst.file_path(i);
                const auto sl = p.find_first_of("/\\");
                h.rename_file(i, fst.num_files() == 1 && sl == std::string::npos ? n : n + p.substr(sl));
            }
            fs::write_atomic(d_->mark(id, ".name"), n);
        } else { err = "action inconnue"; return false; }
        h.save_resume_data(lt::torrent_handle::save_info_dict);  // priorités, limites, ordre : gardés au prochain démarrage
        return true;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

int Session::port() const { return d_->port; }
bool Session::incoming() const { return d_->cfg.incoming; }
bool Session::valid_block(const std::string& entry, std::string& err) {
    lt::address a, b;
    return block_range(entry, a, b, err);
}
std::string Session::external_ip() const { std::lock_guard<std::mutex> l(d_->mu); return d_->ext_ip; }
bool Session::map_port(int port) {
    if (!d_->ses || !d_->cfg.upnp || port <= 0) return false;
    try {
        d_->ses->add_port_mapping(lt::portmap_protocol::tcp, port, port);
        return true;
    } catch (const std::exception& e) { log("bt: ouverture du port %d : %s", port, e.what()); return false; }
}

std::string Session::create(const std::string& path, std::string& err) {
    if (!d_->ses) { err = "BitTorrent n'est pas démarré"; return {}; }
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    const auto sl = p.rfind('/');
    if (p.empty() || p == "/" || sl == std::string::npos || sl + 1 >= p.size()) { err = "choisissez un fichier ou un dossier"; return {}; }
    if (!fs::exists(fs::host(p))) { err = "introuvable : " + p; return {}; }
    auto j = std::make_shared<CreateJob>();
    j->path = p;
    j->name = p.substr(sl + 1);
    const std::string parent = sl == 0 ? "/" : p.substr(0, sl);
    std::shared_ptr<Shared> sh = d_->shared;
    {
        std::lock_guard<std::mutex> l(sh->mu);
        for (const auto& o : sh->jobs) if (o->path == p && o->state == "hashing") { err = "ce lien est déjà en préparation"; return {}; }
        j->job = "c" + std::to_string(sh->next++);
        sh->jobs.erase(std::remove_if(sh->jobs.begin(), sh->jobs.end(), [](const std::shared_ptr<CreateJob>& o) { return o->state == "done"; }), sh->jobs.end());
        sh->jobs.push_back(j);
    }
    const std::string state_dir = d_->cfg.state_dir;
    std::thread([sh, j, parent, state_dir] {
        std::string e;
        try {
            lt::file_storage fsr;
            lt::add_files(fsr, fs::host(j->path), [](const std::string& f) {  // ni fichiers cachés ni dossiers système
                const auto s = f.rfind('/');
                const std::string base = s == std::string::npos ? f : f.substr(s + 1);
                return base.empty() || base[0] != '.';
            }, lt::create_torrent::v1_only);
            if (fsr.num_files() == 0 || fsr.total_size() == 0) throw std::runtime_error("rien à partager (dossier vide)");
            lt::create_torrent ct(fsr, 0, lt::create_torrent::v1_only);
            const int n = ct.num_pieces();
            { std::lock_guard<std::mutex> l(sh->mu); j->total = fsr.total_size(); }
            ct.set_creator("PS5 Backup Center");
            lt::error_code ec;
            const std::int64_t plen = ct.piece_length();
            // Lecture simple (pread), comme la session : projeter l'image d'un jeu échoue sur la PS5 (« Operation canceled »).
            lt::set_piece_hashes(ct, fs::host(parent), lt::settings_pack(), lt::posix_disk_io_constructor, [&](lt::piece_index_t i) {
                if (j->cancel) throw Cancelled{};
                std::lock_guard<std::mutex> l(sh->mu);
                j->done = std::min<std::int64_t>(j->total, (static_cast<int>(i) + 1) * plen);
            }, ec);
            if (ec) throw std::runtime_error(ec.message());
            (void)n;
            const std::vector<char> buf = ct.generate_buf();
            lt::add_torrent_params atp;
            atp.ti = std::make_shared<lt::torrent_info>(lt::span<char const>(buf.data(), static_cast<std::ptrdiff_t>(buf.size())), lt::from_span);
            atp.save_path = fs::host(parent);
            atp.flags |= lt::torrent_flags::seed_mode;  // les empreintes viennent d'être calculées : pas de revérification
            std::lock_guard<std::mutex> l(sh->mu);
            if (!sh->ses) throw std::runtime_error("BitTorrent arrêté");
            lt::torrent_handle h = sh->ses->add_torrent(std::move(atp), ec);
            if (ec) throw std::runtime_error(ec.message());
            h.save_resume_data(lt::torrent_handle::save_info_dict);
            j->id = hex_id(h.info_hashes());
            fs::write_atomic(fs::host(fs::join(state_dir, j->id + ".created")), j->path);  // partagé depuis ses fichiers d'origine
            j->done = j->total;
            j->state = "done";
            log("bt: lien créé pour %s (%s)", j->path.c_str(), j->id.c_str());
            return;
        } catch (const Cancelled&) {
            std::lock_guard<std::mutex> l(sh->mu);
            sh->jobs.erase(std::remove(sh->jobs.begin(), sh->jobs.end(), j), sh->jobs.end());
            return;
        } catch (const std::exception& x) { e = x.what(); }
        catch (...) { e = "erreur inconnue"; }
        std::lock_guard<std::mutex> l(sh->mu);
        j->state = "error";
        j->error = e;
        log("bt: lien impossible pour %s : %s", j->path.c_str(), e.c_str());
    }).detach();
    return j->job;
}

bool Session::cancel_create(const std::string& job, std::string& err) {
    std::lock_guard<std::mutex> l(d_->shared->mu);
    auto& v = d_->shared->jobs;
    for (auto it = v.begin(); it != v.end(); ++it) {
        if ((*it)->job != job) continue;
        if ((*it)->state == "hashing") (*it)->cancel = true;  // le fil s'arrête au prochain morceau et se retire
        else v.erase(it);                                      // raté ou fini : on l'oublie
        return true;
    }
    err = "introuvable";
    return false;
}

Json Session::creating() const {
    Json out = Json::array();
    std::lock_guard<std::mutex> l(d_->shared->mu);
    for (const auto& j : d_->shared->jobs) {
        if (j->state == "done" || j->cancel) continue;
        Json o = Json::object();
        o.set("job", j->job);
        o.set("name", j->name);
        o.set("path", j->path);
        o.set("done", j->done);
        o.set("total", j->total);
        o.set("state", j->state);
        if (!j->error.empty()) o.set("error", j->error);
        out.push(o);
    }
    return out;
}

std::string Session::torrent_file(const std::string& id, std::string& name) const {
    if (!d_->ses) return {};
    try {
        lt::torrent_handle h = find(*d_->ses, id);
        if (!h.is_valid()) return {};
        const auto ti = h.torrent_file();
        if (!ti) return {};
        name = ti->name();
        const std::vector<char> buf = lt::create_torrent(*ti).generate_buf();
        return std::string(buf.begin(), buf.end());
    } catch (const std::exception& e) { log("bt: .torrent : %s", e.what()); return {}; }
}

}  // namespace pbc::bt

// Ce que la bibliothèque C de la PS5 n'expose pas (déclaré dans bt/ps5_compat.hpp, inclus d'office pour libtorrent).
#if defined(PBCK_PS5)
#include <errno.h>
#include <fcntl.h>
extern "C" ssize_t getrandom(void* buf, size_t len, unsigned int) {
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) { errno = ENOSYS; return -1; }
    size_t got = 0;
    while (got < len) {
        const ssize_t n = ::read(fd, static_cast<char*>(buf) + got, len - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    ::close(fd);
    return static_cast<ssize_t>(got);
}
#endif

#endif  // PBC_WITH_BT
