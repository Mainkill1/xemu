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

PGRAPHVkOmissionDecision pgraph_vk_classify_draw_omission(
    PGRAPHState *pg, const PGRAPHVkState *r)
{
    PGRAPHVkOmissionDecision decision = { 0 };
    if (!pg || !r || pg->clearing ||
        (!r->color_binding && !r->zeta_binding) ||
        (r->color_binding && !r->color_binding->initialized) ||
        (r->zeta_binding && !r->zeta_binding->initialized)) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_UNKNOWN;
        return decision;
    }

    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    uint32_t control_1 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1);
    if (pg->zpass_pixel_count_enable || r->query_in_flight ||
        r->new_query_needed) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_QUERY;
    }
    if (control_0 & (NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE |
                     NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE |
                     NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE |
                     NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE)) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_COLOR_WRITE;
    }
    if (control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_DEPTH_WRITE;
    }
    if ((control_0 & NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE) &&
        (control_1 & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE)) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_STENCIL;
    }
    if ((r->color_binding &&
         (r->color_binding->download_pending ||
          r->color_binding->upload_pending ||
          r->color_binding->readback_superseded_by_guest)) ||
        (r->zeta_binding &&
         (r->zeta_binding->download_pending ||
          r->zeta_binding->upload_pending ||
          r->zeta_binding->readback_superseded_by_guest))) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_SURFACE_DEP;
    }
    if (r->report_queue_depth) {
        decision.blockers |= PGRAPH_VK_OMIT_BLOCK_REPORT_DEP;
    }
    decision.safe = decision.blockers == 0;
    return decision;
}

void pgraph_vk_draw_omission_checkpoint_capture(
    const PGRAPHState *pg, const PGRAPHVkState *r,
    PGRAPHVkDrawEncoding encoding,
    PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    *checkpoint = (PGRAPHVkDrawOmissionCheckpoint) {
        .encoding = encoding,
        .vertex_inline_staging_offset =
            r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset,
        .index_staging_offset =
            r->storage_buffers[BUFFER_INDEX_STAGING].buffer_offset,
        .vertex_ram_stale_page_count = r->vertex_ram_stale_page_count,
        .compressed_attrs = pg->compressed_attrs,
        .uniform_attrs = pg->uniform_attrs,
        .swizzle_attrs = pg->swizzle_attrs,
        .num_vertex_ram_buffer_syncs = r->num_vertex_ram_buffer_syncs,
        .num_pending_vertex_ram_reads = r->num_pending_vertex_ram_reads,
        .num_active_vertex_attribute_descriptions =
            r->num_active_vertex_attribute_descriptions,
        .num_active_vertex_binding_descriptions =
            r->num_active_vertex_binding_descriptions,
    };
    memcpy(checkpoint->vertex_attributes, pg->vertex_attributes,
           sizeof(checkpoint->vertex_attributes));
    memcpy(checkpoint->vertex_ram_buffer_syncs,
           r->vertex_ram_buffer_syncs,
           sizeof(checkpoint->vertex_ram_buffer_syncs));
    memcpy(checkpoint->pending_vertex_ram_reads,
           r->pending_vertex_ram_reads,
           sizeof(checkpoint->pending_vertex_ram_reads));
    memcpy(checkpoint->vertex_attribute_descriptions,
           r->vertex_attribute_descriptions,
           sizeof(checkpoint->vertex_attribute_descriptions));
    memcpy(checkpoint->vertex_attribute_to_description_location,
           r->vertex_attribute_to_description_location,
           sizeof(checkpoint->vertex_attribute_to_description_location));
    memcpy(checkpoint->vertex_binding_descriptions,
           r->vertex_binding_descriptions,
           sizeof(checkpoint->vertex_binding_descriptions));
    memcpy(checkpoint->vertex_attribute_offsets,
           r->vertex_attribute_offsets,
           sizeof(checkpoint->vertex_attribute_offsets));
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
    PGRAPHState *pg, PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    g_assert(pgraph_vk_draw_omission_state_is_clean(r, checkpoint));
    pg->compressed_attrs = checkpoint->compressed_attrs;
    pg->uniform_attrs = checkpoint->uniform_attrs;
    pg->swizzle_attrs = checkpoint->swizzle_attrs;
    memcpy(pg->vertex_attributes, checkpoint->vertex_attributes,
           sizeof(checkpoint->vertex_attributes));
    memcpy(r->vertex_ram_buffer_syncs,
           checkpoint->vertex_ram_buffer_syncs,
           sizeof(checkpoint->vertex_ram_buffer_syncs));
    r->num_vertex_ram_buffer_syncs =
        checkpoint->num_vertex_ram_buffer_syncs;
    memcpy(r->pending_vertex_ram_reads,
           checkpoint->pending_vertex_ram_reads,
           sizeof(checkpoint->pending_vertex_ram_reads));
    r->num_pending_vertex_ram_reads =
        checkpoint->num_pending_vertex_ram_reads;
    memcpy(r->vertex_attribute_descriptions,
           checkpoint->vertex_attribute_descriptions,
           sizeof(checkpoint->vertex_attribute_descriptions));
    memcpy(r->vertex_attribute_to_description_location,
           checkpoint->vertex_attribute_to_description_location,
           sizeof(checkpoint->vertex_attribute_to_description_location));
    r->num_active_vertex_attribute_descriptions =
        checkpoint->num_active_vertex_attribute_descriptions;
    memcpy(r->vertex_binding_descriptions,
           checkpoint->vertex_binding_descriptions,
           sizeof(checkpoint->vertex_binding_descriptions));
    r->num_active_vertex_binding_descriptions =
        checkpoint->num_active_vertex_binding_descriptions;
    memcpy(r->vertex_attribute_offsets,
           checkpoint->vertex_attribute_offsets,
           sizeof(checkpoint->vertex_attribute_offsets));
    g_assert(pgraph_vk_draw_omission_transaction_restored(
        pg, r, checkpoint));
}

