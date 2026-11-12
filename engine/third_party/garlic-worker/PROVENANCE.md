# garlic-worker — provenance

- Source : https://github.com/earthonion/garlic-worker
- Commit figé : `48345a9b6e5b4fadaeb146770f7fd61733c224bf` (v1.3.9, 2026-11-07)
- Archive téléchargée : https://codeload.github.com/earthonion/garlic-worker/tar.gz/48345a9b6e5b (SHA-256 `33361d615b67f63e045a13c9e56424fbf1a6cbda74c1462667b62099d44798ba`)
- Licence : GPL-3.0 (fichier LICENSE joint, inchangé)
- Auteur : earthonion. Crédits amont : c0w-ar (rétro-ingénierie des sauvegardes PS5), hzhreal (HTOS), Team-Alua (cecie).

## Règles
- Fichiers copiés **sans modification**. Toute adaptation se fait dans engine/src/saves/ (nos fichiers), jamais ici.
- Seule la logique de sauvegarde (`src/ps4/savedata.*`, `src/ps5/savedata.*` et leurs dépendances) est destinée à être compilée. Le service en ligne (worker, http, killswitch, config) n'est pas utilisé.
- Mise à jour : uniquement volontaire. Nouveau commit → relire le diff, relancer les tests de sauvegarde sur la console, puis mettre à jour ce fichier. Jamais de « dernière version » automatique.

## Empreintes (SHA-256)

- `LICENSE` 3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986
- `Makefile` fd59ea711e1962618ee66d819e2ddf12923c23a11249fd4a356f76569ee73434
- `README.md` b9b69dc43460913b7f8d94aaf72bde7346326ec72e70c5389a3579ecf24134cd
- `src/config.c` 59ad3b702d8e974e4e16787700a9ca2b4d252cd099886cb93d76aeeb2ab4d252
- `src/config.h` babe3c1d831b16fb6e3411160d23ab3b907e0d54a332cb0cbe9e018f3fd3d7bb
- `src/http.h` b245499560ed854aa22bbedbbe54ff12307c0731552a5b3a6a94ac452943647f
- `src/json.c` 0921d74b718d1b3787004667a1fe713432173343f028c8cfc0e5fd08ede4c137
- `src/json.h` 75cf3e29de8e9722b181ac1a442a545297165a9bb8b268bd176296031bb28aaf
- `src/killswitch.c` 79b851ebdab2a6814b378670f4b13d1f77cc38ba0fbdd80230aae705d38ba318
- `src/killswitch.h` 0ab9d38fbb2f60038211791f35394081523c69a7f636c0650e07c081c3527786
- `src/log.c` 2a9da5c22b15cb73e8de187b1bfcde314d2791eb3e0ab0cc9479c6b4ee6fef96
- `src/log.h` 0c25df00482de1dea4cdfcc16322ad07db56eb9e491a98edc651817e70d9e98e
- `src/ps4/http.c` d18715cb15195bb24baec7743874b5f09c347df956fd248ea3ed4f47cc714b5e
- `src/ps4/main.c` b3e3729b31db7fe45857642d5dc6091d9ef64028b4133c84fcbbdf103f446d35
- `src/ps4/savedata.c` 574246b35c83fb1f195620bdda59a438289662f73ed342650c7c98ebcba23c7f
- `src/ps4/savedata.h` e533c54705d3b6d2b44f632f5f53c87b2b2456dac572add209bd4e31fe7b2f7f
- `src/ps4/worker.c` 77e3a9f4f277b46a461843c8759240d8560ab64407613c0ba2360f8b82c49b43
- `src/ps5/http.c` 5d08c10c0feb69849a58541a1723528f27d9af8924331c5380ed0be2e517f9bf
- `src/ps5/main.c` cb2e97a18e3de35f0a1560f81d062a2bf01865ac6ba14d0264426bc20a521a27
- `src/ps5/savedata.c` fa241980890edd36c7673c754d4b4c6369413363a53f205741a575e9d46ff9fb
- `src/ps5/savedata.h` faa9a950092b240d7b9c5f7d9aa0e4435d2d1455b4461a0eed645bc68e3eb459
- `src/ps5/worker.c` f3c8d6b42b2e469c264650ff9e1517293f71f4a0a86de3a1d3e3a481e25be255
- `src/tcp.c` 51a3ac79b2fcc5f7a81e38b525b12af270913e2bf8ce11fc384fc1b76ae76a23
- `src/tcp.h` 7090742092c5012dc37377a9cc7d0c0687465a1e0f29b8f90c30f3cf54521d87
- `src/util.c` ded8eacad7cf7d56e3daa9b61ed8c246e3718b6b50e37d4ff907f1ca7c869236
- `src/util.h` 8829405feb83d39825debee2bbababb0fba534a7658b5dc3cd6ad6852db0270a
- `src/worker.h` 90d36bf7c80378a21e32d80a20518a38e864ff8ab5bb6aaac5812f7d32584c4c
- `src/zip.c` f7de59e22f7f5fac5b5b110c9d576b82647d339c41b59e1cf728a60364ab7e89
- `src/zip.h` c9cdfa94c7e988abab52b3d9fe337673e7abe5ad03a4c861df1d64cda6c79085
