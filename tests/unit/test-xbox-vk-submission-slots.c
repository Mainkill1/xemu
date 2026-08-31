/*
 * Vulkan NV2A submission-slot tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/submission-slots.h"

static void test_slot_selection(void)
{
    g_assert_cmpuint(pgraph_vk_next_submission_slot(0, 1), ==, 0);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(0, 2), ==, 1);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(1, 2), ==, 0);
    g_assert_cmpuint(pgraph_vk_next_submission_slot(2, 3), ==, 0);
}

static void test_slot_lifetime(void)
{
    PGRAPHVkSubmissionSlotState slot = { 0 };

    g_assert_false(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 0);

    pgraph_vk_submission_slot_mark_submitted(&slot, 7);
    g_assert_true(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 7);

    pgraph_vk_submission_slot_mark_retired(&slot);
    g_assert_false(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 7);

    pgraph_vk_submission_slot_mark_submitted(&slot, 8);
    g_assert_true(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 8);

    pgraph_vk_submission_slot_mark_retired(&slot);
    pgraph_vk_submission_slot_mark_submitted(&slot, 0);
    g_assert_true(slot.in_flight);
    g_assert_cmpuint(slot.submission_serial, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-submission-slots/selection",
                    test_slot_selection);
    g_test_add_func("/xbox/vk-submission-slots/lifetime",
                    test_slot_lifetime);
    return g_test_run();
}
