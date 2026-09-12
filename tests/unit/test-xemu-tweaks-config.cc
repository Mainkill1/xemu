// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the generated configuration used by xemu, including saved Off values.
#include "qemu/osdep.h"
#include <cassert>
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-tweaks.h"
extern "C" {
#include "qemu/timer.h"
}
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

int main()
{
    const char *default_on_keys[] = {
        "cpu_saving_wait", "pgraph_bulk_packets", "pgraph_fence_fastpath",
        "vk_color_download_folding", "vk_bounded_vertex_uploads",
        "vk_transient_buffer_growth", "gl_native_s3tc",
    };
    assert(!xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    config_tree.reset_to_defaults();
    config_tree.store_to_struct(&g_config);
    xemu_tweaks_apply(true);
    assert(qemu_poll_get_cpu_saving());
    for (unsigned i = 0; i < XEMU_TWEAK_VK_HYBRID_UBERSHADERS; i++) {
        assert(xemu_tweak_enabled(static_cast<XemuTweak>(i)));
    }
    assert(!g_config.tweaks.vk_hybrid_ubershaders);
    assert(!xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    assert(g_config.perf.cache_shaders);
    auto tweaks = config_tree.child("tweaks");
    for (const char *key : default_on_keys) {
        auto node = tweaks->child(key);
        assert(node && node->data.boolean.val);
        node->data.boolean.val = false;
    }
    auto hybrid = tweaks->child("vk_hybrid_ubershaders");
    assert(hybrid && !hybrid->data.boolean.val);
    hybrid->data.boolean.val = true;
    auto cache_shaders = config_tree.child("perf")->child("cache_shaders");
    assert(cache_shaders && cache_shaders->data.boolean.val);
    cache_shaders->data.boolean.val = false;
    config_tree.free_allocations(&g_config);
    config_tree.store_to_struct(&g_config);
    xemu_tweaks_apply(false);
    assert(!qemu_poll_get_cpu_saving());
    for (unsigned i = 0; i < XEMU_TWEAK_COUNT; i++) {
        auto tweak = static_cast<XemuTweak>(i);
        bool restart = tweak == XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH ||
                       tweak == XEMU_TWEAK_GL_NATIVE_S3TC ||
                       tweak == XEMU_TWEAK_VK_HYBRID_UBERSHADERS;
        bool expected = restart &&
                        tweak != XEMU_TWEAK_VK_HYBRID_UBERSHADERS;
        assert(xemu_tweak_enabled(tweak) == expected);
    }
    assert(!xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    xemu_tweaks_apply(true);
    for (unsigned i = 0; i < XEMU_TWEAK_VK_HYBRID_UBERSHADERS; i++) {
        assert(!xemu_tweak_enabled(static_cast<XemuTweak>(i)));
    }
    assert(xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    config_tree.update_from_struct(&g_config);
    auto saved = config_tree.generate_delta_toml();
    config_tree.reset_to_defaults();
    config_tree.update_from_table(toml::parse(saved));
    for (const char *key : default_on_keys) {
        assert(!tweaks->child(key)->data.boolean.val);
    }
    assert(tweaks->child("vk_hybrid_ubershaders")->data.boolean.val);
    assert(!config_tree.child("perf")->child("cache_shaders")
                ->data.boolean.val);
    // A config written before the other tweaks existed keeps its saved choice.
    config_tree.reset_to_defaults();
    config_tree.update_from_table(toml::parse(
        "[perf]\ncache_shaders = false\n"
        "[tweaks]\ncpu_saving_wait = false\n"));
    assert(!tweaks->child(default_on_keys[0])->data.boolean.val);
    for (unsigned i = 1;
         i < sizeof(default_on_keys) / sizeof(default_on_keys[0]); i++) {
        assert(tweaks->child(default_on_keys[i])->data.boolean.val);
    }
    assert(!tweaks->child("vk_hybrid_ubershaders")->data.boolean.val);
    assert(!config_tree.child("perf")->child("cache_shaders")
                ->data.boolean.val);
    config_tree.free_allocations(&g_config);
    config_tree.store_to_struct(&g_config);
    xemu_tweaks_apply(true);
    assert(!qemu_poll_get_cpu_saving());
    assert(!xemu_tweak_enabled(XEMU_TWEAK_CPU_SAVING_WAIT));
    for (unsigned i = 1; i < XEMU_TWEAK_VK_HYBRID_UBERSHADERS; i++) {
        assert(xemu_tweak_enabled(static_cast<XemuTweak>(i)));
    }
    assert(!xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    // Toggling one live option does not change any other option.
    g_config.tweaks.pgraph_bulk_packets = false;
    xemu_tweaks_apply(false);
    assert(!xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
    assert(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_FENCE_FASTPATH));
    g_config.tweaks.pgraph_bulk_packets = true;
    xemu_tweaks_apply(false);
    assert(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
    assert(!xemu_tweak_enabled(XEMU_TWEAK_VK_HYBRID_UBERSHADERS));
    config_tree.free_allocations(&g_config);
    puts("PASS: defaults, persistence, migration, live changes and restart policy");
}
