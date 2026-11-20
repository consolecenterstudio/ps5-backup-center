// Essai BitTorrent : ce qui manque à la bibliothèque C de la PS5 pour libtorrent (rempli au fil des erreurs).
// Écrit par nous ; aucun code repris de PS5Torrent (GPL-3).
#include "ps5_compat.hpp"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" ssize_t getrandom(void* buf, size_t len, unsigned int) {
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) { errno = ENOSYS; return -1; }  // libtorrent retombe alors sur sa lecture de /dev/urandom… qui échouera aussi : erreur claire
    size_t got = 0;
    while (got < len) {
        const ssize_t n = ::read(fd, static_cast<char*>(buf) + got, len - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    ::close(fd);
    return static_cast<ssize_t>(got);
}
