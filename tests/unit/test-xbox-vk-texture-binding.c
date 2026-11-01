/*
 * NV2A Vulkan texture-binding transition tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/texture-binding-state.h"
#include "hw/xbox/nv2a/pgraph/vk/texture-stage-counters.h"

static bool test_disabled_stage_keeps_dirty_for_reenable(void)
{
    bool enabled = false;
    bool dirty = true;
    bool bound = false;
    bool dummy = false;

    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }

    /* The first disabled bind selects the dummy without retiring guest state. */
    bound = true;
    dummy = true;
    if (pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy) ||
        !dirty) {
        return false;
    }

    enabled = true;
    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }
    dummy = false;
    dirty = false;
    if (pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }

    enabled = false;
    if (!pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy)) {
        return false;
    }
    dummy = true;
    return !pgraph_vk_texture_stage_needs_rebind(enabled, dirty, bound, dummy);
}

static bool test_failed_active_bind_remains_retryable(void)
{
    /* A failed bind selects the dummy and retains dirtiness. */
    return pgraph_vk_texture_stage_needs_rebind(true, true, true, true) &&
           pgraph_vk_texture_stage_needs_rebind(true, false, true, true);
}

static bool test_clean_stage_policy_gate(void)
{
    return pgraph_vk_should_skip_clean_texture_stage(true, true) &&
           !pgraph_vk_should_skip_clean_texture_stage(false, true) &&
           !pgraph_vk_should_skip_clean_texture_stage(true, false) &&
           !pgraph_vk_should_skip_clean_texture_stage(false, false);
}

static bool test_texture_source_identity(void)
{
    const uint64_t texture = 0x1000;
    const uint64_t palette = 0x2000;

    return pgraph_vk_texture_source_identity_matches(
               false, true, false, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, false, false, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, true, false,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, true,
               texture + 1, texture, 32, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 0, palette + 1, palette) &&
           pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 32, palette, palette) &&
           !pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture + 1, texture, 0, palette, palette) &&
           !pgraph_vk_texture_source_identity_matches(
               true, true, false, false,
               texture, texture, 32, palette + 1, palette);
}

static bool test_descriptor_identity_changes_only_for_visible_binding(void)
{
    const PGRAPHVkTextureDescriptorIdentity dummy = {
        .image_view = 1,
        .sampler = 2,
    };
    const PGRAPHVkTextureDescriptorIdentity real_a = {
        .image_view = 3,
        .sampler = 4,
    };
    const PGRAPHVkTextureDescriptorIdentity real_a_content_update = {
        .image_view = 3,
        .sampler = 4,
    };
    const PGRAPHVkTextureDescriptorIdentity view_only = {
        .image_view = 5,
        .sampler = 4,
    };
    const PGRAPHVkTextureDescriptorIdentity sampler_only = {
        .image_view = 3,
        .sampler = 6,
    };
    const PGRAPHVkTextureDescriptorIdentity real_b = {
        .image_view = 5,
        .sampler = 6,
    };

    return !pgraph_vk_texture_descriptor_identity_changed(dummy, dummy) &&
           pgraph_vk_texture_descriptor_identity_changed(real_a, dummy) &&
           pgraph_vk_texture_descriptor_identity_changed(dummy, real_a) &&
           !pgraph_vk_texture_descriptor_identity_changed(
               real_a, real_a_content_update) &&
           pgraph_vk_texture_descriptor_identity_changed(real_a, view_only) &&
           pgraph_vk_texture_descriptor_identity_changed(real_a,
                                                          sampler_only) &&
           pgraph_vk_texture_descriptor_identity_changed(real_a, real_b) &&
           pgraph_vk_texture_descriptor_identity_changed(real_b, dummy);
}

