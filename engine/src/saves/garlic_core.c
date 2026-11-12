/*
 * Cœur de restauration des sauvegardes, compilé dans le moteur — voir garlic_core.h.
 *
 * Adapté de Garlic SaveMgr v1.15-beta12 par earthonion (https://github.com/earthonion/garlic-savemgr),
 * GPL-3.0. Source d'origine, figée et non modifiée : engine/third_party/garlic-savemgr/src/main.c
 * (et sfo_account.h). Les fonctions ci-dessous en reprennent la logique telle quelle (noms d'origine
 * conservés pour la relecture côte à côte) ; ont été retirés : serveur HTTP, interface, nuage, éditeur,
 * zip, création d'images, génération de param.sfo et empreinte HMAC des PARAMS (inutiles à l'import).
 * Différences volontaires, toutes marquées « PBC : » :
 *   - l'image source est un fichier du backup (copiée dans /data/save_files) au lieu d'un envoi HTTP ;
 *   - le montage de vérification ne recopie jamais la copie de travail sur la sauvegarde installée ;
 *   - les droits du processus (authid, capacités) sont rendus après chaque opération, le moteur restant
 *     un démon de longue durée ;
 *   - pas de copie « _ps5image_archive » (elle ne servait qu'à l'export zip de Garlic).
 */
/* PBC : nos propres points de montage et dossier de travail, distincts de ceux du garlic-savemgr d'origine
 * (/data/mount_sd, /data/mount_owner, /data/save_files) pour ne jamais démonter ou écraser son travail
 * s'il tourne en même temps. */
#define PBC_MNT_SD "/data/pbc-backup-center/mnt/sd"
#define PBC_MNT_OWNER "/data/pbc-backup-center/mnt/owner"
#define PBC_WORK "/data/pbc-backup-center/work"
#include "saves/garlic_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void (*g_log_fn)(const char *line) = NULL;

void gc_set_log(void (*fn)(const char *line)) { g_log_fn = fn; }

static void logprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logprintf(const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    size_t n = strlen(tmp);
    while (n && (tmp[n - 1] == '\n' || tmp[n - 1] == '\r')) tmp[--n] = 0;
    if (g_log_fn) g_log_fn(tmp);
}

static void set_err(char *err, size_t errsz, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void set_err(char *err, size_t errsz, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (err && errsz) snprintf(err, errsz, "%s", tmp);
    logprintf("erreur : %s", tmp);
}

#if !defined(PBCK_PS5)

/* Build hôte (Mac) : pas d'appels système PS5. */
int gc_import_encrypted(const char *image_path, const char *key_path_or_null, int ps4, const char *uid_hex,
                        gc_import_info *out, char *err, size_t errsz) {
    (void)image_path; (void)key_path_or_null; (void)ps4; (void)uid_hex;
    if (out) memset(out, 0, sizeof(*out));
    set_err(err, errsz, "non disponible sur Mac");
    return -1;
}
int gc_import_finish(const char *uid_hex, const gc_finish_options *opt, gc_finish_info *out, char *err, size_t errsz) {
    (void)uid_hex; (void)opt;
    if (out) memset(out, 0, sizeof(*out));
    set_err(err, errsz, "non disponible sur Mac");
    return -1;
}
int gc_mount_save(const char *image_path, gc_mount_info *out, char *err, size_t errsz) {
    (void)image_path;
    if (out) memset(out, 0, sizeof(*out));
    set_err(err, errsz, "non disponible sur Mac");
    return -1;
}
int gc_unmount(void) { return -1; }
void gc_startup_cleanup(void) {}
int gc_running_title(char out[16]) { out[0] = 0; return 0; }

#else

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "sqlite3.h"

/* ── SDK imports (main.c l. 34-67) ─────────────────────────────── */
typedef struct { uint8_t reserved; char *budgetid; } MountOpt;
typedef struct { uint8_t dummy; } UmountOpt;

int sceFsInitMountSaveDataOpt(MountOpt *opt);
int sceFsMountSaveData(MountOpt *opt, const char *path, const char *mount, uint8_t *key);
int sceFsInitUmountSaveDataOpt(UmountOpt *opt);
int sceFsUmountSaveData(UmountOpt *opt, const char *mount, int handle, int ignore);
int sceFsTransactionSaveDataReadParam(const char *path, void *buf, uint32_t size);
int sceFsTransactionSaveDataWriteParam(const char *path, const void *buf, uint32_t size);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
void sceLncUtilGetAppTitleId(unsigned int app_id, char *title_id);
int sceRegMgrGetBin(int, void *, size_t);
int sceRegMgrGetStr(int, char *, size_t);

#define BUF_SIZE        (512 * 1024)
#define MAX_PATH_LEN    1024

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

static char g_mounted_path[MAX_PATH_LEN] = {0};
static char g_mount_point[MAX_PATH_LEN] = {0};
static int g_mounted = 0;
static char g_local_copy[MAX_PATH_LEN] = {0};
static char g_local_key[MAX_PATH_LEN] = {0};  /* PBC : clé PS4 copiée pour un montage de vérification */
static int g_enc_ps4 = 0;
static uint8_t g_mount_real_sfo[0x8000];
static uint32_t g_mount_real_sfo_len = 0;

/* ── SFO types (main.c l. 1327) ─────────────────────────────────── */
typedef struct sfo_info {
    char title_id[16];
    char dir_name[64];
    char main_title[128];
    char sub_title[128];
    char detail[256];
    char game_title_id[16];
    uint32_t user_param;
    uint32_t list_param;
    int has_list_param;
    uint32_t blocks;
    uint64_t account_id;
    uint32_t user_id;
    uint32_t aid_offset;
    uint32_t uid_offset;
    char format[4];
} sfo_info_t;

static int sfo_is_ps4(const sfo_info_t *sfo, int fallback) {
    if (strncmp(sfo->title_id, "CUSA", 4) == 0) return 1;
    if (strncmp(sfo->title_id, "PPSA", 4) == 0) return 0;
    if (strcmp(sfo->format, "obs") == 0 || strcmp(sfo->format, "ob") == 0) return 1;
    if (strcmp(sfo->format, "ppr") == 0) return 0;
    return fallback;
}

/* ── Droits du processus ─────────────────────────────────────────── */
/* PBC : Garlic élève ses droits une fois pour toutes ; le moteur les rend après chaque opération. */
static uint64_t g_saved_authid = 0;
static uint8_t g_saved_caps[16];
static int g_creds_saved = 0;

static void creds_save(void) {
    if (g_creds_saved) return;
    g_saved_authid = kernel_get_ucred_authid(getpid());
    kernel_get_ucred_caps(getpid(), g_saved_caps);
    g_creds_saved = 1;
}

static void creds_restore(void) {
    if (!g_creds_saved || g_mounted) return;
    kernel_set_ucred_authid(getpid(), g_saved_authid);
    kernel_set_ucred_caps(getpid(), g_saved_caps);
    g_creds_saved = 0;
}

static void elevate(void) {
    kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);
    uint8_t caps[16];
    kernel_get_ucred_caps(getpid(), caps);
    caps[7] |= 0x40;
    kernel_set_ucred_caps(getpid(), caps);
    setuid(0);
}

/* ── Compte du registre (main.c l. 215) ──────────────────────────── */
static int get_account_id_for_user(const char *uid_hex, uint64_t *account_id) {
    char upath[MAX_PATH_LEN];
    snprintf(upath, sizeof(upath), "/user/home/%s/username.dat", uid_hex);
    char username[17] = {0};
    int fd = open(upath, O_RDONLY);
    if (fd >= 0) {
        if (read(fd, username, 16) < 0) username[0] = 0;
        username[16] = 0;
        close(fd);
    }
    if (!username[0]) return -1;
    for (int i = 1; i <= 16; i++) {
        int key_name = (i - 1) * 65536 + 125829632;
        char reg_name[32] = {0};
        if (sceRegMgrGetStr(key_name, reg_name, sizeof(reg_name)) < 0) continue;
        if (!reg_name[0]) continue;
        if (strcmp(username, reg_name) == 0) {
            int key_aid = (i - 1) * 65536 + 125830400;
            uint64_t aid = 0;
            if (sceRegMgrGetBin(key_aid, &aid, sizeof(aid)) < 0) return -1;
            logprintf("registre : utilisateur '%s' = compte %d, aid=%llx", username, i, (unsigned long long)aid);
            *account_id = aid;
            return 0;
        }
    }
    logprintf("registre : aucun compte pour l'utilisateur '%s'", username);
    return -1;
}

