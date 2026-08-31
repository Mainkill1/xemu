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
#include "buffer-layout.h"
#include "qemu/error-report.h"

static const size_t BUFFER_STREAM_INITIAL_SIZE = 8 * MiB;

static bool buffer_is_persistently_mapped(int index)
{
    switch (index) {
    case BUFFER_VERTEX_RAM:
    case BUFFER_INDEX_STAGING:
    case BUFFER_VERTEX_INLINE_STAGING:
    case BUFFER_UNIFORM_STAGING:
        return true;
    default:
        return false;
    }
}

static int paired_buffer_index(int index)
{
    switch (index) {
    case BUFFER_STAGING_DST:
        return BUFFER_STAGING_SRC;
    case BUFFER_STAGING_SRC:
        return BUFFER_STAGING_DST;
    case BUFFER_COMPUTE_DST:
        return BUFFER_COMPUTE_SRC;
    case BUFFER_COMPUTE_SRC:
        return BUFFER_COMPUTE_DST;
    case BUFFER_INDEX:
        return BUFFER_INDEX_STAGING;
    case BUFFER_INDEX_STAGING:
        return BUFFER_INDEX;
    case BUFFER_VERTEX_INLINE:
        return BUFFER_VERTEX_INLINE_STAGING;
    case BUFFER_VERTEX_INLINE_STAGING:
        return BUFFER_VERTEX_INLINE;
    case BUFFER_UNIFORM:
        return BUFFER_UNIFORM_STAGING;
    case BUFFER_UNIFORM_STAGING:
        return BUFFER_UNIFORM;
    default:
        return -1;
    }
}

static bool buffer_pair_has_capacity(PGRAPHVkState *r, int index,
                                     VkDeviceSize required_size)
{
    int paired_index = paired_buffer_index(index);
    StorageBuffer *buffer = &r->storage_buffers[index];
    StorageBuffer *paired = &r->storage_buffers[paired_index];

    return buffer->buffer != VK_NULL_HANDLE &&
           paired->buffer != VK_NULL_HANDLE &&
           buffer->buffer_size >= required_size &&
           paired->buffer_size >= required_size;
}

static void create_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer->buffer_size,
        .usage = buffer->usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vmaCreateBuffer(r->allocator, &buffer_create_info,
                             &buffer->alloc_info, &buffer->buffer,
                             &buffer->allocation, NULL));
}

static void destroy_buffer(PGRAPHState *pg, StorageBuffer *buffer)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (buffer->buffer == VK_NULL_HANDLE) {
        assert(buffer->allocation == VK_NULL_HANDLE);
        return;
    }

    vmaDestroyBuffer(r->allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
}

VkDeviceSize pgraph_vk_buffer_get_write_offset(PGRAPHState *pg, int index)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (index == BUFFER_UNIFORM_STAGING) {
        return pgraph_vk_current_submission_slot(r)
            ->state.uniform_staging_offset;
    }
    return r->storage_buffers[index].buffer_offset;
}

void pgraph_vk_buffer_set_write_offset(PGRAPHState *pg, int index,
                                       VkDeviceSize offset)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (index == BUFFER_UNIFORM_STAGING) {
        pgraph_vk_current_submission_slot(r)->state.uniform_staging_offset =
            offset;
        return;
    }
    r->storage_buffers[index].buffer_offset = offset;
}

static void resize_buffer(PGRAPHState *pg, int index, size_t size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[index];

    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);

    if (buffer->mapped) {
        vmaUnmapMemory(r->allocator, buffer->allocation);
        buffer->mapped = NULL;
    }

    destroy_buffer(pg, buffer);
    buffer->buffer_offset = 0;
    if (index == BUFFER_UNIFORM_STAGING) {
        for (size_t i = 0; i < ARRAY_SIZE(r->submission_slots); i++) {
            assert(!r->submission_slots[i].state.in_flight);
            r->submission_slots[i].state.uniform_staging_offset = 0;
        }
    }
    buffer->buffer_size = size;
    create_buffer(pg, buffer);

    if (buffer_is_persistently_mapped(index)) {
        VK_CHECK(vmaMapMemory(r->allocator, buffer->allocation,
                              (void **)&buffer->mapped));
    }
}

