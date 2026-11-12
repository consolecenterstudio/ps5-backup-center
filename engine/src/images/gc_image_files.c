/*
 * Lecture (seule) des fichiers d'une image de jeu — voir gc_image_files.h.
 *
 * pfs_ampr_hotswap.c (PS5 Game Compressor v1.0.4, juma-sayeh, GPL-3.0) est inclus TEL QUEL : ses fonctions
 * internes (lecteur exFAT, ouverture PFSC, lecture logique) sont réutilisées ici, sans les modifier ; nous
 * n'appelons aucune de ses fonctions d'écriture.
 */
#include "pfs_ampr_hotswap.c"

#include "images/gc_image_files.h"

struct pbc_img {
  int is_pfsc;
  int fd;
  hs_pfsc_image_t pfsc;
  hs_exfat_t ex;
};

typedef struct pbc_entry {
  int is_dir;
  uint32_t first;
  uint8_t flags;
  uint64_t len;
} pbc_entry_t;

int
pbc_img_open(const char *path, pbc_img **out, char *err, size_t err_size) {
  *out = NULL;
  pbc_img *img = calloc(1, sizeof(*img));
  if(!img) { hs_set_err(err, err_size, "out of memory"); return -1; }
  img->fd = -1;
  img->pfsc.fd = -1;
  size_t n = strlen(path);
  if(n > 7 && !strcasecmp(path + n - 7, ".ffpfsc")) {
    img->is_pfsc = 1;
    if(hs_pfsc_open(path, &img->pfsc, err, err_size) != 0 ||
       hs_exfat_open_reader(hs_pfsc_read_logical_cb, &img->pfsc, &img->ex, err, err_size) != 0) {
      pbc_img_close(img);
      return -1;
    }
  } else {
    img->fd = open(path, O_RDONLY);
    if(img->fd < 0) { hs_set_err(err, err_size, "open image: %s", strerror(errno)); pbc_img_close(img); return -1; }
    if(hs_exfat_open_reader(hs_fd_read_cb, &img->fd, &img->ex, err, err_size) != 0) {
      pbc_img_close(img);
      return -1;
    }
  }
  *out = img;
  return 0;
}

void
pbc_img_close(pbc_img *img) {
  if(!img) return;
  if(img->is_pfsc) hs_pfsc_close(&img->pfsc);
  if(img->fd >= 0) close(img->fd);
  free(img);
}

/* Parcourt un dossier ; si `want` est donné, s'arrête sur cette entrée (insensible à la casse). */
static int
pbc_walk_dir(pbc_img *img, const pbc_entry_t *dir, const char *want, pbc_entry_t *found,
             void (*cb)(void *, const char *, int, uint64_t), void *ctx, char *err, size_t err_size) {
  const hs_exfat_t *ex = &img->ex;
  uint64_t dir_size = 0;
  if(hs_exfat_dir_size(ex, dir->first, dir->len, dir->flags, &dir_size, err, err_size) != 0) return -1;
  unsigned char entry[32];
  for(uint64_t off = 0; off + 32ULL <= dir_size;) {
    if(hs_exfat_read_stream(ex, dir->first, dir->flags, off, entry, sizeof(entry), err, err_size) != 0) return -1;
    if(entry[0] == 0x00) break;
    if(entry[0] != 0x85) { off += 32ULL; continue; }
    size_t entry_count = (size_t)entry[1] + 1;
    if(entry_count < 3 || entry_count > 19 || off + entry_count * 32ULL > dir_size) {
      hs_set_err(err, err_size, "invalid exFAT directory entry set");
      return -1;
    }
    unsigned char set[32 * 19];
    if(hs_exfat_read_stream(ex, dir->first, dir->flags, off, set, entry_count * 32U, err, err_size) != 0) return -1;
    const unsigned char *stream = set + 32;
    off += entry_count * 32ULL;
    if(stream[0] != 0xC0) continue;
    char name[64];
    int rc = hs_exfat_decode_name(set, entry_count, stream[3], name, sizeof(name), err, err_size);
    if(rc != 0) continue;  /* nom non pris en charge : ignoré */
    pbc_entry_t e;
    e.is_dir = (hs_rd16(set + 4) & 0x10) != 0;
    e.flags = stream[1];
    e.first = hs_rd32(stream + 20);
    e.len = hs_rd64(stream + 24);
    if(want) {
      if(!strcasecmp(name, want)) { *found = e; return 0; }
    } else if(cb) {
      cb(ctx, name, e.is_dir, e.len);
    }
  }
  if(want) { hs_set_err(err, err_size, "not found: %s", want); errno = ENOENT; return -1; }
  return 0;
}

static int
pbc_resolve(pbc_img *img, const char *path, pbc_entry_t *out, char *err, size_t err_size) {
  pbc_entry_t cur = { 1, img->ex.root_cluster, 0, 0 };
  char seg[64];
  const char *p = path;
  while(*p) {
    while(*p == '/') p++;
    if(!*p) break;
    size_t n = 0;
    while(p[n] && p[n] != '/') n++;
    if(n == 0 || n >= sizeof(seg)) { hs_set_err(err, err_size, "bad path"); return -1; }
    memcpy(seg, p, n);
    seg[n] = 0;
    if(!cur.is_dir) { hs_set_err(err, err_size, "not a directory"); return -1; }
    pbc_entry_t next;
    if(pbc_walk_dir(img, &cur, seg, &next, NULL, NULL, err, err_size) != 0) return -1;
    cur = next;
    p += n;
  }
  *out = cur;
  return 0;
}

int
pbc_img_list(pbc_img *img, const char *dir, void (*cb)(void *, const char *, int, uint64_t), void *ctx,
             char *err, size_t err_size) {
  pbc_entry_t d;
  if(pbc_resolve(img, dir ? dir : "", &d, err, err_size) != 0) return -1;
  if(!d.is_dir) { hs_set_err(err, err_size, "not a directory"); return -1; }
  return pbc_walk_dir(img, &d, NULL, NULL, cb, ctx, err, err_size);
}

int
pbc_img_read(pbc_img *img, const char *file, uint64_t off, void *buf, size_t size, size_t *got, uint64_t *file_size,
             char *err, size_t err_size) {
  pbc_entry_t f;
  *got = 0;
  if(pbc_resolve(img, file, &f, err, err_size) != 0) return -1;
  if(f.is_dir) { hs_set_err(err, err_size, "is a directory"); return -1; }
  if(file_size) *file_size = f.len;
  if(off >= f.len) return 0;
  if(size > f.len - off) size = (size_t)(f.len - off);
  if(size && hs_exfat_read_stream(&img->ex, f.first, f.flags, off, buf, size, err, err_size) != 0) return -1;
  *got = size;
  return 0;
}
