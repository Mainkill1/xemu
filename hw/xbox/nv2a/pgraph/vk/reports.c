/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "renderer.h"
#include "qemu/error-report.h"
#include "report-accumulator.h"

static const size_t max_pending_reports = 4096;

static void ensure_report_queue_space(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (r->report_queue->len >= max_pending_reports) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
    }
    assert(r->report_queue->len < max_pending_reports);
}

void pgraph_vk_init_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->num_queries_in_flight = 0;
    r->max_queries_in_flight = 1024;
    r->new_query_needed = false;
    r->query_in_flight = false;
    r->zpass_pixel_count_result = 0;
    r->report_queue = g_array_new(false, false, sizeof(QueryReport));
    r->query_results = g_new(uint64_t, r->max_queries_in_flight);

    VkQueryPoolCreateInfo pool_create_info = (VkQueryPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = r->max_queries_in_flight,
    };
    VK_CHECK(
        vkCreateQueryPool(r->device, &pool_create_info, NULL, &r->query_pool));
}

void pgraph_vk_finalize_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_clear_pointer(&r->report_queue, g_array_unref);
    g_clear_pointer(&r->query_results, g_free);

    vkDestroyQueryPool(r->device, r->query_pool, NULL);
}

void pgraph_vk_clear_report_value(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    ensure_report_queue_space(pg);

    QueryReport report = {
        .clear = true,
        .query_count = r->num_queries_in_flight,
    };
    g_array_append_val(r->report_queue, report);

    r->new_query_needed = true;
}

void pgraph_vk_get_report(NV2AState *d, uint32_t parameter)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    ensure_report_queue_space(pg);

    uint8_t type = GET_MASK(parameter, NV097_GET_REPORT_TYPE);
    assert(type == NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT);

    QueryReport report = {
        .clear = false,
        .parameter = parameter,
        .query_count = r->num_queries_in_flight,
    };
    g_array_append_val(r->report_queue, report);

    r->new_query_needed = true;
}

void pgraph_vk_process_pending_reports_internal(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    NV2A_VK_DGROUP_BEGIN("Processing queries");

    assert(!r->in_command_buffer);

    if (r->num_queries_in_flight > r->max_queries_in_flight) {
        error_report("Vulkan query result count exceeds allocated capacity");
        r->num_queries_in_flight = r->max_queries_in_flight;
    }

    if (r->num_queries_in_flight > 0) {
        size_t size_of_results = r->num_queries_in_flight * sizeof(uint64_t);
        /* pgraph_vk_finish() waits for the fence covering these query
         * commands before calling us, so requesting another driver-side wait
         * is redundant. */
        VK_CHECK(vkGetQueryPoolResults(
            r->device, r->query_pool, 0, r->num_queries_in_flight,
            size_of_results, r->query_results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT));
    }

    // Write out queries
    size_t num_results_counted = 0;
    const int result_divisor =
        pg->surface_scale_factor * pg->surface_scale_factor;

    for (size_t i = 0; i < r->report_queue->len; i++) {
        QueryReport *report = &g_array_index(r->report_queue, QueryReport, i);
        bool write_report;
        uint32_t report_value = 0;
        bool valid = pgraph_vk_process_report_boundary(
            &r->zpass_pixel_count_result, &num_results_counted,
            r->query_results, r->num_queries_in_flight, report->query_count,
            report->clear, result_divisor, &write_report, &report_value);
        if (!valid) {
            error_report("Invalid Vulkan query report boundary");
            break;
        }

        if (!write_report) {
            NV2A_VK_DPRINTF("Cleared");
        } else {
            pgraph_write_zpass_pixel_cnt_report(
                d, report->parameter, report_value);
        }
    }
    g_array_set_size(r->report_queue, 0);

    // Add remaining results
    bool valid = pgraph_vk_accumulate_remaining_query_results(
        &r->zpass_pixel_count_result, &num_results_counted,
        r->query_results, r->num_queries_in_flight);
    if (!valid) {
        error_report("Invalid Vulkan query result accumulator state");
    }

    r->num_queries_in_flight = 0;
    NV2A_VK_DGROUP_END();
}

void pgraph_vk_process_pending_reports(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint32_t *dma_get = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET];
    uint32_t *dma_put = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT];

    if (*dma_get == *dma_put && r->in_command_buffer &&
        r->report_queue->len) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
    }
}
