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

static bool tail_guest_write;

static unsigned dirty_reads;
static hwaddr first_dirty_size;
static bool expect_prefix_query;

static void record_dirty_query(hwaddr size)
{
    if (!dirty_reads++) {
        first_dirty_size = size;
    }
    if (expect_prefix_query) {
        g_assert_cmpuint(size, ==, 64);
        g_printerr("retarget keeps requested dirty range\n");
        _exit(42);
    }
}

static bool expect_depth_rejection;

bool memory_region_take_dirty_pages(MemoryRegion *mr, hwaddr start, hwaddr size,
                                    unsigned client, unsigned long *pages,
                                    size_t capacity)
{
    record_dirty_query(size);
    memset(pages, 0, capacity * sizeof(*pages));
    if (tail_guest_write && start <= 8192 && start + size > 8192) {
        pages[(8192 / 4096 - start / 4096) / (sizeof(long) * 8)] |=
            1ul << ((8192 / 4096 - start / 4096) % (sizeof(long) * 8));
        tail_guest_write = false;
        return true;
    }
    return false;
}

/* External GPU commands are forbidden in this clean-surface fixture. */
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
void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    if (expect_depth_rejection && reason == VK_FINISH_REASON_SURFACE_DOWN) {
        g_printerr("incompatible depth storage rejected before reuse\n");
        _exit(42);
    }
    g_assert_not_reached();
}
void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg)
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

/* The cached depth attachment covers the color viewport without a transfer. */
static void test_larger_linear_depth_case(bool guest_write, int reject_case)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    MemoryRegion vram = { .size = int128_make64(16384) };
    SurfaceBinding color = { 0 };
    SurfaceBinding depth = { 0 };
    PGRAPHState *pg = &d->pgraph;

    pg->vk_renderer_state = r;
    d->vram = &vram;
    qemu_mutex_init(&pg->lock);
    qemu_mutex_init(&d->pfifo.lock);
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    pg->surface_shape.clip_width = 4;
    pg->surface_shape.clip_height = 4;
    pg->surface_shape.zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z16;
    pg->surface_zeta.pitch = 16;
    pg->surface_scale_factor = 1;
    pg->regs_[NV_PGRAPH_CONTROL_0] = NV_PGRAPH_CONTROL_0_ZENABLE;
    pg->last_surface_shape = pg->surface_shape;
    pg->surface_binding_dim.width = pg->surface_binding_dim.height = 4;
    pg->surface_binding_dim.clip_width = 4;
    pg->surface_binding_dim.clip_height = 4;
    r->kelvin_surface_zeta_vk_map[pg->surface_shape.zeta_format] =
        (SurfaceFormatInfo){ .vk_format = VK_FORMAT_D16_UNORM,
                             .host_bytes_per_pixel = 2 };
    color.color = true;
    color.width = color.height = 4;
    color.initialized = true;
    color.vram_addr = 4096;
    color.shape = pg->surface_shape;
    color.d = d;
    r->color_binding = &color;
    depth.host_fmt.vk_format = VK_FORMAT_D16_UNORM;
    depth.fmt.bytes_per_pixel = 2;
    depth.shape = pg->surface_shape;
    depth.width = 8;
    depth.height = guest_write ? 1024 : 8;
    depth.pitch = 16;
    depth.size = depth.height * depth.pitch;
    depth.initialized = true;
    depth.d = d;
    switch (reject_case) {
    case 1:
        depth.shape.anti_aliasing = 1;
        break;
    case 2:
        depth.swizzle = true;
        break;
    case 3:
        depth.pitch = 32;
        depth.size = depth.height * depth.pitch;
        break;
    case 4:
        depth.host_fmt.vk_format = VK_FORMAT_D32_SFLOAT;
        break;
    case 5:
        depth.width = 2;
        break;
    }
    QTAILQ_INSERT_TAIL(&r->surfaces, &depth, entry);

    if (guest_write) {
        tail_guest_write = true;
        update_surface_part(d, true, false);
        g_assert_false(tail_guest_write);
        g_assert_true(depth.upload_pending);
        g_assert_true(depth.readback_superseded_by_guest);
    } else {
        pgraph_vk_surface_update(d, true, false, true);
    }
    g_assert_true(r->zeta_binding == &depth);
    g_assert_cmpuint(depth.width, ==, 8);
    g_assert_cmpuint(depth.height, ==, guest_write ? 1024 : 8);
    g_assert_cmpuint(pg->surface_binding_dim.width, ==, 4);
    g_assert_cmpuint(pg->surface_binding_dim.height, ==, 4);
    g_assert_cmpuint(pg->surface_binding_dim.clip_width, ==, 4);
    g_assert_cmpuint(pg->surface_binding_dim.clip_height, ==, 4);
    g_assert_cmpint(depth.upload_pending, ==, guest_write);
    g_assert_false(depth.draw_dirty);
    g_assert_true(QTAILQ_FIRST(&r->surfaces) == &depth);
    g_assert_true(QTAILQ_EMPTY(&r->invalid_surfaces));

    qemu_mutex_destroy(&d->pfifo.lock);
    qemu_mutex_destroy(&pg->lock);
    g_free(r->surface_dirty_page_bits);
}

