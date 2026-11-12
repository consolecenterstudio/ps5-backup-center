// Règle de décision de la synchro des parties actives (sync_parties.cpp), à part pour être testée seule.
#pragma once

#include <cstdint>
#include <string>

namespace pbc {

struct PartiesSide { std::int64_t at = 0, play = -1; };       // heure écrite par le jeu (0 = pas de partie), temps de jeu (-1 = inconnu)
struct PartiesDecision { std::string state, reason; };          // here_newer / there_newer / same / only_here / only_there / conflict

// known : le jeu a déjà été synchronisé (journal) ; rec_* : heures notées des deux côtés à la fin de cette synchro ;
// resolve : conflit tranché par l'utilisateur (« here » / « there » / vide).
PartiesDecision parties_decide(const PartiesSide& here, const PartiesSide& there, bool known, std::int64_t rec_here, std::int64_t rec_there, const std::string& resolve);

class Json;
// DLC / mises à jour : compare deux inventaires (/api/v2/dlc/list d'ici et de là-bas). Lecture seule, rien n'est copié.
// Par jeu : DLC qui manquent de chaque côté, version de mise à jour de chaque côté, et l'état d'ensemble.
Json dlc_compare(const Json& here, const Json& there);

}  // namespace pbc
