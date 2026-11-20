// Notre ShadowMount intégré (docs/PLAN-integration-shadowmount.md) : le code de ShadowMountPlus 1.7beta12
// (engine/third_party/shadowmountplus, GPL-3.0) compilé avec le moteur, démarré dans un fil du moteur à la place de
// son programme. Ce démarrage reprend celui de son main.c, sans ce qu'on ne veut pas : ni gestionnaires de signaux
// (ceux du moteur restent), ni crochets dans SceShellCore, ni interface web ni API (aucun port), ni mise à jour
// d'ampr-emu depuis Internet, ni arrêt d'autres programmes. Les images sont toujours gardées montées
// (persistent_image_mounts, imposé à la construction : sans crochets, rien ne les monterait au lancement d'un jeu).
// Mêmes dossiers que le sien (/data/shadowmount : liste manuelle, journal) ; ses réglages sont lus mais jamais modifiés
// (copie privée, voir refresh_private_config). Jamais en même temps que le ShadowMountPlus installé (engine : smp.cpp).
#include "smp_bridge.h"

#include "sm_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sm_appdb.h"
#include "sm_config_mount.h"
#include "sm_filesystem.h"
#include "sm_game_lifecycle.h"
#include "sm_image.h"
#include "sm_install.h"
#include "sm_kstuff.h"
#include "sm_log.h"
#include "sm_mount_device.h"
#include "sm_paths.h"
#include "sm_runtime.h"
#include "sm_scan.h"
#include "sm_scanner.h"
#include "sm_time.h"

#define PBC_SMP_AUTHID 0x4800000000000006ull  // celui de ShadowMountPlus (main.c, AUTHID_BASE)

extern unsigned char config_ini_example[];
extern unsigned int config_ini_example_len;

static atomic_int g_state = PBC_SMP_OFF;
static pthread_t g_thread;
static void (*g_log)(const char*) = NULL;

static void note(const char* line) {
    if (g_log) g_log(line);
}

// Ses réglages ne sont jamais touchés : notre version lit une copie dans notre dossier (chemins de sm_paths.h changés à
// la construction, build.py), refaite à chaque démarrage depuis /data/shadowmount. S'il faut convertir un config.ini v1
// au format v2, c'est la copie qui l'est ; les fichiers du ShadowMountPlus installé restent tels quels.
static bool copy_config_file(const char* from, const char* to) {
    int in = open(from, O_RDONLY);
    if (in < 0) return errno == ENOENT;
    char temp[256];
    snprintf(temp, sizeof temp, "%s.tmp", to);
    int out = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = out >= 0;
    char buf[16384];
    while (ok) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = n == 0; break; }
        for (ssize_t off = 0; ok && off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) ok = false; else off += w;
        }
    }
    close(in);
    if (out >= 0 && close(out)) ok = false;
    if (ok && rename(temp, to)) ok = false;
    if (!ok) unlink(temp);
    return ok;
}

static bool refresh_private_config(void) {
    static const char* const names[] = {"config.ini", "profiles.ini", "autotune.ini", "smb.json"};
    static const char* const ours[] = {CONFIG_FILE, PROFILES_FILE, LEGACY_AUTOTUNE_FILE, LEGACY_SMB_FILE};
    static const char* const backups[] = {"config.ini", "autotune.ini", "smb.json", "ready.ini", "profiles-v2.ini"};
    char path[256];
    (void)mkdir("/data/pbc-backup-center", 0777);
    char dir[256];
    snprintf(dir, sizeof dir, "%s", CONFIG_FILE);
    *strrchr(dir, '/') = 0;
    if (mkdir(dir, 0777) && errno != EEXIST) return false;
    for (size_t i = 0; i < sizeof backups / sizeof backups[0]; ++i) {  // l'ancienne conversion de la copie
        snprintf(path, sizeof path, "%s/%s", CONFIG_BACKUP_DIR, backups[i]);
        (void)unlink(path);
    }
    (void)rmdir(CONFIG_BACKUP_DIR);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        (void)unlink(ours[i]);
        snprintf(path, sizeof path, "/data/shadowmount/%s", names[i]);
        if (!copy_config_file(path, ours[i])) return false;
    }
    return true;
}

// main.c : ensure_runtime_config_file — un config.ini d'exemple si aucun réglage n'existe encore.
static bool ensure_config(void) {
    const char* paths[] = {CONFIG_FILE, LEGACY_AUTOTUNE_FILE, LEGACY_SMB_FILE};
    struct stat st;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        if (lstat(paths[i], &st) == 0) return true;
        if (errno != ENOENT) return false;
    }
    char temp[] = CONFIG_FILE ".XXXXXX";
    int fd = mkstemp(temp);
    if (fd < 0) return false;
    size_t off = 0;
    int error = 0;
    while (off < config_ini_example_len) {
        ssize_t w = write(fd, config_ini_example + off, config_ini_example_len - off);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { error = errno ? errno : EIO; break; }
        off += (size_t)w;
    }
    if (!error && fsync(fd)) error = errno;
    if (close(fd) && !error) error = errno;
    if (!error && rename(temp, CONFIG_FILE)) error = errno;
    if (error) unlink(temp);
    errno = error;
    return !error;
}

