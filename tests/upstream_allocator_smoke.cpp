// SPDX-License-Identifier: GPL-2.0-or-later
// Tests the real allocator imported from pinned Vita3K, independently of the Xbox UI.
#include <mem/allocator.h>
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    BitmapAllocator slots(64);
    assert(slots.free_slot_count(0, 64) == 64);

    std::uint32_t requested = 4;
    assert(slots.allocate_from(0, requested) == 0);
    assert(requested == 4);
    assert(slots.free_slot_count(0, 64) == 60);
    assert(slots.allocate_at(8, 2) == 0);
    assert(slots.free_slot_count(0, 64) == 58);
    slots.free(0, 4);
    assert(slots.free_slot_count(0, 64) == 62);
    slots.free(8, 2);
    assert(slots.free_slot_count(0, 64) == 64);

    std::cout << "Vita3K upstream memory allocator: PASS\n";
    return 0;
}