/* ── Fichiers (main.c l. 1470, 2771) ─────────────────────────────── */
static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0777);
    if (dfd < 0) { int saved_errno = errno; close(sfd); errno = saved_errno; return -2; }
    fchmod(dfd, 0777);
    char *buf = malloc(BUF_SIZE);
    if (!buf) { close(sfd); close(dfd); errno = ENOMEM; return -3; }
    int result = 0, saved_errno = 0;
    for (;;) {
        ssize_t n = read(sfd, buf, BUF_SIZE);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { result = -4; saved_errno = errno; break; }
        if (n == 0) break;
        ssize_t offset = 0;
        while (offset < n) {
            ssize_t written = write(dfd, buf + offset, (size_t)(n - offset));
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                result = -5; saved_errno = written < 0 ? errno : EIO;
                break;
            }
            offset += written;
        }
        if (result < 0) break;
    }
    free(buf);
    close(sfd);
    if (close(dfd) < 0 && result == 0) { result = -5; saved_errno = errno; }
    if (result < 0) errno = saved_errno;
    return result;
}

/* PBC : Garlic tourne avec umask(0) ; on garde des dossiers créés en 0777 sans toucher au umask du moteur. */
static void mkdir_0777(const char *path) {
    if (mkdir(path, 0777) == 0) chmod(path, 0777);
}

static void recursive_mkdir(const char *path) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir_0777(tmp);
            *p = '/';
        }
    }
    mkdir_0777(tmp);
}

static unsigned count_entries(const char *base) {
    DIR *d = opendir(base);
    if (!d) return 0;
    unsigned n = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char p[MAX_PATH_LEN];
        snprintf(p, sizeof(p), "%s/%s", base, ent->d_name);
        struct stat st;
        if (stat(p, &st) < 0) continue;
        ++n;
        if (S_ISDIR(st.st_mode)) n += count_entries(p);
    }
    closedir(d);
    return n;
}

/* ── SFO (main.c l. 2147, 2232, 2525, 2545, 2559) ────────────────── */
static int parse_sfo_mem(const uint8_t *buf, size_t size, sfo_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (!buf || size < 20) return -1;
    int all_zero = 1;
    size_t check_len = size < 256 ? size : 256;
    for (size_t i = 0; i < check_len; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    if (all_zero) return -2;
    if (buf[0] != 0x00 || buf[1] != 0x50 || buf[2] != 0x53 || buf[3] != 0x46)
        return -1;
    uint32_t key_off, data_off, n_entries;
    memcpy(&key_off, buf + 8, 4);
    memcpy(&data_off, buf + 12, 4);
    memcpy(&n_entries, buf + 16, 4);
    if (n_entries > (size - 20) / 16 || key_off >= size || data_off > size)
        return -1;
    for (uint32_t i = 0; i < n_entries; i++) {
        const uint8_t *idx = buf + 20 + i * 16;
        uint16_t koff;
        uint32_t param_len, doff;
        memcpy(&koff, idx, 2);
        memcpy(&param_len, idx + 4, 4);
        memcpy(&doff, idx + 12, 4);
        uint64_t key_pos = (uint64_t)key_off + koff;
        uint64_t val_pos = (uint64_t)data_off + doff;
        if (key_pos >= size || val_pos >= size) continue;
        size_t key_avail = size - (size_t)key_pos;
        const char *key = (const char *)(buf + key_pos);
        if (strnlen(key, key_avail) == key_avail) continue;
        size_t val_avail = size - (size_t)val_pos;
        uint32_t n = param_len <= val_avail ? param_len : (uint32_t)val_avail;
        const uint8_t *val = buf + val_pos;
        if (strcmp(key, "TITLE_ID") == 0)
            snprintf(info->title_id, sizeof(info->title_id), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "SAVEDATA_DIRECTORY") == 0)
            snprintf(info->dir_name, sizeof(info->dir_name), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "MAINTITLE") == 0 || strcmp(key, "TITLE") == 0)
            snprintf(info->main_title, sizeof(info->main_title), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "SUBTITLE") == 0)
            snprintf(info->sub_title, sizeof(info->sub_title), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "DETAIL") == 0)
            snprintf(info->detail, sizeof(info->detail), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "SAVEDATA_BLOCKS") == 0 && n >= 4)
            memcpy(&info->blocks, val, 4);
        else if (strcmp(key, "PARAMS") == 0 || strcmp(key, "USER_PARAM") == 0) {
            if (n >= 4) memcpy(&info->user_param, val, 4);
        }
        else if (strcmp(key, "ACCOUNT_ID") == 0 && n >= 8)
            memcpy(&info->account_id, val, 8);
        else if (strcmp(key, "USER_ID") == 0 && n >= 4)
            memcpy(&info->user_id, val, 4);
        else if (strcmp(key, "FORMAT") == 0)
            snprintf(info->format, sizeof(info->format), "%.*s", (int)n, (const char *)val);
        else if (strcmp(key, "SAVEDATA_LIST_PARAM") == 0 && n >= 4) {
            memcpy(&info->list_param, val, 4);
            info->has_list_param = 1;
        }
    }
    if (!info->has_list_param)
        info->list_param = info->user_param;
    if (!info->game_title_id[0])
        strncpy(info->game_title_id, info->title_id, sizeof(info->game_title_id) - 1);
    return 0;
}

static int parse_sfo(const char *path, sfo_info_t *info) {
    memset(info, 0, sizeof(*info));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 20) { close(fd); return -1; }
    uint8_t *buf = malloc((size_t)st.st_size);
    if (!buf) { close(fd); return -1; }
    if (read(fd, buf, (size_t)st.st_size) != st.st_size) { free(buf); close(fd); return -1; }
    close(fd);
    int ret = parse_sfo_mem(buf, (size_t)st.st_size, info);
    free(buf);
    return ret;
}

static int sfo_bytes_valid(const uint8_t *buf, size_t size) {
    if (!buf || size < 20) return 0;
    if (buf[0] != 0x00 || buf[1] != 'P' || buf[2] != 'S' || buf[3] != 'F') return 0;
    int all_zero = 1;
    size_t check_len = size < 256 ? size : 256;
    for (size_t i = 0; i < check_len; i++)
        if (buf[i]) { all_zero = 0; break; }
    if (all_zero) return 0;
    uint32_t koff, doff, nent;
    memcpy(&koff, buf + 8, 4);
    memcpy(&doff, buf + 12, 4);
    memcpy(&nent, buf + 16, 4);
    if (nent == 0 || nent > 64) return 0;
    if (koff >= size || doff >= size) return 0;
    if (20 + (uint64_t)nent * 16 > size) return 0;
    return 1;
}

static int install_sfo_file(const char *path, const uint8_t *buf, uint32_t len) {
    if (!path || !buf || !len) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    ssize_t w = write(fd, buf, len);
    close(fd);
    if (w != (ssize_t)len) return -1;
    logprintf("param.sfo réel installé (%u o) -> %s", len, path);
    return 0;
}

static int read_effective_param_sfo(const char *sfo_path, uint8_t *buf, uint32_t bufsize, int *from_blob) {
    if (from_blob) *from_blob = 0;
    if (g_mount_real_sfo_len && sfo_bytes_valid(g_mount_real_sfo, g_mount_real_sfo_len)) {
        uint32_t n = g_mount_real_sfo_len <= bufsize ? g_mount_real_sfo_len : bufsize;
        memcpy(buf, g_mount_real_sfo, n);
        if (from_blob) *from_blob = 1;
        return (int)n;
    }
    if (!sfo_path) return 0;
    int fd = open(sfo_path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, bufsize);
    close(fd);
    if (n <= 0) return -1;
    if (!sfo_bytes_valid(buf, (size_t)n)) return 0;
    return (int)n;
}

