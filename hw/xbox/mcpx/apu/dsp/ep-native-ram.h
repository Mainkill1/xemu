/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_EP_NATIVE_RAM_H
#define XEMU_EP_NATIVE_RAM_H

#include "qemu/rcu.h"
#include "system/memory.h"

typedef struct EPNativeRAMView {
    AddressSpace *as;
    MemoryRegion *ram;
} EPNativeRAMView;

/* The APU's existing scratch path indexes its RAM backing by physical
 * address. Reject aliases, overlays, and ranges that resolve differently,
 * without invoking any device read callbacks. The caller owns APU locking. */
static inline bool ep_native_ram_read(void *opaque, uint64_t address,
                                      uint8_t *output, size_t length)
{
    EPNativeRAMView *view = opaque;
    if (!view || !view->as || !view->ram || !output ||
        !memory_region_is_ram(view->ram) ||
        memory_region_is_ram_device(view->ram)) {
        return false;
    }
    uint64_t ram_size = memory_region_size(view->ram);
    if (address > ram_size || length > ram_size - address) {
        return false;
    }
    if (!length) {
        return true;
    }
    RCU_READ_LOCK_GUARD();
    hwaddr offset = 0, mapped_length = length;
    MemoryRegion *region =
        address_space_translate(view->as, address, &offset, &mapped_length,
                                false, MEMTXATTRS_UNSPECIFIED);
    if (region != view->ram || offset != address || mapped_length < length) {
        return false;
    }
    const uint8_t *ram = memory_region_get_ram_ptr(view->ram);
    if (!ram) {
        return false;
    }
    memcpy(output, ram + offset, length);
    return true;
}
#endif
