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

unsigned int xemu_tweaks_active =
    ((1u << XEMU_TWEAK_COUNT) - 1) &
    ~((1u << XEMU_TWEAK_VK_HYBRID_UBERSHADERS) |
      (1u << XEMU_TWEAK_VK_SHADER_FASTPATH));
static int xemu_vulkan_ubershader_active_mode =
    XEMU_VK_UBERSHADER_OFF;
static bool xemu_vulkan_renderer_active;

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
           mode == XEMU_VK_UBERSHADER_FALLBACK;
}

XemuVulkanUbershaderRuntimeState
xemu_vulkan_ubershader_runtime_state(void)
{
    XemuVulkanUbershaderMode requested =
        (XemuVulkanUbershaderMode)g_config.tweaks.vk_ubershader_mode;
    XemuVulkanUbershaderMode active =
        qatomic_read(&xemu_vulkan_ubershader_active_mode);
    XemuVulkanUbershaderRuntimeState state = {
        .requested = requested,
        .active = active,
        .available = xemu_vulkan_renderer_active &&
                     xemu_vulkan_ubershader_mode_selectable(requested),
    };

    if (!xemu_vulkan_renderer_active) {
        state.reason = "Available with the Vulkan renderer.";
    } else if (!xemu_vulkan_ubershader_mode_selectable(requested)) {
        state.reason = "This mode is planned and is not active yet.";
    } else if (requested != active) {
        state.restart_pending = true;
        state.reason = "Restart xemu to activate the selected mode.";
    } else {
        state.reason = "Active.";
    }

    return state;
}

void xemu_tweaks_apply(bool startup)
{
    XemuVulkanUbershaderMode requested_mode =
        (XemuVulkanUbershaderMode)g_config.tweaks.vk_ubershader_mode;
    if (startup) {
        xemu_vulkan_renderer_active =
            g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN;
        XemuVulkanUbershaderMode active_mode =
            xemu_vulkan_renderer_active &&
                    xemu_vulkan_ubershader_mode_selectable(requested_mode) ?
                requested_mode :
                XEMU_VK_UBERSHADER_OFF;
        qatomic_set(&xemu_vulkan_ubershader_active_mode, active_mode);
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
            qatomic_read(&xemu_vulkan_ubershader_active_mode) !=
            XEMU_VK_UBERSHADER_OFF,
        [XEMU_TWEAK_VK_SHADER_FASTPATH] =
            g_config.tweaks.vk_shader_fastpath,
    };
    unsigned int active = qatomic_read(&xemu_tweaks_active);

    for (unsigned int i = 0; i < XEMU_TWEAK_COUNT; i++) {
        if (!startup && xemu_tweak_requires_restart(i)) {
            continue;
        }
        if (selected[i]) {
            active |= 1u << i;
        } else {
            active &= ~(1u << i);
        }
    }
    qatomic_set(&xemu_tweaks_active, active);
    qemu_poll_set_cpu_saving(selected[XEMU_TWEAK_CPU_SAVING_WAIT]);
}
