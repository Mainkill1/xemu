/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "xemu-settings.h"
#include "xemu-tweaks.h"

G_STATIC_ASSERT((int)XEMU_VK_UBERSHADER_OFF ==
                CONFIG_TWEAKS_VK_UBERSHADER_MODE_OFF);
G_STATIC_ASSERT((int)XEMU_VK_UBERSHADER_FALLBACK ==
                CONFIG_TWEAKS_VK_UBERSHADER_MODE_FALLBACK);
G_STATIC_ASSERT((int)XEMU_VK_UBERSHADER_PREWARM ==
                CONFIG_TWEAKS_VK_UBERSHADER_MODE_PREWARM);
G_STATIC_ASSERT((int)XEMU_VK_UBERSHADER_ALWAYS ==
                CONFIG_TWEAKS_VK_UBERSHADER_MODE_ALWAYS);

XemuTweakBits xemu_tweaks_active;
static int xemu_vulkan_ubershader_latched_policy = XEMU_VK_UBERSHADER_OFF;
static GMutex profile_mutex;
static XemuTweakRequestedState requested_profile;
static XemuTweakEnvironment environment = {
#ifdef _WIN32
    .windows_host = true,
#endif
};
static XemuTweakResolution startup_profile;
static XemuTweakResolution published_profile;

uint64_t xemu_tweaks_active_snapshot(void)
{
    return qatomic_read_u64(&xemu_tweaks_active);
}

/* A pre-startup query represents no installed permissions. No config read or
 * platform effect is allowed merely to inspect this state. */
static void initialize_profile_locked(void)
{
    if (published_profile.sequence) {
        return;
    }
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        requested_profile.policy[i] = XEMU_TWEAK_POLICY_DISABLED;
    }
    startup_profile =
        xemu_tweaks_resolve(&requested_profile, &environment, NULL, true);
    published_profile = startup_profile;
    published_profile.sequence = 1;
}

static void publish_profile_locked(void)
{
    XemuTweakResolution next = xemu_tweaks_resolve(
        &requested_profile, &environment, &startup_profile, false);
    next.sequence = published_profile.sequence + 1;
    qemu_poll_set_cpu_saving(next.state[XEMU_TWEAK_CPU_SAVING_WAIT].effective);
    qatomic_set_u64(&xemu_tweaks_active, next.effective_bits);
    published_profile = next;
}

XemuTweakResolution xemu_tweaks_snapshot(void)
{
    g_mutex_lock(&profile_mutex);
    initialize_profile_locked();
    XemuTweakResolution result = published_profile;
    g_mutex_unlock(&profile_mutex);
    return result;
}

void xemu_tweaks_publish_renderer(XemuTweakRenderer renderer)
{
    if ((unsigned int)renderer > XEMU_TWEAK_RENDERER_VULKAN) {
        renderer = XEMU_TWEAK_RENDERER_NONE;
    }
    g_mutex_lock(&profile_mutex);
    initialize_profile_locked();
    environment.renderer = renderer;
    if (renderer != XEMU_TWEAK_RENDERER_VULKAN) {
        environment.ubershader_installed = false;
        environment.ubershader_operational = false;
    }
    publish_profile_locked();
    g_mutex_unlock(&profile_mutex);
}

XemuTweakRuntimeState xemu_tweak_runtime_state(XemuTweak tweak)
{
    if ((unsigned int)tweak >= XEMU_TWEAK_COUNT) {
        XemuTweakRuntimeState unknown = {
            .availability = XEMU_TWEAK_UNSUPPORTED_CAPABILITY,
            .reason = "Unknown Advanced setting.",
        };
        return unknown;
    }
    return xemu_tweaks_snapshot().state[tweak];
}

static const char *tweak_policy_name(XemuTweakPolicy policy)
{
    switch (policy) {
    case XEMU_TWEAK_POLICY_AUTO:
        return "auto";
    case XEMU_TWEAK_POLICY_DISABLED:
        return "disabled";
    case XEMU_TWEAK_POLICY_ENABLED:
        return "enabled";
    default:
        return "unknown";
    }
}

static const char *tweak_availability_name(XemuTweakAvailability availability)
{
    switch (availability) {
    case XEMU_TWEAK_AVAILABLE:
        return "available";
    case XEMU_TWEAK_UNSUPPORTED_BACKEND:
        return "backend";
    case XEMU_TWEAK_UNSUPPORTED_PLATFORM:
        return "platform";
    case XEMU_TWEAK_UNSUPPORTED_CAPABILITY:
        return "capability";
    case XEMU_TWEAK_BLOCKED_DEPENDENCY:
        return "dependency";
    default:
        return "unknown";
    }
}

static const char *ubershader_mode_name(XemuVulkanUbershaderMode mode)
{
    switch (mode) {
    case XEMU_VK_UBERSHADER_OFF:
        return "off";
    case XEMU_VK_UBERSHADER_FALLBACK:
        return "fallback";
    case XEMU_VK_UBERSHADER_PREWARM:
        return "prewarm";
    case XEMU_VK_UBERSHADER_ALWAYS:
        return "always";
    default:
        return "unknown";
    }
}

