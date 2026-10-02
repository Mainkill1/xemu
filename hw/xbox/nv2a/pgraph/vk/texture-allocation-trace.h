/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_ALLOCATION_TRACE_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_ALLOCATION_TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

typedef struct PGRAPHVkTextureAllocationTrace PGRAPHVkTextureAllocationTrace;

/* Renderer-owned, with no asynchronous reader or hot binding-path counters.
 * A cap or I/O failure makes close return false and the trace incomplete. */
PGRAPHVkTextureAllocationTrace *
pgraph_vk_texture_allocation_trace_open(const char *path, uint64_t max_records);
bool pgraph_vk_texture_allocation_trace_close(
    PGRAPHVkTextureAllocationTrace *trace);
void pgraph_vk_texture_allocation_trace_frame(
    PGRAPHVkTextureAllocationTrace *trace);
void pgraph_vk_texture_allocation_trace_teardown(
    PGRAPHVkTextureAllocationTrace *trace);

VkResult pgraph_vk_texture_image_create(
    PGRAPHVkTextureAllocationTrace *trace, VmaAllocator allocator,
    const VkImageCreateInfo *image_info,
    const VmaAllocationCreateInfo *allocation_info, VkImage *image,
    VmaAllocation *allocation, bool surface_copy, uint32_t submission);
void pgraph_vk_texture_image_destroy(PGRAPHVkTextureAllocationTrace *trace,
                                     VmaAllocator allocator, VkImage image,
                                     VmaAllocation allocation,
                                     uint32_t submission);

#endif
