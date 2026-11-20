// Paquets FPKG (Kraken) : notre façade Rust (engine/fpkg/pbc-fpkg) au-dessus du code de Dump Forge / PS5Upload
// (engine/third_party/fpkg, GPL-3.0-or-later). Bibliothèque statique liée au moteur : aucun processus, aucun port.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Avancement : étape 1 lecture, 2 plan, 3 compression, 4 écriture, 5 vérification ; octets faits / total si connus.
typedef void (*pbc_fpkg_progress_fn)(void* ud, unsigned stage, uint64_t done, uint64_t total);
// `level` : 0 sans compression, 1 Fast, 2 Balanced, 3 Smallest ; `threads` 0 = tous les cœurs ; `cancel` : un booléen
// atomique d'un octet (std::atomic<bool>). Retour 0 fait (info = {"size","content_id","warnings"}), 1 annulé, 2 erreur.
int pbc_fpkg_build(const char* src, const char* out, unsigned level, unsigned threads, pbc_fpkg_progress_fn progress, void* ud,
                   const void* cancel, char* info, size_t info_len);
// Identité d'un paquet (en-tête et conteneur, sans vérifier) : info = {"content_id","title_id","title"} ; 0 lu, 2 sinon.
int pbc_fpkg_info(const char* path, char* info, size_t info_len);
// Relit et vérifie tout un .pkg. Retour 0 bon, 1 annulé, 2 mauvais ou illisible (info = la raison).
int pbc_fpkg_verify(const char* path, pbc_fpkg_progress_fn progress, void* ud, const void* cancel, char* info, size_t info_len);
#ifdef __cplusplus
}
#endif
