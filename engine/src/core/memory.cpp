// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/memory.hpp"
#include "core/util.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(PBCK_PS5)
extern "C" {
struct PbcMallocManagedSize {
    unsigned short size;
    unsigned short version;
    unsigned int reserved;
    std::size_t max_system_size;
    std::size_t current_system_size;
    std::size_t max_inuse_size;
    std::size_t current_inuse_size;
};
int malloc_stats_fast(PbcMallocManagedSize* stats);                // libSceLibcInternal (malloc de Sony)
int sceKernelAvailableFlexibleMemorySize(std::size_t* size);        // mémoire flexible encore libre pour tout le système
}
#endif

namespace pbc::memory {

namespace { std::atomic<std::uint64_t> g_purges{0}; }

Json stats() {
    Json j = Json::object();
#if defined(PBCK_PS5)
    PbcMallocManagedSize m{};
    m.size = sizeof m;
    m.version = 1;
    if (malloc_stats_fast(&m) == 0) {
        j.set("system_mb", static_cast<std::int64_t>(m.current_system_size >> 20));   // pris au système par le moteur
        j.set("system_peak_mb", static_cast<std::int64_t>(m.max_system_size >> 20));
        j.set("inuse_mb", static_cast<std::int64_t>(m.current_inuse_size >> 20));      // vraiment utilisé
        j.set("inuse_peak_mb", static_cast<std::int64_t>(m.max_inuse_size >> 20));
    }
    std::size_t avail = 0;
    if (sceKernelAvailableFlexibleMemorySize(&avail) == 0) j.set("flexible_free_mb", static_cast<std::int64_t>(avail >> 20));
#endif
    j.set("purges", static_cast<std::int64_t>(g_purges.load()));
    return j;
}

void purge() { ++g_purges; }

std::uint64_t peak_inuse() {
#if defined(PBCK_PS5)
    PbcMallocManagedSize m{};
    m.size = sizeof m;
    m.version = 1;
    if (malloc_stats_fast(&m) == 0) return m.max_inuse_size;
#endif
    return 0;
}

void note_peak(const char* what, std::uint64_t before) {
    const std::uint64_t now = peak_inuse();
    if (now > before + (16u << 20)) log("mem peak +%llu Mo (record %llu Mo) pendant : %s", static_cast<unsigned long long>((now - before) >> 20), static_cast<unsigned long long>(now >> 20), what);
}  // le malloc de Sony ne rend pas la mémoire à la demande : on réduit plutôt ce qu'on prend

}  // namespace pbc::memory
