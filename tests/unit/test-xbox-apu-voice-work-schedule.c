/*
 * MCPX APU voice work scheduling policy tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/mcpx/apu/vp/voice-work-schedule.h"

static void test_inline_threshold(void)
{
    g_assert_true(mcpx_apu_voice_work_should_process_inline(0));
    g_assert_true(mcpx_apu_voice_work_should_process_inline(1));
    g_assert_true(mcpx_apu_voice_work_should_process_inline(2));
    g_assert_false(mcpx_apu_voice_work_should_process_inline(3));
}

static void test_bounded_workers_and_signal_count(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(&schedule, 3);

    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 1, 0),
                     ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 2, 0),
                     ==, 1);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 4, 0),
                     ==, 2);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 8, 0),
                     ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 16, 0),
                     ==, 1);
    g_assert_cmphex(schedule.workers_pending, ==, 0x7);
    g_assert_cmphex(schedule.touched_mixbins, ==, 0x1f);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 3);
}

static void test_grouped_multipass_affinity(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(&schedule, 4);

    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, MULTIPASS_BIN_MASK, 0),
                     ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, MULTIPASS_BIN_MASK, 1,
                         MULTIPASS_BIN_MASK),
                     ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 2, 0),
                     ==, 1);
    g_assert_cmphex(schedule.workers_pending, ==, 0x3);
    g_assert_cmphex(schedule.touched_mixbins, ==,
                    ((uint32_t)MULTIPASS_BIN_MASK) | 0x3);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 2);
}

static void test_independent_distribution(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(&schedule, 4);

    for (size_t i = 0; i < 4; i++) {
        g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                             &schedule, 0, 1U << i, 0),
                         ==, i);
    }
    g_assert_cmphex(schedule.workers_pending, ==, 0xf);
    g_assert_cmphex(schedule.touched_mixbins, ==, 0xf);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 4);
}

static void test_touched_mixbin_mask(void)
{
    uint32_t direct = mcpx_apu_voice_work_touched_mixbins(0, 0x5, 0);
    uint32_t multipass = mcpx_apu_voice_work_touched_mixbins(
        MULTIPASS_BIN_MASK, 0x3, MULTIPASS_BIN_MASK);

    g_assert_cmphex(direct, ==, 0x5);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(direct), ==, 2);

    g_assert_cmphex(multipass, ==, ((uint32_t)MULTIPASS_BIN_MASK) | 0x3);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(multipass), ==, 3);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(UINT32_MAX), ==,
                     NUM_MIXBINS);
}

static void test_mixbin_mask_policy(void)
{
    uint32_t sparse = 0x5;
    uint32_t dense = (1U << (VOICE_WORK_DENSE_MIXBIN_THRESHOLD + 1)) - 1;
    uint32_t full = mcpx_apu_voice_work_full_mixbin_mask();

    g_assert_cmphex(full, ==, UINT32_MAX);
    g_assert_false(mcpx_apu_voice_work_mixbin_mask_is_full(sparse));
    g_assert_false(mcpx_apu_voice_work_mixbin_mask_is_dense(sparse));
    g_assert_false(mcpx_apu_voice_work_mixbin_mask_is_full(dense));
    g_assert_true(mcpx_apu_voice_work_mixbin_mask_is_dense(dense));
    g_assert_true(mcpx_apu_voice_work_mixbin_mask_is_full(full));
    g_assert_true(mcpx_apu_voice_work_mixbin_mask_is_dense(full));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/apu/voice-work/inline-threshold",
                    test_inline_threshold);
    g_test_add_func("/xbox/apu/voice-work/bounded-workers",
                    test_bounded_workers_and_signal_count);
    g_test_add_func("/xbox/apu/voice-work/grouped-multipass-affinity",
                    test_grouped_multipass_affinity);
    g_test_add_func("/xbox/apu/voice-work/independent-distribution",
                    test_independent_distribution);
    g_test_add_func("/xbox/apu/voice-work/touched-mixbin-mask",
                    test_touched_mixbin_mask);
    g_test_add_func("/xbox/apu/voice-work/mixbin-mask-policy",
                    test_mixbin_mask_policy);
    return g_test_run();
}