// main.c : ensure_kstuff_noautomount_file — dit à kstuff de ne pas monter lui-même pendant que nous montons.
static bool ensure_noautomount(void) {
    struct stat st;
    if (stat(KSTUFF_NOAUTOMOUNT_FILE, &st) == 0) return true;
    int fd = open(KSTUFF_NOAUTOMOUNT_FILE, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) return false;
    (void)close(fd);
    return true;
}

static void* run(void* arg) {
    (void)arg;
    sceUserServiceInitialize(0);
    sceAppInstUtilInitialize();
    kernel_set_ucred_authid(-1, PBC_SMP_AUTHID);
    mkdir(LOG_DIR, 0777);
    if (!ensure_noautomount()) { note("smp : sentinelle kstuff impossible"); atomic_store(&g_state, PBC_SMP_FAILED); return NULL; }
    (void)remove(KILL_FILE);
    (void)unlink(LOG_FILE_PREV);
    (void)rename(LOG_FILE, LOG_FILE_PREV);
    if (!refresh_private_config() || !ensure_config() || !sm_config_prepare_files() || !load_runtime_config()) {
        note("smp : réglages illisibles (/data/shadowmount), rien n'est monté");
        atomic_store(&g_state, PBC_SMP_FAILED);
        return NULL;
    }
    if (!sm_scanner_init()) log_debug("  [SCAN] scanner service init incomplete");
    log_debug("ShadowMount+ v%s (intégré à PS5 Backup Center : sans crochets SceShellCore, sans API ; images gardées montées)", SHADOWMOUNT_VERSION);
    sm_notifications_init();
    while (!sm_kstuff_is_loaded()) {  // main.c : wait_for_kstuff_ready
        if (should_stop_requested()) goto shutdown;
        sceKernelUsleep(2000000);
    }
    if (!refresh_game_lifecycle_watcher()) log_debug("  [GAME] lifecycle watcher unavailable");
    if (mkdir("/system_ex/app", 0777) != 0 && errno != EEXIST) log_debug("  [MOUNT] failed to create /system_ex/app: %s", strerror(errno));
    if (remount_system_ex() != 0) log_debug("  [MOUNT] remount_system_ex failed: %s", strerror(errno));
    cleanup_mount_dirs();
    if (!wait_for_lvd_release()) goto shutdown;
    runtime_mount_state_lock();
    cleanup_staged_mount_links();
    cleanup_duplicate_title_mounts();
    runtime_mount_state_unlock();
    if (!app_db_run_startup_maintenance()) log_debug("  [DB] startup snd0info maintenance unavailable");
    atomic_store(&g_state, PBC_SMP_RUNNING);
    note("smp : notre ShadowMount intégré tourne (parcours des disques, montage, inscription)");
    if (!sm_scanner_run_startup_sync()) goto shutdown;
    sm_scanner_run_loop();

shutdown:
    stop_game_lifecycle_watcher();
    sm_scanner_shutdown();
    {
        const bool titles = shutdown_title_mounts();
        const bool images = titles && shutdown_image_mounts();
        if (titles && images && find_pid_by_name("shadowmountplus.elf", true) == 0) (void)unlink(KSTUFF_NOAUTOMOUNT_FILE);
        if (!titles || !images) log_debug("[SHUTDOWN] some image mounts or devices were not fully released");
    }
    shutdown_app_db();
    log_debug("[SHUTDOWN] notre ShadowMount intégré arrêté");
    sm_log_shutdown();
    note("smp : notre ShadowMount intégré arrêté");
    atomic_store(&g_state, PBC_SMP_STOPPED);
    return NULL;
}

int pbc_smp_start(void (*log)(const char*)) {
    int expected = PBC_SMP_OFF;
    if (!atomic_compare_exchange_strong(&g_state, &expected, PBC_SMP_STARTING)) return expected == PBC_SMP_STARTING || expected == PBC_SMP_RUNNING ? 0 : -1;
    g_log = log;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 4u << 20);  // son code met de gros tampons sur la pile (chemins, listes) ; son programme a 8 Mo
    const int r = pthread_create(&g_thread, &a, run, NULL);
    pthread_attr_destroy(&a);
    if (r != 0) { atomic_store(&g_state, PBC_SMP_FAILED); return -1; }
    return 0;
}

void pbc_smp_stop(void) {
    const int s = atomic_load(&g_state);
    if (s != PBC_SMP_STARTING && s != PBC_SMP_RUNNING) return;
    request_shutdown_stop("PS5 Backup Center");
    pthread_join(g_thread, NULL);
}

int pbc_smp_state(void) { return atomic_load(&g_state); }

void pbc_smp_scan_now(void) {
    if (atomic_load(&g_state) == PBC_SMP_RUNNING) (void)request_scan_now("PS5 Backup Center");
}
