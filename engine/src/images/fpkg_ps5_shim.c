/* Paquets FPKG sur PS5 : le strict nécessaire pour lier notre bibliothèque Rust (engine/fpkg/pbc-fpkg) au moteur.
 * Repris de ps5-ai-cli (platform/freebsd11.c, GPL-3.0-or-later, https://github.com/saawant12/ps5-ai-cli, eb1d494), tel
 * que Dump Forge l'emploie ; seules les fonctions que la bibliothèque demande sont gardées, et rien d'autre du moteur
 * n'est enveloppé (pas de --wrap) : le reste du moteur garde exactement ses fonctions habituelles.
 *
 * - La std Rust est construite pour l'ABI FreeBSD 11 (stat de 120 octets, dirent de 264), celle du noyau de la PS5,
 *   et demande ces fonctions sous leur nom versionné « @FBSD_1.0 » ; le SDK les exporte sans version : on transmet.
 * - pthread_setname_np : la PS5 n'a que l'ancien nom (pthread_set_name_np).
 * Compilé seulement pour la PS5 (build.py) ; sur le Mac, les fonctions du système suffisent. */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

int pbc_fbsd11_stat(const char *path, struct stat *buffer) { return stat(path, buffer); }
int pbc_fbsd11_fstat(int fd, struct stat *buffer) { return fstat(fd, buffer); }
int pbc_fbsd11_lstat(const char *path, struct stat *buffer) { return lstat(path, buffer); }
struct dirent *pbc_fbsd11_readdir(DIR *directory) { return readdir(directory); }
__asm__(".symver pbc_fbsd11_stat,stat@FBSD_1.0");
__asm__(".symver pbc_fbsd11_fstat,fstat@FBSD_1.0");
__asm__(".symver pbc_fbsd11_lstat,lstat@FBSD_1.0");
__asm__(".symver pbc_fbsd11_readdir,readdir@FBSD_1.0");
/* Lancement depuis un .pkg (engine/kraken/pbc-kraken, code de Kraken Mount) : disques, table des montages, kqueue. Mêmes
 * structures FreeBSD 11 que celles du noyau de la PS5 et du SDK. */
int pbc_fbsd11_statfs(const char *path, struct statfs *buf) { return statfs(path, buf); }
int pbc_fbsd11_getmntinfo(struct statfs **mntbufp, int mode) { return getmntinfo(mntbufp, mode); }
int pbc_fbsd11_kevent(int kq, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents,
                      const struct timespec *timeout) { return kevent(kq, changelist, nchanges, eventlist, nevents, timeout); }
__asm__(".symver pbc_fbsd11_statfs,statfs@FBSD_1.0");
__asm__(".symver pbc_fbsd11_getmntinfo,getmntinfo@FBSD_1.0");
__asm__(".symver pbc_fbsd11_kevent,kevent@FBSD_1.0");

/* getrandom : déjà fourni au moteur par engine/src/bt/bt.cpp (libtorrent), avec arc4random_buf. */

int pthread_setname_np(pthread_t thread, const char *name) {
    pthread_set_name_np(thread, name);
    return 0;
}
