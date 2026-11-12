// SPDX-License-Identifier: GPL-3.0-or-later
#include "pbck_ui_memory.hpp"
#include <cerrno>
#if defined(PBCK_PS5)
extern "C" {
std::size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(std::int64_t,std::int64_t,std::size_t,std::size_t,int,std::int64_t*);
int sceKernelMapDirectMemory(void**,std::size_t,int,int,std::int64_t,std::size_t);
int sceKernelMunmap(void*,std::size_t);
int sceKernelReleaseDirectMemory(std::int64_t,std::size_t);
}
#else
#include <cstdlib>
#endif

namespace pbck {
bool UiMemory::reset() noexcept {
#if defined(PBCK_PS5)
    if(address_) {
        operation_="UNMAP";status_=sceKernelMunmap(address_,capacity_);
        // Do not release physical storage while an unsuccessful unmap leaves
        // a live virtual mapping. Retain ownership so reset can be retried.
        if(status_!=0) return false;
        address_=nullptr;
    }
    if(physical_>=0) {
        operation_="RELEASE";status_=sceKernelReleaseDirectMemory(physical_,capacity_);
        if(status_!=0) return false;
        physical_=-1;
    }
#else
    std::free(address_);address_=nullptr;
#endif
    used_=0;status_=0;operation_="NONE";return true;
}
bool UiMemory::initialize() noexcept {
    if(!reset()) return false;
#if defined(PBCK_PS5)
    constexpr std::size_t alignment=0x200000;
    constexpr int cpu_cached_memory=12, cpu_read_write=0x03;
    const auto pool=sceKernelGetDirectMemorySize();
    operation_="ALLOCATE";
    if(pool<capacity_ || pool>INT64_MAX) {status_=ENOMEM;return false;}
    std::int64_t physical=-1;
    status_=sceKernelAllocateDirectMemory(0,static_cast<std::int64_t>(pool),capacity_,
                                         alignment,cpu_cached_memory,&physical);
    if(status_!=0) return false;
    physical_=physical;
    void* mapped=nullptr;
    operation_="MAP";
    status_=sceKernelMapDirectMemory(&mapped,capacity_,cpu_read_write,0,physical_,alignment);
    if(status_!=0 || !mapped) {
        const int failure=status_!=0 ? status_ : EFAULT;
        (void)reset(); // Releases the allocation if mapping failed.
        operation_="MAP";status_=failure;return false;
    }
    address_=mapped;
#else
    operation_="ALLOCATE";
    address_=std::malloc(capacity_);
    if(!address_) {status_=ENOMEM;return false;}
#endif
    operation_="READY";status_=0;return true;
}
std::uint32_t* UiMemory::pixels(std::size_t count) noexcept {
    const auto aligned=(used_+63u)&~std::size_t{63};
    if(!address_ || !count || aligned>capacity_ || count>(capacity_-aligned)/sizeof(std::uint32_t)) {
        operation_="CAPACITY";status_=ENOMEM;return nullptr;
    }
    auto* result=reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(address_)+aligned);
    used_=aligned+count*sizeof(std::uint32_t);
    return result;
}
}
