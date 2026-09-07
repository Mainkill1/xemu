/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Focused tests for Vulkan texture revalidation state. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture-state.h"

static void test_unchanged_write_retires_hint(void)
{
    uint64_t hash = 0x1234;
    bool dirty = true;

    g_assert_true(pgraph_vk_texture_complete_revalidation(
        false, true, hash, &hash, &dirty));
    g_assert_false(dirty);
    g_assert_cmpuint(hash, ==, 0x1234);

    for (unsigned int draw = 0; draw < 100; draw++) {
        g_assert_false(pgraph_vk_texture_needs_revalidation(dirty, false));
    }
}

static void test_changed_write_commits_after_upload(void)
{
    uint64_t hash = 0x1234;
    bool dirty = true;

    g_assert_true(pgraph_vk_texture_complete_revalidation(
        true, true, 0x5678, &hash, &dirty));
    g_assert_false(dirty);
    g_assert_cmpuint(hash, ==, 0x5678);
}

static void test_failed_upload_remains_retryable(void)
{
    uint64_t hash = 0x1234;
    bool dirty = true;

    g_assert_false(pgraph_vk_texture_complete_revalidation(
        true, false, 0x5678, &hash, &dirty));
    g_assert_true(dirty);
    g_assert_cmpuint(hash, ==, 0x1234);
}

static void test_palette_hash_uses_same_transition(void)
{
    uint64_t hash = 0x1000 ^ 0x10;
    bool dirty = true;
    uint64_t changed_palette_hash = 0x1000 ^ 0x20;

    g_assert_true(pgraph_vk_texture_complete_revalidation(
        changed_palette_hash != hash, true, changed_palette_hash,
        &hash, &dirty));
    g_assert_false(dirty);
    g_assert_cmpuint(hash, ==, changed_palette_hash);
}

static void test_shared_page_hints_retire_independently(void)
{
    uint64_t first_hash = 1, second_hash = 2;
    bool first_dirty = true, second_dirty = true;

    g_assert_true(pgraph_vk_texture_complete_revalidation(
        false, true, first_hash, &first_hash, &first_dirty));
    g_assert_false(first_dirty);
    g_assert_true(second_dirty);
    g_assert_true(pgraph_vk_texture_needs_revalidation(second_dirty, false));

    g_assert_true(pgraph_vk_texture_complete_revalidation(
        false, true, second_hash, &second_hash, &second_dirty));
    g_assert_false(second_dirty);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/nv2a/vk/texture-state/unchanged",
                    test_unchanged_write_retires_hint);
    g_test_add_func("/xbox/nv2a/vk/texture-state/changed",
                    test_changed_write_commits_after_upload);
    g_test_add_func("/xbox/nv2a/vk/texture-state/upload-failure",
                    test_failed_upload_remains_retryable);
    g_test_add_func("/xbox/nv2a/vk/texture-state/palette",
                    test_palette_hash_uses_same_transition);
    g_test_add_func("/xbox/nv2a/vk/texture-state/shared-page",
                    test_shared_page_hints_retire_independently);
    return g_test_run();
}