static void test_larger_linear_depth(void)
{
    test_larger_linear_depth_case(false, 0);
}

static void test_larger_depth_tail_guest_write(void)
{
    test_larger_linear_depth_case(true, 0);
}

static void test_incompatible_depth(gconstpointer data)
{
    if (g_test_subprocess()) {
        expect_depth_rejection = true;
        test_larger_linear_depth_case(false, GPOINTER_TO_INT(data));
        return;
    }
    g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_failed();
    g_test_trap_assert_stderr(
        "*incompatible depth storage rejected before reuse*");
}


/*
 * The dirty producer is the host-memory boundary. Production target selection,
 * dirty-range resolution and binding execute unchanged. Negative retarget cases
 * stop at their first query, before the unrelated GPU replacement path.
 */
static void test_bound_depth_dirty_range(gconstpointer opaque)
{
    int test_case = GPOINTER_TO_INT(opaque);
    if (test_case >= 4 && !g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_failed();
        g_test_trap_assert_stderr("*retarget keeps requested dirty range*");
        return;
    }
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    MemoryRegion vram = { .size = int128_make64(32768) };
    SurfaceBinding color = { .color = true, .width = 4, .height = 4 };
    SurfaceBinding depth = {
        .width = 8, .height = 1024, .pitch = 16, .size = 16384
    };
    PGRAPHState *pg = &d->pgraph;
    pg->vk_renderer_state = r;
    d->vram = &vram;
    qemu_mutex_init(&pg->lock);
    qemu_mutex_init(&d->pfifo.lock);
    pg->surface_shape.clip_width = pg->surface_shape.clip_height = 4;
    pg->surface_shape.zeta_format = NV097_SET_SURFACE_FORMAT_ZETA_Z16;
    pg->surface_zeta.pitch = 16;
    pg->surface_scale_factor = 1;
    pg->last_surface_shape = pg->surface_shape;
    pg->surface_binding_dim.width = pg->surface_binding_dim.height = 4;
    color.shape = depth.shape = pg->surface_shape;
    depth.d = d;
    depth.initialized = color.initialized = true;
    depth.host_fmt.vk_format = VK_FORMAT_D16_UNORM;
    depth.fmt.bytes_per_pixel = 2;
    r->kelvin_surface_zeta_vk_map[pg->surface_shape.zeta_format] =
        (SurfaceFormatInfo){ .vk_format = VK_FORMAT_D16_UNORM,
                             .host_bytes_per_pixel = 2 };
    r->color_binding = &color;
    r->zeta_binding = &depth;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    QTAILQ_INSERT_TAIL(&r->surfaces, &depth, entry);
    switch (test_case) {
    case 4:
        depth.vram_addr = 4096;
        break;
    case 5:
        depth.shape.anti_aliasing = 1;
        break;
    case 6:
        depth.swizzle = true;
        break;
    case 7:
        pg->surface_type = NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE;
        break;
    case 8:
        depth.host_fmt.vk_format = VK_FORMAT_D32_SFLOAT;
        break;
    case 9:
        depth.pitch = 8;
        depth.size = 8192;
        break;
    case 10:
        depth.width = 2;
        break;
    case 11:
        depth.height = 2;
        depth.pitch = 16;
        depth.size = 32;
        break;
    case 12:
        depth.width = depth.height = 4;
        depth.size = 64;
        break;
    default:
        break;
    }
    pg->surface_zeta.buffer_dirty = test_case >= 4;
    expect_prefix_query = test_case >= 4;
    dirty_reads = 0;
    first_dirty_size = 0;
    tail_guest_write = test_case != 2;
    tcg_allowed = test_case == 1;
    if (tcg_allowed) {
        surface_access_callback(&depth, &vram, 8192, 4, true);
        g_assert_true(depth.upload_pending);
        g_assert_false(pg->surface_zeta.buffer_dirty);
    }
    update_surface_part(d, test_case != 3, false);
    g_assert_true(r->zeta_binding == &depth);
    bool consumed = test_case == 0 || test_case == 1;
    g_assert_cmpint(tail_guest_write, ==, test_case != 2 && !consumed);
    g_assert_cmpint(depth.upload_pending, ==, consumed || tcg_allowed);
    g_assert_cmpuint(first_dirty_size, ==, test_case == 3 ? 64 : 16384);
    g_assert_cmpuint(dirty_reads, ==, 1);
    g_assert_cmpuint(pg->surface_binding_dim.width, ==, 4);
    g_assert_cmpuint(pg->surface_binding_dim.height, ==, 4);
    g_assert_cmpint(depth.readback_superseded_by_guest, ==, consumed);
    tcg_allowed = false;
    tail_guest_write = false;
    qemu_mutex_destroy(&d->pfifo.lock);
    qemu_mutex_destroy(&pg->lock);
    g_free(r->surface_dirty_page_bits);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/surface/read-only-depth-ownership",
                    test_read_only_surface);
    g_test_add_func("/xbox/vk/surface/explicit-clear-ownership",
                    test_explicit_clear_ownership);
    g_test_add_func("/xbox/vk/surface/larger-linear-depth",
                    test_larger_linear_depth);
    g_test_add_func("/xbox/vk/surface/larger-depth-tail-guest-write",
                    test_larger_depth_tail_guest_write);
    g_test_add_data_func("/xbox/vk/surface/reject-aa", GINT_TO_POINTER(1),
                         test_incompatible_depth);
    g_test_add_data_func("/xbox/vk/surface/reject-swizzle", GINT_TO_POINTER(2),
                         test_incompatible_depth);
    g_test_add_data_func("/xbox/vk/surface/reject-pitch", GINT_TO_POINTER(3),
                         test_incompatible_depth);
    g_test_add_data_func("/xbox/vk/surface/reject-format", GINT_TO_POINTER(4),
                         test_incompatible_depth);
    g_test_add_data_func("/xbox/vk/surface/reject-short-width",
                         GINT_TO_POINTER(5), test_incompatible_depth);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/kvm",
                         GINT_TO_POINTER(0), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/tcg",
                         GINT_TO_POINTER(1), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/no-write",
                         GINT_TO_POINTER(2), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/readback",
                         GINT_TO_POINTER(3), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/address",
                         GINT_TO_POINTER(4), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/aa",
                         GINT_TO_POINTER(5), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/binding-layout",
                         GINT_TO_POINTER(6), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/target-layout",
                         GINT_TO_POINTER(7), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/format",
                         GINT_TO_POINTER(8), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/pitch",
                         GINT_TO_POINTER(9), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/short-width",
                         GINT_TO_POINTER(10), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/short-height",
                         GINT_TO_POINTER(11), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/vk/surface/bound-tail/equal-extent",
                         GINT_TO_POINTER(12), test_bound_depth_dirty_range);
    return g_test_run();
}
