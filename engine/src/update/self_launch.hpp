#pragma once
#include <string>

namespace pbc {

// Lance un ELF (notre moteur) dans un nouveau processus, sans chargeur externe ni port : le code d'elfldr
// (John Törnblom, GPL-3.0, engine/third_party/elfldr) compilé dans le moteur. PS5 seulement.
// Renvoie le PID du nouveau processus, ou -1 (error dit pourquoi).
int spawn_elf(const std::string& elf, const char* procname, std::string& error);

// Au tout début du moteur, avant tout fil : reprendre ses droits élevés (racine, hors « prison », tous les droits), comme
// quand Arsenal le lançait. Les chargeurs du débridage (9021, Payload Manager, l'elfldr d'origine) rendent au programme
// ses droits ordinaires, et le moteur plantait alors (pile des fils trop petite). Donne aussi au processus son vrai nom
// (« pbc-engine2.elf », pas « payload.elf »). Renvoie un résumé pour le journal.
std::string raise_own_privileges();

}  // namespace pbc
