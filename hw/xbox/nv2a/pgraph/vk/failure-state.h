/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Vulkan preparation failure bookkeeping shared with focused unit tests.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_FAILURE_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_FAILURE_STATE_H

#include "qemu/osdep.h"

static inline bool pgraph_vk_complete_surface_download(
    bool succeeded, bool *download_pending, bool *draw_dirty)
{
    if (!succeeded) {
        return false;
    }
    *download_pending = false;
    *draw_dirty = false;
    return true;
}

static inline bool pgraph_vk_complete_texture_upload(
    bool succeeded, uint64_t new_hash, uint64_t *stored_hash,
    bool *possibly_dirty)
{
    if (!succeeded) {
        *possibly_dirty = true;
        return false;
    }
    *stored_hash = new_hash;
    *possibly_dirty = false;
    return true;
}

#endif
