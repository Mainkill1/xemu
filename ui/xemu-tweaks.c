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

XemuTweakBits xemu_tweaks_active =
    (UINT64_MAX >> (64 - XEMU_TWEAK_COUNT)) &
    ~((UINT64_C(1) << XEMU_TWEAK_VK_HYBRID_UBERSHADERS) |
      (UINT64_C(1) << XEMU_TWEAK_VK_SHADER_FASTPATH) |
      (UINT64_C(1) << XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION) |
      (UINT64_C(1) << XEMU_TWEAK_NV20_VERTEX_ARITHMETIC));
static int xemu_vulkan_ubershader_latched_policy =
    XEMU_VK_UBERSHADER_OFF;

typedef enum XemuVulkanUbershaderRuntimeStatus {
    XEMU_VK_UBERSHADER_RUNTIME_NO_VULKAN,
    XEMU_VK_UBERSHADER_RUNTIME_ACTIVE,
    XEMU_VK_UBERSHADER_RUNTIME_DEGRADED,
} XemuVulkanUbershaderRuntimeStatus;

static int xemu_vulkan_ubershader_runtime_status =
    XEMU_VK_UBERSHADER_RUNTIME_NO_VULKAN;
static int xemu_tweaks_renderer = XEMU_TWEAK_RENDERER_NONE;

uint64_t xemu_tweaks_active_snapshot(void)
{
    return qatomic_read_u64(&xemu_tweaks_active);
}

void xemu_tweaks_publish_renderer(XemuTweakRenderer renderer)
{
    if (renderer < XEMU_TWEAK_RENDERER_NONE ||
        renderer > XEMU_TWEAK_RENDERER_VULKAN) {
        renderer = XEMU_TWEAK_RENDERER_NONE;
    }
    qatomic_set(&xemu_tweaks_renderer, renderer);
}

static bool xemu_tweak_requested(XemuTweak tweak)
{
    switch (tweak) {
    case XEMU_TWEAK_CPU_SAVING_WAIT:
        return g_config.tweaks.cpu_saving_wait;
    case XEMU_TWEAK_PGRAPH_BULK_PACKETS:
        return g_config.tweaks.pgraph_bulk_packets;
    case XEMU_TWEAK_PGRAPH_FENCE_FASTPATH:
        return g_config.tweaks.pgraph_fence_fastpath;
    case XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING:
        return g_config.tweaks.vk_color_download_folding;
    case XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS:
        return g_config.tweaks.vk_bounded_vertex_uploads;
    case XEMU_TWEAK_VK_VERTEX_COPY_SHORTCUTS:
        return g_config.tweaks.vk_vertex_copy_shortcuts;
    case XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH:
        return g_config.tweaks.vk_transient_buffer_growth;
    case XEMU_TWEAK_GL_NATIVE_S3TC:
        return g_config.tweaks.gl_native_s3tc;
    case XEMU_TWEAK_VK_HYBRID_UBERSHADERS:
        return g_config.tweaks.vk_ubershader_mode !=
               XEMU_VK_UBERSHADER_OFF;
    case XEMU_TWEAK_VK_SHADER_FASTPATH:
        return g_config.tweaks.vk_shader_fastpath;
    case XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION:
        return g_config.tweaks.issue149_effect_suppression;
    case XEMU_TWEAK_NV20_VERTEX_ARITHMETIC:
        return g_config.tweaks.nv20_vertex_arithmetic;
    default:
        return false;
    }
}

