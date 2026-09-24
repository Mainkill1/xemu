/*
 * Vulkan background worker scheduling helpers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_BACKGROUND_WORKER_PRIORITY_H
#define HW_XBOX_NV2A_PGRAPH_VK_BACKGROUND_WORKER_PRIORITY_H

typedef enum PGRAPHVkWorkerPriorityResult {
    PGRAPH_VK_WORKER_PRIORITY_APPLIED,
    PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED,
    PGRAPH_VK_WORKER_PRIORITY_FAILED,
} PGRAPHVkWorkerPriorityResult;

typedef PGRAPHVkWorkerPriorityResult
(*PGRAPHVkSetWorkerPriorityFunc)(void *opaque);

typedef enum PGRAPHVkWorkerSchedulingMode {
    PGRAPH_VK_WORKER_SCHEDULING_CURRENT,
    PGRAPH_VK_WORKER_SCHEDULING_ALL_LOW,
    PGRAPH_VK_WORKER_SCHEDULING_SPLIT_DEMAND,
} PGRAPHVkWorkerSchedulingMode;

PGRAPHVkWorkerPriorityResult
pgraph_vk_lower_current_worker_priority(void);

PGRAPHVkWorkerPriorityResult
pgraph_vk_lower_current_worker_priority_callback(void *opaque);

PGRAPHVkWorkerSchedulingMode
pgraph_vk_worker_scheduling_mode_from_string(const char *value);

PGRAPHVkWorkerSchedulingMode
pgraph_vk_worker_scheduling_mode_from_environment(void);

const char *pgraph_vk_worker_scheduling_mode_name(
    PGRAPHVkWorkerSchedulingMode mode);

const char *pgraph_vk_worker_priority_result_name(
    PGRAPHVkWorkerPriorityResult result);

#endif
