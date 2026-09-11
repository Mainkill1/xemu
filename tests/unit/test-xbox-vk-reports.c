/*
 * NV2A Vulkan report queue progress tests
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdbool.h>
#include <stdio.h>

#include "hw/xbox/nv2a/pgraph/vk/reports.h"

static bool test_idle_pending_reports_finish_without_command_buffer(void)
{
    return pgraph_vk_report_queue_needs_finish(true, true);
}

static bool test_empty_queue_does_not_finish(void)
{
    return !pgraph_vk_report_queue_needs_finish(true, false);
}

static bool test_busy_fifo_does_not_finish(void)
{
    return !pgraph_vk_report_queue_needs_finish(false, true);
}

int main(void)
{
    bool idle_pending = test_idle_pending_reports_finish_without_command_buffer();
    bool empty = test_empty_queue_does_not_finish();
    bool busy = test_busy_fifo_does_not_finish();

    puts("TAP version 13");
    puts("1..3");
    printf("%s 1 - idle pending reports finish without a command buffer\n",
           idle_pending ? "ok" : "not ok");
    printf("%s 2 - an empty queue does not finish\n", empty ? "ok" : "not ok");
    printf("%s 3 - a busy FIFO does not finish\n", busy ? "ok" : "not ok");

    return idle_pending && empty && busy ? 0 : 1;
}
