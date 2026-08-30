/*
 * Vulkan NV2A report accumulator tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/report-accumulator.h"

static void test_report_boundaries(void)
{
    const uint64_t results[] = { 4, 8, 16, 32 };
    uint32_t accumulator = 0;
    size_t consumed = 0;
    bool write_report;
    uint32_t report_value;

    g_assert_true(pgraph_vk_process_report_boundary(
        &accumulator, &consumed, results, G_N_ELEMENTS(results), 2, false, 4,
        &write_report, &report_value));
    g_assert_true(write_report);
    g_assert_cmpuint(report_value, ==, 3);
    g_assert_cmpuint(accumulator, ==, 12);
    g_assert_cmpuint(consumed, ==, 2);

    g_assert_true(pgraph_vk_process_report_boundary(
        &accumulator, &consumed, results, G_N_ELEMENTS(results), 3, true, 4,
        &write_report, &report_value));
    g_assert_false(write_report);
    g_assert_cmpuint(accumulator, ==, 0);
    g_assert_cmpuint(consumed, ==, 3);

    g_assert_true(pgraph_vk_accumulate_remaining_query_results(
        &accumulator, &consumed, results, G_N_ELEMENTS(results)));
    g_assert_cmpuint(accumulator, ==, 32);
    g_assert_cmpuint(consumed, ==, G_N_ELEMENTS(results));
}

static void test_invalid_boundaries(void)
{
    const uint64_t results[] = { 1, 2 };
    uint32_t accumulator = 0;
    size_t consumed = 1;
    bool write_report;
    uint32_t report_value;

    g_assert_false(pgraph_vk_process_report_boundary(
        &accumulator, &consumed, results, G_N_ELEMENTS(results), 0, false, 1,
        &write_report, &report_value));
    g_assert_false(pgraph_vk_process_report_boundary(
        &accumulator, &consumed, results, G_N_ELEMENTS(results), 3, false, 1,
        &write_report, &report_value));
    g_assert_false(pgraph_vk_process_report_boundary(
        &accumulator, &consumed, results, G_N_ELEMENTS(results), 1, false, 0,
        &write_report, &report_value));
}

static void test_uint32_wrap(void)
{
    const uint64_t results[] = { UINT32_MAX, 2 };
    uint32_t accumulator = 0;
    size_t consumed = 0;

    g_assert_true(pgraph_vk_accumulate_remaining_query_results(
        &accumulator, &consumed, results, G_N_ELEMENTS(results)));
    g_assert_cmpuint(accumulator, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk-reports/boundaries", test_report_boundaries);
    g_test_add_func("/xbox/vk-reports/invalid-boundaries",
                    test_invalid_boundaries);
    g_test_add_func("/xbox/vk-reports/uint32-wrap", test_uint32_wrap);
    return g_test_run();
}
