// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-shortcut-config.h"
#include "ui/xemu-tweak-overrides.h"
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

static bool child()
{
    if (g_test_subprocess()) {
        config_tree.reset_to_defaults();
        config_tree.store_to_struct(&g_config);
        return true;
    }
    g_test_trap_subprocess(nullptr, 0, static_cast<GTestSubprocessFlags>(0));
    g_test_trap_assert_passed();
    return false;
}

static void test_private_copy()
{
    if (!child()) {
        return;
    }
    auto tree_before = config_tree.generate_delta_toml();
    g_autofree char *before = xemu_shortcut_base_config_sha256();
    g_assert_nonnull(before);
    g_assert_cmpuint(strlen(before), ==, 64);
    g_autofree char *again = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, again);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    g_autofree char *changed = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, !=, changed);
    g_assert_true(config_tree.generate_delta_toml() == tree_before);
    g_assert_true(g_config.general.skip_boot_anim);
    g_config.general.skip_boot_anim = !g_config.general.skip_boot_anim;
    g_autofree char *restored = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, restored);
}

static void test_excludes_process_overlay()
{
    if (!child()) {
        return;
    }
    g_autofree char *before = xemu_shortcut_base_config_sha256();
    g_assert_nonnull(before);
    XemuTweakOverride values[2];
    g_assert_true(xemu_tweak_parse_override("pgraph_bulk_packets=disabled",
                                            &values[0], &error_abort));
    g_assert_true(xemu_tweak_parse_override("cache_shaders=disabled",
                                            &values[1], &error_abort));
    g_assert_true(xemu_tweaks_set_overrides(values, 2, &error_abort));
    xemu_tweaks_apply(true);
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_true(g_config.perf.cache_shaders);
    g_assert_true(g_config.tweaks.pgraph_bulk_packets);
    g_autofree char *after = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, after);
    config_tree.update_from_struct(&g_config);
    auto saved = config_tree.generate_delta_toml();
    g_assert_true(saved.find("cache_shaders = false") == std::string::npos);
    g_autofree char *after_save = xemu_shortcut_base_config_sha256();
    g_assert_cmpstr(before, ==, after_save);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/xemu/shortcut-config/private-copy", test_private_copy);
    g_test_add_func("/xemu/shortcut-config/excludes-overlay",
                    test_excludes_process_overlay);
    return g_test_run();
}