/* ── Propriétaire du param.sfo (sfo_account.h) ───────────────────── */
static int sfo_field_offset(int fd, const char *name, size_t within, size_t length, off_t *result) {
    struct stat st;
    uint8_t header[20];
    if (fstat(fd, &st) < 0 || st.st_size < 20 ||
        pread(fd, header, sizeof(header), 0) != (ssize_t)sizeof(header) ||
        memcmp(header, "\0PSF", 4)) return -1;
    uint32_t keys, data, count;
    memcpy(&keys, header + 8, 4);
    memcpy(&data, header + 12, 4);
    memcpy(&count, header + 16, 4);
    if (count > ((uint64_t)st.st_size - 20) / 16 ||
        keys < 20 + (uint64_t)count * 16 || keys >= data || data > (uint64_t)st.st_size) return -1;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t entry[16];
        uint16_t key;
        uint32_t size, capacity, offset;
        if (pread(fd, entry, sizeof(entry), 20 + (off_t)i * 16) != (ssize_t)sizeof(entry)) return -1;
        memcpy(&key, entry, 2);
        memcpy(&size, entry + 4, 4);
        memcpy(&capacity, entry + 8, 4);
        memcpy(&offset, entry + 12, 4);
        uint64_t key_pos = (uint64_t)keys + key;
        uint64_t value_pos = (uint64_t)data + offset;
        if (key_pos >= data || size > capacity || value_pos > (uint64_t)st.st_size ||
            capacity > (uint64_t)st.st_size - value_pos) return -1;
        size_t key_len = strlen(name) + 1;
        char key_name[32];
        if (key_len > sizeof(key_name) || key_len > data - key_pos) continue;
        if (pread(fd, key_name, key_len, (off_t)key_pos) != (ssize_t)key_len) return -1;
        if (memcmp(key_name, name, key_len)) continue;
        if (within > size || length > size - within) return -1;
        *result = (off_t)(value_pos + within);
        return 0;
    }
    return -1;
}

static int patch_sfo_account(const char *path, const uint8_t account[8], const uint32_t *uid) {
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    off_t account_offset, user_offset = 0;
    uint8_t check[8];
    int ret = -1;
    if (sfo_field_offset(fd, "ACCOUNT_ID", 0, 8, &account_offset) < 0) goto done;
    if (uid && sfo_field_offset(fd, "USER_ID", 0, 4, &user_offset) < 0 &&
        sfo_field_offset(fd, "PARAMS", 4, 4, &user_offset) < 0) goto done;
    if (pwrite(fd, account, 8, account_offset) != 8) goto done;
    if (uid && pwrite(fd, uid, 4, user_offset) != 4) goto done;
    if (fsync(fd) < 0 || pread(fd, check, 8, account_offset) != 8 ||
        memcmp(check, account, 8)) goto done;
    if (uid && (pread(fd, check, 4, user_offset) != 4 || memcmp(check, uid, 4))) goto done;
    ret = 0;
done:
    close(fd);
    return ret;
}

static int sfo_read_owner_params(const char *path, const uint8_t account[8], uint32_t uid, uint8_t params[1024]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    off_t account_offset, params_offset;
    uint8_t stored_account[8];
    uint32_t stored_uid;
    int ret = -1, nonzero = 0;
    if (sfo_field_offset(fd, "ACCOUNT_ID", 0, 8, &account_offset) < 0 ||
        sfo_field_offset(fd, "PARAMS", 0, 1024, &params_offset) < 0 ||
        pread(fd, stored_account, 8, account_offset) != 8 || memcmp(stored_account, account, 8) ||
        pread(fd, params, 1024, params_offset) != 1024) goto done;
    memcpy(&stored_uid, params + 4, 4);
    for (int i = 8; i < 40; i++) nonzero |= params[i];
    if (stored_uid == uid && nonzero) ret = 0;
done:
    close(fd);
    return ret;
}

static int patch_sfo_owner_params(const char *path, const uint8_t account[8], uint32_t uid, const uint8_t params[1024]) {
    uint32_t template_uid;
    memcpy(&template_uid, params + 4, 4);
    if (template_uid != uid) return -1;
    int fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    off_t account_offset, params_offset;
    uint8_t check[1024];
    int ret = -1;
    if (sfo_field_offset(fd, "ACCOUNT_ID", 0, 8, &account_offset) < 0 ||
        sfo_field_offset(fd, "PARAMS", 0, 1024, &params_offset) < 0) goto done;
    if (pwrite(fd, account, 8, account_offset) != 8 ||
        pwrite(fd, params, 1024, params_offset) != 1024 || fsync(fd) < 0) goto done;
    if (pread(fd, check, 8, account_offset) != 8 || memcmp(check, account, 8) ||
        pread(fd, check, 1024, params_offset) != 1024 || memcmp(check, params, 1024)) goto done;
    ret = 0;
done:
    close(fd);
    return ret;
}

static int patch_sfo_owner_identity(const char *path, const uint8_t account[8], uint32_t uid, const uint8_t donor[1024]) {
    uint8_t params[1024];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    off_t offset;
    int valid = sfo_field_offset(fd, "PARAMS", 0, 1024, &offset) == 0 &&
                pread(fd, params, sizeof(params), offset) == sizeof(params);
    close(fd);
    if (!valid) return -1;
    memcpy(params + 4, donor + 4, 36);
    return patch_sfo_owner_params(path, account, uid, params);
}

/* ── Bloc PS5PARAM (main.c l. 1596-1781) ─────────────────────────── */
static int ps5param_enable_blob_mode(const char *img, uint64_t *saved) {
    if (!img || !img[0] || !saved) return -1;
    *saved = 0;
    int fd = open(img, O_RDWR);
    if (fd < 0) return -errno;
    uint8_t type = 0;
    uint8_t raw[8];
    if (pread(fd, &type, 1, 0) != 1 || type != 0x02 || pread(fd, raw, 8, 0x368) != 8) {
        close(fd);
        return -1;
    }
    uint64_t cur;
    memcpy(&cur, raw, 8);
    *saved = cur;
    uint32_t lo;
    memcpy(&lo, raw, 4);
    if ((lo & 0xFF) == 0x60) { close(fd); return 0; }
    uint32_t mode32 = 0x60;
    if (pwrite(fd, &mode32, 4, 0x368) != 4) { close(fd); return -1; }
    fsync(fd);
    close(fd);
    logprintf("PS5PARAM : mode bloc activé (0x368 lo=0x60, avant 0x%08x) sur %s", lo, img);
    return 1;
}

static int ps5param_read_sfo(const char *img, uint8_t *sfo, uint32_t sfomax) {
    if (!img || !img[0] || !sfo || sfomax < 20) return -1;
    static uint8_t blob[0x8000];
    memset(blob, 0, sizeof(blob));
    elevate();
    int r = sceFsTransactionSaveDataReadParam(img, blob, sizeof(blob));
    if (r < 0) return r;
    uint64_t magic;
    uint32_t ver, sz, off;
    memcpy(&magic, blob, 8);
    memcpy(&ver, blob + 8, 4);
    memcpy(&sz, blob + 12, 4);
    memcpy(&off, blob + 16, 4);
    if (magic != 0x4D41524150355350ULL) return 0;
    if (ver != 0 || sz == 0 || sz > sfomax || off < 20 || (uint64_t)off + sz > sizeof(blob)) {
        logprintf("PS5PARAM : en-tête de bloc invalide ver=%u size=%u off=%u", ver, sz, off);
        return -1002;
    }
    memcpy(sfo, blob + off, sz);
    return (int)sz;
}

