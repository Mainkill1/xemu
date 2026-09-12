/*
 * NV2A Vulkan texture-binding state helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_BINDING_STATE_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_BINDING_STATE_H

#include <stdbool.h>

/* A disabled stage retains its guest dirtiness until it is re-enabled. */
static inline bool pgraph_vk_texture_stage_needs_rebind(bool enabled,
                                                       bool dirty, bool bound,
                                                       bool bound_dummy)
{
    if (!enabled) {
        return !bound_dummy;
    }

    return !bound || bound_dummy || dirty;
}

#endif