static XemuTweakRuntimeState tweak_runtime_state(
    XemuTweak tweak, XemuTweakRenderer renderer, uint64_t active,
    const XemuVulkanUbershaderRuntimeState *ubershader)
{
    XemuTweakRuntimeState state = { 0 };

    if ((unsigned int)tweak >= XEMU_TWEAK_COUNT) {
        state.reason = "Unknown Advanced setting.";
        return state;
    }

    state.requested = xemu_tweak_requested(tweak);
    state.policy_requested = state.requested ?
        XEMU_TWEAK_POLICY_ENABLED : XEMU_TWEAK_POLICY_DISABLED;

    switch (tweak) {
    case XEMU_TWEAK_CPU_SAVING_WAIT:
#ifdef _WIN32
        state.available = true;
#else
        state.reason = "This host wait route is available on Windows.";
#endif
        break;
    case XEMU_TWEAK_PGRAPH_BULK_PACKETS:
    case XEMU_TWEAK_PGRAPH_FENCE_FASTPATH:
    case XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION:
        state.available = renderer != XEMU_TWEAK_RENDERER_NONE;
        if (!state.available) {
            state.reason = "Available when a renderer is installed.";
        }
        break;
    case XEMU_TWEAK_GL_NATIVE_S3TC:
        state.available = renderer == XEMU_TWEAK_RENDERER_OPENGL;
        if (!state.available) {
            state.reason = "Available with the OpenGL renderer.";
        }
        break;
    case XEMU_TWEAK_NV20_VERTEX_ARITHMETIC:
        state.available = renderer != XEMU_TWEAK_RENDERER_NONE;
        if (!state.available) {
            state.reason = "Available when a renderer is installed.";
        }
        break;
    case XEMU_TWEAK_VK_SHADER_FASTPATH:
        state.available = renderer == XEMU_TWEAK_RENDERER_VULKAN &&
            ubershader->active !=
                XEMU_VK_UBERSHADER_OFF;
        if (!state.available) {
            state.reason = "Requires an active Vulkan ubershader mode.";
        }
        break;
    case XEMU_TWEAK_VK_HYBRID_UBERSHADERS:
        state.available = renderer == XEMU_TWEAK_RENDERER_VULKAN &&
            ubershader->available;
        if (!state.available) {
            state.reason = ubershader->reason;
        }
        break;
    default:
        state.available = renderer == XEMU_TWEAK_RENDERER_VULKAN;
        if (!state.available) {
            state.reason = "Available with the Vulkan renderer.";
        }
        break;
    }

    state.availability = state.available ? XEMU_TWEAK_AVAILABLE :
                         XEMU_TWEAK_UNSUPPORTED_BACKEND;
    if (!state.available) {
        if (tweak == XEMU_TWEAK_CPU_SAVING_WAIT) {
            state.availability = XEMU_TWEAK_UNSUPPORTED_PLATFORM;
        } else if (renderer == XEMU_TWEAK_RENDERER_VULKAN &&
                   tweak == XEMU_TWEAK_VK_SHADER_FASTPATH) {
            state.availability = XEMU_TWEAK_BLOCKED_DEPENDENCY;
        } else if (renderer == XEMU_TWEAK_RENDERER_VULKAN &&
                   tweak == XEMU_TWEAK_VK_HYBRID_UBERSHADERS) {
            state.availability = XEMU_TWEAK_UNSUPPORTED_CAPABILITY;
        }
    }
    XemuTweakPolicyResolution resolved = xemu_tweak_policy_resolve(
        state.policy_requested, false,
        (active & (UINT64_C(1) << tweak)) != 0, false,
        xemu_tweak_requires_restart(tweak), state.availability);
    state.selected = resolved.selected;
    state.effective = resolved.effective;
    state.restart_pending = resolved.restart_pending;
    if (!state.reason) {
        state.reason = resolved.reason;
    }
    return state;
}

XemuTweakRuntimeState xemu_tweak_runtime_state(XemuTweak tweak)
{
    XemuVulkanUbershaderRuntimeState ubershader =
        xemu_vulkan_ubershader_runtime_state();
    return tweak_runtime_state(tweak, qatomic_read(&xemu_tweaks_renderer),
                               xemu_tweaks_active_snapshot(), &ubershader);
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
        { XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION,
          "issue149_effect_suppression" },
        { XEMU_TWEAK_NV20_VERTEX_ARITHMETIC, "nv20_vertex_arithmetic" },
    };
    G_STATIC_ASSERT(ARRAY_SIZE(names) == XEMU_TWEAK_COUNT);
    assert(buffer || size == 0);

    /* One captured mask/backend/mode serves every row. Configuration is owned
     * by this caller's UI thread. Renderer lifecycle publication can overlap
     * this read; this is not a transaction with a particular guest draw. */
    XemuTweakRenderer renderer = qatomic_read(&xemu_tweaks_renderer);
    uint64_t active = xemu_tweaks_active_snapshot();
    XemuVulkanUbershaderRuntimeState ubershader =
        xemu_vulkan_ubershader_runtime_state();
    if (renderer != XEMU_TWEAK_RENDERER_VULKAN) {
        ubershader.active = XEMU_VK_UBERSHADER_OFF;
        ubershader.available = false;
        ubershader.reason = "Available when the Vulkan renderer is installed.";
    }
    GString *profile = g_string_new("schema=xemu-tweak-profile/v1\n");
    g_string_append_printf(profile, "renderer=%s\n",
                           renderer == XEMU_TWEAK_RENDERER_VULKAN ? "vulkan" :
                           renderer == XEMU_TWEAK_RENDERER_OPENGL ? "opengl" :
                                                                    "none");
    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        XemuTweakRuntimeState state =
            tweak_runtime_state(names[i].tweak, renderer, active, &ubershader);
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
    bool mode_present, XemuVulkanUbershaderMode mode,
    bool legacy_enabled)
{
    if (mode_present) {
        return mode;
    }

    return legacy_enabled ? XEMU_VK_UBERSHADER_FALLBACK :
                            XEMU_VK_UBERSHADER_OFF;
}

bool xemu_vulkan_ubershader_mode_selectable(
    XemuVulkanUbershaderMode mode)
{
    return mode == XEMU_VK_UBERSHADER_OFF ||
           mode == XEMU_VK_UBERSHADER_FALLBACK ||
           mode == XEMU_VK_UBERSHADER_PREWARM ||
           mode == XEMU_VK_UBERSHADER_ALWAYS;
}