static size_t budget_aware_buffer_pair_growth(PGRAPHState *pg, int index,
                                              size_t required_size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int paired_index = paired_buffer_index(index);
    StorageBuffer *buffer = &r->storage_buffers[index];
    StorageBuffer *paired = &r->storage_buffers[paired_index];
    size_t current_size = MAX(buffer->buffer_size, paired->buffer_size);
    size_t new_size = pgraph_vk_buffer_growth_target(
        current_size, BUFFER_STREAM_INITIAL_SIZE, required_size);

    /* A memory budget is advisory. Use it to avoid geometric slack when the
     * pair's additional allocation would exceed every heap's headroom, while
     * still allowing VMA to attempt the exact size required by the guest. */
    if (r->memory_budget_extension_enabled && new_size > required_size) {
        VkPhysicalDeviceMemoryProperties memory_props;
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {0};
        VkDeviceSize max_headroom = 0;

        vkGetPhysicalDeviceMemoryProperties(r->physical_device,
                                            &memory_props);
        vmaGetHeapBudgets(r->allocator, budgets);
        for (uint32_t i = 0; i < memory_props.memoryHeapCount; i++) {
            VkDeviceSize headroom =
                budgets[i].statistics.allocationBytes < budgets[i].budget ?
                    budgets[i].budget -
                        budgets[i].statistics.allocationBytes :
                    0;
            max_headroom = MAX(max_headroom, headroom);
        }

        uint64_t new_pair_size;
        uint64_t current_pair_size;
        bool valid = pgraph_vk_buffer_checked_mul(new_size, 2,
                                                  &new_pair_size) &&
                     pgraph_vk_buffer_checked_add(buffer->buffer_size,
                                                  paired->buffer_size,
                                                  &current_pair_size);
        if (!valid) {
            return required_size;
        }
        VkDeviceSize additional_size =
            new_pair_size > current_pair_size ?
                new_pair_size - current_pair_size : 0;
        if (additional_size > max_headroom) {
            new_size = required_size;
        }
    }

    return new_size;
}

bool pgraph_vk_ensure_buffer_pair_capacity(PGRAPHState *pg, int index,
                                           VkDeviceSize required_size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int paired_index = paired_buffer_index(index);

    assert(required_size);
    assert(paired_index >= 0);
    assert(!r->in_command_buffer);
    assert(!r->in_aux_command_buffer);

    if (required_size > SIZE_MAX) {
        error_report("Vulkan buffer request exceeds host address space");
        return false;
    }

    StorageBuffer *buffer = &r->storage_buffers[index];
    StorageBuffer *paired = &r->storage_buffers[paired_index];
    if (buffer_pair_has_capacity(r, index, required_size)) {
        return true;
    }

    size_t new_size = budget_aware_buffer_pair_growth(
        pg, index, required_size);

    if (buffer->buffer == VK_NULL_HANDLE || buffer->buffer_size < new_size) {
        resize_buffer(pg, index, new_size);
    }
    if (paired->buffer == VK_NULL_HANDLE || paired->buffer_size < new_size) {
        resize_buffer(pg, paired_index, new_size);
    }
    return true;
}

bool pgraph_vk_prepare_buffer_pair(PGRAPHState *pg, int index,
                                   VkDeviceSize required_size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(required_size);
    assert(!r->in_aux_command_buffer);

    if (buffer_pair_has_capacity(r, index, required_size)) {
        return true;
    }

    if (r->in_command_buffer) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    }
    return pgraph_vk_ensure_buffer_pair_capacity(pg, index, required_size);
}

