// SPDX-License-Identifier: GPL-3.0-or-later
// Mémoire de l'app télé (remplace app/tooling/native/app_cpp_runtime.cpp pour app).
//
// Le tas système d'une app (malloc) ne fait qu'environ 13 Mo sur la console : trop juste dès que les
// réponses du moteur grossissent (2.29 : plantages). On ouvre donc une deuxième réserve, bien plus grande,
// sur de la mémoire directe que l'app réserve elle-même, gérée par l'allocateur de la console
// (sceLibcMspace*, libSceLibcInternal). Tout « new » (chaînes, JSON, listes) y va ; le tas système ne sert
// plus qu'au démarrage et en secours. heap::stats() dit combien est utilisé, pour le journal.
#include "heap.hpp"

#include <cstddef>
#include <new>

extern "C" {
void* malloc(std::size_t size);
void free(void* address);
int posix_memalign(void** address, std::size_t alignment, std::size_t size);

using SceLibcMspace = void*;
struct SceLibcMallocManagedSize {
    unsigned short size;
    unsigned short version;
    unsigned int reserved;
    std::size_t max_system_size;
    std::size_t current_system_size;
    std::size_t max_inuse_size;
    std::size_t current_inuse_size;
};
SceLibcMspace sceLibcMspaceCreate(const char* name, void* base, std::size_t capacity, unsigned int flag);
void* sceLibcMspaceMalloc(SceLibcMspace msp, std::size_t size);
void sceLibcMspaceFree(SceLibcMspace msp, void* ptr);
int sceLibcMspacePosixMemalign(SceLibcMspace msp, void** ptr, std::size_t alignment, std::size_t size);
int sceLibcMspaceMallocStatsFast(SceLibcMspace msp, SceLibcMallocManagedSize* stats);
int malloc_stats_fast(SceLibcMallocManagedSize* stats);
}

namespace {
SceLibcMspace space_ = nullptr;
const unsigned char* base_ = nullptr;
std::size_t capacity_ = 0;
std::size_t fallbacks_ = 0;  // allocations parties dans le tas système faute de place dans la réserve

bool ours(const void* p) {
    const auto* c = static_cast<const unsigned char*>(p);
    return space_ && c >= base_ && c < base_ + capacity_;
}

void* allocate(std::size_t size) noexcept {
    if (size == 0) size = 1;
    if (space_) {
        if (void* p = sceLibcMspaceMalloc(space_, size)) return p;
        ++fallbacks_;
    }
    return malloc(size);
}

void* allocate_aligned(std::size_t size, std::size_t alignment) noexcept {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if ((alignment & (alignment - 1)) != 0) return nullptr;
    if (size == 0) size = 1;
    void* p = nullptr;
    if (space_ && sceLibcMspacePosixMemalign(space_, &p, alignment, size) == 0 && p) return p;
    return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
}

void release(void* p) noexcept {
    if (!p) return;
    if (ours(p)) sceLibcMspaceFree(space_, p);
    else free(p);
}

[[noreturn]] void allocation_failure() noexcept { __builtin_trap(); }
}  // namespace

namespace app::heap {

bool init(void* base, std::size_t capacity) {
    if (space_ || !base || capacity < (1u << 20)) return false;
    // Drapeau 0 : réserve partagée entre les fils (verrou interne).
    space_ = sceLibcMspaceCreate("pbc-app", base, capacity, 0);
    if (!space_) return false;
    base_ = static_cast<const unsigned char*>(base);
    capacity_ = capacity;
    return true;
}

Stats stats() {
    Stats s;
    s.capacity = capacity_;
    s.fallbacks = fallbacks_;
    SceLibcMallocManagedSize m{};
    m.size = sizeof m;
    m.version = 1;
    if (space_ && sceLibcMspaceMallocStatsFast(space_, &m) == 0) { s.in_use = m.current_inuse_size; s.peak = m.max_inuse_size; }
    SceLibcMallocManagedSize sys{};
    sys.size = sizeof sys;
    sys.version = 1;
    if (malloc_stats_fast(&sys) == 0) { s.system_in_use = sys.current_inuse_size; s.system_peak = sys.max_inuse_size; }
    return s;
}

}  // namespace app::heap

extern "C" __attribute__((noinline, visibility("hidden"))) bool ps5ObserveOwnedAllocation(const void* address) noexcept {
    __asm__ volatile("" : : "r"(address) : "memory");
    return address != nullptr;
}

void* operator new(std::size_t size) {
    if (void* p = allocate(size)) return p;
    allocation_failure();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t a) {
    if (void* p = allocate_aligned(size, static_cast<std::size_t>(a))) return p;
    allocation_failure();
}
void* operator new[](std::size_t size, std::align_val_t a) { return ::operator new(size, a); }
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate_aligned(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate_aligned(size, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }
