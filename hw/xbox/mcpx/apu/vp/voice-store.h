/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef MCPX_APU_VP_VOICE_STORE_H
#define MCPX_APU_VP_VOICE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "qemu/atomic.h"
#include "qemu/bswap.h"
#include "system/memory.h"

/*
 * The Xbox maps ordinary system RAM from 1 MiB to the configured RAM end.
 * Lower addresses may contain ROM or device overlays, so their writes must
 * still go through the physical-memory accessor even when values are equal.
 */
#define MCPX_APU_VOICE_PLAIN_RAM_START 0x100000ULL

static inline bool mcpx_apu_voice_store_required(uint64_t address,
                                                 uint64_t ram_size,
                                                 uint32_t old_value,
                                                 uint32_t new_value)
{
    return old_value != new_value || address < MCPX_APU_VOICE_PLAIN_RAM_START ||
           address > ram_size || ram_size - address < sizeof(uint32_t);
}

/*
 * A successful compare-exchange is the equal store's linearization point.
 * If another writer changed the word after the APU read, the caller must
 * perform its original physical store instead.
 */
static inline bool mcpx_apu_voice_try_equal_store(uint32_t *ram_word,
                                                   uint32_t expected)
{
    uint32_t little_endian = cpu_to_le32(expected);

    return qatomic_cmpxchg(ram_word, little_endian,
                           little_endian) == little_endian;
}

void mcpx_apu_voice_store_masked(AddressSpace *as, MemoryRegion *ram,
                                 hwaddr address, uint32_t mask, uint32_t val);
void mcpx_apu_voice_store_masked_from_read(AddressSpace *as, MemoryRegion *ram,
                                           hwaddr address, uint32_t mask,
                                           uint32_t val, uint32_t old_value);

#endif
