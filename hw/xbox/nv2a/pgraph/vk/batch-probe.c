/*
 * Bounded diagnostic for issue #250. No additional submits or GPU waits.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include <math.h>
#include "batch-probe.h"

bool pgraph_vk_batch_interval(uint64_t start, uint64_t end, uint32_t bits,
                             double period_ns, uint64_t host_bound_us,
                             double *span_us)
{
    if (bits == 0 || bits > 64 || !isfinite(period_ns) || period_ns <= 0) {
        return false;
    }
    /* Reject intervals long enough to conceal one or more full wraps. */
    double wrap_us = ldexp(period_ns, bits) / 1000.0;
    if ((double)host_bound_us >= wrap_us) {
        return false;
    }
    uint64_t mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    *span_us = ((end - start) & mask) * period_ns / 1000.0;
    return true;
}

bool pgraph_vk_batch_probe_init(PGRAPHVkBatchProbe *p, VkDevice device,
                               uint32_t bits, double period_ns, FILE *file)
{
    double unused;
    if (!file || !pgraph_vk_batch_interval(0, 0, bits, period_ns, 0, &unused)) {
        return false;
    }
    VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,
    };
    VkQueryPool pool;
    if (vkCreateQueryPool(device, &info, NULL, &pool) != VK_SUCCESS) {
        return false;
    }
    *p = (PGRAPHVkBatchProbe) {
        .file = file, .device = device, .pool = pool,
        .valid_bits = bits, .period_ns = period_ns,
    };
    fprintf(file, "{\"type\":\"batch_probe_schema\",\"version\":1,"
            "\"timestamp_valid_bits\":%u,\"timestamp_period_ns\":%.9g,"
            "\"limit\":%u,\"host_clock\":\"qemu_realtime_us\","
            "\"scope\":\"aux_staging_to_main_completion\","
            "\"gpu_host_calibrated\":false}\n",
            bits, period_ns, VK_BATCH_PROBE_LIMIT);
    return true;
}

void pgraph_vk_batch_probe_finalize(PGRAPHVkBatchProbe *p)
{
    /* Caller has completed the final existing finish before this point. */
    if (p->pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(p->device, p->pool, NULL);
    }
    p->pool = VK_NULL_HANDLE;
}

void pgraph_vk_batch_probe_aux(PGRAPHVkBatchProbe *p, VkCommandBuffer cmd)
{
    vkCmdResetQueryPool(cmd, p->pool, 0, 2);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, p->pool, 0);
}

void pgraph_vk_batch_probe_main(PGRAPHVkBatchProbe *p, VkCommandBuffer cmd)
{
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, p->pool, 1);
}

void pgraph_vk_batch_probe_complete(PGRAPHVkBatchProbe *p,
                                   const PGRAPHVkBatchHost *h,
                                   uint64_t serial, unsigned reason,
                                   uint64_t staged_bytes)
{
    uint64_t values[4] = { 0 };
    double span_us = 0;
    const char *status = "unavailable";
    /* No WAIT flag or retry: caller already observed the submission fence. */
    VkResult result = vkGetQueryPoolResults(
        p->device, p->pool, 0, 2, sizeof(values), values,
        2 * sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bool ordered = h->record_us <= h->finish_us &&
                   h->finish_us <= h->submit_us &&
                   h->submit_us <= h->submitted_us &&
                   h->submitted_us <= h->wait_us &&
                   h->wait_us <= h->completed_us;
    if (!ordered) {
        status = "host_clock_order";
    } else if (result == VK_SUCCESS && values[1] && values[3]) {
        status = pgraph_vk_batch_interval(
            values[0], values[2], p->valid_bits, p->period_ns,
            h->completed_us - h->submit_us, &span_us) ? "ready" :
                                                     "wrap_ambiguous";
    } else if (result != VK_SUCCESS && result != VK_NOT_READY) {
        status = "query_error";
    }
    fprintf(p->file, "{\"type\":\"batch\",\"serial\":%" PRIu64
            ",\"finish_reason\":%u,\"staged_bytes\":%" PRIu64
            ",\"record_us\":%" PRId64 ",\"finish_us\":%" PRId64
            ",\"submit_us\":%" PRId64 ",\"submitted_us\":%" PRId64
            ",\"wait_us\":%" PRId64 ",\"completed_us\":%" PRId64
            ",\"status\":\"%s\",\"query_result\":%d,\"gpu_span_us\":",
            serial, reason, staged_bytes, h->record_us, h->finish_us,
            h->submit_us, h->submitted_us, h->wait_us, h->completed_us,
            status, result);
    if (!strcmp(status, "ready")) {
        fprintf(p->file, "%.3f", span_us);
    } else {
        fputs("null", p->file);
    }
    fputs("}\n", p->file);
    p->records++;
    if (p->records == VK_BATCH_PROBE_LIMIT) {
        fputs("{\"type\":\"batch_probe_limit\"}\n", p->file);
        fflush(p->file);
    }
}
