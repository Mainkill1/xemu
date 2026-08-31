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

static void create_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);

    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices.queue_family,
    };
    VK_CHECK(
        vkCreateCommandPool(r->device, &create_info, NULL, &r->command_pool));
}

static void destroy_command_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyCommandPool(r->device, r->command_pool, NULL);
}

static void select_submission_slot(PGRAPHVkState *r, uint32_t slot_index)
{
    assert(slot_index < ARRAY_SIZE(r->submission_slots));
    PGRAPHVkSubmissionSlot *slot = &r->submission_slots[slot_index];
    assert(!slot->state.in_flight);
    assert(!slot->state.submission_serial_valid);

    r->active_submission_slot = slot_index;
    r->command_buffer = slot->command_buffer;
    r->aux_command_buffer = slot->aux_command_buffer;
}

typedef struct PGRAPHVkSubmissionSlotRetireContext {
    PGRAPHState *pgraph;
    PGRAPHVkState *renderer;
    PGRAPHVkSubmissionSlot *slot;
} PGRAPHVkSubmissionSlotRetireContext;

static void wait_for_submission_slot(void *opaque)
{
    PGRAPHVkSubmissionSlotRetireContext *context = opaque;

    VK_CHECK(vkWaitForFences(context->renderer->device, 1,
                             &context->slot->fence, VK_TRUE, UINT64_MAX));
}

static void reset_submission_slot_fence(void *opaque)
{
    PGRAPHVkSubmissionSlotRetireContext *context = opaque;

    VK_CHECK(vkResetFences(context->renderer->device, 1,
                           &context->slot->fence));
}

static void retire_submission_slot_resources(void *opaque)
{
    PGRAPHVkSubmissionSlotRetireContext *context = opaque;

    pgraph_vk_process_submission_slot_reports(context->pgraph,
                                               context->slot);
    bool retired = pgraph_vk_resource_pin_registry_retire(
        &context->slot->resource_pins,
        context->slot->state.submission_serial);
    assert(retired);
}

bool pgraph_vk_retire_submission_slot(PGRAPHState *pg, uint32_t slot_index)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(slot_index < ARRAY_SIZE(r->submission_slots));

    PGRAPHVkSubmissionSlot *slot = &r->submission_slots[slot_index];
    PGRAPHVkSubmissionSlotRetireContext context = {
        .pgraph = pg,
        .renderer = r,
        .slot = slot,
    };
    const PGRAPHVkSubmissionSlotRetireCallbacks callbacks = {
        .wait_for_completion = wait_for_submission_slot,
        .retire_resources = retire_submission_slot_resources,
        .reset_completion = reset_submission_slot_fence,
    };

    return pgraph_vk_submission_slot_retire_with_callbacks(
        &slot->state, &callbacks, &context);
}

void pgraph_vk_acquire_submission_slot(PGRAPHState *pg, uint32_t slot_index)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);
    pgraph_vk_retire_submission_slot(pg, slot_index);
    select_submission_slot(r, slot_index);
}

void pgraph_vk_advance_submission_slot(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);
    uint32_t next = pgraph_vk_next_submission_slot(
        r->active_submission_slot, ARRAY_SIZE(r->submission_slots));
    pgraph_vk_acquire_submission_slot(pg, next);
}

void pgraph_vk_submit_current_submission_slot(
    PGRAPHState *pg, uint32_t submit_info_count,
    const VkSubmitInfo *submit_infos, uint32_t submission_serial)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkSubmissionSlot *slot = pgraph_vk_current_submission_slot(r);

    assert(submit_info_count > 0);
    assert(submit_infos);
    assert(!slot->state.in_flight);
    assert(!slot->state.submission_serial_valid);
    VK_CHECK(vkQueueSubmit(r->queue, submit_info_count, submit_infos,
                           slot->fence));
    pgraph_vk_submission_slot_mark_submitted(&slot->state,
                                              submission_serial);
    bool pins_submitted = pgraph_vk_resource_pin_registry_mark_submitted(
        &slot->resource_pins, submission_serial);
    assert(pins_submitted);
}

