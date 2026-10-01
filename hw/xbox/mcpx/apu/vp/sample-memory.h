/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef MCPX_APU_VP_SAMPLE_MEMORY_H
#define MCPX_APU_VP_SAMPLE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#endif
