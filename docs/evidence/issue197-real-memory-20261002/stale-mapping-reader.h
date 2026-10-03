/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MCPX_APU_VP_SAMPLE_MEMORY_H
#define MCPX_APU_VP_SAMPLE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "system/memory.h"

static inline size_t mcpx_apu_adpcm_chunk_bytes(uint32_t linear_address,
                                                size_t remaining_bytes,
                                                uint32_t page_size)
{
    size_t page_remaining = page_size - linear_address % page_size;
    return remaining_bytes < page_remaining ? remaining_bytes : page_remaining;
}

static inline bool mcpx_apu_cached_word_eligible(uint64_t mapped_physical,
                                                 uint64_t actual_physical,
                                                 size_t offset,
                                                 size_t mapped_bytes)
{
    return actual_physical >= mapped_physical &&
           actual_physical - mapped_physical == offset &&
           offset <= mapped_bytes && 4 <= mapped_bytes - offset;
}

static inline hwaddr mcpx_apu_adpcm_word_address(hwaddr sge_base, uint32_t addr)
{
    unsigned int entry = addr / TARGET_PAGE_SIZE;
    uint32_t prd_address =
        ldl_le_phys(&address_space_memory, sge_base + entry * 4 * 2);
    return prd_address + addr % TARGET_PAGE_SIZE;
}

static inline void mcpx_apu_read_adpcm_block(const uint32_t *sge_base_reg,
                                             uint32_t linear_addr,
                                             uint32_t *words,
                                             unsigned int word_count)
{
    while (word_count) {
        size_t chunk_bytes = mcpx_apu_adpcm_chunk_bytes(
            linear_addr, word_count * sizeof(*words), TARGET_PAGE_SIZE);
        unsigned int chunk_words =
            MIN(word_count, DIV_ROUND_UP(chunk_bytes, sizeof(*words)));
        hwaddr mapped_physical =
            mcpx_apu_adpcm_word_address(*sge_base_reg, linear_addr);
        MemoryRegionCache cache = { 0 };
        int64_t mapped_bytes = 0;
        if (chunk_words > 1) {
            mapped_bytes =
                address_space_cache_init(&cache, &address_space_memory,
                                         mapped_physical, chunk_bytes, false);
        }

        for (unsigned int i = 0; i < chunk_words; i++) {
            hwaddr physical = mapped_physical;
            if (i) {
                /* Both the table base and its entries can change between words.
                 */
                physical =
                    mcpx_apu_adpcm_word_address(*sge_base_reg, linear_addr);
            }
            size_t offset = i * sizeof(*words);
            if (mapped_bytes > 0 && cache.ptr &&
                mcpx_apu_cached_word_eligible(mapped_physical, physical, offset,
                                              mapped_bytes)) {
                words[i] = address_space_ldl_le_cached(
                    &cache, offset, MEMTXATTRS_UNSPECIFIED, NULL);
            } else {
                words[i] = ldl_le_phys(&address_space_memory, physical);
            }
            linear_addr += sizeof(*words);
        }

        address_space_cache_destroy(&cache);
        words += chunk_words;
        word_count -= chunk_words;
    }
}

#endif
