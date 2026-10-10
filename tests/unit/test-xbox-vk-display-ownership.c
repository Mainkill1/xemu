/*
 * Production Vulkan accelerated display ownership checks.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface.c"

bool tcg_allowed;
static unsigned int dirty_queries;
static bool guest_dirty;
static MemoryRegion *expected_region;

uint64_t memory_region_size(MemoryRegion *mr)
{
    g_assert_true(mr == expected_region);
    return 3 * TARGET_PAGE_SIZE;
}

bool memory_region_take_dirty_pages(MemoryRegion *mr, hwaddr addr, hwaddr size,
                                    unsigned int client, unsigned long *pages,
                                    size_t capacity_words)
{
    g_assert_true(mr == expected_region);
    g_assert_cmpuint(addr, ==, TARGET_PAGE_SIZE);
    g_assert_cmpuint(size, ==, TARGET_PAGE_SIZE);
    g_assert_cmpuint(client, ==, DIRTY_MEMORY_NV2A_SURFACE);
    g_assert_cmpuint(capacity_words, >=, 1);
    dirty_queries++;
    memset(pages, 0, capacity_words * sizeof(*pages));
    if (guest_dirty) {
        pages[0] = 1;
        guest_dirty = false;
        return true;
    }
    return false;
}

typedef struct DisplayFixture {
    NV2AState d;
    PGRAPHVkState renderer;
    MemoryRegion vram;
    SurfaceBinding surface;
    SurfaceBinding overlap;
} DisplayFixture;

static void fixture_init(DisplayFixture *f)
{
    f->d.pgraph.vk_renderer_state = &f->renderer;
    f->d.vram = &f->vram;
    expected_region = &f->vram;
    tcg_allowed = false;
    dirty_queries = 0;
    guest_dirty = false;
    QTAILQ_INIT(&f->renderer.surfaces);
    f->surface.vram_addr = TARGET_PAGE_SIZE;
    f->surface.size = TARGET_PAGE_SIZE;
    QTAILQ_INSERT_TAIL(&f->renderer.surfaces, &f->surface, entry);
}

static void fixture_cleanup(DisplayFixture *f)
{
    g_free(f->renderer.surface_dirty_page_bits);
}

static void test_gpu_newer(void)
{
    g_autofree DisplayFixture *f = g_new0(DisplayFixture, 1);
    fixture_init(f);
    f->surface.draw_dirty = true;
    g_assert_false(pgraph_vk_surface_force_display_upload(&f->d, &f->surface));
    g_assert_true(f->surface.draw_dirty);
    g_assert_false(f->surface.upload_pending);
    g_assert_cmpuint(dirty_queries, ==, 1);
    fixture_cleanup(f);
}

static void test_guest_newer(void)
{
    g_autofree DisplayFixture *f = g_new0(DisplayFixture, 1);
    fixture_init(f);
    f->surface.draw_dirty = true;
    f->surface.download_pending = true;
    guest_dirty = true;
    g_assert_true(pgraph_vk_surface_force_display_upload(&f->d, &f->surface));
    g_assert_false(f->surface.draw_dirty);
    g_assert_false(f->surface.download_pending);
    g_assert_true(f->surface.upload_pending);
    g_assert_true(f->surface.readback_superseded_by_guest);
    g_assert_cmpuint(dirty_queries, ==, 1);
    fixture_cleanup(f);
}

static void test_clean_accelerated(void)
{
    g_autofree DisplayFixture *f = g_new0(DisplayFixture, 1);
    fixture_init(f);
    g_assert_true(pgraph_vk_surface_force_display_upload(&f->d, &f->surface));
    g_assert_cmpuint(dirty_queries, ==, 1);
    fixture_cleanup(f);
}

static void test_tcg_unchanged(void)
{
    g_autofree DisplayFixture *f = g_new0(DisplayFixture, 1);
    fixture_init(f);
    tcg_allowed = true;
    guest_dirty = true;
    f->surface.draw_dirty = true;
    g_assert_false(pgraph_vk_surface_force_display_upload(&f->d, &f->surface));
    g_assert_true(f->surface.draw_dirty);
    g_assert_cmpuint(dirty_queries, ==, 0);
    fixture_cleanup(f);
}

static void test_overlapping_guest_write(void)
{
    g_autofree DisplayFixture *f = g_new0(DisplayFixture, 1);
    fixture_init(f);
    f->surface.draw_dirty = true;
    f->overlap.vram_addr = TARGET_PAGE_SIZE + 64;
    f->overlap.size = 128;
    f->overlap.draw_dirty = true;
    QTAILQ_INSERT_TAIL(&f->renderer.surfaces, &f->overlap, entry);
    guest_dirty = true;
    g_assert_true(pgraph_vk_surface_force_display_upload(&f->d, &f->surface));
    g_assert_false(f->overlap.draw_dirty);
    g_assert_true(f->overlap.upload_pending);
    g_assert_true(f->overlap.readback_superseded_by_guest);
    fixture_cleanup(f);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/display/gpu-newer", test_gpu_newer);
    g_test_add_func("/xbox/vk/display/guest-newer", test_guest_newer);
    g_test_add_func("/xbox/vk/display/clean-accelerated",
                    test_clean_accelerated);
    g_test_add_func("/xbox/vk/display/tcg-unchanged", test_tcg_unchanged);
    g_test_add_func("/xbox/vk/display/overlap-guest-write",
                    test_overlapping_guest_write);
    return g_test_run();
}
