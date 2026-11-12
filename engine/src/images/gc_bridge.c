/*
 * Images de jeux — pont entre le moteur et le code de PS5 Game Compressor.
 *
 * Adapté de engine/third_party/game-compressor (PS5 Game Compressor v1.0.4,
 * juma-sayeh, GPL-3.0) : gc_job.c (g_job, job_*) et gc_diag.c (gc_log,
 * gc_checkpoint) réécrits pour le moteur — pas de fichier journal propre, pas
 * de gestionnaire de signaux, pas de serveur web. Les fonctions de calcul
 * (pfs_compress.c, pfs_decompress.c, pfs_repair.c…) sont compilées telles
 * quelles et n'appellent que ce qui est défini ici.
 */
#include "images/gc_bridge.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "transfer_internal.h"
#include "zlib.h"

int pbc_gc_zlib_level = 6;

struct job_state g_job = {
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .busy = 0,
  .cancel = 0,
};

static void (*g_logger)(const char *line) = NULL;
static char g_checkpoint_name[128] = "idle";

void
pbc_gc_set_logger(void (*fn)(const char *line)) {
  g_logger = fn;
}

/* --- Journal (remplace gc_diag.c) ------------------------------------- */

void
gc_log(const char *fmt, ...) {
  char body[1024];
  va_list ap;
  if(!g_logger) return;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt ? fmt : "", ap);
  va_end(ap);
  g_logger(body);
}

void
gc_checkpoint(const char *checkpoint) {
  snprintf(g_checkpoint_name, sizeof(g_checkpoint_name), "%s",
           checkpoint ? checkpoint : "unknown");
  gc_log("checkpoint=%s", g_checkpoint_name);
}

/* --- zlib sans allocateur par défaut (Z_SOLO) -------------------------- */

static voidpf
pbc_zalloc(voidpf opaque, uInt items, uInt size) {
  (void)opaque;
  return calloc(items, size);
}

static void
pbc_zfree(voidpf opaque, voidpf address) {
  (void)opaque;
  free(address);
}

int
pbc_gc_deflateInit_(z_streamp strm, int level, const char *version,
                    int stream_size) {
  if(strm && !strm->zalloc) {
    strm->zalloc = pbc_zalloc;
    strm->zfree = pbc_zfree;
    strm->opaque = Z_NULL;
  }
  return deflateInit_(strm, level, version, stream_size);
}

int
pbc_gc_compress2(unsigned char *dest, unsigned long *dest_len,
                 const unsigned char *source, unsigned long source_len,
                 int level) {
  z_stream s;
  int rc;
  memset(&s, 0, sizeof(s));
  s.zalloc = pbc_zalloc;
  s.zfree = pbc_zfree;
  rc = deflateInit_(&s, level, ZLIB_VERSION, (int)sizeof(s));
  if(rc != Z_OK) return rc;
  s.next_in = (Bytef *)source;
  s.avail_in = (uInt)source_len;
  s.next_out = dest;
  s.avail_out = (uInt)*dest_len;
  rc = deflate(&s, Z_FINISH);
  *dest_len = s.total_out;
  deflateEnd(&s);
  if(rc == Z_STREAM_END) return Z_OK;
  return rc == Z_OK ? Z_BUF_ERROR : rc;
}

/* --- État de travail (remplace gc_job.c) ------------------------------- */

void
job_set_current(const char *path) {
  pthread_mutex_lock(&g_job.lock);
  snprintf(g_job.current, sizeof(g_job.current), "%s", path ? path : "");
  pthread_mutex_unlock(&g_job.lock);
}

void
job_set_phase(const char *phase, long step, long count, const char *current) {
  pthread_mutex_lock(&g_job.lock);
  if(strcmp(g_job.phase, phase ? phase : "") != 0) {
    g_job.phase_started_at = time(NULL);
  }
  snprintf(g_job.phase, sizeof(g_job.phase), "%s", phase ? phase : "");
  snprintf(g_job.current, sizeof(g_job.current), "%s", current ? current : "");
  atomic_store(&g_job.phase_step, step);
  atomic_store(&g_job.phase_count, count);
  pthread_mutex_unlock(&g_job.lock);
}

void
job_set_target(const char *path) {
  pthread_mutex_lock(&g_job.lock);
  snprintf(g_job.target, sizeof(g_job.target), "%s", path ? path : "");
  pthread_mutex_unlock(&g_job.lock);
}

int
job_cancelled(void) {
  return atomic_load(&g_job.cancel) != 0;
}

