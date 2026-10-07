/*
 * Vulkan readback ownership
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_RETENTION_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_RETENTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef bool (*PGRAPHVkSurfaceReadImage)(void *opaque, uint8_t *destination);

static inline bool
pgraph_vk_surface_readback_owned(uint8_t *guest, uint8_t *owned, size_t size,
                                 PGRAPHVkSurfaceReadImage read_image,
                                 void *opaque)
{
    /*
     * Preserve unused pitch bytes without reading guest RAM after readback.
     * The reader overwrites every byte represented by the retained image.
     */
    memcpy(owned, guest, size);
    return read_image(opaque, owned);
}

#endif
