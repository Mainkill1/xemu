/*
 * Guest Z24S8 surface alias layout tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "hw/xbox/nv2a/pgraph/vk/surface-alias-map.h"

static void compare_to_guest_swizzle(uint32_t width, uint32_t height)
{
    uint32_t linear[1024];
    uint32_t swizzled[1024] = { 0 };
    uint32_t count = width * height;

    g_assert_cmpuint(count, <=, G_N_ELEMENTS(linear));
    for (uint32_t i = 0; i < count; i++) {
        linear[i] = 0x12000000u | i;
    }
    swizzle_box((const uint8_t *)linear, width, height, 1,
                (uint8_t *)swizzled, width * sizeof(uint32_t), 0,
                sizeof(uint32_t));

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t index = UINT32_MAX;

            g_assert_true(pgraph_vk_alias_morton_index(
                x, y, width, height, &index));
            g_assert_cmpuint(index, <, count);
            g_assert_cmphex(swizzled[index], ==, linear[y * width + x]);
        }
    }
}

static void test_guest_morton_layout(void)
{
    compare_to_guest_swizzle(32, 32);
    compare_to_guest_swizzle(16, 32);
    compare_to_guest_swizzle(32, 16);
}

static void test_invalid_morton_layout(void)
{
    uint32_t index = UINT32_MAX;

    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 0, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 0, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 30, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 30, &index));
    g_assert_false(pgraph_vk_alias_morton_index(32, 0, 32, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 32, 32, 32, &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 65536, 65536,
                                                &index));
    g_assert_false(pgraph_vk_alias_morton_index(0, 0, 32, 32, NULL));
}

static void test_read_only_alias_eligibility(void)
{
    PGRAPHVkDepthAliasView producer = {
        .address = 0x200000,
        .dma_address = 0x100000,
        .dma_length = 0x800000,
        .extent = 640 * 480 * 4,
        .width = 640,
        .height = 480,
        .pitch = 640 * 4,
        .host_format = 1,
        .guest_z24s8 = true,
        .host_supported = true,
        .initialized = true,
    };
    PGRAPHVkDepthAliasView view = producer;

    view.width = 32;
    view.height = 32;
    view.extent = 32 * producer.pitch;
    view.swizzled = true;

    g_assert_true(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));

    /* Each otherwise valid pair must reject an incompatible field alone. */
#define REJECT_FIELD(which, field, value) do {                            \
        PGRAPHVkDepthAliasView saved = which;                            \
        which.field = value;                                            \
        g_assert_false(pgraph_vk_depth_alias_read_only_eligible(         \
            &producer, &view, false, false, 1, false));                  \
        which = saved;                                                  \
    } while (0)
    REJECT_FIELD(producer, color, true);
    REJECT_FIELD(view, color, true);
    REJECT_FIELD(producer, swizzled, true);
    REJECT_FIELD(view, swizzled, false);
    REJECT_FIELD(producer, initialized, false);
    REJECT_FIELD(producer, download_pending, true);
    REJECT_FIELD(producer, guest_z24s8, false);
    REJECT_FIELD(view, guest_z24s8, false);
    REJECT_FIELD(view, host_supported, false);
    REJECT_FIELD(producer, host_format, 0);
    REJECT_FIELD(view, host_format, 2);
    REJECT_FIELD(view, dma_length, view.dma_length - 1);
    REJECT_FIELD(view, pitch, view.pitch + 4);
    REJECT_FIELD(view, extent, view.width * view.height * 4 - 1);
    REJECT_FIELD(view, width, 1024);
    REJECT_FIELD(view, height, 512);
    REJECT_FIELD(view, height, 0);
#undef REJECT_FIELD
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        NULL, &view, false, false, 1, false));
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, NULL, false, false, 1, false));

    producer.upload_pending = true;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    producer.upload_pending = false;

    view.dma_address++;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    view.dma_address--;

    view.address += 4;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    view.address -= 4;

    producer.pitch += 4;
    view.pitch += 4;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    producer.pitch -= 4;
    view.pitch -= 4;

    producer.extent = view.extent;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    producer.extent = 640 * 480 * 4;

    producer.host_supported = false;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    producer.host_supported = true;

    producer.superseded_by_guest = true;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
    producer.superseded_by_guest = false;

    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, true, false, 1, false));
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, true, 1, false));
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 2, false));
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, true));

    view.width = 30;
    g_assert_false(pgraph_vk_depth_alias_read_only_eligible(
        &producer, &view, false, false, 1, false));
}

static void test_depth_alias_buffer_plan(void)
{
    PGRAPHVkDepthAliasPlan plan;

    g_assert_true(pgraph_vk_depth_alias_plan(640, 480, 32, 32, 256,
                                             2 * 1024 * 1024, &plan));
    g_assert_cmpuint(plan.producer_pixels, ==, 1280);
    g_assert_cmpuint(plan.view_pixels, ==, 1024);
    g_assert_cmpuint(plan.producer_stencil_offset, ==, 5120);
    g_assert_cmpuint(plan.view_stencil_offset, ==, 4096);
    g_assert_cmpuint(plan.compute_dst_bytes, ==, 6400);
    g_assert_cmpuint(plan.compute_src_bytes, ==, 5120);

    /* One partial source row and a final partial row must stay in bounds. */
    g_assert_true(pgraph_vk_depth_alias_plan(64, 16, 4, 4, 256,
                                             4096, &plan));
    g_assert_cmpuint(plan.producer_pixels, ==, 64);
    g_assert_true(pgraph_vk_depth_alias_plan(640, 480, 512, 512, 256,
                                             2 * 1024 * 1024, &plan));
    g_assert_cmpuint(plan.producer_pixels, ==, 262400);
    g_assert_false(pgraph_vk_depth_alias_plan(64, 16, 64, 32, 256,
                                              2 * 1024 * 1024, &plan));

    /* Packed stencil reads use uint words, including padded source rows. */
    g_assert_true(pgraph_vk_depth_alias_plan(34, 64, 32, 32, 256,
                                             8192, &plan));
    g_assert_cmpuint(plan.producer_pixels, ==, 1088);
    g_assert_true(pgraph_vk_depth_alias_plan(35, 64, 32, 32, 256,
                                             8192, &plan));
    g_assert_cmpuint(plan.producer_pixels, ==, 1120);
    g_assert_false(pgraph_vk_depth_alias_plan(1, 1, 1, 1, 256,
                                              4096, &plan));

    g_assert_false(pgraph_vk_depth_alias_plan(640, 480, 32, 32, 3,
                                              2 * 1024 * 1024, &plan));
    g_assert_false(pgraph_vk_depth_alias_plan(640, 480, 32, 32, 256,
                                              6399, &plan));
    g_assert_false(pgraph_vk_depth_alias_plan(640, 480, 0, 32, 256,
                                              2 * 1024 * 1024, &plan));
    g_assert_false(pgraph_vk_depth_alias_plan(640, 480, 32, 32, 256,
                                              2 * 1024 * 1024, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/surface-alias/guest-morton-layout",
                    test_guest_morton_layout);
    g_test_add_func("/xbox/vk/surface-alias/reject-invalid-shapes",
                    test_invalid_morton_layout);
    g_test_add_func("/xbox/vk/surface-alias/read-only-eligibility",
                    test_read_only_alias_eligibility);
    g_test_add_func("/xbox/vk/surface-alias/buffer-plan",
                    test_depth_alias_buffer_plan);
    return g_test_run();
}
