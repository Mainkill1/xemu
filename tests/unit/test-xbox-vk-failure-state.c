/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/failure-state.h"

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

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/failure/surface-download-state",
                    test_failed_surface_download_preserves_authoritative_state);
    g_test_add_func("/xbox/vk/failure/texture-upload-state",
                    test_failed_texture_upload_remains_retryable);
    return g_test_run();
}
