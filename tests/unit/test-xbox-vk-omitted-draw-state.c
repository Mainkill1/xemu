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
        PGRAPHState *pg = g_new0(PGRAPHState, 1);
        PGRAPHVkState renderer = { 0 };
        pg->vk_renderer_state = &renderer;
        pg->compressed_attrs = 1;
        pg->uniform_attrs = 2;
        pg->swizzle_attrs = 4;
        pg->vertex_attributes[0].inline_array_offset = 17;
        pg->vertex_attributes[0].inline_buffer_populated = true;
        pg->vertex_attributes[0].inline_value[0] = 1.25f;
        renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset =
            128;
        renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset = 64;
        renderer.vertex_ram_stale_page_count = 3;
        renderer.num_pending_vertex_ram_reads = 4;
        renderer.num_vertex_ram_buffer_syncs = 2;
        renderer.vertex_ram_buffer_syncs[0].addr = 0x1000;
        renderer.pending_vertex_ram_reads[0].addr = 0x2000;
        renderer.num_active_vertex_attribute_descriptions = 1;
        renderer.vertex_attribute_descriptions[0].location = 3;
        renderer.vertex_attribute_to_description_location[0] = 5;
        renderer.num_active_vertex_binding_descriptions = 1;
        renderer.vertex_binding_descriptions[0].stride = 32;
        renderer.vertex_attribute_offsets[0] = 0x3000;

        for (unsigned int attempt = 0; attempt < 3; attempt++) {
            PGRAPHVkDrawOmissionCheckpoint checkpoint;
            pgraph_vk_draw_omission_checkpoint_capture(
                pg, &renderer, encodings[i], &checkpoint);

            renderer.num_pending_vertex_ram_reads = 9;
            renderer.num_vertex_ram_buffer_syncs = 8;
            pg->compressed_attrs = 8;
            pg->uniform_attrs = 16;
            pg->swizzle_attrs = 32;
            pg->vertex_attributes[0].inline_array_offset = 99;
            pg->vertex_attributes[0].inline_buffer_populated = false;
            pg->vertex_attributes[0].inline_value[0] = -3.0f;
            renderer.vertex_ram_buffer_syncs[0].addr = 0x4000;
            renderer.pending_vertex_ram_reads[0].addr = 0x5000;
            renderer.num_active_vertex_attribute_descriptions = 2;
            renderer.vertex_attribute_descriptions[0].location = 7;
            renderer.vertex_attribute_to_description_location[0] = 9;
            renderer.num_active_vertex_binding_descriptions = 2;
            renderer.vertex_binding_descriptions[0].stride = 64;
            renderer.vertex_attribute_offsets[0] = 0x6000;
            /* A completed mirror upload may make a stale page current. */
            renderer.vertex_ram_stale_page_count = 2;

            g_assert_true(pgraph_vk_draw_omission_state_is_clean(
                &renderer, &checkpoint));
            pgraph_vk_discard_unsubmitted_draw_state(
                pg, &renderer, &checkpoint);

            g_assert_cmpuint(renderer.num_pending_vertex_ram_reads, ==, 4);
            g_assert_cmpuint(renderer.num_vertex_ram_buffer_syncs, ==, 2);
            g_assert_cmpuint(
                renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING]
                    .buffer_offset,
                ==, 128);
            g_assert_cmpuint(
                renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset,
                ==, 64);
            g_assert_cmpuint(renderer.vertex_ram_stale_page_count, ==, 2);
            g_assert_cmpuint(pg->compressed_attrs, ==, 1);
            g_assert_cmpuint(pg->uniform_attrs, ==, 2);
            g_assert_cmpuint(pg->swizzle_attrs, ==, 4);
            g_assert_cmpuint(pg->vertex_attributes[0].inline_array_offset, ==,
                             17);
            g_assert_true(
                pg->vertex_attributes[0].inline_buffer_populated);
            g_assert_cmpfloat(pg->vertex_attributes[0].inline_value[0], ==,
                              1.25f);
            g_assert_cmphex(renderer.vertex_ram_buffer_syncs[0].addr, ==,
                            0x1000);
            g_assert_cmphex(renderer.pending_vertex_ram_reads[0].addr, ==,
                            0x2000);
            g_assert_cmpuint(
                renderer.num_active_vertex_attribute_descriptions, ==, 1);
            g_assert_cmpuint(renderer.vertex_attribute_descriptions[0].location,
                             ==, 3);
            g_assert_cmpint(
                renderer.vertex_attribute_to_description_location[0], ==, 5);
            g_assert_cmpuint(renderer.num_active_vertex_binding_descriptions,
                             ==, 1);
            g_assert_cmpuint(renderer.vertex_binding_descriptions[0].stride,
                             ==, 32);
            g_assert_cmphex(renderer.vertex_attribute_offsets[0], ==, 0x3000);
            g_assert_true(pgraph_vk_draw_omission_transaction_restored(
                pg, &renderer, &checkpoint));
        }
        g_free(pg);
    }
}

static void test_omission_rejects_staging_or_stale_page_commit(void)
{
    PGRAPHVkState renderer = { 0 };
    renderer.storage_buffers[BUFFER_VERTEX_INLINE_STAGING].buffer_offset = 32;
    renderer.storage_buffers[BUFFER_INDEX_STAGING].buffer_offset = 16;
    renderer.vertex_ram_stale_page_count = 1;

    PGRAPHVkDrawOmissionCheckpoint checkpoint;
    PGRAPHState *pg = g_new0(PGRAPHState, 1);
    pg->vk_renderer_state = &renderer;
    pgraph_vk_draw_omission_checkpoint_capture(
        pg, &renderer, PGRAPH_VK_DRAW_ENCODING_INLINE_ELEMENTS, &checkpoint);

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
    g_free(pg);
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
