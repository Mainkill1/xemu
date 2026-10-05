/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "xemu-settings.h"
#include "xemu-tweaks.h"
#include "xemu-tweak-overrides.h"

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
static XemuTweakOverride *process_overrides;
static size_t process_override_count;
static uint64_t override_bits;
static bool mode_overridden;
static bool cache_overridden;
static bool startup_applied;
static int cache_requested_enabled;
static void initialize_profile_locked(void);
static void publish_profile_locked(void);

bool xemu_tweaks_set_overrides(const XemuTweakOverride *overrides, size_t count,
                              Error **errp)
{
    XemuTweakRequestedState checked = { 0 };
    if (count > SIZE_MAX / sizeof(*overrides) ||
        !xemu_tweak_apply_overrides(&checked, overrides, count, errp)) {
        if (count > SIZE_MAX / sizeof(*overrides)) {
            error_setg(errp, "Too many Advanced overrides.");
        }
        return false;
    }
    g_mutex_lock(&profile_mutex);
    if (startup_applied) {
        g_mutex_unlock(&profile_mutex);
        error_setg(errp, "Advanced overrides must be set before startup.");
        return false;
    }
    XemuTweakOverride *owned = count ? g_new(XemuTweakOverride, count) : NULL;
    if (count) {
        memcpy(owned, overrides, count * sizeof(*owned));
    }
    uint64_t bits = 0;
    bool mode = false;
    bool cache = false;
    for (size_t i = 0; i < count; i++) {
        if (owned[i].kind == XEMU_TWEAK_OVERRIDE_POLICY) {
            bits |= UINT64_C(1) << owned[i].tweak;
        } else if (owned[i].kind == XEMU_TWEAK_OVERRIDE_MODE) {
            bits |= UINT64_C(1) << XEMU_TWEAK_VK_HYBRID_UBERSHADERS;
            mode = true;
        } else {
            cache = true;
        }
    }
    g_free(process_overrides);
    process_overrides = owned;
    process_override_count = count;
    override_bits = bits;
    mode_overridden = mode;
    cache_overridden = cache;
    g_mutex_unlock(&profile_mutex);
    return true;
}

bool xemu_tweaks_cache_requested_enabled(void)
{
    return qatomic_read(&cache_requested_enabled);
}

void xemu_tweaks_publish_cache(XemuTweakRenderer owner, bool installed,
                              bool session_eligible)
{
    if (owner != XEMU_TWEAK_RENDERER_OPENGL &&
        owner != XEMU_TWEAK_RENDERER_VULKAN) {
        return;
    }
    g_mutex_lock(&profile_mutex);
    initialize_profile_locked();
    /* Setup precedes common renderer installation. Reject late publication
     * from a different backend without discarding matching pre-install state. */
    if (environment.renderer == XEMU_TWEAK_RENDERER_NONE ||
        environment.renderer == owner) {
        environment.cache_renderer = owner;
        environment.cache_installed = installed;
        environment.cache_session_eligible = installed && session_eligible;
        publish_profile_locked();
    }
    g_mutex_unlock(&profile_mutex);
}

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
    requested_profile.cache_policy = XEMU_TWEAK_POLICY_DISABLED;
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
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        next.state[i].overridden = (override_bits & (UINT64_C(1) << i)) != 0;
    }
    next.ubershader.overridden = mode_overridden;
    next.cache.overridden = cache_overridden;
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
    if (renderer == XEMU_TWEAK_RENDERER_NONE ||
        environment.cache_renderer != renderer) {
        environment.cache_renderer = XEMU_TWEAK_RENDERER_NONE;
        environment.cache_installed = false;
        environment.cache_session_eligible = false;
    }
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
        XemuTweakRuntimeState state = snapshot.state[i];
        g_string_append_printf(
            profile,
            "tweak.%s=requested:%s,effective:%s,available:%s,restart:%s,"
            "availability:%s,reason:%s,origin:%s\n",
            xemu_tweak_name((XemuTweak)i),
            tweak_policy_name(state.policy_requested),
            state.effective ? "enabled" : "disabled",
            state.available ? "yes" : "no",
            state.restart_pending ? "yes" : "no",
            tweak_availability_name(state.availability), state.reason,
            state.overridden ? "override" : "saved");
    }
    g_string_append_printf(
        profile,
        "tweak.vk_ubershader_mode=requested:%s,effective:%s,available:%s,"
        "restart:%s,reason:%s,origin:%s\n",
        ubershader_mode_name(ubershader.requested),
        ubershader_mode_name(ubershader.active),
        ubershader.available ? "yes" : "no",
        ubershader.restart_pending ? "yes" : "no", ubershader.reason,
        ubershader.overridden ? "override" : "saved");
    XemuTweakRuntimeState cache = snapshot.cache;
    g_string_append_printf(
        profile,
        "tweak.cache_shaders=requested:%s,effective:%s,available:%s,"
        "restart:%s,availability:%s,reason:%s,origin:%s,installed:%s,"
        "session_eligible:%s\n",
        tweak_policy_name(cache.policy_requested),
        cache.effective ? "enabled" : "disabled",
        cache.available ? "yes" : "no", cache.restart_pending ? "yes" : "no",
        tweak_availability_name(cache.availability), cache.reason,
        cache.overridden ? "override" : "saved",
        snapshot.cache_installed ? "yes" : "no",
        snapshot.cache_session_eligible ? "yes" : "no");
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
        .cache_policy = g_config.perf.cache_shaders ?
                            XEMU_TWEAK_POLICY_ENABLED :
                            XEMU_TWEAK_POLICY_DISABLED,
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
    /* The span is validated and owned before startup, never saved to config. */
    if (!xemu_tweak_apply_overrides(
            &requested, process_overrides, process_override_count,
            &error_abort)) {
        abort();
    }
    requested_profile = requested;
    qatomic_set(&cache_requested_enabled,
                requested.cache_policy != XEMU_TWEAK_POLICY_DISABLED);
    if (startup) {
        startup_applied = true;
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
