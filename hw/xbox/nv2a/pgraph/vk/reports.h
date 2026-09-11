/*
 * NV2A Vulkan report queue helpers
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_REPORTS_H
#define HW_XBOX_NV2A_PGRAPH_VK_REPORTS_H

#include <stdbool.h>

static inline bool pgraph_vk_report_queue_needs_finish(bool fifo_idle,
                                                        bool reports_pending)
{
    return fifo_idle && reports_pending;
}

#endif
