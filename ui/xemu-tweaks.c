/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "xemu-settings.h"
#include "xemu-tweaks.h"

unsigned int xemu_tweaks_active =
    ((1u << XEMU_TWEAK_COUNT) - 1) &
    ~(1u << XEMU_TWEAK_VK_HYBRID_UBERSHADERS);

void xemu_tweaks_apply(bool startup)
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
        [XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH] =
            g_config.tweaks.vk_transient_buffer_growth,
        [XEMU_TWEAK_GL_NATIVE_S3TC] = g_config.tweaks.gl_native_s3tc,
        [XEMU_TWEAK_VK_HYBRID_UBERSHADERS] =
            g_config.tweaks.vk_hybrid_ubershaders,
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
