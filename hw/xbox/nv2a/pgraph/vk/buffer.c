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

/*
 * Morrowind stages about 4 MiB of vertex RAM updates per guest frame. Keep the
 * default above that measured floor, allow evidence sweeps to select 4, 8, or
 * 16 MiB, and never grow this per-submission storage past 16 MiB.
 */
static const VkDeviceSize BUFFER_VERTEX_RAM_STAGING_DEFAULT_SIZE = 8 * MiB;
static const VkDeviceSize BUFFER_VERTEX_RAM_STAGING_MAX_SIZE =
    PGRAPH_VK_VERTEX_RAM_STAGING_MAX_SIZE;

static VkDeviceSize vertex_ram_staging_initial_size(void)
{
    const char *value = g_getenv("XEMU_VK_VERTEX_STAGING_INITIAL_MIB");
    if (!value || !value[0]) {
        return BUFFER_VERTEX_RAM_STAGING_DEFAULT_SIZE;
    }

    char *end = NULL;
    uint64_t mib = g_ascii_strtoull(value, &end, 10);
    if (end == value || *end != '\0' ||
        (mib != 4 && mib != 8 && mib != 16)) {
        fprintf(stderr,
                "nv2a: XEMU_VK_VERTEX_STAGING_INITIAL_MIB must be 4, 8, "
                "or 16; using 8\n");
        return BUFFER_VERTEX_RAM_STAGING_DEFAULT_SIZE;
    }

    return mib * MiB;
}

static bool buffer_is_persistently_mapped(int index)
{
    switch (index) {
    case BUFFER_VERTEX_RAM:
    case BUFFER_VERTEX_RAM_STAGING:
    case BUFFER_INDEX_STAGING:
    case BUFFER_VERTEX_INLINE_STAGING:
    case BUFFER_UNIFORM_STAGING:
        return true;
    default:
        return false;
    }
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

    vmaDestroyBuffer(r->allocator, buffer->buffer, buffer->allocation);
    buffer->buffer = VK_NULL_HANDLE;
    buffer->allocation = VK_NULL_HANDLE;
}

static bool resize_buffer(PGRAPHState *pg, int index, VkDeviceSize size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[index];

    if (r->in_command_buffer || r->in_aux_command_buffer || !size) {
        return false;
    }

    if (buffer->mapped) {
        vmaUnmapMemory(r->allocator, buffer->allocation);
        buffer->mapped = NULL;
    }

    destroy_buffer(pg, buffer);
    buffer->buffer_offset = 0;
    buffer->buffer_size = size;
    create_buffer(pg, buffer);

    if (buffer_is_persistently_mapped(index)) {
        VK_CHECK(vmaMapMemory(r->allocator, buffer->allocation,
                              (void **)&buffer->mapped));
    }
    return true;
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
        .buffer_size = 4096 * 4096 * 4,
    };

    r->storage_buffers[BUFFER_STAGING_SRC] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_STAGING_DST].buffer_size,
    };

    r->storage_buffers[BUFFER_COMPUTE_DST] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = (1024 * 10) * (1024 * 10) * 8,
    };

    r->storage_buffers[BUFFER_COMPUTE_SRC] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .buffer_size = r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size,
    };

    r->storage_buffers[BUFFER_INDEX] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .buffer_size = sizeof(pg->inline_elements) * 100,
    };

    r->storage_buffers[BUFFER_INDEX_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = r->storage_buffers[BUFFER_INDEX].buffer_size,
    };

    // FIXME: Don't assume that we can render with host mapped buffer
    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = memory_region_size(d->vram),
    };

    r->storage_buffers[BUFFER_VERTEX_RAM_STAGING] = (StorageBuffer){
        .alloc_info = host_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .buffer_size = vertex_ram_staging_initial_size(),
    };

    r->storage_buffers[BUFFER_VERTEX_INLINE] = (StorageBuffer){
        .alloc_info = device_alloc_create_info,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .buffer_size = NV2A_VERTEXSHADER_ATTRIBUTES * NV2A_MAX_BATCH_LENGTH *
                       4 * sizeof(float) * 10,
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
        create_buffer(pg, &r->storage_buffers[i]);
    }

    // FIXME: Add fallback path for device using host mapped memory

    int buffers_to_map[] = { BUFFER_VERTEX_RAM,
                             BUFFER_VERTEX_RAM_STAGING,
                             BUFFER_INDEX_STAGING,
                             BUFFER_VERTEX_INLINE_STAGING,
                             BUFFER_UNIFORM_STAGING };

    for (int i = 0; i < ARRAY_SIZE(buffers_to_map); i++) {
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
}

bool pgraph_vk_grow_vertex_ram_staging_buffer(PGRAPHState *pg,
                                               VkDeviceSize required_size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer =
        &r->storage_buffers[BUFFER_VERTEX_RAM_STAGING];

    if (required_size > BUFFER_VERTEX_RAM_STAGING_MAX_SIZE ||
        buffer->buffer_size >= BUFFER_VERTEX_RAM_STAGING_MAX_SIZE ||
        r->in_command_buffer || r->in_aux_command_buffer) {
        return false;
    }

    VkDeviceSize new_size = MIN(buffer->buffer_size * 2,
                                BUFFER_VERTEX_RAM_STAGING_MAX_SIZE);
    new_size = MAX(new_size, required_size);
    if (new_size > BUFFER_VERTEX_RAM_STAGING_MAX_SIZE) {
        return false;
    }

    return resize_buffer(pg, BUFFER_VERTEX_RAM_STAGING, new_size);
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                    VkDeviceSize size,
                                    VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *b = &r->storage_buffers[index];
    if (!alignment || (alignment & (alignment - 1)) ||
        b->buffer_offset > b->buffer_size ||
        b->buffer_offset > UINT64_MAX - (alignment - 1)) {
        return false;
    }

    VkDeviceSize aligned = ROUND_UP(b->buffer_offset, alignment);
    return aligned <= b->buffer_size && size <= b->buffer_size - aligned;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDeviceSize total_size = 0;
    for (int i = 0; i < count; i++) {
        if (!sizes || !data || (sizes[i] && !data[i]) ||
            sizes[i] > UINT64_MAX - total_size) {
            return VK_WHOLE_SIZE;
        }
        total_size += sizes[i];
    }
    if (!pgraph_vk_buffer_has_space_for(pg, index, total_size, alignment)) {
        return VK_WHOLE_SIZE;
    }

    StorageBuffer *b = &r->storage_buffers[index];
    VkDeviceSize starting_offset = ROUND_UP(b->buffer_offset, alignment);

    if (!b->mapped || !data) {
        return VK_WHOLE_SIZE;
    }

    for (int i = 0; i < count; i++) {
        b->buffer_offset = ROUND_UP(b->buffer_offset, alignment);
        memcpy(b->mapped + b->buffer_offset, data[i], sizes[i]);
        b->buffer_offset += sizes[i];
    }

    return starting_offset;
}
