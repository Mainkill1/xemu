/*
 * NV2A Vulkan compile-required pipeline probe tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/xbox/nv2a/pgraph/vk/pipeline-probe.h"

typedef struct TestPipelineCreate {
    VkResult result;
    VkPipeline output;
    VkGraphicsPipelineCreateInfo observed_info;
    unsigned int calls;
} TestPipelineCreate;

static void
assert_same_recipe(const VkGraphicsPipelineCreateInfo *actual,
                   const VkGraphicsPipelineCreateInfo *expected)
{
    g_assert_cmpint(actual->sType, ==, expected->sType);
    g_assert_true(actual->pNext == expected->pNext);
    g_assert_cmpuint(actual->flags, ==, expected->flags);
    g_assert_cmpuint(actual->stageCount, ==, expected->stageCount);
    g_assert_true(actual->pStages == expected->pStages);
    g_assert_true(actual->pVertexInputState == expected->pVertexInputState);
    g_assert_true(actual->pInputAssemblyState == expected->pInputAssemblyState);
    g_assert_true(actual->pTessellationState == expected->pTessellationState);
    g_assert_true(actual->pViewportState == expected->pViewportState);
    g_assert_true(actual->pRasterizationState == expected->pRasterizationState);
    g_assert_true(actual->pMultisampleState == expected->pMultisampleState);
    g_assert_true(actual->pDepthStencilState == expected->pDepthStencilState);
    g_assert_true(actual->pColorBlendState == expected->pColorBlendState);
    g_assert_true(actual->pDynamicState == expected->pDynamicState);
    g_assert_true(actual->layout == expected->layout);
    g_assert_true(actual->renderPass == expected->renderPass);
    g_assert_cmpuint(actual->subpass, ==, expected->subpass);
    g_assert_true(actual->basePipelineHandle == expected->basePipelineHandle);
    g_assert_cmpint(actual->basePipelineIndex, ==, expected->basePipelineIndex);
}

static VkResult
test_create_pipeline(void *opaque, VkDevice device, VkPipelineCache cache,
                     const VkGraphicsPipelineCreateInfo *create_info,
                     VkPipeline *pipeline)
{
    TestPipelineCreate *test = opaque;

    (void)device;
    (void)cache;
    test->calls++;
    test->observed_info = *create_info;
    if (test->result == VK_SUCCESS) {
        *pipeline = test->output;
    }
    return test->result;
}

static void test_ready_preserves_recipe_flags(void)
{
    TestPipelineCreate test = {
        .result = VK_SUCCESS,
        .output = (VkPipeline)(uintptr_t)1,
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = (const void *)(uintptr_t)1,
        .flags = VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT,
        .stageCount = 3,
        .pStages = (const VkPipelineShaderStageCreateInfo *)(uintptr_t)2,
        .pVertexInputState =
            (const VkPipelineVertexInputStateCreateInfo *)(uintptr_t)3,
        .pInputAssemblyState =
            (const VkPipelineInputAssemblyStateCreateInfo *)(uintptr_t)4,
        .pTessellationState =
            (const VkPipelineTessellationStateCreateInfo *)(uintptr_t)5,
        .pViewportState =
            (const VkPipelineViewportStateCreateInfo *)(uintptr_t)6,
        .pRasterizationState =
            (const VkPipelineRasterizationStateCreateInfo *)(uintptr_t)7,
        .pMultisampleState =
            (const VkPipelineMultisampleStateCreateInfo *)(uintptr_t)8,
        .pDepthStencilState =
            (const VkPipelineDepthStencilStateCreateInfo *)(uintptr_t)9,
        .pColorBlendState =
            (const VkPipelineColorBlendStateCreateInfo *)(uintptr_t)10,
        .pDynamicState =
            (const VkPipelineDynamicStateCreateInfo *)(uintptr_t)11,
        .layout = (VkPipelineLayout)(uintptr_t)12,
        .renderPass = (VkRenderPass)(uintptr_t)13,
        .subpass = 14,
        .basePipelineHandle = (VkPipeline)(uintptr_t)15,
        .basePipelineIndex = 16,
    };
    VkGraphicsPipelineCreateInfo expected_info = create_info;
    expected_info.flags |=
        VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT;
    VkPipeline pipeline = VK_NULL_HANDLE;

    PGRAPHVkPipelineProbeOutcome outcome =
        pgraph_vk_probe_pipeline_without_compile(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 &create_info, &pipeline,
                                                 test_create_pipeline, &test);

    g_assert_cmpint(outcome.status, ==, PGRAPH_VK_PIPELINE_PROBE_READY);
    g_assert_cmpint(outcome.vk_result, ==, VK_SUCCESS);
    g_assert_true(pipeline == test.output);
    g_assert_cmpuint(test.calls, ==, 1);
    assert_same_recipe(&test.observed_info, &expected_info);
    g_assert_cmpuint(create_info.flags, ==,
                     VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT);
}

static void test_compile_required_is_not_an_error(void)
{
    TestPipelineCreate test = {
        .result = VK_PIPELINE_COMPILE_REQUIRED,
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    };
    VkPipeline pipeline = (VkPipeline)(uintptr_t)1;

    PGRAPHVkPipelineProbeOutcome outcome =
        pgraph_vk_probe_pipeline_without_compile(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 &create_info, &pipeline,
                                                 test_create_pipeline, &test);

    g_assert_cmpint(outcome.status, ==,
                    PGRAPH_VK_PIPELINE_PROBE_COMPILE_REQUIRED);
    g_assert_cmpint(outcome.vk_result, ==, VK_PIPELINE_COMPILE_REQUIRED);
    g_assert_true(pipeline == VK_NULL_HANDLE);
    g_assert_cmpuint(test.calls, ==, 1);
}

static void test_real_vulkan_error_remains_an_error(void)
{
    TestPipelineCreate test = {
        .result = VK_ERROR_OUT_OF_HOST_MEMORY,
    };
    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    };
    VkPipeline pipeline = (VkPipeline)(uintptr_t)1;

    PGRAPHVkPipelineProbeOutcome outcome =
        pgraph_vk_probe_pipeline_without_compile(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 &create_info, &pipeline,
                                                 test_create_pipeline, &test);

    g_assert_cmpint(outcome.status, ==, PGRAPH_VK_PIPELINE_PROBE_ERROR);
    g_assert_cmpint(outcome.vk_result, ==, VK_ERROR_OUT_OF_HOST_MEMORY);
    g_assert_true(pipeline == VK_NULL_HANDLE);
    g_assert_cmpuint(test.calls, ==, 1);
}

static void test_invalid_request_never_calls_driver(void)
{
    TestPipelineCreate test = { 0 };
    VkPipeline pipeline = (VkPipeline)(uintptr_t)1;

    PGRAPHVkPipelineProbeOutcome outcome =
        pgraph_vk_probe_pipeline_without_compile(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 NULL, &pipeline,
                                                 test_create_pipeline, &test);

    g_assert_cmpint(outcome.status, ==, PGRAPH_VK_PIPELINE_PROBE_ERROR);
    g_assert_cmpint(outcome.vk_result, ==, VK_ERROR_INITIALIZATION_FAILED);
    g_assert_true(pipeline == VK_NULL_HANDLE);
    g_assert_cmpuint(test.calls, ==, 0);
}

static void test_core_promotion_requires_instance_and_device_vulkan_1_3(void)
{
    g_assert_true(pgraph_vk_pipeline_cache_control_is_core(VK_API_VERSION_1_3,
                                                           VK_API_VERSION_1_3));
    g_assert_false(pgraph_vk_pipeline_cache_control_is_core(
        VK_API_VERSION_1_2, VK_API_VERSION_1_3));
    g_assert_false(pgraph_vk_pipeline_cache_control_is_core(
        VK_API_VERSION_1_3, VK_API_VERSION_1_2));
    g_assert_true(pgraph_vk_pipeline_cache_control_api_available(
        VK_API_VERSION_1_2, VK_API_VERSION_1_3, true));
    g_assert_false(pgraph_vk_pipeline_cache_control_api_available(
        VK_API_VERSION_1_2, VK_API_VERSION_1_3, false));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/pipeline-probe/ready",
                    test_ready_preserves_recipe_flags);
    g_test_add_func("/xbox/vk/pipeline-probe/compile-required",
                    test_compile_required_is_not_an_error);
    g_test_add_func("/xbox/vk/pipeline-probe/error",
                    test_real_vulkan_error_remains_an_error);
    g_test_add_func("/xbox/vk/pipeline-probe/invalid",
                    test_invalid_request_never_calls_driver);
    g_test_add_func(
        "/xbox/vk/pipeline-probe/core-promotion",
        test_core_promotion_requires_instance_and_device_vulkan_1_3);
    return g_test_run();
}
