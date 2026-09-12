/*
 * NV2A PTIMER host scheduling decisions.
 *
 * Copyright (c) 2026 Mainkill1 contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PTIMER_CORE_H
#define HW_XBOX_NV2A_PTIMER_CORE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum PtimerHostState {
    PTIMER_HOST_UNKNOWN,
    PTIMER_HOST_ABSENT,
    PTIMER_HOST_QUEUED,
} PtimerHostState;

/* Derived queue ownership, not guest-visible alarm state or VMState. */
typedef struct PtimerHostSchedule {
    PtimerHostState state;
    int64_t deadline_ns;
    bool dirty;
} PtimerHostSchedule;

/* Desired QEMU_CLOCK_VIRTUAL deadline. */
typedef struct PtimerDeadline {
    bool queued;
    int64_t deadline_ns;
} PtimerDeadline;

typedef enum PtimerQueueAction {
    PTIMER_QUEUE_KEEP,
    PTIMER_QUEUE_CANCEL,
    PTIMER_QUEUE_ARM,
} PtimerQueueAction;

PtimerQueueAction ptimer_queue_action(const PtimerHostSchedule *host,
                                      const PtimerDeadline *desired);
bool ptimer_schedule_reusable(const PtimerHostSchedule *host,
                              bool needs_callback, int64_t now_ns);

#endif
