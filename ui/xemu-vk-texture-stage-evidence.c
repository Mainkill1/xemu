/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "ui/xemu-vk-texture-stage-evidence.h"

static const XemuShortcutCounterDescriptor
    descriptors[VK_TEXTURE_COUNTER_COUNT] = {
        [VK_TEXTURE_WHOLE_CLEAN_RETURNS] = { "vk.texture.whole_clean_returns",
                                             "calls" },
        [VK_TEXTURE_SLOW_BIND_CALLS] = { "vk.texture.slow_bind_calls",
                                         "calls" },
        [VK_TEXTURE_STAGE_CHECKS] = { "vk.texture.stage_checks", "stages" },
        [VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] = { "vk.texture.clean_stage_eligible",
                                              "stages" },
        [VK_TEXTURE_CLEAN_STAGE_SKIPS] = { "vk.texture.clean_stage_skips",
                                           "stages" },
        [VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE] = { "vk.texture.clean_stage_"
                                                      "forced_reference",
                                                      "stages" },
        [VK_TEXTURE_DIRTY_RANGE_CHECKS] = { "vk.texture.dirty_range_checks",
                                            "calls" },
        [VK_TEXTURE_CACHE_WALKS] = { "vk.texture.cache_walks", "walks" },
        [VK_TEXTURE_SURFACE_OVERLAP_QUERIES] = { "vk.texture.surface_overlap_"
                                                 "queries",
                                                 "calls" },
        [VK_TEXTURE_DMA_RESOLVES] = { "vk.texture.dma_resolves", "calls" },
        [VK_TEXTURE_PALETTE_DMA_RESOLVES] = { "vk.texture.palette_dma_resolves",
                                              "calls" },
        [VK_TEXTURE_POLICY_ENABLED_BIND_CALLS] = { "vk.texture.policy_enabled_"
                                                   "bind_calls",
                                                   "calls" },
        [VK_TEXTURE_POLICY_REFERENCE_BIND_CALLS] = { "vk.texture.policy_"
                                                     "reference_bind_calls",
                                                     "calls" },
        [VK_TEXTURE_PERF_ENABLED_BIND_CALLS] = { "vk.texture.perf_enabled_bind_"
                                                 "calls",
                                                 "calls" },
        [VK_TEXTURE_PERF_DISABLED_BIND_CALLS] = { "vk.texture.perf_disabled_"
                                                  "bind_calls",
                                                  "calls" },
        [VK_TEXTURE_MIXED_POLICY_INTERVALS] = { "vk.texture.mixed_policy_"
                                                "intervals",
                                                "intervals" },
        [VK_TEXTURE_MIXED_PERF_INTERVALS] = { "vk.texture.mixed_perf_intervals",
                                              "intervals" },
        [VK_TEXTURE_FLIP_STALL_INTERVALS] = { "vk.texture.flip_stall_intervals",
                                              "intervals" },
    };

XemuShortcutWindow *xemu_vk_texture_stage_evidence_create(bool perf_enabled,
                                                          Error **errp)
{
    if (!xemu_shortcut_evidence_enabled()) {
        return NULL;
    }
    XemuShortcutWindow *window = g_new0(XemuShortcutWindow, 1);
    if (!xemu_shortcut_window_init(window, "vk.texture_stage.flip_stall_method",
                                   descriptors, ARRAY_SIZE(descriptors), 1,
                                   errp)) {
        g_free(window);
        return NULL;
    }
    if (!window->handle) {
        g_free(window);
        return NULL;
    }
    g_autoptr(QDict) fields = qdict_new();
    qdict_put_str(fields, "policy_key", "vk_skip_clean_texture_stages");
    qdict_put_str(fields, "progress_boundary", "nv097_set_flip_stall_method");
    qdict_put_bool(fields, "perf_log_enabled", perf_enabled);
    if (!xemu_shortcut_evidence_publish_execution("vulkan_texture_stage",
                                                  fields, errp)) {
        xemu_shortcut_window_destroy(window, &error_abort);
        g_free(window);
        return NULL;
    }
    return window;
}

void xemu_vk_texture_stage_evidence_boundary(
    XemuShortcutWindow *window, PGRAPHVkTextureStageCounters *counters,
    uint64_t monotonic_ns, const XemuTweakResolution *profile)
{
    if (!window) {
        return;
    }
    if (window->active) {
        for (size_t i = 0; i < VK_TEXTURE_COUNTER_COUNT; i++) {
            xemu_shortcut_window_add(window, i, counters->values[i]);
        }
        uint64_t eligible = counters->values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE];
        uint64_t skips = counters->values[VK_TEXTURE_CLEAN_STAGE_SKIPS];
        uint64_t reference =
            counters->values[VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE];
        if (counters->overflowed) {
            window->live.overflowed = true;
        }
        uint8_t expected_policy =
            profile->effective_bits &
                    (UINT64_C(1) << XEMU_TWEAK_VK_SKIP_CLEAN_TEXTURE_STAGES) ?
                2 :
                1;
        if (counters->overflowed || skips > eligible ||
            reference != eligible - skips ||
            (counters->observed_policy &&
             counters->observed_policy != expected_policy) ||
            counters->observed_perf == 3) {
            xemu_shortcut_window_invalidate(window);
        }
    }
    if (counters->frame_overflowed) {
        window->live.overflowed = true;
        xemu_shortcut_window_invalidate(window);
    }
    xemu_shortcut_window_boundary(window, counters->frame, monotonic_ns,
                                  profile);
    counters->collect_evidence = window->active;
}

void xemu_vk_texture_stage_evidence_invalidate(
    XemuShortcutWindow *window, PGRAPHVkTextureStageCounters *counters)
{
    if (window) {
        xemu_shortcut_window_invalidate(window);
        counters->collect_evidence = window->active;
    }
}

void xemu_vk_texture_stage_evidence_destroy(XemuShortcutWindow *window)
{
    if (window) {
        xemu_shortcut_window_destroy(window, &error_abort);
        g_free(window);
    }
}
