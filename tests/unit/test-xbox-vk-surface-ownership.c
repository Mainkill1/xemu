/*
 * Production Vulkan read-only surface ownership regression
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface.c"


int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;
void pgraph_vk_text_debug_printf(const char *format, ...)
{
    (void)format;
}

DMAObject nv_dma_load(NV2AState *d, hwaddr address)
{
    return (DMAObject){ .dma_class = NV_DMA_IN_MEMORY_CLASS,
                        .address = 0,
                        .limit = 16383 };
}

bool memory_region_take_dirty_pages(MemoryRegion *mr, hwaddr start, hwaddr size,
                                    unsigned client, unsigned long *pages,
                                    size_t capacity)
{
    memset(pages, 0, capacity * sizeof(*pages));
    return false;
}

/* External GPU commands are forbidden in this clean-surface fixture. */
bool pgraph_vk_convert_depth_alias(PGRAPHState *pg, SurfaceBinding *producer,
                                   SurfaceBinding *view)
{
    g_assert_not_reached();
}

NV2AStats g_nv2a_stats;
XemuTweakBits xemu_tweaks_active;
bool tcg_allowed;
/* RAM metadata and a clean dirty-page response replace the host memory
 * boundary. Target selection, binding, upload gating, dirty attribution and
 * the CPU-read callback execute the maintained production code. */
uint64_t memory_region_size(MemoryRegion *mr)
{
    return int128_get64(mr->size);
}
void memory_region_set_client_dirty(MemoryRegion *mr, hwaddr a, hwaddr s,
                                    unsigned client)
{
    g_assert_not_reached();
}
void nv2a_profile_log_event_once(NV2AProfileEvent event)
{
    g_assert_not_reached();
}
void pfifo_kick(NV2AState *d)
{
    g_assert_not_reached();
}
CPUState *qemu_get_cpu(int index)
{
    g_assert_not_reached();
}
MemAccessCallback *mem_access_callback_insert(CPUState *cpu, MemoryRegion *mr,
                                              hwaddr offset, hwaddr len,
                                              MemAccessCallbackFunc func,
                                              void *opaque)
{
    g_assert_not_reached();
}
void mem_access_callback_remove_by_ref(CPUState *cpu, MemAccessCallback *cb)
{
    g_assert_not_reached();
}
void __wrap_pgraph_vk_finish(PGRAPHState *pg, FinishReason reason);
void __wrap_pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    g_assert_not_reached();
}
void __wrap_pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg);
void __wrap_pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg)
{
    g_assert_false(pg->vk_renderer_state->in_render_pass);
}
void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...)
{
    g_assert_not_reached();
}
void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
    g_assert_not_reached();
}
VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{
    g_assert_not_reached();
}
void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SingleTimeReason reason,
                                        uint64_t staged_bytes)
{
    g_assert_not_reached();
}
void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer cmd,
                                       VkImage image, VkFormat format,
                                       VkImageLayout oldLayout,
                                       VkImageLayout newLayout)
{
    g_assert_not_reached();
}
void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize required_size)
{
    g_assert_not_reached();
}
bool pgraph_vk_compute_needs_finish(PGRAPHVkState *r)
{
    g_assert_not_reached();
}
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    g_assert_not_reached();
}
void pgraph_vk_unpack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                    VkCommandBuffer cmd, VkBuffer src,
                                    VkBuffer dst)
{
    g_assert_not_reached();
}

/* Removing read-only zeta access must fail the binding assertion.
 * Incorrect dirty attribution must fail before reaching a GPU wake/transfer. */
static void test_read_only_surface(void)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    MemoryRegion vram = { .size = int128_make64(16384) };
    SurfaceBinding binding = { 0 };
    d->pgraph.vk_renderer_state = r;
    qemu_mutex_init(&d->pgraph.lock);
    qemu_mutex_init(&d->pfifo.lock);
    d->vram = &vram;
    PGRAPHState *pg = &d->pgraph;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    pg->surface_shape.clip_width = 4;
    pg->surface_shape.clip_height = 4;
    pg->surface_shape.zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z16;
    pg->surface_zeta.pitch = 8;
    pg->surface_scale_factor = 1;
    pg->regs_[NV_PGRAPH_CONTROL_0] = NV_PGRAPH_CONTROL_0_ZENABLE;
    pg->last_surface_shape = pg->surface_shape;
    r->kelvin_surface_zeta_vk_map[pg->surface_shape.zeta_format] =
        (SurfaceFormatInfo){ .vk_format = VK_FORMAT_D16_UNORM,
                             .host_bytes_per_pixel = 2 };
    binding.host_fmt.vk_format = VK_FORMAT_D16_UNORM;
    binding.fmt.bytes_per_pixel = 2;
    binding.shape = pg->surface_shape;
    binding.width = binding.height = 4;
    binding.pitch = 8;
    binding.size = 32;
    binding.initialized = true;
    binding.d = d;
    QTAILQ_INSERT_TAIL(&r->surfaces, &binding, entry);
    pgraph_vk_surface_update(d, true, false, true);
    g_assert_true(r->zeta_binding == &binding);
    g_assert_cmpuint(pg->draw_time, ==, 1);
    g_assert_cmpuint(binding.draw_time, ==, 1);
    pgraph_vk_set_surface_dirty(pg, false, true);
    g_assert_false(pg->surface_zeta.draw_dirty);
    g_assert_false(binding.draw_dirty);
    surface_access_callback(&binding, &vram, 0, 2, false);
    g_assert_false(binding.download_pending);
    g_assert_true(pgraph_vk_surface_download_if_dirty(d, &binding));
    qemu_mutex_destroy(&d->pfifo.lock);
    qemu_mutex_destroy(&d->pgraph.lock);
    g_free(r->surface_dirty_page_bits);
}


/* Clear attribution is independent of ordinary draw write-enable bits.
 * This exercises the production setter; native clear execution remains a
 * separate gate and is not simulated by this fixture. */
static void test_explicit_clear_ownership(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    SurfaceBinding zeta_binding = { 0 };
    SurfaceBinding color_binding = { 0 };

    pg->vk_renderer_state = r;
    r->zeta_binding = &zeta_binding;
    r->color_binding = &color_binding;
    pg->clearing = true;
    pgraph_vk_set_surface_dirty(pg, false, true);
    g_assert_true(pg->surface_zeta.draw_dirty);
    g_assert_true(zeta_binding.draw_dirty);
    g_assert_false(pg->surface_color.draw_dirty);
    g_assert_false(color_binding.draw_dirty);

    /* An explicit color clear owns its result with draw masks disabled. */
    pgraph_vk_set_surface_dirty(pg, true, false);
    g_assert_true(pg->surface_color.draw_dirty);
    g_assert_true(color_binding.draw_dirty);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/surface/read-only-depth-ownership",
                    test_read_only_surface);
    g_test_add_func("/xbox/vk/surface/explicit-clear-ownership",
                    test_explicit_clear_ownership);
    return g_test_run();
}
