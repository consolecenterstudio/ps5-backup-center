// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>

// Réserve de mémoire de l'app télé (voir heap.cpp).
namespace app::heap {

struct Stats {
    std::size_t capacity = 0, in_use = 0, peak = 0;          // notre réserve
    std::size_t system_in_use = 0, system_peak = 0;          // tas système (~13 Mo)
    std::size_t fallbacks = 0;                               // allocations parties au tas système faute de place
};

// À appeler tôt (avant les fils) avec une zone de mémoire directe lisible / inscriptible.
bool init(void* base, std::size_t capacity);
Stats stats();

}  // namespace app::heap
