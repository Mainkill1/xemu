/*
 * NV2A PTIMER host scheduling decisions.
 *
 * Copyright (c) 2026 Mainkill1 contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "ptimer_core.h"

PtimerQueueAction ptimer_queue_action(const PtimerHostSchedule *host,
                                      const PtimerDeadline *desired)
{
    if (!desired->queued) {
        return host->state == PTIMER_HOST_ABSENT ?
               PTIMER_QUEUE_KEEP : PTIMER_QUEUE_CANCEL;
    }

    if (host->state == PTIMER_HOST_QUEUED &&
        host->deadline_ns == desired->deadline_ns) {
        return PTIMER_QUEUE_KEEP;
    }

    return PTIMER_QUEUE_ARM;
}

bool ptimer_schedule_reusable(const PtimerHostSchedule *host,
                              bool needs_callback, int64_t now_ns)
{
    if (host->dirty) {
        return false;
    }

    if (!needs_callback) {
        return host->state == PTIMER_HOST_ABSENT;
    }

    return host->state == PTIMER_HOST_QUEUED &&
           host->deadline_ns > now_ns;
}
