"""Exercise the production staging-copy function with Vulkan/VMA call doubles."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    source = (root / "hw/xbox/nv2a/pgraph/vk/draw.c").read_text()
    start = source.index("static void sync_staging_buffer(")
    end = source.index("static void flush_memory_buffer(", start)
    production_function = source[start:end]

    harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t VkDeviceSize;
typedef uint32_t VkAccessFlags;
typedef uint32_t VkPipelineStageFlags;
typedef int VkCommandBuffer;
typedef int VkBuffer;
typedef struct { VkDeviceSize size; } VkBufferCopy;
typedef struct {
    int sType;
    VkAccessFlags srcAccessMask, dstAccessMask;
    uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex;
    VkBuffer buffer;
    VkDeviceSize size;
} VkBufferMemoryBarrier;
typedef struct { VkBuffer buffer; int allocation; VkDeviceSize buffer_offset; } StorageBuffer;
typedef struct { int allocator; StorageBuffer storage_buffers[6]; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;

#define BUFFER_INDEX 1
#define BUFFER_VERTEX_INLINE 3
#define BUFFER_UNIFORM 5
#define VK_SUCCESS 0
#define VK_CHECK(call) assert((call) == VK_SUCCESS)
#define VK_ACCESS_INDEX_READ_BIT 1
#define VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT 2
#define VK_ACCESS_UNIFORM_READ_BIT 4
#define VK_ACCESS_TRANSFER_WRITE_BIT 8
#define VK_PIPELINE_STAGE_VERTEX_INPUT_BIT 1
#define VK_PIPELINE_STAGE_VERTEX_SHADER_BIT 2
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT 4
#define VK_PIPELINE_STAGE_TRANSFER_BIT 8
#define VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER 1
#define VK_QUEUE_FAMILY_IGNORED 0xffffffffu

static int events[3], event_count, flush_allocation;
static VkDeviceSize flush_offset, flush_size, copy_size, barrier_size;

static int vmaFlushAllocation(int allocator, int allocation,
                              VkDeviceSize offset, VkDeviceSize size)
{
    assert(allocator == 7);
    events[event_count++] = 1;
    flush_allocation = allocation;
    flush_offset = offset;
    flush_size = size;
    return VK_SUCCESS;
}

static void vkCmdCopyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst,
                            uint32_t count, const VkBufferCopy *copy)
{
    assert(cmd == 8 && src == 10 && dst == 11 && count == 1);
    events[event_count++] = 2;
    copy_size = copy->size;
}

static void vkCmdPipelineBarrier(VkCommandBuffer cmd,
    VkPipelineStageFlags src, VkPipelineStageFlags dst, uint32_t dependencies,
    uint32_t memory_count, const void *memory,
    uint32_t buffer_count, const VkBufferMemoryBarrier *barrier,
    uint32_t image_count, const void *images)
{
    assert(cmd == 8 && src == VK_PIPELINE_STAGE_TRANSFER_BIT);
    assert(dst == VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
    assert(dependencies == 0 && memory_count == 0 && memory == 0);
    assert(buffer_count == 1 && image_count == 0 && images == 0);
    events[event_count++] = 3;
    barrier_size = barrier->size;
}
'''
    harness += production_function
    harness += r'''
int main(void)
{
    PGRAPHVkState r = { .allocator = 7 };
    PGRAPHState pg = { .vk_renderer_state = &r };
    r.storage_buffers[0] = (StorageBuffer){ .buffer = 10, .allocation = 42 };
    r.storage_buffers[BUFFER_INDEX] = (StorageBuffer){ .buffer = 11 };

    sync_staging_buffer(&pg, 8, 0, BUFFER_INDEX);
    assert(event_count == 0);

    r.storage_buffers[0].buffer_offset = 129;
    sync_staging_buffer(&pg, 8, 0, BUFFER_INDEX);
    assert(event_count == 3);
    assert(events[0] == 1 && events[1] == 2 && events[2] == 3);
    assert(flush_allocation == 42 && flush_offset == 0 && flush_size == 129);
    assert(copy_size == 129 && barrier_size == 129);
    assert(r.storage_buffers[0].buffer_offset == 0);
    return 0;
}
'''

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(harness)
        subprocess.run([args.cc, "-std=c11", "-Wall", "-Werror",
                        "-Wno-unused-function", "-o",
                        str(path / "test"), str(path / "test.c")], check=True)
        subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    main()