static bool test_failed_multistage_bind_detects_recovery_changes(void)
{
    const PGRAPHVkTextureDescriptorIdentity real_a = {
        .image_view = 3,
        .sampler = 4,
    };
    const PGRAPHVkTextureDescriptorIdentity real_b = {
        .image_view = 5,
        .sampler = 6,
    };
    const PGRAPHVkTextureDescriptorIdentity dummy = {
        .image_view = 1,
        .sampler = 2,
    };

    /* An earlier stage changes before a later stage fails to the dummy. The
     * draw is skipped, so the per-call change is not published. On retry the
     * recovered stage still changes from dummy to real and requires a fresh
     * descriptor publication. */
    bool failed_attempt_changed =
        pgraph_vk_texture_descriptor_identity_changed(real_a, real_b) ||
        pgraph_vk_texture_descriptor_identity_changed(real_a, dummy);
    bool recovery_attempt_changed =
        !pgraph_vk_texture_descriptor_identity_changed(real_b, real_b) &&
        pgraph_vk_texture_descriptor_identity_changed(dummy, real_a);

    return failed_attempt_changed && recovery_attempt_changed;
}

static bool test_failed_bind_retains_unpublished_descriptor_change(void)
{
    const PGRAPHVkTextureDescriptorIdentity dummy = {
        .image_view = 1,
        .sampler = 2,
    };
    const PGRAPHVkTextureDescriptorIdentity real_a = {
        .image_view = 3,
        .sampler = 4,
    };
    const PGRAPHVkTextureDescriptorIdentity real_b = {
        .image_view = 5,
        .sampler = 6,
    };
    bool publication_pending = false;

    /* Stage 0 changes before stage 1 fails while already bound to the dummy.
     * The failed draw cannot publish either stage. */
    pgraph_vk_texture_descriptor_publication_observe(
        &publication_pending, real_a, real_b);
    pgraph_vk_texture_descriptor_publication_observe(
        &publication_pending, dummy, dummy);
    if (!publication_pending) {
        return false;
    }

    /* The following draw disables stage 1 and changes no handles. The earlier
     * unpublished stage-0 change must still force a descriptor write. */
    pgraph_vk_texture_descriptor_publication_observe(
        &publication_pending, real_b, real_b);
    pgraph_vk_texture_descriptor_publication_observe(
        &publication_pending, dummy, dummy);
    if (!publication_pending) {
        return false;
    }

    pgraph_vk_texture_descriptor_publication_complete(
        &publication_pending);
    return !publication_pending;
}

static bool test_texture_stage_counter_accounting(void)
{
    PGRAPHVkTextureStageCounters counters = {0};
    pgraph_vk_texture_stage_counter_begin(&counters, false, true);
    pgraph_vk_texture_stage_counter_add(&counters, VK_TEXTURE_DIRTY_RANGE_CHECKS);
    pgraph_vk_texture_stage_counter_eligible(&counters, true);
    pgraph_vk_texture_stage_counter_end(&counters);
    if (counters.values[VK_TEXTURE_DIRTY_RANGE_CHECKS] ||
        counters.values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE]) {
        return false;
    }
    counters.collect_evidence = true;
    pgraph_vk_texture_stage_counter_begin(&counters, false, true);
    pgraph_vk_texture_stage_counter_add(&counters, VK_TEXTURE_DIRTY_RANGE_CHECKS);
    pgraph_vk_texture_stage_counter_eligible(&counters, true);
    pgraph_vk_texture_stage_counter_end(&counters);
    /* An external cache invalidation is not texture-bind work. */
    pgraph_vk_texture_stage_counter_add(&counters, VK_TEXTURE_CACHE_WALKS);
    pgraph_vk_texture_stage_counter_begin(&counters, true, false);
    pgraph_vk_texture_stage_counter_eligible(&counters, false);
    pgraph_vk_texture_stage_counter_end(&counters);
    pgraph_vk_texture_stage_counter_finish_interval(&counters, true, false);
    return counters.values[VK_TEXTURE_DIRTY_RANGE_CHECKS] == 1 &&
           counters.values[VK_TEXTURE_CACHE_WALKS] == 0 &&
           counters.values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] == 2 &&
           counters.values[VK_TEXTURE_CLEAN_STAGE_SKIPS] == 1 &&
           counters.values[VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE] == 1 &&
           counters.values[VK_TEXTURE_PERF_ENABLED_BIND_CALLS] == 1 &&
           counters.values[VK_TEXTURE_PERF_DISABLED_BIND_CALLS] == 1 &&
           counters.values[VK_TEXTURE_MIXED_PERF_INTERVALS] == 1 &&
           counters.values[VK_TEXTURE_MIXED_POLICY_INTERVALS] == 1 &&
           counters.values[VK_TEXTURE_FLIP_STALL_INTERVALS] == 1 &&
           counters.frame == 1 && !counters.overflowed;
}

