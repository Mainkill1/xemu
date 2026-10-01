/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/rcu.h"
#include "system/xen.h"
#include "hw/xbox/mcpx/apu/vp/voice-store.h"

void mcpx_apu_voice_store_masked_from_read(AddressSpace *as, MemoryRegion *ram,
                                           hwaddr address, uint32_t mask,
                                           uint32_t val, uint32_t old_value)
{
    uint32_t new_value = (old_value & ~mask) |
                         ((val << ctz32(mask)) & mask);

    /*
     * Equal-write elision follows the prototype in izzy2lost/xemu:
     * https://github.com/izzy2lost/xemu/commit/a7765290d17750e10a2bfc2d94cd85279f3b591e
     * A direct, aligned RAM compare-exchange is the write point. If another
     * writer changes the word after our read, the original physical store
     * below wins instead. Unchanged bytes do not require dirty tracking or
     * code invalidation; Xen observes writes independently, so it falls back.
     */
    if (!mcpx_apu_voice_store_required(address, memory_region_size(ram),
                                        old_value, new_value)) {
        RCU_READ_LOCK_GUARD();
        hwaddr translated, len = sizeof(new_value);
        MemoryRegion *region = address_space_translate(
            as, address, &translated, &len, true,
            MEMTXATTRS_UNSPECIFIED);

        if (region == ram && len == sizeof(new_value) &&
            memory_access_is_direct(region, true, MEMTXATTRS_UNSPECIFIED) &&
            !xen_enabled() && !(translated & (sizeof(new_value) - 1))) {
            uint32_t *ram_word = qemu_map_ram_ptr(region->ram_block,
                                                   translated);

            if (!((uintptr_t)ram_word & (sizeof(new_value) - 1)) &&
                mcpx_apu_voice_try_equal_store(ram_word, old_value)) {
                return;
            }
        }
    }

    stl_le_phys(as, address, new_value);
}

void mcpx_apu_voice_store_masked(AddressSpace *as, MemoryRegion *ram,
                                 hwaddr address, uint32_t mask, uint32_t val)
{
    uint32_t old_value = ldl_le_phys(as, address);

    mcpx_apu_voice_store_masked_from_read(as, ram, address, mask, val,
                                          old_value);
}
