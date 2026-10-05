// SPDX-License-Identifier: GPL-2.0-or-later
#include "qemu/osdep.h"
#include <cnode.h>
#include "ui/xemu-settings.h"
#include "ui/xemu-settings-migration.hh"
#include "ui/xemu-tweak-overrides.h"
#define DEFINE_CONFIG_TREE
#include "xemu-config.h"

struct config g_config;

static void defaults()
{
    config_tree.reset_to_defaults();
    config_tree.free_allocations(&g_config);
    config_tree.store_to_struct(&g_config);
}

static XemuTweakOverride parse(const char *token)
{
    XemuTweakOverride value;
    Error *error = nullptr;
    g_assert_true(xemu_tweak_parse_override(token, &value, &error));
    g_assert_null(error);
    return value;
}

static void set(const XemuTweakOverride *values, size_t count)
{
    Error *error = nullptr;
    g_assert_true(xemu_tweaks_set_overrides(values, count, &error));
    g_assert_null(error);
}

/* Each fixture gets a fresh process because process overrides become immutable
 * at startup. No production reset API exists merely for test isolation. */
static bool child()
{
    if (g_test_subprocess()) {
        defaults();
        return true;
    }
    g_test_trap_subprocess(nullptr, 0, static_cast<GTestSubprocessFlags>(0));
    g_test_trap_assert_passed();
    return false;
}

