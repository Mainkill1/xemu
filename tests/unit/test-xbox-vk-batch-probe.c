/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include <math.h>
#include "hw/xbox/nv2a/pgraph/vk/batch-probe.h"

static unsigned creates, destroys, resets, writes, reads;
static uint64_t results[4] = { 250, 1, 5, 1 };
static VkResult result_status = VK_SUCCESS;

static VKAPI_ATTR VkResult VKAPI_CALL create_pool(
    VkDevice device, const VkQueryPoolCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkQueryPool *pool)
{
    g_assert_cmpint(info->queryType, ==, VK_QUERY_TYPE_TIMESTAMP);
    g_assert_cmpuint(info->queryCount, ==, 2);
    creates++;
    *pool = (VkQueryPool)(uintptr_t)1;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL destroy_pool(
    VkDevice device, VkQueryPool pool, const VkAllocationCallbacks *allocator)
{
    destroys++;
}

static VKAPI_ATTR void VKAPI_CALL reset_pool(
    VkCommandBuffer cmd, VkQueryPool pool, uint32_t first, uint32_t count)
{
    g_assert_cmpuint(first, ==, 0);
    g_assert_cmpuint(count, ==, 2);
    resets++;
}

static VKAPI_ATTR void VKAPI_CALL timestamp(
    VkCommandBuffer cmd, VkPipelineStageFlagBits stage,
    VkQueryPool pool, uint32_t query)
{
    g_assert_cmpint(stage, ==, query ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT :
                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    g_assert_cmpuint(query, <, 2);
    writes++;
}

static VKAPI_ATTR VkResult VKAPI_CALL read_pool(
    VkDevice device, VkQueryPool pool, uint32_t first, uint32_t count,
    size_t size, void *data, VkDeviceSize stride, VkQueryResultFlags flags)
{
    g_assert_cmpuint(flags, ==, VK_QUERY_RESULT_64_BIT |
                              VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    g_assert_cmpuint(first, ==, 0);
    g_assert_cmpuint(count, ==, 2);
    g_assert_cmpuint(size, ==, sizeof(results));
    g_assert_cmpuint(stride, ==, 2 * sizeof(uint64_t));
    memcpy(data, results, size);
    reads++;
    return result_status;
}

PFN_vkCreateQueryPool vkCreateQueryPool = create_pool;
PFN_vkDestroyQueryPool vkDestroyQueryPool = destroy_pool;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool = reset_pool;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp = timestamp;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults = read_pool;

static void test_interval(void)
{
    double us = -1;
    /* 250 -> 5 wraps an 8-bit counter: 11 ticks at 1000 ns/tick. */
    g_assert_true(pgraph_vk_batch_interval(250, 5, 8, 1000, 20, &us));
    g_assert_cmpfloat(us, ==, 11);
    g_assert_false(pgraph_vk_batch_interval(250, 5, 8, 1000, 256, &us));
    g_assert_false(pgraph_vk_batch_interval(1, 2, 0, 1, 20, &us));
    g_assert_false(pgraph_vk_batch_interval(1, 2, 65, 1, 20, &us));
    g_assert_false(pgraph_vk_batch_interval(1, 2, 64, NAN, 20, &us));
    g_assert_false(pgraph_vk_batch_interval(1, 2, 64, 0, 20, &us));
    g_assert_true(pgraph_vk_batch_interval(UINT64_MAX - 3, 3, 64,
                                         1000, 20, &us));
    g_assert_cmpfloat(us, ==, 7);
}

static void test_probe(void)
{
    PGRAPHVkBatchProbe p = { 0 };
    PGRAPHVkBatchHost host = {
        .record_us = 50, .finish_us = 80, .submit_us = 100,
        .submitted_us = 102, .wait_us = 105, .completed_us = 120,
    };
    FILE *file = tmpfile();
    char output[8192];

    g_assert_nonnull(file);
    g_assert_false(pgraph_vk_batch_probe_init(&p, VK_NULL_HANDLE, 0,
                                            1000, file));
    g_assert_cmpuint(creates, ==, 0);
    g_assert_true(pgraph_vk_batch_probe_init(&p, VK_NULL_HANDLE, 8,
                                           1000, file));
    g_assert_cmpuint(creates, ==, 1);
    pgraph_vk_batch_probe_aux(&p, VK_NULL_HANDLE);
    pgraph_vk_batch_probe_main(&p, VK_NULL_HANDLE);
    pgraph_vk_batch_probe_complete(&p, &host, 7, 8, 4096);
    g_assert_cmpuint(resets, ==, 1);
    g_assert_cmpuint(writes, ==, 2);
    g_assert_cmpuint(reads, ==, 1);

    /* Unavailable results must never masquerade as a zero GPU duration. */
    results[3] = 0;
    result_status = VK_NOT_READY;
    pgraph_vk_batch_probe_aux(&p, VK_NULL_HANDLE);
    pgraph_vk_batch_probe_main(&p, VK_NULL_HANDLE);
    pgraph_vk_batch_probe_complete(&p, &host, 8, 8, 0);
    g_assert_cmpuint(reads, ==, 2);
    rewind(file);
    size_t n = fread(output, 1, sizeof(output) - 1, file);
    output[n] = 0;
    g_assert_nonnull(strstr(output, "\"gpu_span_us\":11.000"));
    g_assert_nonnull(strstr(output, "\"gpu_span_us\":null"));
    g_assert_nonnull(strstr(output, "\"status\":\"unavailable\""));
    pgraph_vk_batch_probe_finalize(&p);
    g_assert_cmpuint(destroys, ==, 1);
    g_assert_false(pgraph_vk_batch_probe_active(&p));
    fclose(file);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vk/batch-probe/wrap-and-ambiguity", test_interval);
    g_test_add_func("/vk/batch-probe/query-contract", test_probe);
    return g_test_run();
}
