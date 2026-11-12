/*
 * Lecture (seule) des fichiers d'une image de jeu : .exfat, ou .ffpfsc contenant une image exFAT.
 * S'appuie sur le lecteur exFAT / PFSC de PS5 Game Compressor (pfs_ampr_hotswap.c, juma-sayeh, GPL-3.0),
 * inclus tel quel par gc_image_files.c. Sert à identifier un jeu (param.json, SDK des exécutables)
 * sans monter l'image.
 */
#ifndef PBC_GC_IMAGE_FILES_H
#define PBC_GC_IMAGE_FILES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pbc_img pbc_img;

/* Ouvre une image .exfat ou .ffpfsc (exFAT imbriqué). 0 si succès. */
int pbc_img_open(const char *path, pbc_img **out, char *err, size_t err_size);
void pbc_img_close(pbc_img *img);

/* Liste un dossier (« » = racine, « sce_module »…) : rappel par entrée. 0 si succès. */
int pbc_img_list(pbc_img *img, const char *dir, void (*cb)(void *ctx, const char *name, int is_dir, uint64_t size), void *ctx,
                 char *err, size_t err_size);

/* Lit `size` octets d'un fichier à partir de `off` (borné à sa taille) ; *got = octets lus, *file_size = taille. */
int pbc_img_read(pbc_img *img, const char *file, uint64_t off, void *buf, size_t size, size_t *got, uint64_t *file_size,
                 char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
