/*
 * NV2A DMA object helpers
 *
 * Copyright (c) 2026 xemu Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_DMA_H
#define HW_XBOX_NV2A_DMA_H

#include "exec/hwaddr.h"

typedef struct DMAObject {
    unsigned int dma_class;
    unsigned int dma_target;
    hwaddr address;
    hwaddr limit;
} DMAObject;

struct NV2AState;

bool nv_dma_load_from_ramin_checked(const uint8_t *ramin, hwaddr ramin_size,
                                    hwaddr dma_obj_address, DMAObject *out);
bool nv_dma_load_checked(struct NV2AState *d, hwaddr dma_obj_address,
                         DMAObject *out);
DMAObject nv_dma_load(struct NV2AState *d, hwaddr dma_obj_address);

bool nv_dma_report_object_supported(const DMAObject *dma);
bool nv_dma_report_record_address(const DMAObject *dma, hwaddr offset,
                                  hwaddr report_size, hwaddr vram_size,
                                  hwaddr *report_address);

#endif
