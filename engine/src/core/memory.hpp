// SPDX-License-Identifier: GPL-3.0-or-later
// Mémoire du moteur sur la console (jemalloc de la libc PS5) : moins de zones gardées en réserve, mémoire libérée
// rendue au système, et relevé de ce qui est vraiment occupé. Raison : la mémoire « flexible » est partagée avec le
// système ; un moteur qui garde 170 Mo empêche le lancement des jeux et applis (erreur CE-15773-3) jusqu'au redémarrage.
#pragma once
#include <cstdint>

#include "core/json.hpp"

namespace pbc::memory {

Json stats();   // {allocated_mb, active_mb, mapped_mb, purged} — vide hors console
void purge();   // rend au système la mémoire libérée (toutes les zones)

// Pic de mémoire « en usage » depuis le démarrage (octets ; 0 hors console). Sert à repérer QUI fait monter la mémoire :
// on compare le pic avant / après une opération, et on note l'opération qui bat le record.
std::uint64_t peak_inuse();
void note_peak(const char* what, std::uint64_t before_peak);  // journal si le record a monté de plus de 16 Mo

}  // namespace pbc::memory
