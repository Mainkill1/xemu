/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef UI_XEMU_VK_TEXTURE_STAGE_EVIDENCE_H
#define UI_XEMU_VK_TEXTURE_STAGE_EVIDENCE_H

#include "ui/xemu-shortcut-window.h"
#include "hw/xbox/nv2a/pgraph/vk/texture-stage-counters.h"

/* The Vulkan renderer owns this window, including publication and retirement.
 */
XemuShortcutWindow *xemu_vk_texture_stage_evidence_create(bool perf_enabled,
                                                          Error **errp);
void xemu_vk_texture_stage_evidence_boundary(
    XemuShortcutWindow *window, PGRAPHVkTextureStageCounters *counters,
    uint64_t monotonic_ns, const XemuTweakResolution *profile);
void xemu_vk_texture_stage_evidence_invalidate(
    XemuShortcutWindow *window, PGRAPHVkTextureStageCounters *counters);
void xemu_vk_texture_stage_evidence_destroy(XemuShortcutWindow *window);

#endif