static void test_owned_nonpersistent()
{
    if (!child()) {
        return;
    }
    g_config.tweaks.vk_ubershader_mode =
        CONFIG_TWEAKS_VK_UBERSHADER_MODE_FALLBACK;
    XemuTweakOverride *values = g_new(XemuTweakOverride, 4);
    values[0] = parse("pgraph_bulk_packets=enabled");
    values[1] = parse("pgraph_bulk_packets=disabled");
    values[2] = parse("cache_shaders=disabled");
    values[3] = parse("vk_ubershader_mode=off");
    set(values, 4);
    memset(values, 0, 4 * sizeof(*values));
    g_free(values);

    xemu_tweaks_apply(true);
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_cmpint(xemu_vulkan_ubershader_policy(), ==,
                    XEMU_VK_UBERSHADER_OFF);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, true, false);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    auto snapshot = xemu_tweaks_snapshot();
    g_assert_false(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
    g_assert_true(snapshot.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].overridden);
    g_assert_true(snapshot.state[XEMU_TWEAK_VK_HYBRID_UBERSHADERS].overridden);
    g_assert_true(snapshot.ubershader.overridden);
    g_assert_true(snapshot.cache.overridden);
    g_assert_cmpint(snapshot.cache.policy_requested, ==,
                    XEMU_TWEAK_POLICY_DISABLED);
    g_assert_false(snapshot.cache.restart_pending);
    size_t length = xemu_tweaks_format_effective_profile(nullptr, 0);
    std::string text(length + 1, '\0');
    xemu_tweaks_format_effective_profile(text.data(), text.size());
    auto offset = text.find("tweak.cache_shaders=requested:disabled,");
    g_assert_true(offset != std::string::npos);
    auto line = text.substr(offset, text.find('\n', offset) - offset);
    g_assert_nonnull(strstr(line.c_str(), ",effective:disabled,"));
    g_assert_nonnull(strstr(line.c_str(), ",origin:override"));
    g_assert_false(snapshot.cache_session_eligible);
    g_assert_true(g_config.tweaks.pgraph_bulk_packets);
    g_assert_true(g_config.perf.cache_shaders);
    g_assert_cmpint(g_config.tweaks.vk_ubershader_mode, ==,
                    CONFIG_TWEAKS_VK_UBERSHADER_MODE_FALLBACK);

    /* Real generated ConfigTree serialization, file write/read and reload.
     * This does not launch the native settings UI. */
    g_config.general.skip_boot_anim = true;
    config_tree.update_from_struct(&g_config);
    auto saved = config_tree.generate_delta_toml();
    char *path = nullptr;
    GError *io_error = nullptr;
    int fd = g_file_open_tmp("xemu-tweak-session-XXXXXX", &path, &io_error);
    g_assert_no_error(io_error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_assert_true(
        g_file_set_contents(path, saved.c_str(), saved.size(), &io_error));
    g_assert_no_error(io_error);
    char *contents = nullptr;
    g_assert_true(g_file_get_contents(path, &contents, nullptr, &io_error));
    g_assert_no_error(io_error);
    defaults();
    xemu_settings_apply_ubershader_migration(config_tree,
                                             toml::parse(contents));
    config_tree.free_allocations(&g_config);
    config_tree.store_to_struct(&g_config);
    g_free(contents);
    g_assert_cmpint(unlink(path), ==, 0);
    g_free(path);
    g_assert_true(g_config.general.skip_boot_anim);
    g_assert_true(g_config.tweaks.pgraph_bulk_packets);
    g_assert_true(g_config.perf.cache_shaders);
    g_assert_cmpint(g_config.tweaks.vk_ubershader_mode, ==,
                    CONFIG_TWEAKS_VK_UBERSHADER_MODE_FALLBACK);
    xemu_tweaks_apply(false);
    g_assert_false(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_cmpint(xemu_vulkan_ubershader_runtime_state().requested, ==,
                    XEMU_VK_UBERSHADER_OFF);
}

static void test_rejected_updates_preserve_overrides()
{
    if (!child()) {
        return;
    }
    auto original = parse("pgraph_bulk_packets=disabled");
    set(&original, 1);
    XemuTweakOverride invalid[] = {parse("cache_shaders=disabled"), original};
    invalid[1].policy = static_cast<XemuTweakPolicy>(99);
    Error *error = nullptr;
    g_assert_false(xemu_tweaks_set_overrides(invalid, 2, &error));
    g_assert_nonnull(error);
    error_free(error);
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_false(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
    g_assert_true(xemu_tweaks_cache_requested_enabled());
    auto before = xemu_tweaks_snapshot();
    auto later = parse("pgraph_bulk_packets=enabled");
    error = nullptr;
    g_assert_false(xemu_tweaks_set_overrides(&later, 1, &error));
    g_assert_nonnull(error);
    g_assert_nonnull(strstr(error_get_pretty(error), "startup"));
    error_free(error);
    g_assert_cmpuint(xemu_tweaks_snapshot().sequence, ==, before.sequence);
    xemu_tweaks_apply(false);
    g_assert_false(xemu_tweak_enabled(XEMU_TWEAK_PGRAPH_BULK_PACKETS));
}

static void test_clear_before_startup()
{
    if (!child()) {
        return;
    }
    auto value = parse("cache_shaders=disabled");
    set(&value, 1);
    set(nullptr, 0);
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_OPENGL, true, true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_OPENGL);
    auto snapshot = xemu_tweaks_snapshot();
    g_assert_true(snapshot.cache.effective);
    g_assert_false(snapshot.cache.overridden);
    g_assert_false(snapshot.ubershader.overridden);
    g_assert_false(snapshot.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].overridden);
    g_config.perf.cache_shaders = false;
    xemu_tweaks_apply(false);
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_false(xemu_tweaks_snapshot().cache.effective);
    g_assert_false(xemu_tweaks_snapshot().cache.restart_pending);
    g_config.perf.cache_shaders = true;
    xemu_tweaks_apply(false);
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
}

static void test_vulkan_cache_requires_eligible_init()
{
    if (!child()) {
        return;
    }
    g_config.perf.cache_shaders = false;
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, true, false);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_false(xemu_tweaks_snapshot().cache.restart_pending);
    g_config.perf.cache_shaders = true;
    xemu_tweaks_apply(false);
    auto state = xemu_tweaks_snapshot().cache;
    g_assert_true(xemu_tweaks_cache_requested_enabled());
    g_assert_true(state.requested);
    g_assert_false(state.effective);
    g_assert_false(state.available);
    g_assert_true(state.restart_pending);
    g_assert_cmpint(state.availability, ==, XEMU_TWEAK_BLOCKED_DEPENDENCY);
    g_assert_nonnull(strstr(state.reason, "initialization"));
    g_config.perf.cache_shaders = false;
    xemu_tweaks_apply(false);
    g_assert_false(xemu_tweaks_snapshot().cache.restart_pending);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_NONE);
    g_config.perf.cache_shaders = true;
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, true, true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
}

