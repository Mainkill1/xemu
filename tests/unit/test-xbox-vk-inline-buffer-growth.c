/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/buffer.c"

static unsigned int creates, destroys, maps, unmaps;
static VkDeviceSize allocated_sizes[8];
static uint8_t mapped_byte;

VkResult vmaCreateBuffer(VmaAllocator allocator, const VkBufferCreateInfo *info,
                         const VmaAllocationCreateInfo *allocation_info,
                         VkBuffer *buffer, VmaAllocation *allocation,
                         VmaAllocationInfo *result)
{
    g_assert_cmpuint(creates, <, ARRAY_SIZE(allocated_sizes));
    allocated_sizes[creates++] = info->size;
    *buffer = (VkBuffer)(uintptr_t)(creates + 100);
    *allocation = (VmaAllocation)(uintptr_t)(creates + 100);
    return VK_SUCCESS;
}

void vmaDestroyBuffer(VmaAllocator allocator, VkBuffer buffer,
                      VmaAllocation allocation)
{
    destroys++;
}

VkResult vmaMapMemory(VmaAllocator allocator, VmaAllocation allocation,
                      void **data)
{
    maps++;
    *data = &mapped_byte;
    return VK_SUCCESS;
}

void vmaUnmapMemory(VmaAllocator allocator, VmaAllocation allocation)
{
    unmaps++;
}

static void initialize_pair(PGRAPHVkState *r, int index, size_t size)
{
    int paired = paired_buffer_index(index);
    creates = destroys = maps = unmaps = 0;
    r->storage_buffers[index] = (StorageBuffer){
        .buffer = (VkBuffer)(uintptr_t)1,
        .buffer_size = size,
        .buffer_offset = 123,
        .mapped = &mapped_byte,
    };
    r->storage_buffers[paired] = (StorageBuffer){
        .buffer = (VkBuffer)(uintptr_t)2,
        .buffer_size = size,
        .buffer_offset = 123,
    };
}

static void test_inline_growth(void)
{
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    PGRAPHState pg = { .vk_renderer_state = r };
    int index = BUFFER_VERTEX_INLINE_STAGING;
    initialize_pair(r, index, 8 * MiB);

    pgraph_vk_ensure_buffer_pair_capacity(&pg, index, 8 * MiB);
    g_assert_cmpuint(creates, ==, 0);
    pgraph_vk_ensure_buffer_pair_capacity(&pg, index, 8 * MiB + 42952);
    g_assert_cmpuint(creates, ==, 2);
    g_assert_cmpuint(allocated_sizes[0], ==, 16 * MiB);
    g_assert_cmpuint(allocated_sizes[1], ==, 16 * MiB);
    g_assert_cmpuint(destroys, ==, 2);
    g_assert_cmpuint(maps, ==, 1);
    g_assert_cmpuint(unmaps, ==, 1);
    g_assert_cmpuint(r->storage_buffers[index].buffer_offset, ==, 0);
    g_assert_cmpuint(r->storage_buffers[BUFFER_VERTEX_INLINE].buffer_offset, ==,
                     0);

    /* Gradually increasing demand must not reallocate once per small step. */
    for (size_t required = 9 * MiB; required <= 16 * MiB; required += MiB) {
        pgraph_vk_ensure_buffer_pair_capacity(&pg, index, required);
    }
    g_assert_cmpuint(creates, ==, 2);

    /* Requests beyond the bounded headroom retain the exact-size fallback. */
    pgraph_vk_ensure_buffer_pair_capacity(&pg, index, 16 * MiB + 1);
    g_assert_cmpuint(creates, ==, 4);
    g_assert_cmpuint(allocated_sizes[2], ==, 16 * MiB + 1);
    g_assert_cmpuint(allocated_sizes[3], ==, 16 * MiB + 1);
}

static void test_other_pairs(void)
{
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    PGRAPHState pg = { .vk_renderer_state = r };
    const int indices[] = { BUFFER_INDEX_STAGING, BUFFER_UNIFORM_STAGING };
    for (unsigned int i = 0; i < ARRAY_SIZE(indices); i++) {
        initialize_pair(r, indices[i], 8 * MiB);
        pgraph_vk_ensure_buffer_pair_capacity(&pg, indices[i], 8 * MiB + 1);
        g_assert_cmpuint(creates, ==, 2);
        g_assert_cmpuint(allocated_sizes[0], ==, 8 * MiB + 1);
        g_assert_cmpuint(allocated_sizes[1], ==, 8 * MiB + 1);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/inline-buffer/growth", test_inline_growth);
    g_test_add_func("/xbox/vk/inline-buffer/other-pairs", test_other_pairs);
    return g_test_run();
}
