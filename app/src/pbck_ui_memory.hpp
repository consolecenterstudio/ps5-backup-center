// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace pbck {
// CPU-only storage for the compositor and its textures. Never uses the title's
// small general heap on console. All slices share this object's lifetime.
class UiMemory final {
public:
    static constexpr std::size_t default_capacity=38u*1024u*1024u;
    // Second arena possible (police de secours de app) : taille arrondie à 2 Mio.
    explicit UiMemory(std::size_t capacity=default_capacity) noexcept
        :capacity_((capacity+0x1FFFFFu)&~std::size_t{0x1FFFFF}) {}
    ~UiMemory() { (void)reset(); }
    UiMemory(const UiMemory&)=delete;
    UiMemory& operator=(const UiMemory&)=delete;
    bool initialize() noexcept;
    bool reset() noexcept;
    std::uint32_t* pixels(std::size_t count) noexcept;
    std::size_t used() const noexcept {return used_;}
    std::size_t capacity() const noexcept {return capacity_;}
    int status() const noexcept {return status_;}
    const char* operation() const noexcept {return operation_;}
private:
    std::size_t capacity_;
    void* address_=nullptr;
    [[maybe_unused]] std::int64_t physical_=-1; // Console allocation handle.
    std::size_t used_=0;
    int status_=0;
    const char* operation_="NONE";
};
}
