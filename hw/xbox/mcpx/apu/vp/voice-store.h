/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef MCPX_APU_VP_VOICE_STORE_H
#define MCPX_APU_VP_VOICE_STORE_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