void pgraph_vk_drain_submission_slots(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);
    for (uint32_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
        pgraph_vk_retire_submission_slot(pg, i);
    }
}

static void create_submission_slots(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkCommandBuffer command_buffers[2 * PGRAPH_VK_SUBMISSION_SLOT_COUNT];

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = r->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = ARRAY_SIZE(command_buffers),
    };
    VK_CHECK(
        vkAllocateCommandBuffers(r->device, &alloc_info, command_buffers));

    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    for (size_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
        PGRAPHVkSubmissionSlot *slot = &r->submission_slots[i];
        bool initialized = pgraph_vk_resource_pin_registry_init(
            &slot->resource_pins, PGRAPH_VK_RESOURCE_PINS_PER_SLOT);
        assert(initialized);
        slot->command_buffer = command_buffers[2 * i];
        slot->aux_command_buffer = command_buffers[2 * i + 1];
        VK_CHECK(vkCreateSemaphore(r->device, &semaphore_info, NULL,
                                   &slot->aux_complete_semaphore));
        VK_CHECK(vkCreateFence(r->device, &fence_info, NULL, &slot->fence));
    }

    pgraph_vk_acquire_submission_slot(pg, 0);
}

static void destroy_submission_slots(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkCommandBuffer command_buffers[2 * PGRAPH_VK_SUBMISSION_SLOT_COUNT];

    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);

    for (size_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
        PGRAPHVkSubmissionSlot *slot = &r->submission_slots[i];
        assert(!slot->state.in_flight);
        assert(!slot->state.submission_serial_valid);
        bool finalized = pgraph_vk_resource_pin_registry_finalize(
            &slot->resource_pins);
        assert(finalized);
        command_buffers[2 * i] = slot->command_buffer;
        command_buffers[2 * i + 1] = slot->aux_command_buffer;
        vkDestroyFence(r->device, slot->fence, NULL);
        vkDestroySemaphore(r->device, slot->aux_complete_semaphore, NULL);
        slot->fence = VK_NULL_HANDLE;
        slot->aux_complete_semaphore = VK_NULL_HANDLE;
        slot->command_buffer = VK_NULL_HANDLE;
        slot->aux_command_buffer = VK_NULL_HANDLE;
    }

    vkFreeCommandBuffers(r->device, r->command_pool,
                         ARRAY_SIZE(command_buffers), command_buffers);

    r->command_buffer = VK_NULL_HANDLE;
    r->aux_command_buffer = VK_NULL_HANDLE;
}

static void create_aux_command_buffer_fence(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkFenceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };

    VK_CHECK(vkCreateFence(r->device, &create_info, NULL,
                           &r->aux_command_buffer_fence));
}

static void destroy_aux_command_buffer_fence(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyFence(r->device, r->aux_command_buffer_fence, NULL);
    r->aux_command_buffer_fence = VK_NULL_HANDLE;
}

VkCommandBuffer pgraph_vk_begin_single_time_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_aux_command_buffer);
    r->in_aux_command_buffer = true;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vkBeginCommandBuffer(r->aux_command_buffer, &begin_info));

    return r->aux_command_buffer;
}

void pgraph_vk_end_single_time_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->in_aux_command_buffer);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    VK_CHECK(vkResetFences(r->device, 1, &r->aux_command_buffer_fence));
    VK_CHECK(vkQueueSubmit(r->queue, 1, &submit_info,
                           r->aux_command_buffer_fence));
    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_AUX);
    VK_CHECK(vkWaitForFences(r->device, 1, &r->aux_command_buffer_fence,
                             VK_TRUE, UINT64_MAX));

    r->in_aux_command_buffer = false;
}

void pgraph_vk_init_command_buffers(PGRAPHState *pg)
{
    create_command_pool(pg);
    create_submission_slots(pg);
    create_aux_command_buffer_fence(pg);
}

void pgraph_vk_finalize_command_buffers(PGRAPHState *pg)
{
    pgraph_vk_drain_submission_slots(pg);
    destroy_aux_command_buffer_fence(pg);
    destroy_submission_slots(pg);
    destroy_command_pool(pg);
}
