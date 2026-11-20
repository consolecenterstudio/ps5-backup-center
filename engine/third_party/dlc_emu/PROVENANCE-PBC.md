# dlc_emu — provenance (PS5 Backup Center)

- Projet : https://github.com/drakmor/dlc_emu, version **0.3** (archive du tag, SHA-256
  e124b010abe4af4656aa5cb282b80d442cc2f3d198340ccc9b11f195b85c5c2b), copiée **telle quelle**.
- Licence : GPL-3.0 (LICENSE).
- Rôle : `libSceAppContent.sprx`, `libSceNpEntitlementAccess.sprx`, `libSceGameUpdate.sprx`, glissées dans un jeu lancé
  depuis son .pkg (comme Kraken Mount) : les DLC du jeu sont reconnus.
- **Compilé par nous** (19/11) : ses sources telles quelles, compilées par prospero-clang++ (kit libre) avec **nos**
  déclarations à la place des en-têtes du kit officiel de Sony (`build-system/prx/sce_compat`, chaque valeur sourcée :
  AnyPS5 pour les tailles, le code machine du module publié pour les trois structures non décrites) ; modules faits par
  `build-system/prx/build_prx.py --pemd` avec ses fichiers `msbuild/pemd`. Recette : `build-system/build_injected.sh`.
  Résultat : mêmes exports que les modules publiés (41, 27 et 8 identifiants), imports tous présents dans le kit.
- Glissé dans le cache seulement si le jeu a un `dlc_emu.ini` à sa racine (comportement de Kraken Mount).
