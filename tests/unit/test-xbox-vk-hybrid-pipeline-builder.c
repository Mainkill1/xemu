/*
 * NV2A Vulkan hybrid graphics-pipeline worker tests
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "hw/xbox/nv2a/pgraph/vk/hybrid-pipeline-builder.h"

typedef struct FakeDriver {
    QemuMutex lock;
    QemuCond entered;
    QemuCond release;
    bool block;
    bool released;
    bool fail;
    unsigned calls;
    unsigned destroys;
    unsigned priority_calls;
    PGRAPHVkWorkerPriorityResult priority_result;
    uint32_t observed_binding;
    uint32_t observed_attribute;
    VkDynamicState observed_dynamic;
    VkColorComponentFlags observed_mask;
    VkShaderModule observed_module;
    float observed_viewport_width;
    int32_t observed_scissor_x;
    VkSampleMask observed_sample_mask;
    char observed_name[32];
} FakeDriver;

static void fake_init(FakeDriver *driver)
{
    *driver = (FakeDriver) {
        .priority_result = PGRAPH_VK_WORKER_PRIORITY_APPLIED,
    };
    qemu_mutex_init(&driver->lock);
    qemu_cond_init(&driver->entered);
    qemu_cond_init(&driver->release);
}

static PGRAPHVkWorkerPriorityResult fake_set_priority(void *opaque)
{
    FakeDriver *driver = opaque;

    qemu_mutex_lock(&driver->lock);
    driver->priority_calls++;
    PGRAPHVkWorkerPriorityResult result = driver->priority_result;
    qemu_cond_broadcast(&driver->entered);
    qemu_mutex_unlock(&driver->lock);
    return result;
}

static void fake_fini(FakeDriver *driver)
{
    qemu_cond_destroy(&driver->release);
    qemu_cond_destroy(&driver->entered);
    qemu_mutex_destroy(&driver->lock);
}

static VkResult fake_create(void *opaque, VkDevice device, VkPipelineCache cache,
                            const VkGraphicsPipelineCreateInfo *info,
                            VkPipeline *pipeline)
{
    FakeDriver *driver = opaque;
    (void)device;
    (void)cache;
    qemu_mutex_lock(&driver->lock);
    driver->calls++;
    qemu_cond_broadcast(&driver->entered);
    while (driver->block && !driver->released) {
        qemu_cond_wait(&driver->release, &driver->lock);
    }
    driver->observed_binding =
        info->pVertexInputState->pVertexBindingDescriptions[0].binding;
    driver->observed_attribute =
        info->pVertexInputState->pVertexAttributeDescriptions[0].location;
    driver->observed_dynamic = info->pDynamicState->pDynamicStates[0];
    driver->observed_mask = info->pColorBlendState->pAttachments[0].colorWriteMask;
    driver->observed_module = info->pStages[0].module;
    driver->observed_viewport_width = info->pViewportState->pViewports[0].width;
    driver->observed_scissor_x = info->pViewportState->pScissors[0].offset.x;
    driver->observed_sample_mask = info->pMultisampleState->pSampleMask[0];
    g_strlcpy(driver->observed_name, info->pStages[0].pName,
              sizeof(driver->observed_name));
    bool fail = driver->fail;
    qemu_mutex_unlock(&driver->lock);
    if (fail) {
        *pipeline = VK_NULL_HANDLE;
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pipeline = (VkPipeline)(uintptr_t)0x1234;
    return VK_SUCCESS;
}

static void fake_destroy(void *opaque, VkDevice device, VkPipeline pipeline)
{
    FakeDriver *driver = opaque;
    (void)device;
    g_assert_cmphex((uintptr_t)pipeline, ==, 0x1234);
    qemu_mutex_lock(&driver->lock);
    driver->destroys++;
    qemu_mutex_unlock(&driver->lock);
}

static void fake_wait_entered(FakeDriver *driver)
{
    qemu_mutex_lock(&driver->lock);
    while (!driver->calls) {
        qemu_cond_wait(&driver->entered, &driver->lock);
    }
    qemu_mutex_unlock(&driver->lock);
}

static void fake_release(FakeDriver *driver)
{
    qemu_mutex_lock(&driver->lock);
    driver->released = true;
    qemu_cond_broadcast(&driver->release);
    qemu_mutex_unlock(&driver->lock);
}

static void fake_wait_destroyed(FakeDriver *driver)
{
    for (unsigned i = 0; i < 5000; i++) {
        qemu_mutex_lock(&driver->lock);
        bool destroyed = driver->destroys != 0;
        qemu_mutex_unlock(&driver->lock);
        if (destroyed) {
            return;
        }
        g_usleep(1000);
    }
    g_error("pipeline result was not destroyed within five seconds");
}

typedef struct TestRecipe {
    VkPipelineShaderStageCreateInfo stage;
    char name[16];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attribute;
    VkPipelineVertexInputStateCreateInfo vertex;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkViewport viewport_value;
    VkRect2D scissor_value;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkSampleMask sample_mask;
    VkPipelineColorBlendAttachmentState attachment;
    VkPipelineColorBlendStateCreateInfo blend;
    VkDynamicState dynamic;
    VkPipelineDynamicStateCreateInfo dynamic_info;
    VkGraphicsPipelineCreateInfo info;
} TestRecipe;

static void recipe_init(TestRecipe *r)
{
    memset(r, 0, sizeof(*r));
    g_strlcpy(r->name, "main", sizeof(r->name));
    r->stage = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = (VkShaderModule)(uintptr_t)1,
        .pName = r->name,
    };
    r->binding = (VkVertexInputBindingDescription) { .binding = 3, .stride = 16 };
    r->attribute = (VkVertexInputAttributeDescription) {
        .location = 7, .binding = 3, .format = VK_FORMAT_R32_SFLOAT,
    };
    r->vertex = (VkPipelineVertexInputStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &r->binding,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &r->attribute,
    };
    r->assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    r->assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    r->viewport_value.width = 640;
    r->scissor_value.offset.x = 12;
    r->viewport = (VkPipelineViewportStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &r->viewport_value,
        .scissorCount = 1, .pScissors = &r->scissor_value,
    };
    r->raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    r->multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    r->multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    r->sample_mask = 0xabbacafe;
    r->multisample.pSampleMask = &r->sample_mask;
    r->attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    r->blend = (VkPipelineColorBlendStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &r->attachment,
    };
    r->dynamic = VK_DYNAMIC_STATE_VIEWPORT;
    r->dynamic_info = (VkPipelineDynamicStateCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1, .pDynamicStates = &r->dynamic,
    };
    r->info = (VkGraphicsPipelineCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 1, .pStages = &r->stage,
        .pVertexInputState = &r->vertex,
        .pInputAssemblyState = &r->assembly,
        .pViewportState = &r->viewport,
        .pRasterizationState = &r->raster,
        .pMultisampleState = &r->multisample,
        .pColorBlendState = &r->blend,
        .pDynamicState = &r->dynamic_info,
        .layout = (VkPipelineLayout)(uintptr_t)2,
        .renderPass = (VkRenderPass)(uintptr_t)3,
    };
}

static PGRAPHVkHybridPipelineBuilderConfig config(FakeDriver *driver)
{
    return (PGRAPHVkHybridPipelineBuilderConfig) {
        .max_jobs = 2, .create = fake_create, .destroy = fake_destroy,
        .opaque = driver,
    };
}

static PGRAPHVkHybridPipelineBuildRequest request(TestRecipe *recipe,
                                                   uint64_t generation,
                                                   uint64_t ticket)
{
    return (PGRAPHVkHybridPipelineBuildRequest) {
        .generation = generation, .ticket = ticket, .key_hash = 0x55,
        .urgency = PGRAPH_VK_HYBRID_PIPELINE_DEMAND,
        .device = (VkDevice)(uintptr_t)4,
        .cache = (VkPipelineCache)(uintptr_t)5,
        .create_info = &recipe->info,
    };
}

static PGRAPHVkHybridPipelineBuildResult take(
    PGRAPHVkHybridPipelineBuilder *builder);

static void destroy_result(PGRAPHVkHybridPipelineBuilder *builder,
                           PGRAPHVkHybridPipelineBuildResult *result)
{
    pgraph_vk_hybrid_pipeline_build_result_destroy(builder, result);
}

static void test_demand_precedes_queued_prewarm(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.max_jobs = 4;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest active = request(&recipe, 1, 40);
    PGRAPHVkHybridPipelineBuildRequest prewarm_a = request(&recipe, 1, 41);
    PGRAPHVkHybridPipelineBuildRequest prewarm_b = request(&recipe, 1, 42);
    PGRAPHVkHybridPipelineBuildRequest demand = request(&recipe, 1, 43);
    prewarm_a.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    prewarm_b.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;

    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &active),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &prewarm_a),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &prewarm_b),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &demand),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_release(&driver);

    const uint64_t expected[] = { 40, 43, 41, 42 };
    for (size_t i = 0; i < G_N_ELEMENTS(expected); i++) {
        PGRAPHVkHybridPipelineBuildResult result = take(&builder);
        g_assert_cmpuint(result.ticket, ==, expected[i]);
        destroy_result(&builder, &result);
    }
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_equal_urgency_remains_fifo(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.max_jobs = 3;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest requests[] = {
        request(&recipe, 1, 50),
        request(&recipe, 1, 51),
        request(&recipe, 1, 52),
    };
    requests[1].urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    requests[2].urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder,
                                                             &requests[0]),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder,
                                                             &requests[1]),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder,
                                                             &requests[2]),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_release(&driver);

    for (size_t i = 0; i < G_N_ELEMENTS(requests); i++) {
        PGRAPHVkHybridPipelineBuildResult result = take(&builder);
        g_assert_cmpuint(result.ticket, ==, 50 + i);
        destroy_result(&builder, &result);
    }
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_queued_prewarm_promotes_in_place(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.max_jobs = 3;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest active = request(&recipe, 7, 60);
    PGRAPHVkHybridPipelineBuildRequest first = request(&recipe, 7, 61);
    PGRAPHVkHybridPipelineBuildRequest promoted = request(&recipe, 7, 62);
    first.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    promoted.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &active),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &first),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &promoted),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    g_usleep(1000);
    PGRAPHVkHybridPipelineUrgency old_urgency =
        PGRAPH_VK_HYBRID_PIPELINE_DEMAND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_promote(
                        &builder, 7, 62,
                        PGRAPH_VK_HYBRID_PIPELINE_DEMAND, &old_urgency),
                    ==, PGRAPH_VK_PIPELINE_PROMOTE_QUEUED_CHANGED);
    g_assert_cmpint(old_urgency, ==,
                    PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_promote(
                        &builder, 7, 62,
                        PGRAPH_VK_HYBRID_PIPELINE_DEMAND, &old_urgency),
                    ==, PGRAPH_VK_PIPELINE_PROMOTE_ALREADY_DEMAND);
    g_assert_cmpint(old_urgency, ==, PGRAPH_VK_HYBRID_PIPELINE_DEMAND);
    PGRAPHVkHybridPipelineQueueTelemetry telemetry;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_get_queue_telemetry(
        &builder, &telemetry));
    g_assert_cmpuint(telemetry.queued_promotions, ==, 1);
    g_assert_cmpuint(telemetry.already_demand_hits, ==, 1);
    g_assert_cmpuint(telemetry.active_background_demand_hits, ==, 0);
    g_assert_cmpuint(telemetry.max_demand_queue_delay_us, ==, 0);
    g_assert_cmpuint(telemetry.oldest_background_age_us, >, 0);
    PGRAPHVkHybridPipelineBuildRequest extra = request(&recipe, 7, 63);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &extra),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_QUEUE_FULL);
    g_usleep(1000);
    fake_release(&driver);

    const uint64_t expected[] = { 60, 62, 61 };
    for (size_t i = 0; i < G_N_ELEMENTS(expected); i++) {
        PGRAPHVkHybridPipelineBuildResult result = take(&builder);
        g_assert_cmpuint(result.ticket, ==, expected[i]);
        if (result.ticket == 62) {
            g_assert_cmpint(result.urgency, ==,
                            PGRAPH_VK_HYBRID_PIPELINE_DEMAND);
        }
        destroy_result(&builder, &result);
    }
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_get_queue_telemetry(
        &builder, &telemetry));
    g_assert_cmpuint(telemetry.max_demand_queue_delay_us, >, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_active_job_is_not_promoted(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest active = request(&recipe, 8, 70);
    active.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &active),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_usleep(1000);
    PGRAPHVkHybridPipelineUrgency old_urgency =
        PGRAPH_VK_HYBRID_PIPELINE_DEMAND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_promote(
                        &builder, 8, 70,
                        PGRAPH_VK_HYBRID_PIPELINE_DEMAND, &old_urgency),
                    ==, PGRAPH_VK_PIPELINE_PROMOTE_ACTIVE_MATCH);
    g_assert_cmpint(old_urgency, ==,
                    PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_promote(
                        &builder, 8, 999,
                        PGRAPH_VK_HYBRID_PIPELINE_DEMAND, &old_urgency),
                    ==, PGRAPH_VK_PIPELINE_PROMOTE_NOT_FOUND);
    PGRAPHVkHybridPipelineQueueTelemetry telemetry;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_get_queue_telemetry(
        &builder, &telemetry));
    g_assert_cmpuint(telemetry.queued_promotions, ==, 0);
    g_assert_cmpuint(telemetry.already_demand_hits, ==, 0);
    g_assert_cmpuint(telemetry.active_background_demand_hits, ==, 1);
    g_assert_cmpuint(telemetry.max_demand_queue_delay_us, ==, 0);
    g_assert_cmpuint(telemetry.oldest_background_age_us, >, 0);
    fake_release(&driver);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpint(result.urgency, ==, PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND);
    destroy_result(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_queue_delay_allows_promotion(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.diagnostic_queue_delay_ms = 1000;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 9, 80);
    req.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineUrgency old_urgency;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_promote(
                        &builder, 9, 80,
                        PGRAPH_VK_HYBRID_PIPELINE_DEMAND, &old_urgency),
                    ==, PGRAPH_VK_PIPELINE_PROMOTE_QUEUED_CHANGED);
    g_assert_cmpint(old_urgency, ==,
                    PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND);

    int64_t promoted_us = g_get_monotonic_time();
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpint(result.urgency, ==, PGRAPH_VK_HYBRID_PIPELINE_DEMAND);
    g_assert_cmpint(result.started_us - promoted_us, <, 500 * 1000);
    destroy_result(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_create_delay_is_measured(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.diagnostic_create_delay_ms = 10;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 10, 81);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpuint(result.finished_us - result.started_us, >=, 9000);
    destroy_result(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_publication_delay_is_measured(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.diagnostic_publish_delay_ms = 10;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 11, 82);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpint(g_get_monotonic_time() - result.finished_us, >=, 9000);
    destroy_result(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_delay_shutdown_is_interruptible(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.diagnostic_queue_delay_ms = 1000;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 12, 83);
    req.urgency = PGRAPH_VK_HYBRID_PIPELINE_BACKGROUND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    int64_t stop_started_us = g_get_monotonic_time();
    pgraph_vk_hybrid_pipeline_builder_stop(&builder);
    pgraph_vk_hybrid_pipeline_builder_join(&builder);
    g_assert_cmpint(g_get_monotonic_time() - stop_started_us, <, 500 * 1000);
    g_assert_cmpuint(driver.calls, ==, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_create_delay_cancellation_is_interruptible(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.diagnostic_create_delay_ms = 1000;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 12, 84);
    req.urgency = PGRAPH_VK_HYBRID_PIPELINE_DEMAND;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineUrgency old_urgency;
    PGRAPHVkPipelinePromoteResult promote_result;
    do {
        promote_result = pgraph_vk_hybrid_pipeline_builder_promote(
            &builder, 12, 84, PGRAPH_VK_HYBRID_PIPELINE_DEMAND,
            &old_urgency);
        g_assert_true(promote_result ==
                          PGRAPH_VK_PIPELINE_PROMOTE_ALREADY_DEMAND ||
                      promote_result == PGRAPH_VK_PIPELINE_PROMOTE_ACTIVE_MATCH);
        g_thread_yield();
    } while (promote_result != PGRAPH_VK_PIPELINE_PROMOTE_ACTIVE_MATCH);

    int64_t cancel_started_us = g_get_monotonic_time();
    pgraph_vk_hybrid_pipeline_builder_cancel_before_generation(&builder, 13);
    pgraph_vk_hybrid_pipeline_builder_stop(&builder);
    pgraph_vk_hybrid_pipeline_builder_join(&builder);
    g_assert_cmpint(g_get_monotonic_time() - cancel_started_us, <, 500 * 1000);
    g_assert_cmpuint(driver.calls, ==, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_diagnostic_delays_are_bounded(void)
{
    FakeDriver driver;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);

    cfg.diagnostic_queue_delay_ms =
        PGRAPH_VK_HYBRID_PIPELINE_MAX_DIAGNOSTIC_DELAY_MS + 1;
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    cfg.diagnostic_queue_delay_ms = 0;
    cfg.diagnostic_create_delay_ms =
        PGRAPH_VK_HYBRID_PIPELINE_MAX_DIAGNOSTIC_DELAY_MS + 1;
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    cfg.diagnostic_create_delay_ms = 0;
    cfg.diagnostic_publish_delay_ms =
        PGRAPH_VK_HYBRID_PIPELINE_MAX_DIAGNOSTIC_DELAY_MS + 1;
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));

    fake_fini(&driver);
}

static PGRAPHVkHybridPipelineBuildResult take(PGRAPHVkHybridPipelineBuilder *builder)
{
    PGRAPHVkHybridPipelineBuildResult result;
    for (unsigned i = 0; i < 5000; i++) {
        if (pgraph_vk_hybrid_pipeline_builder_take_result(builder, &result)) {
            return result;
        }
        g_usleep(1000);
    }
    g_error("pipeline result was not published within five seconds");
}

static void test_deep_owned_recipe(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 11);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    recipe.binding.binding = 99;
    recipe.attribute.location = 99;
    recipe.dynamic = VK_DYNAMIC_STATE_SCISSOR;
    recipe.attachment.colorWriteMask = VK_COLOR_COMPONENT_G_BIT;
    recipe.stage.module = (VkShaderModule)(uintptr_t)99;
    recipe.viewport_value.width = 1;
    recipe.scissor_value.offset.x = 99;
    recipe.sample_mask = 0;
    memset(recipe.name, 'X', sizeof(recipe.name));
    fake_release(&driver);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpint(result.vk_result, ==, VK_SUCCESS);
    g_assert_cmpuint(result.ticket, ==, 11);
    g_assert_cmpuint(driver.observed_binding, ==, 3);
    g_assert_cmpuint(driver.observed_attribute, ==, 7);
    g_assert_cmpint(driver.observed_dynamic, ==, VK_DYNAMIC_STATE_VIEWPORT);
    g_assert_cmpuint(driver.observed_mask, ==, VK_COLOR_COMPONENT_R_BIT);
    g_assert_cmphex((uintptr_t)driver.observed_module, ==, 1);
    g_assert_cmpfloat(driver.observed_viewport_width, ==, 640);
    g_assert_cmpint(driver.observed_scissor_x, ==, 12);
    g_assert_cmphex(driver.observed_sample_mask, ==, 0xabbacafe);
    g_assert_cmpstr(driver.observed_name, ==, "main");
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_cmpuint(driver.destroys, ==, 1);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_result_flag_tracks_empty_and_completed_queue(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_has_result(&builder));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 31);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_has_result(&builder));
    fake_release(&driver);
    bool available = false;
    for (unsigned int i = 0; i < 5000; i++) {
        available = pgraph_vk_hybrid_pipeline_builder_has_result(&builder);
        if (available) {
            break;
        }
        g_usleep(1000);
    }
    g_assert_true(available);
    PGRAPHVkHybridPipelineBuildResult result;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_take_result(
        &builder, &result));
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_has_result(&builder));
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_reject_unsupported_chain(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    recipe.stage.pNext = &recipe.info;
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 12);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_UNSUPPORTED_RECIPE);
    recipe.stage.pNext = NULL;
    VkSpecializationInfo spec = { 0 };
    recipe.stage.pSpecializationInfo = &spec;
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_UNSUPPORTED_RECIPE);
    g_assert_cmpuint(driver.calls, ==, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_stale_result_destroyed(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 13);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    pgraph_vk_hybrid_pipeline_builder_cancel_before_generation(&builder, 2);
    fake_release(&driver);
    fake_wait_destroyed(&driver);
    PGRAPHVkHybridPipelineBuildResult result;
    g_assert_false(pgraph_vk_hybrid_pipeline_builder_take_result(&builder, &result));
    g_assert_cmpuint(driver.destroys, ==, 1);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_driver_failure_is_returned(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.fail = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 15);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_cmpint(result.vk_result, ==, VK_ERROR_INITIALIZATION_FAILED);
    g_assert_true(result.pipeline == VK_NULL_HANDLE);
    g_assert_cmpuint(result.ticket, ==, 15);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_cmpuint(driver.destroys, ==, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_queue_cap_includes_active_job(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.max_jobs = 1;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest first = request(&recipe, 1, 16);
    PGRAPHVkHybridPipelineBuildRequest second = request(&recipe, 1, 17);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &first),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &second),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_QUEUE_FULL);
    fake_release(&driver);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &second),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    result = take(&builder);
    g_assert_cmpuint(result.ticket, ==, 17);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_taken_result_destroyed_after_builder_teardown(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 18);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    g_assert_true(result.pipeline != VK_NULL_HANDLE);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_cmpuint(driver.destroys, ==, 1);
    g_assert_true(result.pipeline == VK_NULL_HANDLE);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_cmpuint(driver.destroys, ==, 1);
    fake_fini(&driver);
}

static void test_shutdown_waits_for_active(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    fake_init(&driver);
    recipe_init(&recipe);
    driver.block = true;
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 14);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    fake_wait_entered(&driver);
    pgraph_vk_hybrid_pipeline_builder_stop(&builder);
    fake_release(&driver);
    pgraph_vk_hybrid_pipeline_builder_join(&builder);
    g_assert_cmpuint(driver.destroys, ==, 1);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

static void test_priority_results_preserve_pipeline_work(void)
{
    const PGRAPHVkWorkerPriorityResult results[] = {
        PGRAPH_VK_WORKER_PRIORITY_APPLIED,
        PGRAPH_VK_WORKER_PRIORITY_UNSUPPORTED,
        PGRAPH_VK_WORKER_PRIORITY_FAILED,
    };

    for (size_t i = 0; i < ARRAY_SIZE(results); i++) {
        FakeDriver driver;
        TestRecipe recipe;
        PGRAPHVkHybridPipelineBuilder builder = { 0 };
        PGRAPHVkHybridPipelineWorkerStatus status;

        fake_init(&driver);
        recipe_init(&recipe);
        driver.priority_result = results[i];
        PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
        cfg.lower_worker_priority = true;
        cfg.set_lower_priority = fake_set_priority;
        cfg.priority_opaque = &driver;
        g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
        PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 21);
        g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                        ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
        PGRAPHVkHybridPipelineBuildResult result = take(&builder);
        g_assert_cmpint(result.vk_result, ==, VK_SUCCESS);
        pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
        g_assert_true(pgraph_vk_hybrid_pipeline_builder_get_worker_status(
            &builder, &status));
        g_assert_true(status.started);
        g_assert_true(status.lower_priority_requested);
        g_assert_cmpint(status.priority_result, ==, results[i]);
        g_assert_cmpuint(driver.priority_calls, ==, 1);
        pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
        fake_fini(&driver);
    }
}

static void test_normal_pipeline_worker_does_not_lower_priority(void)
{
    FakeDriver driver;
    TestRecipe recipe;
    PGRAPHVkHybridPipelineBuilder builder = { 0 };
    PGRAPHVkHybridPipelineWorkerStatus status;

    fake_init(&driver);
    recipe_init(&recipe);
    PGRAPHVkHybridPipelineBuilderConfig cfg = config(&driver);
    cfg.set_lower_priority = fake_set_priority;
    cfg.priority_opaque = &driver;
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_init(&builder, &cfg));
    PGRAPHVkHybridPipelineBuildRequest req = request(&recipe, 1, 22);
    g_assert_cmpint(pgraph_vk_hybrid_pipeline_builder_submit(&builder, &req),
                    ==, PGRAPH_VK_HYBRID_PIPELINE_ACCEPTED);
    PGRAPHVkHybridPipelineBuildResult result = take(&builder);
    pgraph_vk_hybrid_pipeline_build_result_destroy(&builder, &result);
    g_assert_true(pgraph_vk_hybrid_pipeline_builder_get_worker_status(
        &builder, &status));
    g_assert_true(status.started);
    g_assert_false(status.lower_priority_requested);
    g_assert_cmpuint(driver.priority_calls, ==, 0);
    pgraph_vk_hybrid_pipeline_builder_destroy(&builder);
    fake_fini(&driver);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vk/hybrid-pipeline/deep-owned-recipe", test_deep_owned_recipe);
    g_test_add_func("/vk/hybrid-pipeline/result-flag",
                    test_result_flag_tracks_empty_and_completed_queue);
    g_test_add_func("/vk/hybrid-pipeline/reject-unsupported-chain", test_reject_unsupported_chain);
    g_test_add_func("/vk/hybrid-pipeline/stale-result-destroyed", test_stale_result_destroyed);
    g_test_add_func("/vk/hybrid-pipeline/driver-failure-returned", test_driver_failure_is_returned);
    g_test_add_func("/vk/hybrid-pipeline/queue-cap-includes-active", test_queue_cap_includes_active_job);
    g_test_add_func("/vk/hybrid-pipeline/late-result-destroy", test_taken_result_destroyed_after_builder_teardown);
    g_test_add_func("/vk/hybrid-pipeline/shutdown-waits-for-active", test_shutdown_waits_for_active);
    g_test_add_func("/vk/hybrid-pipeline/priority-results-preserve-work",
                    test_priority_results_preserve_pipeline_work);
    g_test_add_func("/vk/hybrid-pipeline/normal-worker-keeps-priority",
                    test_normal_pipeline_worker_does_not_lower_priority);
    g_test_add_func("/vk/hybrid-pipeline/demand-before-prewarm",
                    test_demand_precedes_queued_prewarm);
    g_test_add_func("/vk/hybrid-pipeline/equal-urgency-fifo",
                    test_equal_urgency_remains_fifo);
    g_test_add_func("/vk/hybrid-pipeline/queued-promotion",
                    test_queued_prewarm_promotes_in_place);
    g_test_add_func("/vk/hybrid-pipeline/active-not-promoted",
                    test_active_job_is_not_promoted);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-queue-delay-promotion",
                    test_diagnostic_queue_delay_allows_promotion);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-create-delay",
                    test_diagnostic_create_delay_is_measured);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-publication-delay",
                    test_diagnostic_publication_delay_is_measured);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-delay-shutdown",
                    test_diagnostic_delay_shutdown_is_interruptible);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-delay-cancellation",
                    test_diagnostic_create_delay_cancellation_is_interruptible);
    g_test_add_func("/vk/hybrid-pipeline/diagnostic-delay-bounds",
                    test_diagnostic_delays_are_bounded);
    return g_test_run();
}