static bool test_texture_stage_counter_overflow_and_reset(void)
{
    PGRAPHVkTextureStageCounters counters = {
        .frame = UINT64_MAX,
        .collect_evidence = true,
    };
    counters.values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] = UINT64_MAX;
    counters.values[VK_TEXTURE_CLEAN_STAGE_SKIPS] = UINT64_MAX;
    counters.values[VK_TEXTURE_DIRTY_RANGE_CHECKS] = UINT64_MAX;
    pgraph_vk_texture_stage_counter_begin(&counters, true, true);
    pgraph_vk_texture_stage_counter_eligible(&counters, false);
    pgraph_vk_texture_stage_counter_add(&counters, VK_TEXTURE_DIRTY_RANGE_CHECKS);
    pgraph_vk_texture_stage_counter_end(&counters);
    pgraph_vk_texture_stage_counter_finish_interval(&counters, true, true);
    if (!counters.overflowed || !counters.frame_overflowed ||
        counters.frame != UINT64_MAX ||
        counters.values[VK_TEXTURE_DIRTY_RANGE_CHECKS] != UINT64_MAX ||
        counters.values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] != UINT64_MAX ||
        counters.values[VK_TEXTURE_CLEAN_STAGE_SKIPS] != UINT64_MAX ||
        counters.values[VK_TEXTURE_CLEAN_STAGE_FORCED_REFERENCE] != 0) {
        return false;
    }
    pgraph_vk_texture_stage_counter_reset_interval(&counters);
    return counters.frame == UINT64_MAX && counters.frame_overflowed &&
           counters.collect_evidence && !counters.overflowed &&
           !counters.values[VK_TEXTURE_DIRTY_RANGE_CHECKS] &&
           !counters.values[VK_TEXTURE_CLEAN_STAGE_ELIGIBLE] &&
           !counters.observed_policy && !counters.observed_perf &&
           !counters.in_bind && !counters.collecting;
}

int main(void)
{
    bool disabled = test_disabled_stage_keeps_dirty_for_reenable();
    bool retry = test_failed_active_bind_remains_retryable();
    bool identity = test_texture_source_identity();
    bool descriptor_identity =
        test_descriptor_identity_changes_only_for_visible_binding();
    bool recovery_changes =
        test_failed_multistage_bind_detects_recovery_changes();
    bool retained_publication =
        test_failed_bind_retains_unpublished_descriptor_change();
    bool policy_gate = test_clean_stage_policy_gate();

    bool counters = test_texture_stage_counter_accounting();
    bool overflow = test_texture_stage_counter_overflow_and_reset();

    puts("TAP version 13");
    puts("1..9");
    printf("%s 1 - disabled dirty state waits for re-enable\n",
           disabled ? "ok" : "not ok");
    printf("%s 2 - failed active bind remains retryable\n",
           retry ? "ok" : "not ok");
    printf("%s 3 - texture source identity gates clean reuse\n",
           identity ? "ok" : "not ok");
    printf("%s 4 - descriptor identity changes only for visible binding\n",
           descriptor_identity ? "ok" : "not ok");
    printf("%s 5 - failed multistage bind detects recovery changes\n",
           recovery_changes ? "ok" : "not ok");
    printf("%s 6 - failed bind retains unpublished descriptor change\n",
           retained_publication ? "ok" : "not ok");
    printf("%s 7 - clean-stage policy gates only an eligible skip\n",
           policy_gate ? "ok" : "not ok");
    printf("%s 8 - texture counters preserve eligibility and observed policy/perf\n",
           counters ? "ok" : "not ok");
    printf("%s 9 - texture counters and progress cannot wrap across interval reset\n",
           overflow ? "ok" : "not ok");
    return (counters && overflow && disabled && retry && identity && descriptor_identity &&
            recovery_changes && retained_publication && policy_gate) ? 0 : 1;
}
