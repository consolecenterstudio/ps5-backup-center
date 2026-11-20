# ampr-emu-ng — provenance (PS5 Backup Center)

- Projet : https://github.com/quer3q/ampr-emu-ng, version **v0.4.1** (archive de la version publiée,
  SHA-256 29cc14692bd42189da144fb3b506eb0069d9fc366ef78ad00fc399fbcf9efaad), copiée **telle quelle**, sans modification.
- Licence : GPL-3.0-or-later (LICENSE). Hard fork de drakmor/ampr_emu.
- Rôle : la bibliothèque `libSceAmpr.sprx` glissée dans un jeu lancé depuis son .pkg (remplace la bibliothèque système
  par l'unionfs du bac à sable) ; elle lit et décompresse le .pkg pendant la partie. Voir docs/PLAN-lancement-kraken.md.
- **Compilée par nous**, jamais un binaire tout fait : `build-system/build_ampr.sh` (leur `make verify`, notre kit PS5,
  LLVM 18, Rust 1.100). Premier essai (19/11) : `payload_prx_ok exports=137 definitions=141 imports=103`,
  732 336 octets (celui livré avec Kraken Mount v0.3.0, construit avec LLVM 19 / Rust 1.99 / kit v0.43 : 733 104 octets).