static int ps5param_write_sfo(const char *img, const uint8_t *sfo, uint32_t sz) {
    if (!img || !img[0] || !sfo || sz < 20 || sz > sizeof(g_mount_real_sfo) - 20)
        return -1;
    static uint8_t blob[0x8000];
    memset(blob, 0, sizeof(blob));
    uint64_t magic = 0x4D41524150355350ULL;
    uint32_t ver = 0, off = 20;
    memcpy(blob, &magic, 8);
    memcpy(blob + 8, &ver, 4);
    memcpy(blob + 12, &sz, 4);
    memcpy(blob + 16, &off, 4);
    memcpy(blob + 20, sfo, sz);
    logprintf("PS5PARAM : écriture de %u o de SFO -> %s", sz, img);
    sfo_info_t wr_info;
    if (parse_sfo_mem(sfo, sz, &wr_info) == 0)
        logprintf("PS5PARAM SFO écrit : TITLE_ID=%s DIR=%s BLOCKS=%u FORMAT=%s ACCOUNT_ID=%016llX USER_ID=0x%x LIST_PARAM=%u",
                  wr_info.title_id, wr_info.dir_name, wr_info.blocks, wr_info.format,
                  (unsigned long long)wr_info.account_id, wr_info.user_id, wr_info.list_param);
    elevate();
    uint64_t saved_mode = 0;
    int r = sceFsTransactionSaveDataWriteParam(img, blob, sizeof(blob));
    if (r < 0) {
        int en = ps5param_enable_blob_mode(img, &saved_mode);
        if (en == 1) {
            logprintf("PS5PARAM : nouvel essai après activation du mode bloc");
            r = sceFsTransactionSaveDataWriteParam(img, blob, sizeof(blob));
            if (r < 0) {
                int fd = open(img, O_RDWR);
                if (fd >= 0) {
                    if (pwrite(fd, &saved_mode, 8, 0x368) != 8) logprintf("PS5PARAM : mode bloc non rétabli");
                    close(fd);
                }
            }
        }
    }
    return r;
}

static void ps5param_capture_for_image(const char *img) {
    g_mount_real_sfo_len = 0;
    if (!img || !img[0]) return;
    struct stat st;
    if (stat(img, &st) < 0 || !S_ISREG(st.st_mode)) return;
    int r = ps5param_read_sfo(img, g_mount_real_sfo, sizeof(g_mount_real_sfo));
    if (r > 0) {
        g_mount_real_sfo_len = (uint32_t)r;
        logprintf("PS5PARAM : param.sfo réel en cache (%d o) depuis %s", r, img);
        sfo_info_t blob_info;
        if (parse_sfo_mem(g_mount_real_sfo, g_mount_real_sfo_len, &blob_info) == 0)
            logprintf("PS5PARAM SFO du bloc : TITLE_ID=%s DIR=%s BLOCKS=%u FORMAT=%s ACCOUNT_ID=%016llX USER_ID=0x%x",
                      blob_info.title_id, blob_info.dir_name, blob_info.blocks, blob_info.format,
                      (unsigned long long)blob_info.account_id, blob_info.user_id);
    } else if (r < 0) {
        logprintf("PS5PARAM : aucun bloc lu dans %s (%d)", img, r);
    }
}