bool pgraph_vk_draw_omission_transaction_restored(
    const PGRAPHState *pg, const PGRAPHVkState *r,
    const PGRAPHVkDrawOmissionCheckpoint *checkpoint)
{
    return pg->compressed_attrs == checkpoint->compressed_attrs &&
           pg->uniform_attrs == checkpoint->uniform_attrs &&
           pg->swizzle_attrs == checkpoint->swizzle_attrs &&
           memcmp(pg->vertex_attributes, checkpoint->vertex_attributes,
                  sizeof(checkpoint->vertex_attributes)) == 0 &&
           r->num_vertex_ram_buffer_syncs ==
               checkpoint->num_vertex_ram_buffer_syncs &&
           memcmp(r->vertex_ram_buffer_syncs,
                  checkpoint->vertex_ram_buffer_syncs,
                  sizeof(checkpoint->vertex_ram_buffer_syncs)) == 0 &&
           r->num_pending_vertex_ram_reads ==
               checkpoint->num_pending_vertex_ram_reads &&
           memcmp(r->pending_vertex_ram_reads,
                  checkpoint->pending_vertex_ram_reads,
                  sizeof(checkpoint->pending_vertex_ram_reads)) == 0 &&
           r->num_active_vertex_attribute_descriptions ==
               checkpoint->num_active_vertex_attribute_descriptions &&
           memcmp(r->vertex_attribute_descriptions,
                  checkpoint->vertex_attribute_descriptions,
                  sizeof(checkpoint->vertex_attribute_descriptions)) == 0 &&
           memcmp(r->vertex_attribute_to_description_location,
                  checkpoint->vertex_attribute_to_description_location,
                  sizeof(checkpoint->vertex_attribute_to_description_location)) ==
               0 &&
           r->num_active_vertex_binding_descriptions ==
               checkpoint->num_active_vertex_binding_descriptions &&
           memcmp(r->vertex_binding_descriptions,
                  checkpoint->vertex_binding_descriptions,
                  sizeof(checkpoint->vertex_binding_descriptions)) == 0 &&
           memcmp(r->vertex_attribute_offsets,
                  checkpoint->vertex_attribute_offsets,
                  sizeof(checkpoint->vertex_attribute_offsets)) == 0;
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
