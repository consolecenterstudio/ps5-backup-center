# pgo_stub — provenance (PS5 Backup Center)

- Projet : https://github.com/drakmor/pgo_stub, version **0.6** (archive du tag, SHA-256
  48e331ac3304dc42b942e74765adbf9c52cb6badd3252b6d8c5ea4173e6feb3b), copiée **telle quelle**.
- Licence : GPL-3.0 (LICENSE).
- Rôle : `libScePlayGo.sprx`, glissée dans un jeu lancé depuis son .pkg (comme Kraken Mount) : elle répond « tout est
  installé » aux jeux qui attendent une installation par morceaux (PlayGo).
- **Compilée par nous** avec notre kit libre (le projet d'origine utilise le kit officiel de Sony, qu'on n'utilise pas) :
  `build-system/build_injected.sh` → prospero-clang++ + `build-system/prx/build_prx.py` (chaîne d'ampr-emu-ng, nom de
  module en paramètre). Contrôle (19/11) : nos 24 identifiants d'export sont tous dans le module publié 0.6.
