/*
 * NV2A Vulkan omitted draw state tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/draw-lifecycle.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

void pgraph_vk_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    (void)pg;
    (void)color;
    (void)zeta;
    g_assert_not_reached();
}

static void test_repeated_omission_discards_draw_local_state(void)
{
    const PGRAPHVkDrawEncoding encodings[] = {
        PGRAPH_VK_DRAW_ENCODING_ARRAYS,
        PGRAPH_VK_DRAW_ENCODING_INLINE_ELEMENTS,
        PGRAPH_VK_DRAW_ENCODING_INLINE_BUFFER,
        PGRAPH_VK_DRAW_ENCODING_INLINE_ARRAY,
    };

    for (size_t i = 0; i < ARRAY_SIZE(encodings); i++) {
        PGRAPHVkState renderer = { 0 };
        renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset =
            128;
        renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset = 64;
        renderer.vertex_ram_stale_page_count = 3;

        for (unsigned int attempt = 0; attempt < 3; attempt++) {
            PGRAPHVkDrawOmissionCheckpoint checkpoint;
            pgraph_vk_draw_omission_checkpoint_capture(
                &renderer, encodings[i], &checkpoint);

            renderer.num_pending_vertex_ram_reads = 4;
            renderer.num_vertex_ram_buffer_syncs = 2;
            /* A completed mirror upload may make a stale page current. */
            renderer.vertex_ram_stale_page_count = 2;

            g_assert_true(pgraph_vk_draw_omission_state_is_clean(
                &renderer, &checkpoint));
            pgraph_vk_discard_unsubmitted_draw_state(
                &renderer, &checkpoint);

            g_assert_cmpuint(renderer.num_pending_vertex_ram_reads, ==, 0);
            g_assert_cmpuint(renderer.num_vertex_ram_buffer_syncs, ==, 0);
            g_assert_cmpuint(
                renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING]
                    .buffer_offset,
                ==, 128);
            g_assert_cmpuint(
                renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset,
                ==, 64);
            g_assert_cmpuint(renderer.vertex_ram_stale_page_count, ==, 2);
        }
    }
}

static void test_omission_rejects_staging_or_stale_page_commit(void)
{
    PGRAPHVkState renderer = { 0 };
    renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset = 32;
    renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset = 16;
    renderer.vertex_ram_stale_page_count = 1;

    PGRAPHVkDrawOmissionCheckpoint checkpoint;
    pgraph_vk_draw_omission_checkpoint_capture(
        &renderer, PGRAPH_VK_DRAW_ENCODING_INLINE_ELEMENTS, &checkpoint);

    renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset++;
    g_assert_false(pgraph_vk_draw_omission_state_is_clean(
        &renderer, &checkpoint));
    renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset--;

    renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset++;
    g_assert_false(pgraph_vk_draw_omission_state_is_clean(
        &renderer, &checkpoint));
    renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset--;

    renderer.vertex_ram_stale_page_count++;
    g_assert_false(pgraph_vk_draw_omission_state_is_clean(
        &renderer, &checkpoint));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(
        "/xbox/vk/omitted-draw-state/repeated-all-encodings",
        test_repeated_omission_discards_draw_local_state);
    g_test_add_func(
        "/xbox/vk/omitted-draw-state/reject-committed-side-effects",
        test_omission_rejects_staging_or_stale_page_commit);
    return g_test_run();
}
