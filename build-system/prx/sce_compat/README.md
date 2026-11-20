# sce_compat — nos en-têtes à la place de ceux du kit officiel de Sony (pour compiler dlc_emu avec notre kit libre)

Écrits par nous (GPL-3.0-or-later, comme le projet). Seulement ce que dlc_emu 0.3 emploie. Chaque valeur a une source :
- **[bin]** relevé dans le code machine du module publié par drakmor (dlc_emu 0.3, compilé avec les en-têtes officiels),
  désassemblé (docs/PLAN-lancement-kraken.md, « dlc_emu — dispositions relevées ») ;
- **[A]** tailles vérifiées d'AnyPS5 (core/libs/SceTypes.hpp, static_assert) — lu comme référence d'interface ;
- **[S]** shadPS4 (src/core/libraries/app_content/app_content_error.h) ;
- **[d]** dlc_emu lui-même (ses propres #define de repli) ; **[F]** valeurs FreeBSD.
Une valeur jamais renvoyée par le code compilé (code retiré par le compilateur) est marquée « sans effet ».
