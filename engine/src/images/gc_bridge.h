/*
 * Images de jeux — pont entre le moteur et le code de PS5 Game Compressor.
 *
 * Adapté de engine/third_party/game-compressor (PS5 Game Compressor v1.0.4,
 * juma-sayeh, GPL-3.0) : remplace gc_job.c / gc_diag.c (état de travail global
 * g_job, journal) par notre propre état, lu par les tâches du moteur. Aucun
 * serveur, aucun port, aucun second processus : seul le code de calcul
 * (pfs_*.c, miniz, index AMPR) est compilé dans le moteur.
 */
#ifndef PBC_GC_BRIDGE_H
#define PBC_GC_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Instantané de l'état du travail en cours (copie de g_job). */
typedef struct pbc_gc_status {
  uint64_t total_bytes;
  uint64_t done_bytes;
  uint64_t total_files;
  uint64_t done_files;
  uint64_t total_blocks;
  uint64_t compressed_blocks;
  uint64_t raw_blocks;
  uint64_t output_bytes;
  uint64_t bad_blocks;
  uint64_t repaired_blocks;
  uint64_t hash_checked_blocks;
  uint64_t hash_mismatched_blocks;
  int destructive_active; /* suppression pendant l'écriture commencée : plus d'annulation */
  char phase[32];
  char current[512];
} pbc_gc_status_t;

extern int pbc_gc_zlib_level;

/* Journal : chaque ligne de Game Compressor est transmise ici (NULL : silence). */
void pbc_gc_set_logger(void (*fn)(const char *line));

/* Un seul travail à la fois (état global partagé par le code d'origine). */
void pbc_gc_job_begin(const char *verb);
void pbc_gc_job_end(void);
void pbc_gc_job_cancel(void);
int pbc_gc_job_cancelled(void);
void pbc_gc_job_status(pbc_gc_status_t *out);
/* Progression fixée par notre code (copie d'arborescence, test de lecture…). */
void pbc_gc_job_set_progress(uint64_t done, uint64_t total);
void pbc_gc_job_set_step(const char *phase, const char *current);

#ifdef __cplusplus
}
#endif

#endif
