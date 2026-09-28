// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include <algorithm>
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-tweaks.h"
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

static void reset_profile(void)
{
    config_tree.reset_to_defaults();
    config_tree.free_allocations(&g_config);
    config_tree.store_to_struct(&g_config);
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    xemu_vulkan_ubershader_publish_runtime(true, true);
}

static std::string profile(void)
{
    size_t length = xemu_tweaks_format_effective_profile(nullptr, 0);
    std::string result(length + 1, '\0');
    g_assert_cmpuint(
        xemu_tweaks_format_effective_profile(result.data(), result.size()), ==,
        length);
    g_assert_cmpuint(strlen(result.c_str()), ==, length);
    result.resize(length);
    return result;
}

static void test_profile_live_and_restart(void)
{
    reset_profile();
    g_config.tweaks.pgraph_bulk_packets = false;
    g_config.tweaks.vk_transient_buffer_growth = false;
    g_config.tweaks.vk_ubershader_mode = CONFIG_TWEAKS_VK_UBERSHADER_MODE_OFF;
    xemu_tweaks_apply(false);
    auto text = profile();
    g_assert_nonnull(strstr(text.c_str(), "schema=xemu-tweak-profile/v1\n"));
    g_assert_nonnull(strstr(text.c_str(), "renderer=vulkan\n"));
    g_assert_nonnull(strstr(
        text.c_str(),
        "tweak.pgraph_bulk_packets=requested:disabled,effective:disabled,"
        "available:yes,restart:no,availability:available,reason:"));
    g_assert_nonnull(strstr(
        text.c_str(),
        "tweak.vk_transient_buffer_growth=requested:disabled,effective:enabled,"
        "available:yes,restart:yes,availability:available,reason:"));
    g_assert_nonnull(
        strstr(text.c_str(),
               "tweak.vk_ubershader_mode=requested:off,effective:prewarm,"
               "available:yes,restart:yes,reason:"));

    /* Reporting cannot apply a saved restart choice or modify configuration. */
    g_assert_true(xemu_tweak_enabled(XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH));
    g_assert_false(g_config.tweaks.vk_transient_buffer_growth);
    xemu_tweaks_apply(true);
    xemu_vulkan_ubershader_publish_runtime(true, true);
    text = profile();
    g_assert_nonnull(strstr(text.c_str(),
                            "tweak.vk_transient_buffer_growth=requested:"
                            "disabled,effective:disabled,"
                            "available:yes,restart:no,"));
}

static void test_profile_unavailable_and_complete(void)
{
    reset_profile();
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_OPENGL);
    xemu_vulkan_ubershader_publish_runtime(false, false);
    auto text = profile();
    g_assert_nonnull(strstr(text.c_str(), "renderer=opengl\n"));
    g_assert_nonnull(strstr(
        text.c_str(),
        "tweak.vk_color_download_folding=requested:enabled,effective:disabled,"
        "available:no,restart:no,availability:backend,reason:"));
    g_assert_nonnull(
        strstr(text.c_str(),
               "tweak.gl_native_s3tc=requested:enabled,effective:enabled,"
               "available:yes,restart:no,"));
    const char *keys[] = {
        "cpu_saving_wait",
        "pgraph_bulk_packets",
        "pgraph_fence_fastpath",
        "vk_color_download_folding",
        "vk_bounded_vertex_uploads",
        "vk_vertex_copy_shortcuts",
        "vk_transient_buffer_growth",
        "gl_native_s3tc",
        "vk_hybrid_ubershaders",
        "vk_shader_fastpath",
        "issue149_effect_suppression",
        "nv20_vertex_arithmetic",
        "vk_ubershader_mode",
    };
    for (const char *key : keys) {
        std::string prefix = std::string("tweak.") + key + "=";
        auto offset = text.find(prefix);
        g_assert_true(offset != std::string::npos);
        g_assert_true(text.find(prefix, offset + 1) == std::string::npos);
        auto end = text.find('\n', offset);
        g_assert_true(end != std::string::npos);
        auto line = text.substr(offset, end - offset);
        g_assert_nonnull(strstr(line.c_str(), ",reason:"));
        g_assert_false(line.back() == ':');
    }
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_NONE);
    text = profile();
    g_assert_nonnull(strstr(text.c_str(), "renderer=none\n"));
    g_assert_nonnull(
        strstr(text.c_str(),
               "tweak.pgraph_bulk_packets=requested:enabled,effective:disabled,"
               "available:no,restart:no,availability:backend,reason:"));
}

static void test_profile_buffer_bounds(void)
{
    reset_profile();
    auto text = profile();
    for (size_t capacity :
         { size_t(0), size_t(1), size_t(16), text.size(), text.size() + 1 }) {
        std::string buffer(capacity + 2, '#');
        size_t length =
            xemu_tweaks_format_effective_profile(buffer.data() + 1, capacity);
        g_assert_cmpuint(length, ==, text.size());
        g_assert_cmpint(buffer.front(), ==, '#');
        g_assert_cmpint(buffer.back(), ==, '#');
        if (capacity) {
            size_t retained = std::min(capacity - 1, text.size());
            g_assert_cmpint(memcmp(buffer.data() + 1, text.data(), retained),
                            ==, 0);
            g_assert_cmpint(buffer[retained + 1], ==, '\0');
        }
    }
}

static void test_profile_degraded_renderer(void)
{
    reset_profile();
    xemu_vulkan_ubershader_publish_runtime(true, false);
    auto text = profile();
    g_assert_nonnull(strstr(
        text.c_str(),
        "tweak.vk_hybrid_ubershaders=requested:enabled,effective:disabled,"
        "available:no,restart:no,availability:capability,reason:"));
    g_assert_nonnull(
        strstr(text.c_str(),
               "tweak.vk_shader_fastpath=requested:enabled,effective:disabled,"
               "available:no,restart:no,availability:dependency,reason:"));
    g_assert_nonnull(
        strstr(text.c_str(),
               "tweak.vk_ubershader_mode=requested:prewarm,effective:off,"
               "available:no,restart:no,reason:"));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/xemu/tweaks/profile/live-restart",
                    test_profile_live_and_restart);
    g_test_add_func("/xemu/tweaks/profile/unavailable-complete",
                    test_profile_unavailable_and_complete);
    g_test_add_func("/xemu/tweaks/profile/buffer-bounds",
                    test_profile_buffer_bounds);
    g_test_add_func("/xemu/tweaks/profile/degraded-renderer",
                    test_profile_degraded_renderer);
    int result = g_test_run();
    config_tree.free_allocations(&g_config);
    return result;
}
