# libtorrent-rasterbar 2.0.11 (BSD 3 clauses) — figé pour PS5 Backup Center

Source : https://github.com/arvidn/libtorrent/releases/download/v2.0.11/libtorrent-rasterbar-2.0.11.tar.gz
SHA-256 de l'archive : f0db58580f4f29ade6cc40fa4ba80e2c9a70c90265cd77332d3cdec37ecf1e6d
Copié tel quel : include/, src/, deps/try_signal/, licences, CMakeLists.txt (listes de sources lues par build-system/build.py).
Aucune modification. Ce qui manque à la PS5 (getrandom) est dans engine/src/bt/ps5_compat.* (écrit par nous).
Compilé avec exceptions et RTTI (lui seulement) ; le reste du moteur l'appelle par engine/src/bt/bt.hpp.