/* ── Clé et montage (main.c l. 1858-1978, 2006-2143) ─────────────── */
/* Déchiffre la clé scellée (PS4 : fichier .bin voisin « <dossier>.bin » ; PS5 : 0x800 de l'image). */
static int decrypt_sealed_key(const char *path, int is_ps4, uint8_t decrypted_key[0x20], char *err, size_t errsz) {
    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;
    uint8_t *data = malloc(0x100);  /* l'ioctl peut écrire au-delà : tampon sur le tas */
    if (!data) return -10;
    memset(data, 0, 0x100);
    int key_ok = 0;
    if (is_ps4) {
        const char *savename = bname + 6;
        char dir[MAX_PATH_LEN];
        strncpy(dir, path, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = 0;
        char *slash = strrchr(dir, '/');
        if (slash) *(slash + 1) = 0; else dir[0] = 0;
        char bin_path[MAX_PATH_LEN];
        snprintf(bin_path, sizeof(bin_path), "%s%s.bin", dir, savename);
        int fd = open(bin_path, O_RDONLY);
        if (fd < 0) {
            set_err(err, errsz, "clé PS4 .bin introuvable (%s, errno=%d)", bin_path, errno);
            free(data);
            return -4;
        }
        ssize_t r = read(fd, data, 0x60);
        close(fd);
        if (r != 0x60) {
            set_err(err, errsz, "clé PS4 .bin incomplète : %zd octets sur 96", r);
            free(data);
            return -5;
        }
        logprintf("montage : clé scellée PS4 %s (keyset=%d)", bin_path, (data[9] << 8) | data[8]);
    } else {
        int fd = open(path, O_RDONLY);
        if (fd < 0) { set_err(err, errsz, "image illisible (errno=%d)", errno); free(data); return -4; }
        ssize_t r = pread(fd, data, 0x60, 0x800);
        close(fd);
        if (r != 0x60) { set_err(err, errsz, "clé de l'image illisible"); free(data); return -5; }
        logprintf("montage : clé PS5 de %s (keyset=%d)", path, (data[9] << 8) | data[8]);
    }
    int pfsmgr = open("/dev/pfsmgr", O_RDWR);
    if (pfsmgr >= 0) {
        int ret = ioctl(pfsmgr, 0xc0845302, data);
        close(pfsmgr);
        if (ret >= 0) {
            memcpy(decrypted_key, data + 0x60, 0x20);
            key_ok = 1;
            logprintf("montage : clé %s déchiffrée", is_ps4 ? "PS4" : "PS5");
        } else {
            logprintf("montage : déchiffrement de la clé refusé (ret=%d, errno=%d)", ret, errno);
        }
    } else {
        logprintf("montage : /dev/pfsmgr inaccessible (errno=%d)", errno);
        if (!is_ps4) { set_err(err, errsz, "/dev/pfsmgr inaccessible"); free(data); return -6; }
    }
    free(data);
    if (!key_ok) logprintf("montage : clé non déchiffrée pour %s (essai avec une clé nulle, comme Garlic)", path);
    return 0;
}

static int image_is_ps4(const char *path) {
    int is_ps4 = 0;
    int hfd = open(path, O_RDONLY);
    if (hfd >= 0) {
        uint8_t hdr;
        if (pread(hfd, &hdr, 1, 0) == 1 && hdr == 0x01) is_ps4 = 1;
        close(hfd);
    }
    return is_ps4;
}

static int do_mount(const char *path, const char *mount_point, uint8_t key[0x20]) {
    recursive_mkdir(mount_point);  /* PBC : points de montage sous /data/pbc-backup-center/mnt */
    mkdir_0777(mount_point);
    MountOpt mopt;
    memset(&mopt, 0, sizeof(mopt));
    sceFsInitMountSaveDataOpt(&mopt);
    mopt.budgetid = "system";
    /* Garlic remet SIGPIPE par défaut pendant le montage, puis l'ignore de nouveau (comme le moteur). */
    void (*old)(int) = signal(SIGPIPE, SIG_DFL);
    int ret = sceFsMountSaveData(&mopt, path, mount_point, key);
    signal(SIGPIPE, old == SIG_ERR ? SIG_IGN : old);
    return ret;
}

static int unmount_save(void) {
    if (!g_mounted) return -1;
    UmountOpt uopt;
    memset(&uopt, 0, sizeof(uopt));
    sceFsInitUmountSaveDataOpt(&uopt);
    sceFsUmountSaveData(&uopt, g_mount_point, 0, 0);
    sync();
    /* PBC : Garlic recopie ici la copie de travail sur la sauvegarde d'origine (édition). Le moteur ne fait
     * que relire : la copie est supprimée, l'original n'est jamais réécrit. */
    if (g_local_copy[0]) unlink(g_local_copy);
    if (g_local_key[0]) unlink(g_local_key);
    g_mounted = 0;
    g_mounted_path[0] = 0;
    g_mount_point[0] = 0;
    g_local_copy[0] = 0;
    g_local_key[0] = 0;
    g_mount_real_sfo_len = 0;
    return 0;
}

static int mount_by_path_at(const char *path, const char *mount_point, int track, char *err, size_t errsz) {
    if (track && g_mounted) unmount_save();
    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;
    int is_ps4 = image_is_ps4(path);
    if (track) g_enc_ps4 = is_ps4;
    logprintf("mount_by_path : %s détectée %s", bname, is_ps4 ? "PS4" : "PS5");
    elevate();
    uint8_t decrypted_key[0x20];
    memset(decrypted_key, 0, sizeof(decrypted_key));
    int kr = decrypt_sealed_key(path, is_ps4, decrypted_key, err, errsz);
    if (kr < 0) return kr;
    if (!is_ps4) ps5param_capture_for_image(path);
    int ret = do_mount(path, mount_point, decrypted_key);
    if (ret >= 0) {
        logprintf("montage : %s monté sur %s (handle=%d)", path, mount_point, ret);
        if (track) {
            snprintf(g_mount_point, sizeof(g_mount_point), "%s", mount_point);
            snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", path);
            g_local_copy[0] = 0;
            g_mounted = 1;
        }
        return 0;
    }
    set_err(err, errsz, "montage refusé : 0x%x (errno=%d)", (unsigned)ret, errno);
    return ret < 0 ? ret : -1;
}

/* ── Propriétaire PS4 / PS5 (main.c l. 2252-2341) ────────────────── */
/* PBC : Garlic cherche le modèle PS4 dans sa liste de sauvegardes ; ici on parcourt directement
 * /user/home/<uid>/savedata (mêmes filtres : image PS4, pas de sdimg_sce_bu_, même utilisateur). */
static int patch_local_sfo_owner(const char *path, const char *uid_hex, const uint8_t account[8], uint32_t uid, int is_ps4) {
    static int template_mounted = 0;
    if (!is_ps4) return patch_sfo_account(path, account, &uid);
    if (template_mounted) return -1;
    sfo_info_t incoming;
    if (parse_sfo(path, &incoming) < 0) return -1;
    uint8_t params[1024];
    const char *image = PBC_WORK "/sdimg__owner_template";
    const char *key = PBC_WORK "/_owner_template.bin";
    const char *point = PBC_MNT_OWNER;
    char root[MAX_PATH_LEN];
    snprintf(root, sizeof(root), "/user/home/%s/savedata", uid_hex);
    for (int pass = 0; pass < 3; pass++) {
        DIR *titles = opendir(root);
        if (!titles) break;
        struct dirent *te;
        while ((te = readdir(titles))) {
            if (te->d_name[0] == '.') continue;
            char tdir[MAX_PATH_LEN];
            snprintf(tdir, sizeof(tdir), "%s/%s", root, te->d_name);
            DIR *td = opendir(tdir);
            if (!td) continue;
            struct dirent *se;
            while ((se = readdir(td))) {
                if (strncmp(se->d_name, "sdimg_", 6) != 0 || strncmp(se->d_name, "sdimg_sce_bu_", 13) == 0) continue;
                const char *dir = se->d_name + 6;
                char save_path[MAX_PATH_LEN];
                snprintf(save_path, sizeof(save_path), "%s/%s", tdir, se->d_name);
                struct stat sst;
                if (stat(save_path, &sst) < 0 || !S_ISREG(sst.st_mode) || !image_is_ps4(save_path)) continue;
                int same_title = strcmp(te->d_name, incoming.title_id) == 0;
                int rank = same_title ? (strcmp(dir, incoming.dir_name) == 0 ? 0 : 1) : 2;
                if (rank != pass) continue;
                char key_src[MAX_PATH_LEN];
                snprintf(key_src, sizeof(key_src), "%s/%s.bin", tdir, dir);
                recursive_mkdir(PBC_WORK); mkdir_0777(PBC_WORK);
                if (copy_file(save_path, image) < 0 || copy_file(key_src, key) < 0) {
                    unlink(image); unlink(key); continue;
                }
                if (mount_by_path_at(image, point, 0, NULL, 0) < 0) {
                    unlink(image); unlink(key); continue;
                }
                template_mounted = 1;
                char template_sfo[MAX_PATH_LEN];
                snprintf(template_sfo, sizeof(template_sfo), "%s/sce_sys/param.sfo", point);
                sfo_info_t donor;
                int valid = parse_sfo(template_sfo, &donor) == 0 &&
                    strcmp(donor.title_id, te->d_name) == 0 &&
                    strcmp(donor.dir_name, dir) == 0 &&
                    sfo_read_owner_params(template_sfo, account, uid, params) == 0;
                UmountOpt opt;
                memset(&opt, 0, sizeof(opt));
                sceFsInitUmountSaveDataOpt(&opt);
                if (sceFsUmountSaveData(&opt, point, 0, 0) < 0) { closedir(td); closedir(titles); return -1; }
                template_mounted = 0;
                unlink(image); unlink(key);
                if (!valid) continue;
                closedir(td);
                closedir(titles);
                int patched = pass == 0 ? patch_sfo_owner_params(path, account, uid, params) :
                                          patch_sfo_owner_identity(path, account, uid, params);
                if (patched < 0) return -1;
                logprintf("propriétaire PS4 %s depuis %s/%s pour l'utilisateur %x",
                          pass == 0 ? "rétabli" : "partiellement retrouvé (non vérifié)", donor.title_id, donor.dir_name, uid);
                return pass == 0 ? 0 : 1;
            }
            closedir(td);
        }
        closedir(titles);
    }
    logprintf("aucun modèle de propriétaire PS4 pour %s/%s et l'utilisateur %x", incoming.title_id, incoming.dir_name, uid);
    return -2;
}

static int patch_import_sfo_owner(const char *path, uint64_t original_aid, const char *uid_hex, uint32_t uid,
                                  int is_ps4, uint64_t *local_aid) {
    if (get_account_id_for_user(uid_hex, local_aid) < 0) {
        *local_aid = original_aid;
        return 4;
    }
    int result = patch_local_sfo_owner(path, uid_hex, (const uint8_t *)local_aid, uid, is_ps4);
    if (result >= 0) return result;
    if (patch_sfo_account(path, (const uint8_t *)local_aid, &uid) == 0) return 2;
    if (patch_sfo_account(path, (const uint8_t *)local_aid, NULL) == 0) return 2;
    *local_aid = original_aid;
    sfo_info_t actual;
    if (parse_sfo(path, &actual) == 0) *local_aid = actual.account_id;
    return 3;
}

static const char *import_owner_warning(int result) {
    switch (result) {
        case 1: return "Owner identity recovered from another local save. Full ownership could not be verified; the game may report corrupt data.";
        case 2: return "Full ownership metadata unavailable. Imported with basic account/user changes only; the game may report corrupt data.";
        case 3: return "Ownership changes could not be verified. Imported anyway; the game may report corrupt data.";
        case 4: return "Selected user Account ID could not be read. Imported with original ownership; the game may report corrupt data.";
        default: return "";
    }
}

/* ── Démarrage : montages laissés par une exécution précédente (main.c l. 9456) ── */
static void init_once(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    struct stat st0;
    const char *points[] = {PBC_MNT_SD, PBC_MNT_OWNER};
    for (int i = 0; i < 2; i++) {
        if (stat(points[i], &st0) != 0) continue;
        UmountOpt u0;
        memset(&u0, 0, sizeof(u0));
        sceFsInitUmountSaveDataOpt(&u0);
        if (sceFsUmountSaveData(&u0, points[i], 0, 0) == 0) logprintf("démarrage : sauvegarde restée montée sur %s démontée", points[i]);
    }
}

static int valid_uid(const char *uid_hex) {
    if (!uid_hex || !uid_hex[0] || strlen(uid_hex) > 8) return 0;
    for (const char *p = uid_hex; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return 0;
    return 1;
}

/* ── API ────────────────────────────────────────────────────────── */
static const char *const TMP_PS5 = PBC_WORK "/_tmp_import";
static const char *const TMP_PS4 = PBC_WORK "/sdimg__tmp_import";
static const char *const TMP_KEY = PBC_WORK "/_tmp_import.bin";

static void remove_import_tmp(void) {
    unlink(TMP_PS5);
    unlink(TMP_PS4);
    unlink(TMP_KEY);
}

/* POST /api/import_encrypted (main.c l. 6426-6547). */
int gc_import_encrypted(const char *image_path, const char *key_path_or_null, int ps4, const char *uid_hex,
                        gc_import_info *out, char *err, size_t errsz) {
    memset(out, 0, sizeof(*out));
    if (err && errsz) err[0] = 0;
    if (!image_path || !valid_uid(uid_hex)) { set_err(err, errsz, "paramètres invalides"); return -1; }
    pthread_mutex_lock(&g_mtx);
    init_once();
    creds_save();
    int rc = -1;
    if (g_mounted) unmount_save();
    recursive_mkdir(PBC_WORK); mkdir_0777(PBC_WORK);
    /* PBC : l'image vient du backup au lieu du corps de la requête HTTP. */
    const char *tmp = ps4 ? TMP_PS4 : TMP_PS5;
    remove_import_tmp();
    if (copy_file(image_path, tmp) < 0) {
        set_err(err, errsz, "copie de l'image vers /data/save_files impossible (errno=%d)", errno);
        unlink(tmp);
        goto out;
    }
    chmod(tmp, 0755);
    if (ps4) {
        if (!key_path_or_null || copy_file(key_path_or_null, TMP_KEY) < 0) {
            set_err(err, errsz, "clé PS4 .bin absente ou illisible");
            remove_import_tmp();
            goto out;
        }
    }
    logprintf("import_encrypted : %s copiée (ps4=%d)", image_path, ps4);

    char merr[256] = {0};
    int ret = mount_by_path_at(tmp, PBC_MNT_SD, 1, merr, sizeof(merr));
    if (ret < 0) {
        unlink(tmp);
        set_err(err, errsz, "montage impossible : %d (%s)", ret, merr);
        goto out;
    }
    char sfo_path[MAX_PATH_LEN];
    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
    if (!ps4 && g_mount_real_sfo_len && sfo_bytes_valid(g_mount_real_sfo, g_mount_real_sfo_len))
        install_sfo_file(sfo_path, g_mount_real_sfo, g_mount_real_sfo_len);
    sfo_info_t sfo;
    int sfo_ret = parse_sfo(sfo_path, &sfo);
    if (sfo_ret == -2) {
        /* param.sfo à zéro : l'image reste montée, l'appelant décide (Garlic : doImportFinish). */
        logprintf("import_encrypted : param.sfo à zéro");
        out->sfo_zeroed = 1;
        out->match = 1;
        out->ps4 = ps4;
        rc = 0;
        goto out;
    }
    if (sfo_ret < 0) {
        unmount_save(); unlink(tmp);
        set_err(err, errsz, "param.sfo illisible");
        goto out;
    }
    if (!sfo.title_id[0] || !sfo.dir_name[0]) {
        unmount_save(); unlink(tmp);
        set_err(err, errsz, "param.sfo sans TITLE_ID ni SAVEDATA_DIRECTORY");
        goto out;
    }
    g_enc_ps4 = sfo_is_ps4(&sfo, g_enc_ps4);
    logprintf("import_encrypted : FORMAT=%s -> %s", sfo.format, g_enc_ps4 ? "PS4" : "PS5");

    uint64_t user_aid = 0;
    int user_aid_known = get_account_id_for_user(uid_hex, &user_aid) == 0;
    int match = !user_aid_known || (sfo.account_id == user_aid);
    char exist_path[MAX_PATH_LEN];
    snprintf(exist_path, sizeof(exist_path), "/user/home/%s/%s/%s/sdimg_%s",
             uid_hex, g_enc_ps4 ? "savedata" : "savedata_prospero", sfo.title_id, sfo.dir_name);
    struct stat ex_st;
    int exists = (stat(exist_path, &ex_st) == 0) ? 1 : 0;
    logprintf("import_encrypted : titre=%s dossier=%s save_aid=%llx user_aid=%llx match=%d exists=%d",
              sfo.title_id, sfo.dir_name, (unsigned long long)sfo.account_id, (unsigned long long)user_aid, match, exists);
    snprintf(out->title_id, sizeof(out->title_id), "%s", sfo.title_id);
    snprintf(out->dir_name, sizeof(out->dir_name), "%s", sfo.dir_name);
    snprintf(out->main_title, sizeof(out->main_title), "%s", sfo.main_title);
    out->save_account_id = sfo.account_id;
    out->user_account_id = user_aid;
    out->user_account_known = user_aid_known;
    out->match = match;
    out->exists = exists;
    out->ps4 = g_enc_ps4;
    rc = 0;
out:
    creds_restore();
    pthread_mutex_unlock(&g_mtx);
    return rc;
}

static void sql_escape(const char *src, char *dst, size_t dstsz) {
    char *d = dst;
    while (*src && d < dst + dstsz - 2) {
        if (*src == '\'') *d++ = '\'';
        *d++ = *src++;
    }
    *d = 0;
}

/* GET /api/import_finish (main.c l. 8299-8755). */
int gc_import_finish(const char *uid_hex, const gc_finish_options *opt, gc_finish_info *out, char *err, size_t errsz) {
    memset(out, 0, sizeof(*out));
    if (err && errsz) err[0] = 0;
    if (!valid_uid(uid_hex)) { set_err(err, errsz, "compte invalide"); return -1; }
    pthread_mutex_lock(&g_mtx);
    creds_save();
    int rc = -1;
    if (!g_mounted) { set_err(err, errsz, "aucune sauvegarde montée"); goto out; }

    char sfo_path[MAX_PATH_LEN];
    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
    if (g_mount_real_sfo_len && sfo_bytes_valid(g_mount_real_sfo, g_mount_real_sfo_len))
        install_sfo_file(sfo_path, g_mount_real_sfo, g_mount_real_sfo_len);
    sfo_info_t sfo;
    int sfo_ret = parse_sfo(sfo_path, &sfo);
    int force = opt && opt->force;
    if (sfo_ret == -2 && !force) {
        /* Garlic ne cherche l'entrée de base que pour une sauvegarde de sa liste ; ici db_found serait faux. */
        out->sfo_zeroed = 1;
        set_err(err, errsz, "param.sfo à zéro");
        goto out;
    }
    if (sfo_ret < 0 && !force) { set_err(err, errsz, "param.sfo illisible (sce_sys/param.sfo absent ?)"); goto out; }
    if (sfo_ret < 0 && force) {
        memset(&sfo, 0, sizeof(sfo));
        if (opt->title_id) snprintf(sfo.title_id, sizeof(sfo.title_id), "%s", opt->title_id);
        if (opt->dir_name) snprintf(sfo.dir_name, sizeof(sfo.dir_name), "%s", opt->dir_name);
        if (sfo.title_id[0]) snprintf(sfo.main_title, sizeof(sfo.main_title), "%s", sfo.title_id);
        snprintf(sfo.game_title_id, sizeof(sfo.game_title_id), "%s", sfo.title_id);
        sfo.blocks = 96;
        strcpy(sfo.format, "ppr");
    }
    if (!sfo.title_id[0] || !sfo.dir_name[0]) { set_err(err, errsz, "param.sfo sans TITLE_ID ni SAVEDATA_DIRECTORY"); goto out; }
    g_enc_ps4 = sfo_is_ps4(&sfo, g_enc_ps4);
    logprintf("import : title_id=%s dir=%s titre=%s blocks=%u FORMAT=%s %s",
              sfo.title_id, sfo.dir_name, sfo.main_title, sfo.blocks, sfo.format, g_enc_ps4 ? "(PS4)" : "(PS5)");
    uint32_t uid = (uint32_t)strtoul(uid_hex, NULL, 16);

    uint64_t local_aid = 0;
    int owner_result = patch_import_sfo_owner(sfo_path, sfo.account_id, uid_hex, uid, g_enc_ps4, &local_aid);
    const char *owner_warning = import_owner_warning(owner_result);
    if (owner_warning[0]) logprintf("AVERTISSEMENT : %s", owner_warning);
    logprintf("import : re-signé aid=%016llX uid=0x%x", (unsigned long long)local_aid, uid);
    out->owner_result = owner_result;
    out->account_id = local_aid;
    snprintf(out->warning, sizeof(out->warning), "%s", owner_warning);

    char icon_src[MAX_PATH_LEN];
    char icon_tmp[MAX_PATH_LEN] = {0};
    snprintf(icon_src, sizeof(icon_src), "%s/sce_sys/icon0.png", g_mount_point);
    struct stat icon_st;
    if (stat(icon_src, &icon_st) == 0) {
        snprintf(icon_tmp, sizeof(icon_tmp), PBC_WORK "/_tmp_icon.png");
        copy_file(icon_src, icon_tmp);
    }

    char tmp_path[MAX_PATH_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%s", g_mounted_path);

    if (g_enc_ps4) {
        struct stat key_st;
        if (stat(TMP_KEY, &key_st) < 0 || key_st.st_size < 96) {
            if (icon_tmp[0]) unlink(icon_tmp);
            set_err(err, errsz, "clé PS4 absente ou incomplète : la sauvegarde existante n'a pas été remplacée");
            goto out;
        }
    }

    static uint8_t imp_sfo[0x8000];
    uint32_t imp_sfo_len = 0;
    if (!g_enc_ps4) {
        int rfd = open(sfo_path, O_RDONLY);
        if (rfd >= 0) {
            ssize_t rn = read(rfd, imp_sfo, sizeof(imp_sfo));
            close(rfd);
            if (rn > 0 && sfo_bytes_valid(imp_sfo, (size_t)rn)) imp_sfo_len = (uint32_t)rn;
        }
    }

    unmount_save();  /* rechiffre l'image PFS */

    if (!g_enc_ps4 && imp_sfo_len) {
        int wr = ps5param_write_sfo(tmp_path, imp_sfo, imp_sfo_len);
        if (wr < 0) logprintf("AVERTISSEMENT : écriture du bloc refusée (%d) ; le vrai param.sfo reste dans le PFS", wr);
        else { out->blob_written = 1; logprintf("PS5PARAM : param.sfo importé écrit dans l'en-tête (%u o)", imp_sfo_len); }
    }
    sync();

    char sd_dir[MAX_PATH_LEN], sd_dst[MAX_PATH_LEN + 80];
    snprintf(sd_dir, sizeof(sd_dir), "/user/home/%s/%s/%s", uid_hex, g_enc_ps4 ? "savedata" : "savedata_prospero", sfo.title_id);
    snprintf(sd_dst, sizeof(sd_dst), "%s/sdimg_%s", sd_dir, sfo.dir_name);
    recursive_mkdir(sd_dir);
    logprintf("import : copie %s -> %s", tmp_path, sd_dst);
    if (copy_file(tmp_path, sd_dst) < 0) {
        unlink(tmp_path);
        if (icon_tmp[0]) unlink(icon_tmp);
        set_err(err, errsz, "copie vers %s impossible (errno=%d)", sd_dst, errno);
        goto out;
    }
    unlink(tmp_path);
    chmod(sd_dst, 0644);

    if (g_enc_ps4) {
        char bin_dst[MAX_PATH_LEN + 80];
        snprintf(bin_dst, sizeof(bin_dst), "%s/%s.bin", sd_dir, sfo.dir_name);
        if (copy_file(TMP_KEY, bin_dst) == 0) {
            chmod(bin_dst, 0644);
            logprintf("import : clé PS4 .bin -> %s", bin_dst);
        } else {
            if (icon_tmp[0]) unlink(icon_tmp);
            set_err(err, errsz, "image PS4 copiée mais clé .bin non installée (errno=%d) : import incomplet", errno);
            goto out;
        }
        unlink(TMP_KEY);
    }

    if (icon_tmp[0]) {
        char icon_dir[MAX_PATH_LEN], icon_dst[MAX_PATH_LEN + 80];
        snprintf(icon_dir, sizeof(icon_dir), "/user/home/%s/savedata_prospero_meta/user/%s", uid_hex, sfo.title_id);
        snprintf(icon_dst, sizeof(icon_dst), "%s/%s_icon0.png", icon_dir, sfo.dir_name);
        recursive_mkdir(icon_dir);
        copy_file(icon_tmp, icon_dst);
        unlink(icon_tmp);
        logprintf("import : icône -> %s", icon_dst);
    }

    snprintf(out->title_id, sizeof(out->title_id), "%s", sfo.title_id);
    snprintf(out->dir_name, sizeof(out->dir_name), "%s", sfo.dir_name);
    snprintf(out->main_title, sizeof(out->main_title), "%s", sfo.main_title);
    rc = 0;  /* l'image est installée ; un échec de la base n'est qu'un avertissement, comme chez Garlic */

    char db_path[MAX_PATH_LEN], db_dir[MAX_PATH_LEN];
    snprintf(db_dir, sizeof(db_dir), "/system_data/%s/%s/db/user", g_enc_ps4 ? "savedata" : "savedata_prospero", uid_hex);
    snprintf(db_path, sizeof(db_path), "%s/savedata.db", db_dir);
    recursive_mkdir(db_dir);
    sqlite3 *db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        logprintf("import : savedata.db inaccessible : %s", db ? sqlite3_errmsg(db) : "?");
        if (db) sqlite3_close(db);
        snprintf(out->warning, sizeof(out->warning), "Save imported but DB update failed. %s", owner_warning);
        goto out;
    }
    char *err_msg = NULL;
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS savedata ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,"
        "title_id NOT NULL, dir_name NOT NULL, main_title NOT NULL,"
        "sub_title, detail, tmp_dir_name, is_broken, user_param,"
        "blocks, free_blocks, size_kib, mtime NOT NULL,"
        "fake_broken, account_id, user_id, faked_owner,"
        "cloud_icon_url, cloud_revision, game_title_id NOT NULL, system_blocks)",
        NULL, 0, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }
    sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS savedata_index ON savedata"
        "(title_id, dir_name, user_param, size_kib, mtime, game_title_id)",
        NULL, 0, &err_msg);
    if (err_msg) { sqlite3_free(err_msg); err_msg = NULL; }

    char esc_title[256], esc_sub[256], esc_detail[512];
    sql_escape(sfo.main_title, esc_title, sizeof(esc_title));
    sql_escape(sfo.sub_title, esc_sub, sizeof(esc_sub));
    sql_escape(sfo.detail, esc_detail, sizeof(esc_detail));
    char mtime[64];
    {
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        strftime(mtime, sizeof(mtime), "%Y-%m-%dT%H:%M:%S.00Z", tm);
    }
    char check_sql[512];
    snprintf(check_sql, sizeof(check_sql), "SELECT COUNT(*) FROM savedata WHERE title_id='%s' AND dir_name='%s'",
             sfo.title_id, sfo.dir_name);
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    if (sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) exists = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    uint64_t aid_val = local_aid;
    uint32_t size_kib = sfo.blocks * 0x40;
    char sql[2048];
    if (exists) {
        snprintf(sql, sizeof(sql),
            "UPDATE savedata SET main_title='%s', sub_title='%s', detail='%s',"
            "is_broken=0, user_param=%u, blocks=%u, free_blocks=%u, size_kib=%u,"
            "mtime='%s', account_id=%lld, user_id=%u "
            "WHERE title_id='%s' AND dir_name='%s'",
            esc_title, esc_sub, esc_detail,
            sfo.list_param, sfo.blocks, 0u, size_kib,
            mtime, (long long)(int64_t)aid_val, uid,
            sfo.title_id, sfo.dir_name);
    } else {
        snprintf(sql, sizeof(sql),
            "INSERT INTO savedata (title_id, dir_name, main_title, sub_title, detail,"
            "is_broken, user_param, blocks, free_blocks, size_kib, mtime,"
            "fake_broken, account_id, user_id, faked_owner, cloud_revision,"
            "game_title_id, system_blocks) VALUES ("
            "'%s','%s','%s','%s','%s',"
            "0,%u,%u,%u,%u,'%s',"
            "0,%lld,%u,0,0,"
            "'%s',0)",
            sfo.title_id, sfo.dir_name, esc_title, esc_sub, esc_detail,
            sfo.list_param, sfo.blocks, 0u, size_kib, mtime,
            (long long)(int64_t)aid_val, uid,
            sfo.game_title_id);
    }
    out->db_existed = exists ? 1 : 0;
    if (sqlite3_exec(db, sql, NULL, 0, &err_msg) != SQLITE_OK) {
        logprintf("import : erreur de base : %s", err_msg ? err_msg : "?");
        sqlite3_free(err_msg);
        snprintf(out->warning, sizeof(out->warning), "Save imported but DB update failed. %s", owner_warning);
    } else {
        out->db_ok = 1;
        logprintf("import : base %s", exists ? "mise à jour" : "complétée");
    }
    sqlite3_close(db);

    /* Contrôle du bloc à destination (journal seulement, comme Garlic). */
    {
        int vfd = open(sd_dst, O_RDONLY);
        if (vfd >= 0) {
            uint8_t vhdr[32];
            ssize_t vr = pread(vfd, vhdr, sizeof(vhdr), 0xC00);
            if (vr >= 20) {
                uint64_t vmag;
                uint32_t vsz, voff;
                memcpy(&vmag, vhdr, 8);
                memcpy(&vsz, vhdr + 12, 4);
                memcpy(&voff, vhdr + 16, 4);
                logprintf("bloc à destination : magic=%s sfo_sz=%u sfo_off=%u",
                          vmag == 0x4D41524150355350ULL ? "PS5PARAM(OK)" : "BAD", vsz, voff);
            }
            uint64_t vmode = 0;
            if (pread(vfd, &vmode, 8, 0x368) == 8) logprintf("bloc à destination : mode@0x368 = 0x%llx", (unsigned long long)vmode);
            close(vfd);
        }
    }
out:
    creds_restore();
    pthread_mutex_unlock(&g_mtx);
    return rc;
}