XemuVulkanUbershaderMode xemu_vulkan_ubershader_policy(void)
{
    return qatomic_read(&xemu_vulkan_ubershader_latched_policy);
}

XemuVulkanUbershaderRuntimeState
xemu_vulkan_ubershader_runtime_state(void)
{
    XemuVulkanUbershaderMode requested =
        (XemuVulkanUbershaderMode)g_config.tweaks.vk_ubershader_mode;
    XemuVulkanUbershaderMode policy =
        qatomic_read(&xemu_vulkan_ubershader_latched_policy);
    XemuVulkanUbershaderRuntimeStatus runtime_status =
        qatomic_read(&xemu_vulkan_ubershader_runtime_status);
    XemuVulkanUbershaderMode active =
        runtime_status == XEMU_VK_UBERSHADER_RUNTIME_ACTIVE ? policy :
        XEMU_VK_UBERSHADER_OFF;
    XemuVulkanUbershaderRuntimeState state = {
        .requested = requested,
        .policy = policy,
        .active = active,
        .available = runtime_status == XEMU_VK_UBERSHADER_RUNTIME_ACTIVE &&
                     xemu_vulkan_ubershader_mode_selectable(requested),
        .restart_pending =
            xemu_vulkan_ubershader_mode_selectable(requested) &&
            requested != policy,
    };

    if (!xemu_vulkan_ubershader_mode_selectable(requested)) {
        state.reason = "This mode is planned and is not active yet.";
    } else if (runtime_status == XEMU_VK_UBERSHADER_RUNTIME_NO_VULKAN) {
        state.reason = "Available when the Vulkan renderer is installed.";
    } else if (runtime_status == XEMU_VK_UBERSHADER_RUNTIME_DEGRADED) {
        state.reason = "Ubershader setup failed; Vulkan is using "
                       "specialized shaders.";
    } else {
        state.reason = "Active.";
    }

    return state;
}

void xemu_vulkan_ubershader_publish_runtime(
    bool vulkan_installed, bool ubershader_operational)
{
    XemuVulkanUbershaderMode policy =
        qatomic_read(&xemu_vulkan_ubershader_latched_policy);
    XemuVulkanUbershaderRuntimeStatus status;

    if (!vulkan_installed) {
        status = XEMU_VK_UBERSHADER_RUNTIME_NO_VULKAN;
    } else if (policy != XEMU_VK_UBERSHADER_OFF && !ubershader_operational) {
        status = XEMU_VK_UBERSHADER_RUNTIME_DEGRADED;
    } else {
        status = XEMU_VK_UBERSHADER_RUNTIME_ACTIVE;
    }
    qatomic_set(&xemu_vulkan_ubershader_runtime_status, status);
}

void xemu_tweaks_apply(bool startup)
{
    XemuVulkanUbershaderMode requested_mode =
        (XemuVulkanUbershaderMode)g_config.tweaks.vk_ubershader_mode;
    if (startup) {
        XemuVulkanUbershaderMode policy =
            xemu_vulkan_ubershader_mode_selectable(requested_mode) ?
                requested_mode :
                XEMU_VK_UBERSHADER_OFF;
        qatomic_set(&xemu_vulkan_ubershader_latched_policy, policy);
        qatomic_set(&xemu_vulkan_ubershader_runtime_status,
                    XEMU_VK_UBERSHADER_RUNTIME_NO_VULKAN);
    }
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
        [XEMU_TWEAK_VK_HYBRID_UBERSHADERS] =
            qatomic_read(&xemu_vulkan_ubershader_latched_policy) !=
            XEMU_VK_UBERSHADER_OFF,
        [XEMU_TWEAK_VK_SHADER_FASTPATH] =
            g_config.tweaks.vk_shader_fastpath,
        [XEMU_TWEAK_ISSUE149_EFFECT_SUPPRESSION] =
            g_config.tweaks.issue149_effect_suppression,
        [XEMU_TWEAK_NV20_VERTEX_ARITHMETIC] =
            g_config.tweaks.nv20_vertex_arithmetic,
    };
    XemuTweakBits active = qatomic_read_u64(&xemu_tweaks_active);

    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        XemuTweakBits bit = UINT64_C(1) << i;
        XemuTweakPolicyResolution resolved = xemu_tweak_policy_resolve(
            selected[i] ? XEMU_TWEAK_POLICY_ENABLED :
                          XEMU_TWEAK_POLICY_DISABLED,
            false, (active & bit) != 0, true,
            !startup && xemu_tweak_requires_restart(i),
            XEMU_TWEAK_AVAILABLE);
        if (resolved.selected) {
            active |= bit;
        } else {
            active &= ~bit;
        }
    }
    qatomic_set_u64(&xemu_tweaks_active, active);
    qemu_poll_set_cpu_saving(selected[XEMU_TWEAK_CPU_SAVING_WAIT]);
}
