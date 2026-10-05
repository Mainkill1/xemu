/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-tweaks.h"

static XemuTweakRequestedState requested_defaults(void)
{
    XemuTweakRequestedState requested = {
        .ubershader_mode = XEMU_VK_UBERSHADER_PREWARM,
    };
    return requested;
}

static XemuTweakEnvironment environment(XemuTweakRenderer renderer)
{
    XemuTweakEnvironment env = {
        .renderer = renderer,
        .windows_host = true,
        .ubershader_installed = true,
        .ubershader_operational = true,
    };
    return env;
}

static void assert_masks(const XemuTweakResolution *resolved)
{
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        uint64_t bit = UINT64_C(1) << i;
        g_assert_cmpint((resolved->selected_bits & bit) != 0, ==,
                        resolved->state[i].selected);
        g_assert_cmpint((resolved->effective_bits & bit) != 0, ==,
                        resolved->state[i].effective);
        g_assert_true(
            !resolved->state[i].effective ||
            (resolved->state[i].selected && resolved->state[i].available));
        g_assert_nonnull(resolved->state[i].reason);
        g_assert_true(resolved->state[i].reason[0] != '\0');
    }
    g_assert_cmpuint(resolved->sequence, ==, 0);
}

static void test_defaults_and_explicit(void)
{
    XemuTweakRequestedState requested = requested_defaults();
    XemuTweakEnvironment env = environment(XEMU_TWEAK_RENDERER_VULKAN);
    XemuTweakResolution resolved =
        xemu_tweaks_resolve(&requested, &env, NULL, true);
    assert_masks(&resolved);
    g_assert_true(resolved.state[XEMU_TWEAK_CPU_SAVING_WAIT].effective);
    g_assert_true(resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].effective);
    g_assert_true(resolved.state[XEMU_TWEAK_PGRAPH_FENCE_FASTPATH].effective);
    g_assert_true(
        resolved.state[XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING].effective);
    g_assert_true(
        resolved.state[XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS].effective);
    g_assert_true(
        resolved.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    g_assert_false(resolved.state[XEMU_TWEAK_GL_NATIVE_S3TC].effective);
    g_assert_true(resolved.state[XEMU_TWEAK_GL_NATIVE_S3TC].selected);
    g_assert_false(resolved.state[XEMU_TWEAK_NV20_VERTEX_ARITHMETIC].effective);

    env.renderer = XEMU_TWEAK_RENDERER_OPENGL;
    resolved = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_true(resolved.state[XEMU_TWEAK_GL_NATIVE_S3TC].effective);
    g_assert_false(
        resolved.state[XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS].effective);
    requested.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS] =
        XEMU_TWEAK_POLICY_DISABLED;
    requested.policy[XEMU_TWEAK_NV20_VERTEX_ARITHMETIC] =
        XEMU_TWEAK_POLICY_ENABLED;
    resolved = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_false(resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].effective);
    g_assert_true(resolved.state[XEMU_TWEAK_NV20_VERTEX_ARITHMETIC].effective);
    assert_masks(&resolved);
}

static void test_live_and_latched(void)
{
    XemuTweakRequestedState requested = requested_defaults();
    XemuTweakEnvironment env = environment(XEMU_TWEAK_RENDERER_VULKAN);
    XemuTweakResolution startup =
        xemu_tweaks_resolve(&requested, &env, NULL, true);
    XemuTweakResolution original = startup;
    requested.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS] =
        XEMU_TWEAK_POLICY_DISABLED;
    requested.policy[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH] =
        XEMU_TWEAK_POLICY_DISABLED;
    requested.policy[XEMU_TWEAK_GL_NATIVE_S3TC] = XEMU_TWEAK_POLICY_DISABLED;
    requested.ubershader_mode = XEMU_VK_UBERSHADER_OFF;
    XemuTweakResolution live =
        xemu_tweaks_resolve(&requested, &env, &startup, false);
    g_assert_false(live.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].effective);
    g_assert_true(live.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    g_assert_true(
        live.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].restart_pending);
    g_assert_true(live.state[XEMU_TWEAK_GL_NATIVE_S3TC].selected);
    g_assert_true(live.state[XEMU_TWEAK_GL_NATIVE_S3TC].restart_pending);
    g_assert_cmpint(live.ubershader.requested, ==, XEMU_VK_UBERSHADER_OFF);
    g_assert_cmpint(live.ubershader.policy, ==, XEMU_VK_UBERSHADER_PREWARM);
    g_assert_cmpint(live.ubershader.active, ==, XEMU_VK_UBERSHADER_PREWARM);
    g_assert_true(live.ubershader.restart_pending);
    g_assert_cmpint(memcmp(&startup, &original, sizeof(startup)), ==, 0);
    assert_masks(&live);

    XemuTweakResolution restarted =
        xemu_tweaks_resolve(&requested, &env, &startup, true);
    g_assert_false(
        restarted.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    g_assert_false(
        restarted.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].restart_pending);
    g_assert_false(restarted.state[XEMU_TWEAK_GL_NATIVE_S3TC].selected);
    g_assert_cmpint(restarted.ubershader.active, ==, XEMU_VK_UBERSHADER_OFF);
    g_assert_false(restarted.ubershader.restart_pending);
    assert_masks(&restarted);
}