/* GET /api/mount (main.c l. 1783-1978 et 5407-5506), pour une image installée. */
int gc_mount_save(const char *image_path, gc_mount_info *out, char *err, size_t errsz) {
    memset(out, 0, sizeof(*out));
    if (err && errsz) err[0] = 0;
    if (!image_path) { set_err(err, errsz, "paramètres invalides"); return -1; }
    pthread_mutex_lock(&g_mtx);
    init_once();
    creds_save();
    int rc = -1;
    if (g_mounted) unmount_save();
    const char *bname = strrchr(image_path, '/');
    bname = bname ? bname + 1 : image_path;
    int is_ps4 = image_is_ps4(image_path);
    logprintf("montage : %s détectée %s", bname, is_ps4 ? "PS4" : "PS5");
    elevate();

    /* Garlic : une image hors de /data est copiée avant montage (montage direct → EPIPE). */
    const char *mount_src = image_path;
    g_local_copy[0] = 0;
    g_local_key[0] = 0;
    if (strncmp(image_path, "/data/", 6) != 0) {
        snprintf(g_local_copy, sizeof(g_local_copy), PBC_WORK "/%s", bname);
        recursive_mkdir(PBC_WORK); mkdir_0777(PBC_WORK);
        if (copy_file(image_path, g_local_copy) < 0) {
            set_err(err, errsz, "copie de travail impossible (errno=%d)", errno);
            unlink(g_local_copy);
            g_local_copy[0] = 0;
            goto out;
        }
        chmod(g_local_copy, 0755);
        mount_src = g_local_copy;
        if (is_ps4 && strncmp(bname, "sdimg_", 6) == 0) {
            const char *savename = bname + 6;
            char src_dir[MAX_PATH_LEN], src_bin[MAX_PATH_LEN + 80];
            snprintf(src_dir, sizeof(src_dir), "%s", image_path);
            char *sl = strrchr(src_dir, '/');
            if (sl) *(sl + 1) = 0;
            snprintf(src_bin, sizeof(src_bin), "%s%s.bin", src_dir, savename);
            snprintf(g_local_key, sizeof(g_local_key), PBC_WORK "/%s.bin", savename);
            if (copy_file(src_bin, g_local_key) < 0) {
                set_err(err, errsz, "clé PS4 %s non copiée (errno=%d)", src_bin, errno);
                unlink(g_local_key); unlink(g_local_copy);
                g_local_key[0] = 0; g_local_copy[0] = 0;
                goto out;
            }
            chmod(g_local_key, 0755);
        }
    }
    uint8_t decrypted_key[0x20];
    memset(decrypted_key, 0, sizeof(decrypted_key));
    if (decrypt_sealed_key(mount_src, is_ps4, decrypted_key, err, errsz) < 0) {
        if (g_local_copy[0]) unlink(g_local_copy);
        if (g_local_key[0]) unlink(g_local_key);
        g_local_copy[0] = 0; g_local_key[0] = 0;
        goto out;
    }
    snprintf(g_mount_point, sizeof(g_mount_point), PBC_MNT_SD);
    if (!is_ps4) ps5param_capture_for_image(mount_src);
    int ret = do_mount(mount_src, g_mount_point, decrypted_key);
    if (ret < 0) {
        set_err(err, errsz, "sceFsMountSaveData : %d (0x%x), %s, errno=%d", ret, (unsigned)ret, is_ps4 ? "PS4" : "PS5", errno);
        if (g_local_copy[0]) unlink(g_local_copy);
        if (g_local_key[0]) unlink(g_local_key);
        g_local_copy[0] = 0; g_local_key[0] = 0; g_mount_point[0] = 0;
        g_mount_real_sfo_len = 0;
        goto out;
    }
    logprintf("montage OK (handle=%d)", ret);
    snprintf(g_mounted_path, sizeof(g_mounted_path), "%s", image_path);
    g_mounted = 1;

    char sfo_path[MAX_PATH_LEN];
    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", g_mount_point);
    sfo_info_t sfo_info;
    static uint8_t sfo_buf[0x8000];
    int sfo_from_blob = 0;
    int sfo_bytes = read_effective_param_sfo(sfo_path, sfo_buf, sizeof(sfo_buf), &sfo_from_blob);
    int sfo_ret = sfo_bytes > 0 ? parse_sfo_mem(sfo_buf, (size_t)sfo_bytes, &sfo_info) : (sfo_bytes == 0 ? -2 : -1);
    if (sfo_ret < 0) memset(&sfo_info, 0, sizeof(sfo_info));
    out->sfo_ok = sfo_ret == 0;
    out->sfo_zeroed = (sfo_ret == -2) && !sfo_from_blob;
    out->from_blob = sfo_from_blob;
    out->account_id = sfo_info.account_id;
    snprintf(out->title_id, sizeof(out->title_id), "%s", sfo_info.title_id);
    snprintf(out->main_title, sizeof(out->main_title), "%s", sfo_info.main_title);
    snprintf(out->mount_point, sizeof(out->mount_point), "%s", g_mount_point);
    out->files = count_entries(g_mount_point);
    if (sfo_bytes > 0) { out->sfo = sfo_buf; out->sfo_len = (unsigned)sfo_bytes; }  /* PBC : comparaison des versions */
    rc = 0;
out:
    creds_restore();
    pthread_mutex_unlock(&g_mtx);
    return rc;
}

/* GET /api/unmount (main.c l. 5967), plus le ménage des fichiers d'import. */
int gc_unmount(void) {
    pthread_mutex_lock(&g_mtx);
    creds_save();
    int ret = unmount_save();
    remove_import_tmp();
    creds_restore();
    pthread_mutex_unlock(&g_mtx);
    return ret;
}

/* PBC : au démarrage du moteur, sans attendre la première sauvegarde ouverte : démonter ce qu'une exécution précédente
 * (arrêt brutal, plantage, console éteinte pendant une lecture) a laissé monté, et effacer ses copies temporaires. */
void gc_startup_cleanup(void) {
    pthread_mutex_lock(&g_mtx);
    creds_save();
    init_once();
    remove_import_tmp();
    creds_restore();
    pthread_mutex_unlock(&g_mtx);
}

/* auto_get_running_title (main.c l. 3477). */
int gc_running_title(char out[16]) {
    out[0] = 0;
    int app_id = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app_id <= 0) return 0;
    char tid[16] = {0};
    sceLncUtilGetAppTitleId((unsigned int)app_id, tid);
    if (!tid[0]) return 0;
    snprintf(out, 16, "%s", tid);
    return 1;
}

#endif