static void test_cache_owner_reset_and_fallback()
{
    if (!child()) {
        return;
    }
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, true, true);
    g_assert_false(xemu_tweaks_snapshot().cache.effective);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
    g_assert_true(xemu_tweaks_snapshot().cache_installed);
    g_assert_true(xemu_tweaks_snapshot().cache_session_eligible);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_NONE);
    g_assert_false(xemu_tweaks_snapshot().cache.available);
    g_assert_false(xemu_tweaks_snapshot().cache_installed);
    g_assert_false(xemu_tweaks_snapshot().cache_session_eligible);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_OPENGL);
    g_assert_false(xemu_tweaks_snapshot().cache.available);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_OPENGL, true, true);
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, false, false);
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_false(xemu_tweaks_snapshot().cache.available);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, false, false);
    g_assert_false(xemu_tweaks_snapshot().cache.effective);
    g_assert_false(xemu_tweaks_snapshot().cache.restart_pending);
}

static void test_lifecycle_does_not_read_mutable_config()
{
    if (!child()) {
        return;
    }
    xemu_tweaks_apply(true);
    g_config.perf.cache_shaders = false;
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_VULKAN, true, true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_VULKAN);
    g_assert_true(xemu_tweaks_cache_requested_enabled());
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
    xemu_tweaks_apply(false);
    g_assert_false(xemu_tweaks_cache_requested_enabled());
    g_assert_false(xemu_tweaks_snapshot().cache.effective);
    g_assert_false(xemu_tweaks_snapshot().cache.restart_pending);
}

static void test_auto_cache_and_invalid_policy()
{
    if (!child()) {
        return;
    }
    auto value = parse("cache_shaders=auto");
    set(&value, 1);
    g_config.perf.cache_shaders = false;
    xemu_tweaks_apply(true);
    xemu_tweaks_publish_cache(XEMU_TWEAK_RENDERER_OPENGL, true, true);
    xemu_tweaks_publish_renderer(XEMU_TWEAK_RENDERER_OPENGL);
    g_assert_true(xemu_tweaks_cache_requested_enabled());
    g_assert_true(xemu_tweaks_snapshot().cache.effective);
    g_assert_cmpint(xemu_tweaks_snapshot().cache.policy_requested, ==,
                    XEMU_TWEAK_POLICY_AUTO);
    g_assert_false(g_config.perf.cache_shaders);
    XemuTweakRequestedState request = {};
    request.cache_policy = static_cast<XemuTweakPolicy>(99);
    XemuTweakEnvironment environment = {};
    environment.renderer = XEMU_TWEAK_RENDERER_OPENGL;
    environment.cache_renderer = XEMU_TWEAK_RENDERER_OPENGL;
    environment.cache_installed = true;
    environment.cache_session_eligible = true;
    auto resolved = xemu_tweaks_resolve(&request, &environment, nullptr, true);
    g_assert_false(resolved.cache.effective);
    g_assert_false(resolved.cache.requested);
    g_assert_false(resolved.cache.restart_pending);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/xemu/tweaks/session/owned-nonpersistent",
                    test_owned_nonpersistent);
    g_test_add_func("/xemu/tweaks/session/rejected-updates",
                    test_rejected_updates_preserve_overrides);
    g_test_add_func("/xemu/tweaks/session/clear-before-startup",
                    test_clear_before_startup);
    g_test_add_func("/xemu/tweaks/session/vulkan-cache-init",
                    test_vulkan_cache_requires_eligible_init);
    g_test_add_func("/xemu/tweaks/session/cache-owner-reset",
                    test_cache_owner_reset_and_fallback);
    g_test_add_func("/xemu/tweaks/session/owned-cache-request",
                    test_lifecycle_does_not_read_mutable_config);
    g_test_add_func("/xemu/tweaks/session/auto-cache",
                    test_auto_cache_and_invalid_policy);
    return g_test_run();
}
