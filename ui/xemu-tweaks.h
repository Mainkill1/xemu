/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_TWEAKS_H
#define XEMU_TWEAKS_H

#include <stdbool.h>
#include "qemu/atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XemuTweak {
    XEMU_TWEAK_CPU_SAVING_WAIT,
    XEMU_TWEAK_PGRAPH_BULK_PACKETS,
    XEMU_TWEAK_PGRAPH_FENCE_FASTPATH,
    XEMU_TWEAK_VK_COLOR_DOWNLOAD_FOLDING,
    XEMU_TWEAK_VK_BOUNDED_VERTEX_UPLOADS,
    XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH,
    XEMU_TWEAK_GL_NATIVE_S3TC,
    XEMU_TWEAK_VK_HYBRID_UBERSHADERS,
    XEMU_TWEAK_COUNT,
} XemuTweak;

/* Workers read this snapshot, never the UI-owned mutable g_config. */
extern unsigned int xemu_tweaks_active;

static inline bool xemu_tweak_enabled(XemuTweak tweak)
{
    return (qatomic_read(&xemu_tweaks_active) & (1u << tweak)) != 0;
}

static inline bool xemu_tweak_requires_restart(XemuTweak tweak)
{
    return tweak == XEMU_TWEAK_VK_TRANSIENT_BUFFER_GROWTH ||
           tweak == XEMU_TWEAK_GL_NATIVE_S3TC ||
           tweak == XEMU_TWEAK_VK_HYBRID_UBERSHADERS;
}

/* UI thread only. startup=true is only valid before workers are created. */
void xemu_tweaks_apply(bool startup);

#ifdef __cplusplus
}
#endif
#endif
