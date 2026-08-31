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

    g_assert_cmpuint(mcpx_apu_voice_work_effective_workers(0, 4), ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_effective_workers(2, 4), ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_effective_workers(3, 4), ==, 2);
}

static void test_bounded_workers_and_signal_count(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(
        &schedule, mcpx_apu_voice_work_effective_workers(5, 8));

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
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 3);
}

static void test_tiny_non_inline_batch_caps_workers(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(
        &schedule, mcpx_apu_voice_work_effective_workers(3, 8));

    g_assert_cmpuint(schedule.num_workers, ==, 2);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 1, 0),
                     ==, 0);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 2, 0),
                     ==, 1);
    g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                         &schedule, 0, 4, 0),
                     ==, 0);
    g_assert_cmphex(schedule.workers_pending, ==, 0x3);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 2);
}

static void test_grouped_multipass_affinity(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(
        &schedule, mcpx_apu_voice_work_effective_workers(3, 4));

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
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 2);
}

static void test_independent_distribution(void)
{
    MCPXAPUVoiceWorkScheduleState schedule;

    mcpx_apu_voice_work_schedule_init(
        &schedule, mcpx_apu_voice_work_effective_workers(4, 4));

    for (size_t i = 0; i < 4; i++) {
        g_assert_cmpuint(mcpx_apu_voice_work_schedule_assign_one(
                             &schedule, 0, 1U << i, 0),
                         ==, i % 2);
    }
    g_assert_cmphex(schedule.workers_pending, ==, 0x3);
    g_assert_cmpuint(mcpx_apu_voice_work_signal_count(
                         schedule.workers_pending),
                     ==, 2);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/apu/voice-work/inline-threshold",
                    test_inline_threshold);
    g_test_add_func("/xbox/apu/voice-work/bounded-workers",
                    test_bounded_workers_and_signal_count);
    g_test_add_func("/xbox/apu/voice-work/tiny-non-inline-cap",
                    test_tiny_non_inline_batch_caps_workers);
    g_test_add_func("/xbox/apu/voice-work/grouped-multipass-affinity",
                    test_grouped_multipass_affinity);
    g_test_add_func("/xbox/apu/voice-work/independent-distribution",
                    test_independent_distribution);
    return g_test_run();
}