void pgraph_vk_init_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    // FIXME: Profile buffer sizes

    VmaAllocationCreateInfo host_alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                 VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    };
    VmaAllocationCreateInfo device_alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    };

    r->storage_buffers[BUFFER_STAGING_DST] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    r->storage_buffers[BUFFER_STAGING_SRC] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    r->storage_buffers[BUFFER_COMPUTE_DST] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };

    r->storage_buffers[BUFFER_COMPUTE_SRC] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };

    r->storage_buffers[BUFFER_INDEX] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    };

    r->storage_buffers[BUFFER_INDEX_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    // FIXME: Don't assume that we can render with host mapped buffer
    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = memory_region_size(d->vram),
    };

    r->bitmap_size = memory_region_size(d->vram) / 4096;
    r->uploaded_bitmap = bitmap_new(r->bitmap_size);
    bitmap_clear(r->uploaded_bitmap, 0, r->bitmap_size);

    r->storage_buffers[BUFFER_VERTEX_INLINE] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = BUFFER_STREAM_INITIAL_SIZE,
    };

    r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_VERTEX_INLINE].buffer_size,
    };

    r->storage_buffers[BUFFER_UNIFORM] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .buffer_size = 8 * 1024 * 1024,
    };

    r->storage_buffers[BUFFER_UNIFORM_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_UNIFORM].buffer_size,
    };

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (r->storage_buffers[i].buffer_size) {
            create_buffer(pg, &r->storage_buffers[i]);
        }
    }

    // FIXME: Add fallback path for device using host mapped memory

    int buffers_to_map[] = { BUFFER_VERTEX_RAM,
                             BUFFER_INDEX_STAGING,
                             BUFFER_VERTEX_INLINE_STAGING,
                             BUFFER_UNIFORM_STAGING };

    for (int i = 0; i < ARRAY_SIZE(buffers_to_map); i++) {
        if (r->storage_buffers[buffers_to_map[i]].buffer == VK_NULL_HANDLE) {
            continue;
        }
        VK_CHECK(vmaMapMemory(
            r->allocator, r->storage_buffers[buffers_to_map[i]].allocation,
            (void **)&r->storage_buffers[buffers_to_map[i]].mapped));
    }
}

void pgraph_vk_finalize_buffers(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (r->storage_buffers[i].mapped) {
            vmaUnmapMemory(r->allocator, r->storage_buffers[i].allocation);
        }
        destroy_buffer(pg, &r->storage_buffers[i]);
    }

    g_free(r->uploaded_bitmap);
    r->uploaded_bitmap = NULL;
}

bool pgraph_vk_buffer_required_size(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment,
                                    VkDeviceSize *required_size)
{
    return pgraph_vk_buffer_layout_required_size(
        pgraph_vk_buffer_get_write_offset(pg, index), &size, 1, alignment,
        required_size);
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize required_size;
    return pgraph_vk_buffer_required_size(
               pg, index, size, alignment, &required_size) &&
           required_size <= b->buffer_size;
}

bool pgraph_vk_buffer_has_space_for_array(PGRAPHState *pg, int index,
                                          const VkDeviceSize *sizes,
                                          size_t count,
                                          VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkDeviceSize required_size;

    return pgraph_vk_buffer_layout_required_size(
               pgraph_vk_buffer_get_write_offset(pg, index), sizes, count,
               alignment, &required_size) &&
           required_size <= r->storage_buffers[index].buffer_size;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(pgraph_vk_buffer_has_space_for_array(pg, index, sizes, count,
                                                alignment));

    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize buffer_offset = pgraph_vk_buffer_get_write_offset(pg, index);
    VkDeviceSize starting_offset = ROUND_UP(buffer_offset, alignment);

    assert(b->mapped);

    for (int i = 0; i < count; i++) {
        buffer_offset = ROUND_UP(buffer_offset, alignment);
        memcpy(b->mapped + buffer_offset, data[i], sizes[i]);
        buffer_offset += sizes[i];
    }
    pgraph_vk_buffer_set_write_offset(pg, index, buffer_offset);

    return starting_offset;
}
