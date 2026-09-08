/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Vulkan texture cache revalidation state transitions.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_STATE_H

#include <stdbool.h>
#include <stdint.h>

static inline bool pgraph_vk_texture_needs_revalidation(bool dirty_hint,
                                                         bool dirty_pages)
{
    return dirty_hint || dirty_pages;
}

static inline bool pgraph_vk_texture_complete_revalidation(
    bool content_changed, bool upload_succeeded, uint64_t content_hash,
    uint64_t *stored_hash, bool *dirty_hint)
{
    if (content_changed && !upload_succeeded) {
        *dirty_hint = true;
        return false;
    }

    if (content_changed) {
        *stored_hash = content_hash;
    }
    *dirty_hint = false;
    return true;
}

#endif
