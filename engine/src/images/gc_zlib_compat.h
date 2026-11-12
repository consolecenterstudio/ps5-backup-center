/*
 * Images de jeux — compatibilité pour le code de PS5 Game Compressor.
 *
 * Adapté de engine/third_party/game-compressor (PS5 Game Compressor v1.0.4,
 * juma-sayeh, GPL-3.0). Les sources d'origine restent intactes : ce fichier est
 * inclus de force (-include) à la compilation des unités pfs_*.c.
 *
 * - Notre zlib est compilée en Z_SOLO (pas d'allocateur par défaut, pas de
 *   compress2). Game Compressor appelle deflateInit() sans allocateur et
 *   compress2() : on redirige ces deux appels vers gc_bridge.c, qui fournit
 *   malloc/free puis appelle la zlib du moteur.
 * - Le niveau zlib est une constante de compilation chez Game Compressor
 *   (GC_PFSC_ZLIB_LEVEL) : la compilation le remplace par la variable
 *   pbc_gc_zlib_level, réglée par tâche (0 à 9).
 */
#ifndef PBC_GC_ZLIB_COMPAT_H
#define PBC_GC_ZLIB_COMPAT_H

#define deflateInit_ pbc_gc_deflateInit_
#define compress2 pbc_gc_compress2

extern int pbc_gc_zlib_level;

int pbc_gc_compress2(unsigned char *dest, unsigned long *dest_len,
                     const unsigned char *source, unsigned long source_len,
                     int level);

#endif
