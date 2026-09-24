/*
 * Vulkan background worker scheduling helpers
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/xbox/nv2a/pgraph/vk/background-worker-priority.h"

#ifdef __linux__
#include <sys/resource.h>
#endif

#ifdef CONFIG_DARWIN
#include <pthread/qos.h>
#endif

PGRAPHVkWorkerPriorityResult
pgraph_vk_lower_current_worker_priority(void)
{
#ifdef _WIN32
    if (SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL)) {
        return PGRAPH_VK_WORKER_PRIORITY_APPLIED;
    }
    error_report_once("nv2a/vk: could not lower shader worker priority "
                      "(Windows error %lu)", GetLastError());
    return PGRAPH_VK_WORKER_PRIORITY_FAILED;
#elif defined(__linux__)
    errno = 0;
    int priority = getpriority(PRIO_PROCESS, 0);
    if (priority == -1 && errno) {
        error_report_once("nv2a/vk: could not read shader worker priority: %s",
                          strerror(errno));
        return PGRAPH_VK_WORKER_PRIORITY_FAILED;
    }
    priority = MIN(priority + 5, 19);
    if (setpriority(PRIO_PROCESS, 0, priority) == 0) {
        return PGRAPH_VK_WORKER_PRIORITY_APPLIED;
    }
    error_report_once("nv2a/vk: could not lower shader worker priority: %s",
                      strerror(errno));
    return PGRAPH_VK_WORKER_PRIORITY_FAILED;
#elif defined(CONFIG_DARWIN)
    int error = pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
    if (!error) {
        return PGRAPH_VK_WORKER_PRIORITY_APPLIED;
    }
    error_report_once("nv2a/vk: could not lower shader worker QoS: %s",
                      strerror(error));
    return PGRAPH_VK_WORKER_PRIORITY_FAILED;
#else
    error_report_once("nv2a/vk: background shader worker priority is not "
                      "supported on this host");
    return PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED;
#endif
}

PGRAPHVkWorkerPriorityResult
pgraph_vk_lower_current_worker_priority_callback(void *opaque)
{
    (void)opaque;
    return pgraph_vk_lower_current_worker_priority();
}

PGRAPHVkWorkerSchedulingMode
pgraph_vk_worker_scheduling_mode_from_string(const char *value)
{
    if (!value || !value[0] || !g_ascii_strcasecmp(value, "current")) {
        return PGRAPH_VK_WORKER_SCHEDULING_CURRENT;
    }
    if (!g_ascii_strcasecmp(value, "all-low")) {
        return PGRAPH_VK_WORKER_SCHEDULING_ALL_LOW;
    }
    if (!g_ascii_strcasecmp(value, "split-demand")) {
        return PGRAPH_VK_WORKER_SCHEDULING_SPLIT_DEMAND;
    }
    error_report_once("nv2a/vk: ignoring unknown XEMU_VK_WORKER_SCHEDULING "
                      "value '%s'", value);
    return PGRAPH_VK_WORKER_SCHEDULING_CURRENT;
}

PGRAPHVkWorkerSchedulingMode
pgraph_vk_worker_scheduling_mode_from_environment(void)
{
    return pgraph_vk_worker_scheduling_mode_from_string(
        g_getenv("XEMU_VK_WORKER_SCHEDULING"));
}

const char *pgraph_vk_worker_scheduling_mode_name(
    PGRAPHVkWorkerSchedulingMode mode)
{
    switch (mode) {
    case PGRAPH_VK_WORKER_SCHEDULING_ALL_LOW:
        return "all-low";
    case PGRAPH_VK_WORKER_SCHEDULING_SPLIT_DEMAND:
        return "split-demand";
    case PGRAPH_VK_WORKER_SCHEDULING_CURRENT:
    default:
        return "current";
    }
}

const char *pgraph_vk_worker_priority_result_name(
    PGRAPHVkWorkerPriorityResult result)
{
    switch (result) {
    case PGRAPH_VK_WORKER_PRIORITY_APPLIED:
        return "applied-low";
    case PGRAPH_VK_WORKER_PRIORITY_FAILED:
        return "failed";
    case PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED:
    default:
        return "unsupported";
    }
}
