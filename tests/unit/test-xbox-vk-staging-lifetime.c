/*
 * Submission must retain staging bytes until completion permits reuse.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#define vmaFlushAllocation test_staging_flush
#include "hw/xbox/nv2a/pgraph/vk/draw.c"
#undef vmaFlushAllocation

static unsigned int copies;
static unsigned int waits;

void pgraph_vk_perf_record_finish_submit(PGRAPHVkState *r, FinishReason reason,
                                        bool timed, uint64_t submit_us,
                                        uint64_t wait_us, uint64_t staged,
                                        uint64_t infos, uint64_t buffers)
{
}

static VKAPI_ATTR VkResult VKAPI_CALL wait_fences(
    VkDevice device, uint32_t count, const VkFence *fences, VkBool32 all,
    uint64_t timeout)
{
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpuint(all, ==, VK_TRUE);
    g_assert_cmpuint(timeout, ==, UINT64_MAX);
    waits++;
    return VK_SUCCESS;
}

VkResult test_staging_flush(VmaAllocator allocator, VmaAllocation allocation,
                            VkDeviceSize offset, VkDeviceSize size)
{
    g_assert_cmpuint(offset, ==, 0);
    g_assert_cmpuint(size, ==, 128);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL copy_buffer(
    VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, uint32_t count,
    const VkBufferCopy *regions)
{
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpuint(regions[0].size, ==, 128);
    copies++;
}

static VKAPI_ATTR void VKAPI_CALL record_barrier(
    VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
    VkDependencyFlags flags, uint32_t memory_count,
    const VkMemoryBarrier *memory, uint32_t buffer_count,
    const VkBufferMemoryBarrier *buffers, uint32_t image_count,
    const VkImageMemoryBarrier *images)
{
    g_assert_cmpuint(buffer_count, ==, 1);
    g_assert_cmpuint(buffers[0].size, ==, 128);
}

static void test_staging_retained(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 128;
    vkCmdCopyBuffer = copy_buffer;
    vkCmdPipelineBarrier = record_barrier;
    sync_staging_buffer(pg, VK_NULL_HANDLE, BUFFER_UNIFORM_STAGING,
                        BUFFER_UNIFORM);
    g_assert_cmpuint(copies, ==, 1);
    /* A later append must start beyond the bytes owned by this submission. */
    g_assert_cmpuint(r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset,
                     ==, 128);
}

static void test_pending_drain(gconstpointer recording)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    pg->vk_renderer_state = r;
    r->submission_pending = true;
    r->submission_retained = true;
    r->in_command_buffer = recording != NULL;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 640;
    r->descriptor_set_index = 7;
    r->num_queries_in_flight = 3;
    r->framebuffer_index = 2;
    vkWaitForFences = wait_fences;
    waits = 0;
    pgraph_vk_wait_pending_submission(pg);
    g_assert_cmpuint(waits, ==, 1);
    g_assert_false(r->submission_pending);
    g_assert_true(r->submission_retained);
    g_assert_cmpint(r->in_command_buffer, ==, recording != NULL);
    g_assert_cmpuint(r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset,
                     ==, 640);
    g_assert_cmpuint(r->descriptor_set_index, ==, 7);
    g_assert_cmpuint(r->num_queries_in_flight, ==, 3);
    g_assert_cmpuint(r->framebuffer_index, ==, 2);
    pgraph_vk_wait_pending_submission(pg);
    g_assert_cmpuint(waits, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vk/staging/retained-until-completion",
                    test_staging_retained);
    g_test_add_data_func("/vk/submission/drain-preserves-recording",
                         GINT_TO_POINTER(1), test_pending_drain);
    g_test_add_data_func("/vk/submission/drain-without-recording", NULL,
                         test_pending_drain);
    return g_test_run();
}
