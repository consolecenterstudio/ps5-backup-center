// Essai BitTorrent (étape 0, docs/ETUDE-bittorrent.md) — l'ELF d'essai sur la PS5, à côté de l'appli (rien d'elle).
// Lit /data/pbc-bt-lab/job.txt :
//   ligne 1 : chemin du .torrent sur la console
//   ligne 2 : adresse du semeur « ip:port »
//   ligne 3 : couper après N Mo reçus (0 : aller jusqu'au bout) — pour l'essai de reprise
// Reçoit dans /data/pbc-bt-lab/dl, garde les données de reprise dans /data/pbc-bt-lab/resume.dat, écrit tout dans
// /data/pbc-bt-lab/lab.log ; à la fin, l'empreinte SHA-256 du fichier reçu (pour la comparer à l'original).
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/hex.hpp>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <signal.h>
#include <ucontext.h>
#include <exception>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>

static const char* kDir = "/data/pbc-bt-lab";
static FILE* g_log = nullptr;

static void say(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (!g_log) g_log = std::fopen((std::string(kDir) + "/lab.log").c_str(), "a");
    if (g_log) { std::fprintf(g_log, "[%ld] %s\n", static_cast<long>(std::time(nullptr)), line); std::fflush(g_log); }
}

static std::vector<char> slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static bool save_resume(lt::session& ses, lt::torrent_handle& h) {
    h.save_resume_data(lt::torrent_handle::save_info_dict);
    for (int i = 0; i < 100; ++i) {
        ses.wait_for_alert(std::chrono::milliseconds(100));
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        for (lt::alert* a : alerts) {
            if (auto* r = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                const std::vector<char> buf = lt::write_resume_data_buf(r->params);
                const std::string tmp = std::string(kDir) + "/resume.tmp";
                std::ofstream(tmp, std::ios::binary).write(buf.data(), static_cast<std::streamsize>(buf.size()));
                std::rename(tmp.c_str(), (std::string(kDir) + "/resume.dat").c_str());
                return true;
            }
            if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) return false;
        }
    }
    return false;
}

