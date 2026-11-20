# json-c — provenance (PS5 Backup Center)

- engine/third_party/json-c : https://github.com/json-c/json-c, release **json-c-0.19-20260627** (archive
  json-c-0.19.tar.gz, SHA-256 37ad0249902e301bd9052bf712e511fcc6acff4ecaad4b5900aad9ce564e26de, celle qu'attend
  ShadowMountPlus), copiée telle quelle, puis le correctif de ShadowMountPlus appliqué
  (shadowmountplus/patches/json-c-0.19-release-free.patch, repris de json-c en amont).
- Licence : MIT (json-c/COPYING).
- Ici (json-c-pbc) : `config.h`, `json_config.h`, `json.h` écrits par nous à la place de ceux que cmake générerait.
- Rôle : utilisé par notre ShadowMount intégré (fiches de jeux, app.db).
