// Essai BitTorrent : inclus avant chaque fichier de libtorrent compilé pour la PS5 (option -include).
// Déclare ce que la bibliothèque C de la PS5 n'expose pas ; l'implémentation est dans ps5_compat.cpp (écrit par nous).
#pragma once
#include <stddef.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
ssize_t getrandom(void* buf, size_t len, unsigned int flags);  // lu dans /dev/urandom, comme le moteur
#ifdef __cplusplus
}
#endif
