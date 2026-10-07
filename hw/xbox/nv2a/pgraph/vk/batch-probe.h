/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_VK_BATCH_PROBE_H
#define NV2A_VK_BATCH_PROBE_H

#include <volk.h>

/* The existing synchronous finish owns both queries through completion. */
#define VK_BATCH_PROBE_LIMIT 100000

typedef struct PGRAPHVkBatchProbe {
    FILE *file;
    VkDevice device;
    VkQueryPool pool;
    uint32_t valid_bits;
    double period_ns;
    uint64_t records;
    int64_t record_us;
    int64_t record_cpu_ns, record_tid;
} PGRAPHVkBatchProbe;

typedef struct PGRAPHVkBatchHost {
    int64_t record_us, finish_us;
    int64_t record_cpu_ns, finish_cpu_ns;
    int64_t record_tid, finish_tid;
    int64_t submit_us, submitted_us;
    int64_t wait_us, completed_us;
} PGRAPHVkBatchHost;

static inline bool pgraph_vk_batch_probe_active(const PGRAPHVkBatchProbe *p)
{
    return p->pool != VK_NULL_HANDLE && p->records < VK_BATCH_PROBE_LIMIT;
}

int64_t pgraph_vk_batch_thread_cpu_ns(int64_t *tid);
bool pgraph_vk_batch_interval(uint64_t start, uint64_t end, uint32_t bits,
                             double period_ns, uint64_t host_bound_us,
                             double *span_us);
bool pgraph_vk_batch_probe_init(PGRAPHVkBatchProbe *p, VkDevice device,
                               uint32_t bits, double period_ns, FILE *file);
void pgraph_vk_batch_probe_finalize(PGRAPHVkBatchProbe *p);
void pgraph_vk_batch_probe_aux(PGRAPHVkBatchProbe *p, VkCommandBuffer cmd);
void pgraph_vk_batch_probe_main(PGRAPHVkBatchProbe *p, VkCommandBuffer cmd);
void pgraph_vk_batch_probe_complete(PGRAPHVkBatchProbe *p,
                                   const PGRAPHVkBatchHost *host,
                                   uint64_t serial, unsigned reason,
                                   uint64_t staged_bytes);
#endif
