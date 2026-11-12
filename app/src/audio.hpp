// Son de l'app télé : effets d'interface et musique d'ambiance, mélangés sur un fil à part.
// Les appels depuis l'interface ne bloquent jamais (un drapeau atomique par son) ; le fil de mélange
// écrit des blocs de 256 trames stéréo 48 kHz vers la sortie audio de la console.
// Sur le Mac (aperçu), tout est sans effet.
#pragma once
#include <cstddef>

namespace app::audio {

enum class Sound { move, select, back, notify, success, error, count };

// Charge assets/sound-*.wav et assets/music.wav depuis `dir` dans `mem` (`bytes` octets fournis par
// l'appelant, l'arène de l'interface sur la console), ouvre la sortie audio et lance le fil de mélange.
// Si la place manque, la musique est laissée de côté ; si la sortie ne s'ouvre pas, tout reste muet.
void start(const char* dir, void* mem, std::size_t bytes);
// Réglages (Paramètres › Général) : effets, musique, volume 0-100. Pris en compte au bloc suivant.
void configure(bool ui_sounds, bool music, int volume);
// Joue un effet (ignoré si les effets sont coupés ou si le son n'a pas démarré). Ne bloque jamais.
void play(Sound s);
// Le son est-il prêt (sortie ouverte) ? Pour l'affichage seulement.
bool ready();

}  // namespace app::audio
