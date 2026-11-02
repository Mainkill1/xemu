/*
 * Production OpenGL retained-depth dirty-range regression.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/gl/renderer.h"

static void fixture_framebuffer_texture(GLenum target, GLenum attachment,
                                        GLenum texture_target, GLuint texture,
                                        GLint level);
static GLenum fixture_framebuffer_status(GLenum target);
#undef glFramebufferTexture2D
#undef glCheckFramebufferStatus
#define glFramebufferTexture2D fixture_framebuffer_texture
#define glCheckFramebufferStatus fixture_framebuffer_status
#include "hw/xbox/nv2a/pgraph/gl/surface.c"

bool tcg_allowed;
NV2AStats g_nv2a_stats;
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

/* Attachment calls are boundaries, not physical GPU validation. */
static void fixture_framebuffer_texture(GLenum target, GLenum attachment,
                                        GLenum texture_target, GLuint texture,
                                        GLint level)
{
    g_assert_cmpuint(target, ==, GL_FRAMEBUFFER);
    g_assert_true(attachment == GL_DEPTH_ATTACHMENT ||
                  attachment == GL_DEPTH_STENCIL_ATTACHMENT);
    g_assert_cmpuint(texture_target, ==, GL_TEXTURE_2D);
    g_assert_cmpuint(texture, ==, 0);
    g_assert_cmpint(level, ==, 0);
}
static GLenum fixture_framebuffer_status(GLenum target)
{
    g_assert_cmpuint(target, ==, GL_FRAMEBUFFER);
    return GL_FRAMEBUFFER_COMPLETE;
}
DMAObject nv_dma_load(NV2AState *d, hwaddr address)
{
    return (DMAObject){ .dma_class = NV_DMA_IN_MEMORY_CLASS,
                        .address = 0,
                        .limit = 32767 };
}
bool memory_region_test_and_clear_dirty(MemoryRegion *mr, hwaddr start,
                                        hwaddr size, unsigned client)
{
    record_dirty_query(size);
    g_assert_cmpuint(client, ==, DIRTY_MEMORY_NV2A);
    if (tail_guest_write && start <= 8192 && start + size > 8192) {
        tail_guest_write = false;
        return true;
    }
    return false;
}
void nv2a_profile_log_event_once(NV2AProfileEvent event)
{
    g_assert_not_reached();
}
void memory_region_set_client_dirty(MemoryRegion *mr, hwaddr start, hwaddr size,
                                    unsigned client)
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
    g_autofree PGRAPHGLState *r = g_new0(PGRAPHGLState, 1);
    MemoryRegion vram = { .size = int128_make64(32768) };
    SurfaceBinding color = { .color = true, .width = 4, .height = 4 };
    SurfaceBinding depth = {
        .width = 8, .height = 1024, .pitch = 16, .size = 16384
    };
    PGRAPHState *pg = &d->pgraph;
    pg->gl_renderer_state = r;
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
    depth.fmt =
        kelvin_surface_zeta_fixed_format_gl_map[pg->surface_shape.zeta_format];
    r->color_binding = &color;
    r->zeta_binding = &depth;
    QTAILQ_INIT(&r->surfaces);

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
        depth.fmt.gl_internal_format = GL_DEPTH_COMPONENT32F;
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
        surface_access_callback(d, &vram, 8192, 4, true);
        g_assert_true(depth.upload_pending);
        g_assert_false(pg->surface_zeta.buffer_dirty);
    }
    update_surface_part(d, test_case != 3, false);
    g_assert_true(r->zeta_binding == &depth);
    bool consumed = test_case == 0;
    g_assert_cmpint(tail_guest_write, ==, test_case != 2 && !consumed);
    g_assert_cmpint(depth.upload_pending, ==, consumed || tcg_allowed);
    g_assert_cmpuint(first_dirty_size, ==,
                     tcg_allowed ? 0 : (test_case == 3 ? 64 : 16384));
    g_assert_cmpuint(dirty_reads, ==, tcg_allowed ? 0 : 1);
    g_assert_cmpuint(pg->surface_binding_dim.width, ==, 4);
    g_assert_cmpuint(pg->surface_binding_dim.height, ==, 4);

    tcg_allowed = false;
    tail_guest_write = false;
    qemu_mutex_destroy(&d->pfifo.lock);
    qemu_mutex_destroy(&pg->lock);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/kvm", GINT_TO_POINTER(0),
                         test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/tcg", GINT_TO_POINTER(1),
                         test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/no-write",
                         GINT_TO_POINTER(2), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/readback",
                         GINT_TO_POINTER(3), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/address",
                         GINT_TO_POINTER(4), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/aa", GINT_TO_POINTER(5),
                         test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/binding-layout",
                         GINT_TO_POINTER(6), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/target-layout",
                         GINT_TO_POINTER(7), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/format",
                         GINT_TO_POINTER(8), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/pitch",
                         GINT_TO_POINTER(9), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/short-width",
                         GINT_TO_POINTER(10), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/short-height",
                         GINT_TO_POINTER(11), test_bound_depth_dirty_range);
    g_test_add_data_func("/xbox/gl/surface/bound-tail/equal-extent",
                         GINT_TO_POINTER(12), test_bound_depth_dirty_range);
    return g_test_run();
}
