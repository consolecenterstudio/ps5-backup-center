# Essai BitTorrent (étape 0) — docs/ETUDE-bittorrent.md

Rien ici n'est dans l'appli publiée. Sources de libtorrent et Boost : `work/bt/src` (ignoré par git), vérifiées par
leur empreinte dans `build_lab.py`.

    python3 engine/lab/bt/build_lab.py host   # out/bt-lab/host/bt_seed (semeur, Mac)
    python3 engine/lab/bt/build_lab.py ps5    # out/bt-lab/ps5/bt-lab.elf (ELF d'essai)

Sur la console : `/data/pbc-bt-lab/job.txt` (torrent, semeur ip:port, coupure en Mo), lancement par
`PS5=<ip> python3 qa/launch_payload.py out/bt-lab/ps5/bt-lab.elf`, journal `lab.log`, plantage `crash.log`.
Semeur : `bt_seed <fichier> <sortie.torrent> <port> [ip:port de la PS5]` (le 4e argument : il appelle la PS5).
