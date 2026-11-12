#include "update/self_launch.hpp"

#include <pthread.h>
#include <unistd.h>
#include <cstdio>

#if defined(PBCK_PS5)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
extern "C" {
#include "elfldr/elfldr.h"
}
#include "core/util.hpp"
#endif

namespace pbc {

int spawn_elf(const std::string& elf, const char* procname, std::string& error) {
#if defined(PBCK_PS5)
    const auto* p = reinterpret_cast<const std::uint8_t*>(elf.data());
    if (elfldr_sanity_check(p, elf.size()) != 0) { error = "moteur installé illisible (ELF invalide)"; return -1; }
    // Sortie du nouveau moteur : nulle part (il écrit son journal dans celui du système) ; jamais un tube fermé.
    const int out = ::open("/dev/null", O_WRONLY);
    char name[64];
    std::snprintf(name, sizeof name, "%s", procname);
    char* argv[] = {name, nullptr};
    const pid_t pid = elfldr_spawn(out, argv, p, elf.size());
    if (out >= 0) ::close(out);
    if (pid < 0) { error = "lancement du nouveau moteur impossible"; return -1; }
    log("self-launch %s pid=%d bytes=%zu", procname, static_cast<int>(pid), elf.size());
    return static_cast<int>(pid);
#else
    (void)elf; (void)procname;
    error = "relance du moteur indisponible ici";
    return -1;
#endif
}

namespace {
std::size_t default_thread_stack() {
    pthread_attr_t at;
    std::size_t stack = 0;
    if (pthread_attr_init(&at) == 0) { pthread_attr_getstacksize(&at, &stack); pthread_attr_destroy(&at); }
    return stack;
}
}  // namespace

std::string raise_own_privileges() {
    char b[160];
#if defined(PBCK_PS5)
    const int uid0 = static_cast<int>(::getuid());
    const std::size_t st0 = default_thread_stack();
    const int rc = elfldr_raise_privileges(::getpid());
    // Notre vrai nom dans la liste des processus : lancé par Payload Manager ou le 9021, le moteur s'appelait « payload.elf ».
    // Même appel que l'elfldr d'origine (elfldr_set_procname) : thr_set_name avec -1 renomme le processus.
    const int named = static_cast<int>(::syscall(SYS_thr_set_name, -1, "pbc-engine2.elf"));
    std::snprintf(b, sizeof b, "uid %d->%d thread_stack %zu->%zu raise=%d name=%d", uid0, static_cast<int>(::getuid()), st0, default_thread_stack(), rc, named);
#else
    std::snprintf(b, sizeof b, "uid %d thread_stack %zu (Mac : rien à faire)", static_cast<int>(::getuid()), default_thread_stack());
#endif
    return b;
}

}  // namespace pbc
