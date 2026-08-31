/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_DIRTY_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_DIRTY_H

#include <stdbool.h>

static inline void pgraph_vk_texture_binding_revalidated(
    bool *binding_possibly_dirty)
{
    *binding_possibly_dirty = false;
}

#endif
