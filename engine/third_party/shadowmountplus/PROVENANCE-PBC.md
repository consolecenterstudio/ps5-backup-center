# ShadowMountPlus — provenance (PS5 Backup Center)

- Projet : https://github.com/drakmor/ShadowMountPlus, version **1.7beta12** (archive du tag, SHA-256
  873bd42266f050d1fde36396a14489d7b9dac298308016bbb7407e1a2ab9b542), copiée **telle quelle**.
- Retiré : `src/pkg_helper_asset.c` — un programme ELF déjà compilé, rangé en tableau d'octets (son installeur de .pkg,
  dont on n'a pas besoin) : jamais de binaire tout fait chez nous.
- Licence : GPL-3.0 (LICENSE) ; bibliothèques citées dans `licenses/`.
- Rôle chez nous (docs/PLAN-integration-shadowmount.md) : **notre ShadowMount intégré**, en repli du ShadowMountPlus
  installé (Paramètres › Montage des jeux) — montage des images et des dossiers, inscription des jeux. Compilé par nous
  avec le moteur ; **sans** ses crochets dans SceShellCore, **sans** son interface web ni son API (port 10101), **sans** son
  serveur de paquets ni la mise à jour d'ampr-emu depuis Internet.
