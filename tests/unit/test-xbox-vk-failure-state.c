/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/failure-state.h"
#include "hw/xbox/nv2a/pgraph/vk/mapped-memory.h"

static unsigned int unmap_count;

static void count_unmap(void *allocator, void *allocation)
{
    g_assert_true(allocator == (void *)0x1111);
    g_assert_true(allocation == (void *)0x2222);
    unmap_count++;
}

static void test_failed_surface_download_preserves_authoritative_state(void)
{
    bool download_pending = true;
    bool draw_dirty = true;

    g_assert_false(pgraph_vk_complete_surface_download(
        false, &download_pending, &draw_dirty));
    g_assert_true(download_pending);
    g_assert_true(draw_dirty);

    g_assert_true(pgraph_vk_complete_surface_download(
        true, &download_pending, &draw_dirty));
    g_assert_false(download_pending);
    g_assert_false(draw_dirty);
}

static void test_failed_texture_upload_remains_retryable(void)
{
    uint64_t stored_hash = 0x1111;
    bool possibly_dirty = false;

    g_assert_false(pgraph_vk_complete_texture_upload(
        false, 0x2222, &stored_hash, &possibly_dirty));
    g_assert_cmpuint(stored_hash, ==, 0x1111);
    g_assert_true(possibly_dirty);

    g_assert_true(pgraph_vk_complete_texture_upload(
        true, 0x2222, &stored_hash, &possibly_dirty));
    g_assert_cmpuint(stored_hash, ==, 0x2222);
    g_assert_false(possibly_dirty);
}

static void test_map_failure_does_not_unmap(void)
{
    unmap_count = 0;
    {
        g_auto(PGRAPHVkMapGuard) guard = { 0 };
        g_assert_false(pgraph_vk_map_guard_complete_map(
            &guard, false, (void *)0x1111, (void *)0x2222, count_unmap));
        g_assert_false(guard.mapped);
    }
    g_assert_cmpuint(unmap_count, ==, 0);
}

static void test_post_map_failure_unmaps_once(void)
{
    unmap_count = 0;
    {
        g_auto(PGRAPHVkMapGuard) guard = { 0 };
        g_assert_true(pgraph_vk_map_guard_complete_map(
            &guard, true, (void *)0x1111, (void *)0x2222, count_unmap));
        g_assert_true(guard.mapped);
        g_assert_false(pgraph_vk_map_guard_complete_coherency(&guard, false));
    }
    g_assert_cmpuint(unmap_count, ==, 1);
}

static void test_success_unmaps_once(void)
{
    unmap_count = 0;
    {
        g_auto(PGRAPHVkMapGuard) guard = { 0 };
        g_assert_true(pgraph_vk_map_guard_complete_map(
            &guard, true, (void *)0x1111, (void *)0x2222, count_unmap));
        g_assert_true(pgraph_vk_map_guard_complete_coherency(&guard, true));
        pgraph_vk_map_guard_clear(&guard);
        g_assert_false(guard.mapped);
    }
    g_assert_cmpuint(unmap_count, ==, 1);
}

static void test_surface_invalidate_failure_preserves_state(void)
{
    bool download_pending = true;
    bool draw_dirty = true;
    unmap_count = 0;

    {
        g_auto(PGRAPHVkMapGuard) guard = { 0 };
        g_assert_true(pgraph_vk_map_guard_complete_map(
            &guard, true, (void *)0x1111, (void *)0x2222, count_unmap));
        bool downloaded = pgraph_vk_map_guard_complete_coherency(&guard, false);
        g_assert_false(pgraph_vk_complete_surface_download(
            downloaded, &download_pending, &draw_dirty));
    }

    g_assert_cmpuint(unmap_count, ==, 1);
    g_assert_true(download_pending);
    g_assert_true(draw_dirty);
}

static void test_texture_flush_failure_preserves_retry_state(void)
{
    uint64_t stored_hash = 0x1111;
    bool possibly_dirty = false;
    unmap_count = 0;

    {
        g_auto(PGRAPHVkMapGuard) guard = { 0 };
        g_assert_true(pgraph_vk_map_guard_complete_map(
            &guard, true, (void *)0x1111, (void *)0x2222, count_unmap));
        bool uploaded = pgraph_vk_map_guard_complete_coherency(&guard, false);
        g_assert_false(pgraph_vk_complete_texture_upload(
            uploaded, 0x2222, &stored_hash, &possibly_dirty));
    }

    g_assert_cmpuint(unmap_count, ==, 1);
    g_assert_cmpuint(stored_hash, ==, 0x1111);
    g_assert_true(possibly_dirty);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/failure/surface-download-state",
                    test_failed_surface_download_preserves_authoritative_state);
    g_test_add_func("/xbox/vk/failure/texture-upload-state",
                    test_failed_texture_upload_remains_retryable);
    g_test_add_func("/xbox/vk/failure/map-failure-no-unmap",
                    test_map_failure_does_not_unmap);
    g_test_add_func("/xbox/vk/failure/post-map-failure-unmap",
                    test_post_map_failure_unmaps_once);
    g_test_add_func("/xbox/vk/failure/map-success-unmap",
                    test_success_unmaps_once);
    g_test_add_func("/xbox/vk/failure/surface-invalidate-contract",
                    test_surface_invalidate_failure_preserves_state);
    g_test_add_func("/xbox/vk/failure/texture-flush-contract",
                    test_texture_flush_failure_preserves_retry_state);
    return g_test_run();
}
