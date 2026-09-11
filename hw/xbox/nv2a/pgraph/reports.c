/*
 * NV2A PGRAPH report helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"

#include "reports.h"

enum {
    ZPASS_REPORT_SIZE = 16,
    /* nv_dma_load currently reads flags, limit, and frame. */
    DMA_DESCRIPTOR_SIZE = 3 * sizeof(uint32_t),
};

bool pgraph_zpass_report_descriptor_fits(uint64_t ramin_size,
                                         uint64_t dma_report)
{
    return dma_report <= ramin_size &&
           DMA_DESCRIPTOR_SIZE <= ramin_size - dma_report;
}

bool pgraph_zpass_report_write(uint8_t *vram, uint64_t base, uint64_t limit,
                               uint64_t offset, uint64_t vram_size,
                               uint32_t result)
{
    if (offset > limit || ZPASS_REPORT_SIZE - 1 > limit - offset ||
        base > vram_size || offset > vram_size - base ||
        ZPASS_REPORT_SIZE > vram_size - base - offset) {
        return false;
    }

    uint8_t *report = vram + base + offset;
    const uint64_t timestamp = UINT64_C(0x0011223344556677);

    stq_le_p(report + 0, timestamp);
    stl_le_p(report + 8, result);
    stl_le_p(report + 12, 0);

    return true;
}
