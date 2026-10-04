/*
 * Production Vulkan depth alias producer selection tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#define pgraph_vk_finish test_unused_real_finish
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#undef pgraph_vk_finish

/* GPU submission is external; ownership and list retirement stay real. */
static void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_assert_false(r->in_draw);
    g_assert_cmpuint(r->debug_depth, ==, 0);
    r->in_command_buffer = false;
    r->in_render_pass = false;
}

#include "hw/xbox/nv2a/pgraph/vk/surface.c"

bool tcg_allowed;

CPUState *qemu_get_cpu(int index)
{
    g_assert_not_reached();
}

void mem_access_callback_remove_by_ref(CPUState *cpu, MemAccessCallback *ref)
{
    g_assert_not_reached();
}

static void test_producer_lookup(gconstpointer data)
{
    VkFormat format = GPOINTER_TO_UINT(data);
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding producer = {
        .vram_addr = 0x200000,
        .dma_addr = 0x100000,
        .dma_len = 0x800000,
        .size = 640 * 480 * 4,
        .width = 640,
        .height = 480,
        .pitch = 640 * 4,
        .fmt.bytes_per_pixel = 4,
        .shape.zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,
        .host_fmt.vk_format = format,
        .initialized = true,
        .draw_dirty = true,
    };
    SurfaceBinding view = producer;
    SurfaceBinding third = producer;

    d->pgraph.vk_renderer_state = r;
    d->pgraph.surface_scale_factor = 1;
    d->pgraph.surface_shape.anti_aliasing =
        NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1;
    view.width = view.height = 32;
    view.size = view.height * view.pitch;
    view.swizzle = true;
    view.draw_dirty = false;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INSERT_TAIL(&r->surfaces, &view, entry);
    QTAILQ_INSERT_TAIL(&r->surfaces, &producer, entry);

    /* Use the production mapping, including guest pitch and byte extent. */
    PGRAPHVkDepthAliasView mapped = depth_alias_view(&view);
    g_assert_cmpuint(mapped.pitch, ==, 2560);
    g_assert_cmpuint(mapped.extent, ==, 81920);
    g_assert_cmpuint(mapped.host_format, ==, format);
    g_assert_true(mapped.host_supported && mapped.guest_z24s8);
    g_assert_true(mapped.swizzled);
    g_assert_false(mapped.draw_dirty);
    g_assert_true(pgraph_vk_find_depth_alias_producer(d, &view, &view) ==
                  &producer);
    /* Generic lookup retains its first-match contract for other callers. */
    g_assert_true(pgraph_vk_surface_get(d, view.vram_addr) == &view);

    producer.draw_dirty = false;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    producer.draw_dirty = true;
    producer.readback_superseded_by_guest = true;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    producer.readback_superseded_by_guest = false;
    producer.upload_pending = true;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    producer.upload_pending = false;
    view.upload_pending = true;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    view.upload_pending = false;
    view.draw_dirty = true;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    view.draw_dirty = false;

    view.dma_addr++;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    view.dma_addr--;
    producer.pitch += 4;
    view.pitch += 4;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    producer.pitch -= 4;
    view.pitch -= 4;
    d->pgraph.clearing = true;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    d->pgraph.clearing = false;
    d->pgraph.regs_[NV_PGRAPH_CONTROL_0] =
        NV_PGRAPH_CONTROL_0_ZENABLE | NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    d->pgraph.regs_[NV_PGRAPH_CONTROL_0] = 0;

    /* An unclassified third overlapping image can hold a newer version. */
    third.vram_addr++;
    QTAILQ_INSERT_TAIL(&r->surfaces, &third, entry);
    g_assert_null(pgraph_vk_find_depth_alias_producer(d, &view, &view));
    QTAILQ_REMOVE(&r->surfaces, &third, entry);
    g_assert_true(pgraph_vk_find_depth_alias_producer(d, &view, &view) ==
                  &producer);
}

static void test_producer_rewrite(gconstpointer data)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding producer = {
        .vram_addr = 4096, .size = 64, .initialized = true, .draw_dirty = true
    };
    SurfaceBinding view = producer;
    SurfaceBinding unrelated = producer;

    d->pgraph.vk_renderer_state = r;
    view.swizzle = true;
    view.draw_dirty = false;
    unrelated.vram_addr = 8192;
    unrelated.swizzle = true;
    unrelated.draw_dirty = false;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    QTAILQ_INSERT_TAIL(&r->surfaces, &view, entry);
    QTAILQ_INSERT_TAIL(&r->surfaces, &producer, entry);
    QTAILQ_INSERT_TAIL(&r->surfaces, &unrelated, entry);
    r->zeta_binding = &producer;

    /* Read-only use preserves the converted image. */
    pgraph_vk_set_surface_dirty(&d->pgraph, false, true);
    g_assert_true(pgraph_vk_surface_get(d, 4096) == &view);
    d->pgraph.clearing = GPOINTER_TO_INT(data);
    if (!d->pgraph.clearing) {
        d->pgraph.regs_[NV_PGRAPH_CONTROL_0] =
            NV_PGRAPH_CONTROL_0_ZENABLE | NV_PGRAPH_CONTROL_0_ZWRITEENABLE;
    }
    r->in_command_buffer = true;
    r->in_render_pass = true;
    pgraph_vk_set_surface_dirty(&d->pgraph, false, true);
    /* A later texture lookup must not receive the old converted image. */
    g_assert_true(pgraph_vk_surface_get(d, 4096) == &producer);
    g_assert_true(surface_is_tracked(r, &producer));
    g_assert_false(surface_is_tracked(r, &view));
    g_assert_true(surface_is_tracked(r, &unrelated));
    g_assert_true(producer.draw_dirty);
    g_assert_false(r->in_command_buffer);
    g_assert_false(r->in_render_pass);
    g_assert_true(QTAILQ_FIRST(&r->invalid_surfaces) == &view);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/alias/producer-d24s8",
                         GUINT_TO_POINTER(VK_FORMAT_D24_UNORM_S8_UINT),
                         test_producer_lookup);
    g_test_add_data_func("/xbox/vk/alias/producer-d32s8",
                         GUINT_TO_POINTER(VK_FORMAT_D32_SFLOAT_S8_UINT),
                         test_producer_lookup);
    g_test_add_data_func("/xbox/vk/alias/producer-rewrite", NULL,
                         test_producer_rewrite);
    g_test_add_data_func("/xbox/vk/alias/producer-clear", GINT_TO_POINTER(1),
                         test_producer_rewrite);
    return g_test_run();
}
