/*
 * NV2A Vulkan surface/guest-memory coherence helpers
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COHERENCE_H
#define HW_XBOX_NV2A_PGRAPH_VK_SURFACE_COHERENCE_H

#include <stdbool.h>

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

#endif
