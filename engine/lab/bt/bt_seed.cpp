// Essai BitTorrent (étape 0, docs/ETUDE-bittorrent.md) — le semeur, sur le Mac.
//   bt_seed <fichier> <sortie.torrent> <port> [ip:port du receveur]
//   (le 4e argument : le semeur appelle lui-même le receveur, quand celui-ci ne peut pas le joindre)
// Crée un torrent PRIVÉ (ni DHT, ni échange de pairs public) pour un fichier jetable, l'écrit, puis le sème sur le
// réseau local jusqu'à Ctrl-C. Rien d'autre : pas de traqueur, pas d'UPnP.
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/bencode.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv) try {
    if (argc < 4) { std::fprintf(stderr, "bt_seed <fichier> <sortie.torrent> <port>\n"); return 2; }
    const std::string file = argv[1], out = argv[2], port = argv[3];
    const std::string dir = file.substr(0, file.find_last_of('/'));
    lt::file_storage fs;
    lt::add_files(fs, file);
    lt::create_torrent ct(fs, 1 << 20, lt::create_torrent::v1_only);  // morceaux de 1 Mio
    ct.set_priv(true);
    ct.set_creator("PS5 Backup Center bt-lab");
    lt::set_piece_hashes(ct, dir);
    std::vector<char> buf;
    lt::bencode(std::back_inserter(buf), ct.generate());
    std::ofstream(out, std::ios::binary).write(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::printf("torrent écrit : %s (%zu octets)\n", out.c_str(), buf.size());

    lt::settings_pack sp;
    sp.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:" + port);
    sp.set_bool(lt::settings_pack::enable_dht, false);
    sp.set_bool(lt::settings_pack::enable_lsd, false);
    sp.set_bool(lt::settings_pack::enable_upnp, false);
    sp.set_bool(lt::settings_pack::enable_natpmp, false);
    // uTP (BitTorrent sur UDP) : l'ELF d'essai s'arrête net dès une connexion uTP sur la PS5 (16/11) — TCP seulement pour l'instant.
    sp.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    sp.set_bool(lt::settings_pack::enable_incoming_utp, false);
    sp.set_int(lt::settings_pack::alert_mask, lt::alert_category::error | lt::alert_category::status | lt::alert_category::peer | lt::alert_category::connect);
    lt::session ses(sp);
    lt::add_torrent_params atp;
    atp.ti = std::make_shared<lt::torrent_info>(out);
    atp.save_path = dir;
    atp.flags |= lt::torrent_flags::seed_mode;
    lt::torrent_handle h = ses.add_torrent(atp);
    std::printf("semé sur le port %s\n", port.c_str());
    const std::string to = argc > 4 ? argv[4] : "";
    int tick = 0;
    for (;;) {
        if (!to.empty() && tick++ % 5 == 0 && h.status().num_peers == 0) {
            const auto c = to.rfind(':');
            lt::error_code ec;
            const auto addr = lt::make_address(to.substr(0, c), ec);
            if (!ec) { h.connect_peer({addr, static_cast<unsigned short>(std::atoi(to.c_str() + c + 1))}); std::printf("appel de %s\n", to.c_str()); }
        }
        std::vector<lt::alert*> alerts;
        ses.pop_alerts(&alerts);
        for (lt::alert* a : alerts)
            std::printf("alerte : %s\n", a->message().c_str());
        const lt::torrent_status st = h.status();
        std::printf("pairs %d · envoi %.1f Mo/s · envoyé %.1f Mo\n", st.num_peers, st.upload_rate / 1e6, static_cast<double>(st.total_upload) / 1e6);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
} catch (const std::exception& e) {
    std::fprintf(stderr, "erreur : %s\n", e.what());
    return 1;
}
