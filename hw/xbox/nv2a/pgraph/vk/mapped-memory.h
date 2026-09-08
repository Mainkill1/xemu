/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Scoped mapped-allocation ownership for recoverable Vulkan preparation.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_MAPPED_MEMORY_H
#define HW_XBOX_NV2A_PGRAPH_VK_MAPPED_MEMORY_H

#include "qemu/osdep.h"

typedef void (*PGRAPHVkUnmapFunc)(void *allocator, void *allocation);

typedef struct PGRAPHVkMapGuard {
    void *allocator;
    void *allocation;
    PGRAPHVkUnmapFunc unmap;
    bool mapped;
} PGRAPHVkMapGuard;

static inline bool pgraph_vk_map_guard_complete_map(
    PGRAPHVkMapGuard *guard, bool succeeded, void *allocator,
    void *allocation, PGRAPHVkUnmapFunc unmap)
{
    if (!succeeded) {
        return false;
    }
    guard->allocator = allocator;
    guard->allocation = allocation;
    guard->unmap = unmap;
    guard->mapped = true;
    return true;
}

static inline bool pgraph_vk_map_guard_complete_coherency(
    PGRAPHVkMapGuard *guard, bool succeeded)
{
    assert(guard->mapped);
    return succeeded;
}

static inline void pgraph_vk_map_guard_clear(PGRAPHVkMapGuard *guard)
{
    if (!guard->mapped) {
        return;
    }
    guard->unmap(guard->allocator, guard->allocation);
    guard->mapped = false;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(PGRAPHVkMapGuard, pgraph_vk_map_guard_clear)

#endif
