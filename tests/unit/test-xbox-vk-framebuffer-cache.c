/*
 * Vulkan NV2A framebuffer cache key tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/framebuffer-cache.h"

static PGRAPHVkFramebufferKey test_key(void)
{
    return (PGRAPHVkFramebufferKey) {
        .render_pass = (VkRenderPass)(uintptr_t)1,
        .attachments = {
            (VkImageView)(uintptr_t)2,
            (VkImageView)(uintptr_t)3,
        },
        .attachment_count = 2,
        .width = 640,
        .height = 480,
        .layers = 1,
    };
}

static void test_key_equality(void)
{
    PGRAPHVkFramebufferKey key = test_key();
    PGRAPHVkFramebufferKey other = key;

    g_assert_true(pgraph_vk_framebuffer_key_equal(&key, &other));

#define CHECK_FIELD(field, value) \
    do {                          \
        other = key;              \
        other.field = value;      \
        g_assert_false(pgraph_vk_framebuffer_key_equal(&key, &other)); \
    } while (0)

    CHECK_FIELD(render_pass, (VkRenderPass)(uintptr_t)4);
    CHECK_FIELD(attachments[0], (VkImageView)(uintptr_t)4);
    CHECK_FIELD(attachments[1], (VkImageView)(uintptr_t)4);
    CHECK_FIELD(attachment_count, 1);
    CHECK_FIELD(width, 1280);
    CHECK_FIELD(height, 720);
    CHECK_FIELD(layers, 2);

#undef CHECK_FIELD

    key.attachment_count = 1;
    other = key;
    other.attachments[1] = (VkImageView)(uintptr_t)4;
    g_assert_true(pgraph_vk_framebuffer_key_equal(&key, &other));
}

static void test_view_references(void)
{
    PGRAPHVkFramebufferKey key = test_key();

    g_assert_true(pgraph_vk_framebuffer_key_references_view(
        &key, (VkImageView)(uintptr_t)2));
    g_assert_true(pgraph_vk_framebuffer_key_references_view(
        &key, (VkImageView)(uintptr_t)3));
    g_assert_false(pgraph_vk_framebuffer_key_references_view(
        &key, (VkImageView)(uintptr_t)4));

    key.attachment_count = 1;
    g_assert_false(pgraph_vk_framebuffer_key_references_view(
        &key, (VkImageView)(uintptr_t)3));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-framebuffer-cache/key-equality",
                    test_key_equality);
    g_test_add_func("/xbox/vk-framebuffer-cache/view-references",
                    test_view_references);
    return g_test_run();
}