// Plantage : on écrit le signal et l'adresse (et sa distance au début du programme, pour la retrouver dans l'ELF).
static void on_crash(int sig, siginfo_t* si, void* ctx) {
    const auto* uc = static_cast<ucontext_t*>(ctx);
    const unsigned long rip = static_cast<unsigned long>(uc->uc_mcontext.mc_rip);
    const unsigned long base = reinterpret_cast<unsigned long>(&say);
    char line[256];
    const int n = std::snprintf(line, sizeof line, "PLANTAGE signal %d adresse fautive %p rip %#lx (say=%#lx, écart rip-say %#lx)\n",
                                sig, si ? si->si_addr : nullptr, rip, base, rip - base);
    const int fd = ::open("/data/pbc-bt-lab/crash.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) { (void)::write(fd, line, static_cast<size_t>(n)); ::close(fd); }
    ::_exit(3);
}

int main() try {
    struct sigaction sa {};
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    for (int sg : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sg, &sa, nullptr);
    std::set_terminate([] { say("PLANTAGE : exception non attrapée (std::terminate)"); ::_exit(4); });
    // Les exceptions marchent-elles dans un ELF lancé par le chargeur ? (libtorrent en lève en interne)
    try { throw std::runtime_error("essai"); } catch (const std::exception& e) { say("exceptions : OK (%s)", e.what()); }
    try { throw lt::system_error(lt::error_code(5, lt::system_category())); } catch (const lt::system_error& e) { say("exceptions système : OK (%s)", e.what()); }
    ::mkdir(kDir, 0755);
    std::ifstream job(std::string(kDir) + "/job.txt");
    std::string tor, peer, stop_s;
    std::getline(job, tor); std::getline(job, peer); std::getline(job, stop_s);
    const long long stop_after = stop_s.empty() ? 0 : std::atoll(stop_s.c_str()) * 1000000LL;
    say("bt-lab : départ (libtorrent %s), torrent %s, semeur %s, coupure après %lld octets", lt::version(), tor.c_str(), peer.c_str(), stop_after);

    lt::settings_pack sp;
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:6891");
    sp.set_bool(lt::settings_pack::enable_dht, false);
    sp.set_bool(lt::settings_pack::enable_lsd, false);
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    // uTP (BitTorrent sur UDP) : l'ELF d'essai s'arrête net dès une connexion uTP sur la PS5 (16/11) — TCP seulement pour l'instant.
    sp.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    sp.set_bool(lt::settings_pack::enable_incoming_utp, false);
    sp.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::storage | lt::alert_category::peer | lt::alert_category::connect);
    lt::session ses(sp);

    lt::add_torrent_params atp;
    const std::vector<char> resume = slurp(std::string(kDir) + "/resume.dat");
    lt::error_code ec;
    if (!resume.empty()) atp = lt::read_resume_data(resume, ec);
    if (resume.empty() || ec) {
        if (ec) say("données de reprise illisibles (%s) : on repart du torrent", ec.message().c_str());
        atp = lt::add_torrent_params();
        atp.ti = std::make_shared<lt::torrent_info>(tor);
    } else say("reprise : données de reprise trouvées (%zu octets)", resume.size());
    atp.save_path = std::string(kDir) + "/dl";
    lt::torrent_handle h = ses.add_torrent(atp);
    const auto colon = peer.rfind(':');
    if (colon != std::string::npos) {
        const auto addr = lt::make_address(peer.substr(0, colon), ec);
        if (!ec) h.connect_peer({addr, static_cast<unsigned short>(std::atoi(peer.c_str() + colon + 1))});
    }

    auto t0 = std::chrono::steady_clock::now(), last_save = t0;
    bool checked_logged = false;
    for (;;) {
        ses.wait_for_alert(std::chrono::seconds(1));
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        for (lt::alert* a : alerts)
            if (lt::alert_cast<lt::torrent_error_alert>(a) || lt::alert_cast<lt::file_error_alert>(a) || lt::alert_cast<lt::listen_failed_alert>(a)
                || lt::alert_cast<lt::peer_connect_alert>(a) || lt::alert_cast<lt::peer_disconnected_alert>(a) || lt::alert_cast<lt::incoming_connection_alert>(a)
                || lt::alert_cast<lt::peer_error_alert>(a))
                say("alerte : %s", a->message().c_str());
        const lt::torrent_status st = h.status();
        if (!checked_logged && st.state == lt::torrent_status::downloading) {
            say("début du téléchargement : déjà %lld octets vérifiés sur le disque", static_cast<long long>(st.total_wanted_done));
            checked_logged = true;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        say("état %d · pairs %d · reçu %lld / %lld · %.1f Mo/s · %.0f s", static_cast<int>(st.state), st.num_peers,
            static_cast<long long>(st.total_wanted_done), static_cast<long long>(st.total_wanted), st.download_rate / 1e6, secs);
        if (st.is_finished) {
            save_resume(ses, h);
            const auto ti = h.torrent_file();
            const std::string path = atp.save_path + "/" + ti->files().file_path(lt::file_index_t{0});
            lt::hasher256 hs;
            std::ifstream f(path, std::ios::binary);
            std::vector<char> b(1 << 20);
            long long total = 0;
            while (f) { f.read(b.data(), static_cast<std::streamsize>(b.size())); const auto n = f.gcount(); if (n > 0) { hs.update({b.data(), static_cast<std::ptrdiff_t>(n)}); total += n; } }
            say("TERMINÉ : %lld octets, sha256 %s, %.0f s", total, lt::aux::to_hex(hs.final()).c_str(), secs);
            return 0;
        }
        if (stop_after > 0 && st.total_wanted_done >= stop_after) {
            const bool ok = save_resume(ses, h);
            say("COUPURE VOULUE à %lld octets (données de reprise %s)", static_cast<long long>(st.total_wanted_done), ok ? "enregistrées" : "NON enregistrées");
            return 0;
        }
        if (std::chrono::steady_clock::now() - last_save > std::chrono::seconds(5)) { save_resume(ses, h); last_save = std::chrono::steady_clock::now(); }
    }
} catch (const std::exception& e) {
    say("erreur : %s", e.what());
    return 1;
}
