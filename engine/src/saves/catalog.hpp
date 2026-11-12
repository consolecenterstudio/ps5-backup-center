#pragma once
// Catalogue « où en est le joueur » (engine/web/catalog/progress.json, intégré au moteur) : pour un jeu connu, quel
// fichier de la sauvegarde lire et comment (lecteurs génériques : json, slots = positions fixes, gvas = Unreal Engine).
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace pbc::progress_catalog {

// Le catalogue a-t-il une entrée pour ce jeu ? Si oui, le nom (ou motif « metadata.*.json ») du fichier à lire.
bool wants(const std::string& title_id, const std::string& file_name);

// Lit l'avancée dans le contenu d'un fichier de sauvegarde : {play_time_seconds?, level?, name?, place?, …,
// "source": "catalog"} ; objet vide si rien de lisible.
Json read(const std::string& title_id, const std::string& file_name, const std::string& data);

// Le catalogue connaît-il les fichiers de partie de ce jeu (« progress_files ») ? 1 : la sauvegarde en a un ;
// 0 : aucun (seulement des réglages) ; -1 : jeu inconnu.
int has_progress_file(const std::string& title_id, const std::vector<std::string>& file_names);

// « Level_SpringMeadows_Main_V2 » → « Spring Meadows » ; « /Game/DLC/Depths/…/LVL_FloatingPlatforms » → « Depths · Floating Platforms ».
std::string pretty_place(const std::string& raw);

// Le catalogue a-t-il une entrée pour ce jeu (alors le lecteur générique ne sert pas) ?
bool known(const std::string& title_id);

// Lecteur générique, pour les jeux absents du catalogue : noms courants (temps de jeu, niveau, chapitre, lieu) dans
// une sauvegarde Unreal (GVAS) ou un fichier JSON. {…, "source": "generic", "verified": false} ou objet vide.
Json read_generic(const std::string& data);

// Relevé : noms de propriétés « intéressants » (temps, niveau, chapitre, carte…) d'une sauvegarde Unreal, avec leur
// type (« TimePlayed:DoubleProperty ») — 40 au plus. Sert à choisir les jeux à ajouter au catalogue.
Json survey(const std::string& data);

// Lecteurs exposés pour les tests.
Json read_json(const Json& entry, const std::string& data);
Json read_slots(const Json& entry, const std::string& data);
Json read_gvas(const Json& entry, const std::string& data);

}  // namespace pbc::progress_catalog