void
pbc_gc_job_begin(const char *verb) {
  /* Remise à zéro complète, comme job_begin() de Game Compressor. */
  pthread_mutex_lock(&g_job.lock);
  atomic_store(&g_job.busy, 1);
  atomic_store(&g_job.cancel, 0);
  atomic_store(&g_job.total_bytes, 0);
  atomic_store(&g_job.copied_bytes, 0);
  atomic_store(&g_job.compressed_output_bytes, 0);
  atomic_store(&g_job.raw_blocks, 0);
  atomic_store(&g_job.compressed_blocks, 0);
  atomic_store(&g_job.skipped_zlib_blocks, 0);
  atomic_store(&g_job.total_blocks, 0);
  atomic_store(&g_job.bad_blocks_found, 0);
  atomic_store(&g_job.repaired_blocks, 0);
  atomic_store(&g_job.hash_checked_blocks, 0);
  atomic_store(&g_job.hash_matched_blocks, 0);
  atomic_store(&g_job.hash_mismatched_blocks, 0);
  atomic_store(&g_job.software_compared_blocks, 0);
  atomic_store(&g_job.phase_step, 0);
  atomic_store(&g_job.phase_count, 0);
  atomic_store(&g_job.writer_wait_us, 0);
  atomic_store(&g_job.worker_wait_us, 0);
  atomic_store(&g_job.scan_bytes, 0);
  atomic_store(&g_job.scan_files, 0);
  atomic_store(&g_job.scan_dirs, 0);
  atomic_store(&g_job.scan_entries, 0);
  atomic_store(&g_job.scan_elapsed_ms, 0);
  atomic_store(&g_job.scan_workers, 0);
  atomic_store(&g_job.repair_read_bytes, 0);
  atomic_store(&g_job.repair_written_bytes, 0);
  atomic_store(&g_job.repair_copy_bytes, 0);
  atomic_store(&g_job.stream_min_free_bytes, 0);
  atomic_store(&g_job.stream_budget_bytes, 0);
  atomic_store(&g_job.stream_current_credit_bytes, 0);
  atomic_store(&g_job.stream_deleted_bytes, 0);
  atomic_store(&g_job.stream_reverse_temp_bytes, 0);
  atomic_store(&g_job.stream_forward_files, 0);
  atomic_store(&g_job.stream_reverse_files, 0);
  atomic_store(&g_job.total_files, 0);
  atomic_store(&g_job.done_files, 0);
  atomic_store(&g_job.failed_files, 0);
  atomic_store(&g_job.cancel_disabled, 0);
  atomic_store(&g_job.destructive_stream_active, 0);
  atomic_store(&g_job.rollback_requested, 0);
  snprintf(g_job.verb, sizeof(g_job.verb), "%s", verb ? verb : "");
  g_job.current[0] = 0;
  g_job.phase[0] = 0;
  g_job.target[0] = 0;
  g_job.log_path[0] = 0;
  g_job.error[0] = 0;
  g_job.cancel_disabled_reason[0] = 0;
  g_job.started_at = time(NULL);
  g_job.phase_started_at = g_job.started_at;
  g_job.ended_at = 0;
  pthread_mutex_unlock(&g_job.lock);
}

void
pbc_gc_job_end(void) {
  pthread_mutex_lock(&g_job.lock);
  g_job.ended_at = time(NULL);
  pthread_mutex_unlock(&g_job.lock);
  atomic_store(&g_job.busy, 0);
}

void
pbc_gc_job_cancel(void) {
  atomic_store(&g_job.cancel, 1);
}

int
pbc_gc_job_cancelled(void) {
  return job_cancelled();
}

static uint64_t
u64_of(long v) {
  return v > 0 ? (uint64_t)v : 0;
}

void
pbc_gc_job_status(pbc_gc_status_t *out) {
  if(!out) return;
  memset(out, 0, sizeof(*out));
  out->total_bytes = u64_of(atomic_load(&g_job.total_bytes));
  out->done_bytes = u64_of(atomic_load(&g_job.copied_bytes));
  out->total_files = (uint64_t)(atomic_load(&g_job.total_files) > 0 ? atomic_load(&g_job.total_files) : 0);
  out->done_files = (uint64_t)(atomic_load(&g_job.done_files) > 0 ? atomic_load(&g_job.done_files) : 0);
  out->total_blocks = u64_of(atomic_load(&g_job.total_blocks));
  out->compressed_blocks = u64_of(atomic_load(&g_job.compressed_blocks));
  out->raw_blocks = u64_of(atomic_load(&g_job.raw_blocks));
  out->output_bytes = u64_of(atomic_load(&g_job.compressed_output_bytes));
  out->bad_blocks = u64_of(atomic_load(&g_job.bad_blocks_found));
  out->repaired_blocks = u64_of(atomic_load(&g_job.repaired_blocks));
  out->hash_checked_blocks = u64_of(atomic_load(&g_job.hash_checked_blocks));
  out->hash_mismatched_blocks = u64_of(atomic_load(&g_job.hash_mismatched_blocks));
  out->destructive_active = atomic_load(&g_job.destructive_stream_active);
  pthread_mutex_lock(&g_job.lock);
  snprintf(out->phase, sizeof(out->phase), "%s", g_job.phase);
  snprintf(out->current, sizeof(out->current), "%s", g_job.current);
  pthread_mutex_unlock(&g_job.lock);
}

void
pbc_gc_job_set_progress(uint64_t done, uint64_t total) {
  atomic_store(&g_job.copied_bytes, done > (uint64_t)LONG_MAX ? LONG_MAX : (long)done);
  atomic_store(&g_job.total_bytes, total > (uint64_t)LONG_MAX ? LONG_MAX : (long)total);
}

void
pbc_gc_job_set_step(const char *phase, const char *current) {
  job_set_phase(phase, 0, 0, current);
}