size_t xemu_tweaks_format_effective_profile(char *buffer, size_t size)
{
    static const struct {
        XemuTweak tweak;
        const char *name;
    } names[] = {
        { XEMU_TWEAK_CPU_SAVING_WAIT, "cpu_saving_wait" },
        { XEMU_TWEAK_PGRAPH_BULK_PACKETS, "pgraph_bulk_packets" },
        { XEMU_TWEAK_PGRAPH_FENCE_FASTPATH, "pgraph_fence_fastpath" },
        { XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING, "vk_color_download_folding" },
        { XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS, "vk_bounded_vertex_uploads" },
        { XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS, "vk_vertex_copy_shortcuts" },
        { XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH, "vk_transient_buffer_growth" },
        { XEMU_TWEAK_GL_NATIVE_S3TC, "gl_native_s3tc" },
        { XEMU_TWEAK_VK_HYBRID_UBERSHADERS, "vk_hybrid_ubershaders" },
        { XEMU_TWEAK_VK_SHADER_FASTPATH, "vk_shader_fastpath" },
        { XEMU_TWEAK_NV20_VERTEX_ARITHMETIC, "nv20_vertex_arithmetic" },
    };
    G_STATIC_ASSERT(ARRAY_SIZE(names) == XEMU_TWEAK_COUNT);
    assert(buffer || size == 0);

    XemuTweakResolution snapshot = xemu_tweaks_snapshot();
    XemuTweakRenderer renderer = snapshot.renderer;
    XemuVulkanUbershaderRuntimeState ubershader = snapshot.ubershader;
    GString *profile = g_string_new("schema=xemu-tweak-profile/v1\n");
    g_string_append_printf(profile, "renderer=%s\n",
                           renderer == XEMU_TWEAK_RENDERER_VULKAN ? "vulkan" :
                           renderer == XEMU_TWEAK_RENDERER_OPENGL ? "opengl" :
                                                                    "none");
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        XemuTweakRuntimeState state = snapshot.state[names[i].tweak];
        g_string_append_printf(
            profile,
            "tweak.%s=requested:%s,effective:%s,available:%s,restart:%s,"
            "availability:%s,reason:%s\n",
            names[i].name, tweak_policy_name(state.policy_requested),
            state.effective ? "enabled" : "disabled",
            state.available ? "yes" : "no",
            state.restart_pending ? "yes" : "no",
            tweak_availability_name(state.availability), state.reason);
    }
    g_string_append_printf(
        profile,
        "tweak.vk_ubershader_mode=requested:%s,effective:%s,available:%s,"
        "restart:%s,reason:%s\n",
        ubershader_mode_name(ubershader.requested),
        ubershader_mode_name(ubershader.active),
        ubershader.available ? "yes" : "no",
        ubershader.restart_pending ? "yes" : "no", ubershader.reason);
    size_t length = profile->len;
    if (size) {
        g_strlcpy(buffer, profile->str, size);
    }
    g_string_free(profile, true);
    return length;
}

XemuVulkanUbershaderMode xemu_vulkan_ubershader_migrate_mode(
    bool mode_present, XemuVulkanUbershaderMode mode, bool legacy_enabled)
{
    if (mode_present) {
        return mode;
    }

    return legacy_enabled ? XEMU_VK_UBERSHADER_FALLBACK :
                            XEMU_VK_UBERSHADER_OFF;
}

XemuVulkanUbershaderMode xemu_vulkan_ubershader_policy(void)
{
    return qatomic_read(&xemu_vulkan_ubershader_latched_policy);
}

XemuVulkanUbershaderRuntimeState
xemu_vulkan_ubershader_runtime_state(void)
{
    return xemu_tweaks_snapshot().ubershader;
}

void xemu_vulkan_ubershader_publish_runtime(
    bool vulkan_installed, bool ubershader_operational)
{
    g_mutex_lock(&profile_mutex);
    initialize_profile_locked();
    environment.ubershader_installed = vulkan_installed;
    environment.ubershader_operational = ubershader_operational;
    publish_profile_locked();
    g_mutex_unlock(&profile_mutex);
}

static XemuTweakRequestedState requested_from_config(void)
{
    bool selected[XEMU_TWEAK_COUNT] = {
        [XEMU_TWEAK_CPU_SAVING_WAIT] = g_config.tweaks.cpu_saving_wait,
        [XEMU_TWEAK_PGRAPH_BULK_PACKETS] = g_config.tweaks.pgraph_bulk_packets,
        [XEMU_TWEAK_PGRAPH_FENCE_FASTPATH] =
            g_config.tweaks.pgraph_fence_fastpath,
        [XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING] =
            g_config.tweaks.vk_color_download_folding,
        [XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS] =
            g_config.tweaks.vk_bounded_vertex_uploads,
        [XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS] =
            g_config.tweaks.vk_vertex_copy_shortcuts,
        [XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH] =
            g_config.tweaks.vk_transient_buffer_growth,
        [XEMU_TWEAK_GL_NATIVE_S3TC] = g_config.tweaks.gl_native_s3tc,
        [XEMU_TWEAK_VK_SHADER_FASTPATH] = g_config.tweaks.vk_shader_fastpath,
        [XEMU_TWEAK_NV20_VERTEX_ARITHMETIC] =
            g_config.tweaks.nv20_vertex_arithmetic,
    };
    XemuTweakRequestedState requested = {
        .ubershader_mode =
            (XemuVulkanUbershaderMode)g_config.tweaks.vk_ubershader_mode,
    };
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        requested.policy[i] = selected[i] ? XEMU_TWEAK_POLICY_ENABLED :
                                            XEMU_TWEAK_POLICY_DISABLED;
    }
    return requested;
}

void xemu_tweaks_apply(bool startup)
{
    XemuTweakRequestedState requested = requested_from_config();
    g_mutex_lock(&profile_mutex);
    initialize_profile_locked();
    requested_profile = requested;
    if (startup) {
        environment.ubershader_installed = false;
        environment.ubershader_operational = false;
        startup_profile =
            xemu_tweaks_resolve(&requested_profile, &environment, NULL, true);
        qatomic_set(&xemu_vulkan_ubershader_latched_policy,
                    startup_profile.ubershader.policy);
    }
    publish_profile_locked();
    g_mutex_unlock(&profile_mutex);
}
