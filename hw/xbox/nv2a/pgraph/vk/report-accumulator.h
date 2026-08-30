/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_REPORT_ACCUMULATOR_H
#define HW_XBOX_NV2A_PGRAPH_VK_REPORT_ACCUMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Fold query results through one NV2A report boundary. Query results are
 * deliberately accumulated into uint32_t to preserve the hardware-facing
 * wraparound behavior of the renderer's report accumulator.
 */
static inline bool pgraph_vk_process_report_boundary(
    uint32_t *accumulator, size_t *num_results_consumed,
    const uint64_t *query_results, size_t num_query_results,
    size_t report_query_count, bool clear, uint32_t result_divisor,
    bool *write_report, uint32_t *report_value)
{
    if (report_query_count < *num_results_consumed ||
        report_query_count > num_query_results || !result_divisor) {
        return false;
    }

    while (*num_results_consumed < report_query_count) {
        *accumulator += query_results[(*num_results_consumed)++];
    }

    if (clear) {
        *accumulator = 0;
        *write_report = false;
    } else {
        *report_value = *accumulator / result_divisor;
        *write_report = true;
    }

    return true;
}

static inline bool pgraph_vk_accumulate_remaining_query_results(
    uint32_t *accumulator, size_t *num_results_consumed,
    const uint64_t *query_results, size_t num_query_results)
{
    if (*num_results_consumed > num_query_results) {
        return false;
    }

    while (*num_results_consumed < num_query_results) {
        *accumulator += query_results[(*num_results_consumed)++];
    }

    return true;
}

#endif
