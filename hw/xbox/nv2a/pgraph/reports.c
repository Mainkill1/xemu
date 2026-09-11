/*
 * NV2A PGRAPH report helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "reports.h"

enum {
    ZPASS_REPORT_SIZE = 16,
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

    for (unsigned int i = 0; i < sizeof(timestamp); i++) {
        report[i] = timestamp >> (i * 8);
    }
    for (unsigned int i = 0; i < sizeof(result); i++) {
        report[sizeof(timestamp) + i] = result >> (i * 8);
    }
    for (unsigned int i = sizeof(timestamp) + sizeof(result);
         i < ZPASS_REPORT_SIZE; i++) {
        report[i] = 0;
    }

    return true;
}
