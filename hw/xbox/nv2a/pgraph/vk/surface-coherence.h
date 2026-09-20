/*
 * NV2A Vulkan surface/guest-memory coherence helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COHERENCE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COHERENCE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool (*PGRAPHVkSurfaceConsumeDirtyRange)(void *opaque,
                                                uint64_t start, uint64_t size);
typedef void (*PGRAPHVkSurfaceVisitDirtyRange)(void *opaque,
                                              uint64_t start, uint64_t size);

static inline bool pgraph_vk_surface_range_overlaps(
    uint64_t surface_start, uint64_t surface_size, uint64_t range_start,
    uint64_t range_size)
{
    if (!surface_size || !range_size) {
        return false;
    }
    if (surface_start <= range_start) {
        return range_start - surface_start < surface_size;
    }
    return surface_start - range_start < range_size;
}

/* One dirty-bit consumer must notify all cached surfaces sharing its pages. */
static inline bool pgraph_vk_surface_consume_dirty_range(
    uint64_t start, uint64_t size, uint64_t page_size,
    PGRAPHVkSurfaceConsumeDirtyRange consume,
    PGRAPHVkSurfaceVisitDirtyRange visit, void *opaque)
{
    assert(page_size && !(page_size & (page_size - 1)));
    assert(start <= UINT64_MAX - (page_size - 1));
    assert(size <= UINT64_MAX - start - (page_size - 1));

    if (!size || !consume(opaque, start, size)) {
        return false;
    }
    uint64_t aligned_start = start & ~(page_size - 1);
    uint64_t aligned_end = (start + size + page_size - 1) & ~(page_size - 1);

    visit(opaque, aligned_start, aligned_end - aligned_start);
    return true;
}

/* Return true only for the transition that first makes an upload necessary. */
static inline bool pgraph_vk_surface_mark_upload_pending(bool *upload_pending)
{
    bool newly_pending = !*upload_pending;
    *upload_pending = true;
    return newly_pending;
}

/*
 * Accelerated guest writes are observed after they reach VRAM.  At that
 * point a cached surface may no longer write its older GPU contents back over
 * the newer guest bytes.  Make the guest copy authoritative and require the
 * cached surface to reload it before the surface is used again.
 */
static inline bool pgraph_vk_surface_resolve_guest_write(
    bool guest_memory_dirty, bool *download_pending, bool *draw_dirty,
    bool *upload_pending)
{
    if (!guest_memory_dirty) {
        return false;
    }

    *download_pending = false;
    *draw_dirty = false;
    pgraph_vk_surface_mark_upload_pending(upload_pending);
    return true;
}

/* A guest write observed before a GPU-to-RAM readback makes that readback
 * stale. Check the entire surface because the readback copies its full extent,
 * even when a caller previously checked a smaller overlapping texture range. */
static inline bool pgraph_vk_surface_readback_preflight(
    bool download_pending, bool force, uint64_t start, uint64_t size,
    PGRAPHVkSurfaceConsumeDirtyRange refresh, void *opaque)
{
    if (!(download_pending || force)) {
        return false;
    }
    assert(refresh);
    return !refresh(opaque, start, size);
}

#endif
