/*
 * NV2A Vulkan draw completion lifecycle
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "draw-lifecycle.h"
#include "renderer.h"

PGRAPHVkDrawShaderMissAction pgraph_vk_draw_shader_miss_action(
    bool continue_requested, bool nonblocking_supported,
    bool omission_supported, bool executable_ready)
{
    if (executable_ready) {
        return PGRAPH_VK_DRAW_MISS_READY;
    }
    if (continue_requested && nonblocking_supported && omission_supported) {
        return PGRAPH_VK_DRAW_MISS_OMIT;
    }
    return PGRAPH_VK_DRAW_MISS_WAIT;
}

bool pgraph_vk_draw_omission_supported(bool query_side_effect,
                                       bool color_write,
                                       bool zeta_write)
{
    return !query_side_effect && !color_write && !zeta_write;
}

void pgraph_vk_draw_omission_checkpoint_capture(
    const PGRAPHVkState *r, PGRAPHVkDrawEncoding encoding,
    PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    *checkpoint = (PGRAPHVkDrawOmissionCheckpoint) {
        .encoding = encoding,
        .vertex_inline_staging_offset =
            r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset,
        .index_staging_offset =
            r->storage_buffers[BUFFER_INDEX_STAGING].buffer_offset,
        .vertex_ram_stale_page_count = r->vertex_ram_stale_page_count,
    };
}

bool pgraph_vk_draw_omission_state_is_clean(
    const PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    return checkpoint->encoding <= PGRAPH_VK_DRAW_ENCODING_INLINE_ARRAY &&
           r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset ==
               checkpoint->vertex_inline_staging_offset &&
           r->storage_buffers[BUFFER_INDEX_STAGING].buffer_offset ==
               checkpoint->index_staging_offset &&
           r->vertex_ram_stale_page_count <=
               checkpoint->vertex_ram_stale_page_count;
}

void pgraph_vk_discard_unsubmitted_draw_state(
    PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    g_assert(pgraph_vk_draw_omission_state_is_clean(r, checkpoint));
    r->num_pending_vertex_ram_reads = 0;
    r->num_vertex_ram_buffer_syncs = 0;
}

void pgraph_vk_complete_draw_lifecycle(
    PGRAPHState *pg, PGRAPHVkState *r, PGRAPHVkDrawResult result,
    bool color_write, bool zeta_write, bool color_dirty, bool zeta_dirty)
{
    if (result != PGRAPH_VK_DRAW_SUBMITTED) {
        return;
    }

    pg->draw_time++;
    if (r->color_binding && color_write) {
        r->color_binding->draw_time = pg->draw_time;
    }
    if (r->zeta_binding && zeta_write) {
        r->zeta_binding->draw_time = pg->draw_time;
    }
    pgraph_vk_set_surface_dirty(pg, color_dirty, zeta_dirty);
}
