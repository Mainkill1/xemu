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
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    if (slot->report_queue->len >= max_pending_reports) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
        slot = pgraph_vk_current_submission_slot(r);
    }
    assert(slot->report_queue->len < max_pending_reports);
}

void pgraph_vk_init_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->zpass_pixel_count_result = 0;
    r->query_results = g_new(uint64_t, PGRAPH_VK_QUERIES_PER_SLOT);

    VkQueryPoolCreateInfo pool_create_info = (VkQueryPoolCreateInfo){
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = PGRAPH_VK_QUERIES_PER_SLOT,
    };
    for (size_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
        PGRAPHVkSubmissionSlot *slot = &r->submission_slots[i];
        slot->report_queue =
            g_array_new(false, false, sizeof(QueryReport));
        VK_CHECK(vkCreateQueryPool(r->device, &pool_create_info, NULL,
                                   &slot->query_pool));
    }
}

void pgraph_vk_finalize_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    g_clear_pointer(&r->query_results, g_free);
    for (size_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
        PGRAPHVkSubmissionSlot *slot = &r->submission_slots[i];
        assert(!slot->state.in_flight);
        assert(!slot->state.query_in_flight);
        g_clear_pointer(&slot->report_queue, g_array_unref);
        vkDestroyQueryPool(r->device, slot->query_pool, NULL);
        slot->query_pool = VK_NULL_HANDLE;
    }
}

void pgraph_vk_clear_report_value(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    ensure_report_queue_space(pg);
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    QueryReport report = {
        .clear = true,
        .query_count = slot->state.num_queries,
    };
    g_array_append_val(slot->report_queue, report);

    pgraph_vk_submission_slot_request_new_query(&slot->state);
}

void pgraph_vk_get_report(NV2AState *d, uint32_t parameter)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    ensure_report_queue_space(pg);
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    uint8_t type = GET_MASK(parameter, NV097_GET_REPORT_TYPE);
    assert(type == NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT);

    QueryReport report = {
        .clear = false,
        .parameter = parameter,
        .query_count = slot->state.num_queries,
    };
    g_array_append_val(slot->report_queue, report);

    pgraph_vk_submission_slot_request_new_query(&slot->state);
}

void pgraph_vk_process_submission_slot_reports(
    PGRAPHState *pg, PGRAPHVkSubmissionSlot *slot)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    uint32_t num_queries = slot->state.num_queries;

    NV2A_VK_DGROUP_BEGIN("Processing queries");

    assert(!slot->state.query_in_flight);

    if (num_queries > PGRAPH_VK_QUERIES_PER_SLOT) {
        error_report("Vulkan query result count exceeds allocated capacity");
        num_queries = PGRAPH_VK_QUERIES_PER_SLOT;
    }

    if (num_queries > 0) {
        size_t size_of_results = num_queries * sizeof(uint64_t);
        /* pgraph_vk_finish() waits for the fence covering these query
         * commands before calling us, so requesting another driver-side wait
         * is redundant. */
        VK_CHECK(vkGetQueryPoolResults(
            r->device, slot->query_pool, 0, num_queries,
            size_of_results, r->query_results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT));
    }

    // Write out queries
    size_t num_results_counted = 0;
    const int result_divisor =
        pg->surface_scale_factor * pg->surface_scale_factor;

    for (size_t i = 0; i < slot->report_queue->len; i++) {
        QueryReport *report =
            &g_array_index(slot->report_queue, QueryReport, i);
        bool write_report;
        uint32_t report_value = 0;
        bool valid = pgraph_vk_process_report_boundary(
            &r->zpass_pixel_count_result, &num_results_counted,
            r->query_results, num_queries, report->query_count,
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
    g_array_set_size(slot->report_queue, 0);

    // Add remaining results
    bool valid = pgraph_vk_accumulate_remaining_query_results(
        &r->zpass_pixel_count_result, &num_results_counted,
        r->query_results, num_queries);
    if (!valid) {
        error_report("Invalid Vulkan query result accumulator state");
    }

    slot->state.num_queries = 0;
    slot->state.new_query_needed = false;
    NV2A_VK_DGROUP_END();
}

void pgraph_vk_process_pending_reports_internal(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    assert(!r->in_command_buffer);
    assert(!slot->state.in_flight);
    pgraph_vk_process_submission_slot_reports(pg, slot);
}

void pgraph_vk_process_pending_reports(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint32_t *dma_get = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_GET];
    uint32_t *dma_put = &d->pfifo.regs[NV_PFIFO_CACHE1_DMA_PUT];
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    if (*dma_get == *dma_put && r->in_command_buffer &&
        slot->report_queue->len) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_STALLED);
    }
}
