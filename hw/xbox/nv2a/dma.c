/*
 * NV2A DMA object helpers
 *
 * Copyright (c) 2026 xemu Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#ifdef NV2A_DMA_HELPERS_ONLY
#include "qemu/bswap.h"
#include "qemu/host-utils.h"
#include "hw/xbox/nv2a/dma.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#else
#include "hw/xbox/nv2a/nv2a_int.h"
#endif

#define NV_DMA_OBJECT_SIZE 12
#define NV2A_VRAM_ADDRESS_MASK 0x07FFFFFF

bool nv_dma_load_from_ramin_checked(const uint8_t *ramin, hwaddr ramin_size,
                                    hwaddr dma_obj_address, DMAObject *out)
{
    if (dma_obj_address > ramin_size ||
        NV_DMA_OBJECT_SIZE > ramin_size - dma_obj_address) {
        return false;
    }

    const uint8_t *dma_obj = ramin + dma_obj_address;
    uint32_t flags = ldl_le_p(dma_obj);
    uint32_t limit = ldl_le_p(dma_obj + 4);
    uint32_t frame = ldl_le_p(dma_obj + 8);

    *out = (DMAObject){
        .dma_class = GET_MASK(flags, NV_DMA_CLASS),
        .dma_target = GET_MASK(flags, NV_DMA_TARGET),
        .address = (frame & NV_DMA_ADDRESS) |
                   GET_MASK(flags, NV_DMA_ADJUST),
        .limit = limit,
    };
    return true;
}

#ifndef NV2A_DMA_HELPERS_ONLY
bool nv_dma_load_checked(NV2AState *d, hwaddr dma_obj_address, DMAObject *out)
{
    return nv_dma_load_from_ramin_checked(
        d->ramin_ptr, memory_region_size(&d->ramin), dma_obj_address, out);
}

DMAObject nv_dma_load(NV2AState *d, hwaddr dma_obj_address)
{
    DMAObject dma = { 0 };
    bool loaded = nv_dma_load_checked(d, dma_obj_address, &dma);

    assert(loaded);
    if (!loaded) {
        return dma;
    }
    return dma;
}
#endif

bool nv_dma_report_object_supported(const DMAObject *dma)
{
    /*
     * Retail titles also address Xbox unified RAM through PCI-target DMA
     * objects (Halo uses class 0x3d, target PCI, base zero for reports).
     * These use the same bounded RAM mapping as NVM. Rejecting PCI here
     * drops the completion record and leaves the guest waiting forever.
     * Keep tiled, AGP, unknown classes and out-of-range addresses rejected.
     */
    bool ram_target =
        dma->dma_target == GET_MASK(NV_DMA_TARGET_NVM, NV_DMA_TARGET) ||
        dma->dma_target == GET_MASK(NV_DMA_TARGET_PCI, NV_DMA_TARGET);

    return dma->dma_class == NV_DMA_IN_MEMORY_CLASS &&
           ram_target &&
           (dma->address & ~NV2A_VRAM_ADDRESS_MASK) == 0;
}

bool nv_dma_report_record_address(const DMAObject *dma, hwaddr offset,
                                  hwaddr report_size, hwaddr vram_size,
                                  hwaddr *report_address)
{
    if (report_size == 0 || !nv_dma_report_object_supported(dma)) {
        return false;
    }

    /* DMAObject.limit is an inclusive maximum offset. */
    if (offset > dma->limit || report_size - 1 > dma->limit - offset) {
        return false;
    }

    if (dma->address > vram_size || offset > vram_size - dma->address ||
        report_size > vram_size - dma->address - offset) {
        return false;
    }

    *report_address = dma->address + offset;
    return true;
}
