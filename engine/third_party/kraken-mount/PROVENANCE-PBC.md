# Kraken Mount — provenance (PS5 Backup Center)

- Projet : https://github.com/quer3q/kraken-mount, version **v0.3.0** (archive du tag, SHA-256
  6a9e6ec5994cb820fd38233223fd22125276b04250c2886d04b2c97c94c869b5), copiée telle quelle **sauf** :
  - **retirés** : les bibliothèques précompilées `vendor/ampr-emu-ng/libSceAmpr.sprx`, `vendor/dlc_emu/*.sprx`,
    `vendor/pgo_stub/libScePlayGo.sprx` (règle du propriétaire : jamais un binaire tout fait ; on les compile nous-mêmes
    depuis engine/third_party/ampr-emu-ng, dlc_emu, pgo_stub — build-system/build_injected.sh).
- Licence : GPL-3.0-or-later (LICENSE ; THIRD_PARTY_NOTICES).
- Rôle : on reprend la construction du cache de démarrage et des index AMPR d'un .pkg (src/cache.rs, src/ampr_index.rs,
  crates/kraken-mount-pkg) pour lancer un jeu depuis son .pkg dans notre moteur (docs/PLAN-lancement-kraken.md). Le reste
  (interface web, port 8097, mise à jour GitHub, tuile) n'est pas utilisé.