static void test_backend_changes_preserve_latches(void)
{
    XemuTweakRequestedState requested = requested_defaults();
    XemuTweakEnvironment env = environment(XEMU_TWEAK_RENDERER_NONE);
    XemuTweakResolution startup =
        xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_true(
        startup.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].selected);
    g_assert_false(
        startup.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    for (unsigned int i = XEMU_TWEAK_RENDERER_NONE;
         i <= XEMU_TWEAK_RENDERER_VULKAN; i++) {
        env.renderer = i;
        XemuTweakResolution same =
            xemu_tweaks_resolve(&requested, &env, &startup, false);
        g_assert_cmpuint(same.selected_bits, ==, startup.selected_bits);
        g_assert_false(
            same.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].restart_pending);
        g_assert_false(same.state[XEMU_TWEAK_GL_NATIVE_S3TC].restart_pending);
        g_assert_false(same.ubershader.restart_pending);
        assert_masks(&same);
    }
    requested.policy[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH] =
        XEMU_TWEAK_POLICY_DISABLED;
    env.renderer = XEMU_TWEAK_RENDERER_OPENGL;
    XemuTweakResolution fallback =
        xemu_tweaks_resolve(&requested, &env, &startup, false);
    g_assert_true(
        fallback.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].selected);
    g_assert_false(
        fallback.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    g_assert_true(
        fallback.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].restart_pending);
    env.renderer = XEMU_TWEAK_RENDERER_VULKAN;
    XemuTweakResolution reinstalled =
        xemu_tweaks_resolve(&requested, &env, &startup, false);
    g_assert_true(
        reinstalled.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH].effective);
    g_assert_true(reinstalled.state[XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH]
                      .restart_pending);
}

static void test_modes_and_capabilities(void)
{
    XemuTweakRequestedState requested = requested_defaults();
    XemuTweakEnvironment env = environment(XEMU_TWEAK_RENDERER_VULKAN);
    XemuTweakResolution startup =
        xemu_tweaks_resolve(&requested, &env, NULL, true);
    requested.ubershader_mode = XEMU_VK_UBERSHADER_FALLBACK;
    XemuTweakResolution live =
        xemu_tweaks_resolve(&requested, &env, &startup, false);
    g_assert_true(live.state[XEMU_TWEAK_VK_HYBRID_UBERSHADERS].restart_pending);
    g_assert_cmpint(live.ubershader.active, ==, XEMU_VK_UBERSHADER_PREWARM);
    env.ubershader_operational = false;
    live = xemu_tweaks_resolve(&requested, &env, &startup, false);
    g_assert_false(live.ubershader.available);
    g_assert_cmpint(live.ubershader.active, ==, XEMU_VK_UBERSHADER_OFF);
    g_assert_cmpint(live.state[XEMU_TWEAK_VK_HYBRID_UBERSHADERS].availability,
                    ==, XEMU_TWEAK_UNSUPPORTED_CAPABILITY);
    g_assert_cmpint(live.state[XEMU_TWEAK_VK_SHADER_FASTPATH].availability, ==,
                    XEMU_TWEAK_BLOCKED_DEPENDENCY);
    g_assert_false(live.state[XEMU_TWEAK_VK_SHADER_FASTPATH].effective);
    assert_masks(&live);
    requested.ubershader_mode = XEMU_VK_UBERSHADER_OFF;
    live = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_true(live.ubershader.available);
    g_assert_false(live.state[XEMU_TWEAK_VK_HYBRID_UBERSHADERS].effective);
    g_assert_false(live.state[XEMU_TWEAK_VK_SHADER_FASTPATH].effective);
}

static void test_platform_and_invalid_inputs(void)
{
    XemuTweakRequestedState requested = requested_defaults();
    XemuTweakEnvironment env = environment(XEMU_TWEAK_RENDERER_NONE);
    env.windows_host = false;
    XemuTweakResolution resolved =
        xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_cmpuint(resolved.effective_bits, ==, 0);
    g_assert_cmpint(resolved.state[XEMU_TWEAK_CPU_SAVING_WAIT].availability, ==,
                    XEMU_TWEAK_UNSUPPORTED_PLATFORM);
    env.windows_host = true;
    resolved = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_true(resolved.state[XEMU_TWEAK_CPU_SAVING_WAIT].effective);
    g_assert_false(resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].effective);
    env.renderer = XEMU_TWEAK_RENDERER_VULKAN;
    requested.policy[XEMU_TWEAK_PGRAPH_BULK_PACKETS] = (XemuTweakPolicy)99;
    requested.ubershader_mode = (XemuVulkanUbershaderMode)99;
    resolved = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_false(resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].selected);
    g_assert_false(resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].effective);
    g_assert_nonnull(strstr(
        resolved.state[XEMU_TWEAK_PGRAPH_BULK_PACKETS].reason, "Unknown"));
    g_assert_cmpint(resolved.ubershader.policy, ==, XEMU_VK_UBERSHADER_OFF);
    g_assert_false(resolved.ubershader.available);
    env.renderer = (XemuTweakRenderer)99;
    resolved = xemu_tweaks_resolve(&requested, &env, NULL, true);
    g_assert_cmpint(resolved.renderer, ==, XEMU_TWEAK_RENDERER_NONE);
    assert_masks(&resolved);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xemu/tweaks/resolve/defaults-explicit",
                    test_defaults_and_explicit);
    g_test_add_func("/xemu/tweaks/resolve/live-latched", test_live_and_latched);
    g_test_add_func("/xemu/tweaks/resolve/backend-latches",
                    test_backend_changes_preserve_latches);
    g_test_add_func("/xemu/tweaks/resolve/modes-capabilities",
                    test_modes_and_capabilities);
    g_test_add_func("/xemu/tweaks/resolve/platform-invalid",
                    test_platform_and_invalid_inputs);
    return g_test_run();
}
