/*
 * Production bridge lifecycle and transactional-recording regressions.
 * Only external SDK, Vulkan, and status publication boundaries are mocked.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#define vmaDestroyImage test_destroy_scratch_image
#include "hw/xbox/nv2a/pgraph/vk/neural-present-vk.c"
#include "hw/xbox/nv2a/pgraph/vk/instance.c"
#undef vmaDestroyImage

static bool sdk_alive, instance_alive, device_alive, module_alive;
static unsigned int shutdown_count, destroy_count, raw_create_count;
static bool reject_output;
static uint32_t display_pixel, scratch_pixel;
static bool check_scratch_cleanup, frame_completed, adapter_references_scratch;
static unsigned int release_count, scratch_view_destroy_count;
static unsigned int scratch_image_destroy_count;

void xemu_dlss_publish_runtime(uint64_t token, const XemuDLSSRuntime *runtime)
{
}
void xemu_dlss_end_runtime(uint64_t token)
{
}
void xemu_neural_plugin_library_close(XemuNeuralPluginLibrary *library)
{
    g_assert_false(instance_alive);
    g_assert_false(device_alive);
    g_assert_cmpuint(destroy_count, ==, 1);
    g_assert_true(module_alive);
    module_alive = false;
    memset(library, 0, sizeof(*library));
}

static void release_runtime(void *context)
{
    g_assert_true(sdk_alive);
    g_assert_true(device_alive);
    if (check_scratch_cleanup) {
        g_assert_true(frame_completed);
        adapter_references_scratch = false;
        release_count++;
    }
}
static VKAPI_ATTR void VKAPI_CALL destroy_scratch_view(
    VkDevice device, VkImageView view, const VkAllocationCallbacks *callbacks)
{
    g_assert_true(frame_completed);
    g_assert_false(adapter_references_scratch);
    g_assert_true(device_alive);
    g_assert_true(module_alive);
    g_assert_false(sdk_alive);
    g_assert_cmpuint(release_count, ==, 2);
    scratch_view_destroy_count++;
}

void test_destroy_scratch_image(VmaAllocator allocator, VkImage image,
                                VmaAllocation allocation)
{
    g_assert_true(frame_completed);
    g_assert_false(adapter_references_scratch);
    g_assert_true(device_alive);
    g_assert_true(module_alive);
    g_assert_cmpuint(scratch_view_destroy_count, ==, 1);
    scratch_image_destroy_count++;
}
static void stop_sdk(void *context)
{
    g_assert_true(sdk_alive);
    g_assert_true(module_alive);
    sdk_alive = false;
    shutdown_count++;
}
static void destroy_bootstrap_context(void *context)
{
    g_assert_false(sdk_alive);
    g_assert_false(instance_alive);
    g_assert_false(device_alive);
    g_assert_true(module_alive);
    destroy_count++;
}
static VKAPI_ATTR void VKAPI_CALL
destroy_device(VkDevice device, const VkAllocationCallbacks *callbacks)
{
    g_assert_false(sdk_alive);
    g_assert_true(module_alive);
    g_assert_true(device_alive);
    device_alive = false;
}
static VKAPI_ATTR void VKAPI_CALL
destroy_instance(VkInstance instance, const VkAllocationCallbacks *callbacks)
{
    g_assert_false(sdk_alive);
    g_assert_false(device_alive);
    g_assert_true(module_alive);
    g_assert_true(instance_alive);
    instance_alive = false;
}
static VKAPI_ATTR VkResult VKAPI_CALL
raw_create_device(VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
                  const VkAllocationCallbacks *callbacks, VkDevice *device)
{
    raw_create_count++;
    return VK_SUCCESS;
}
static int32_t reject_device(void *context, uintptr_t physical,
                             const void *info, const void *callbacks,
                             uintptr_t *device)
{
    *device = 0;
    return VK_ERROR_INITIALIZATION_FAILED;
}

static XemuNeuralPluginResult
write_output(void *context, const XemuNeuralPluginVulkanRecordV1 *record,
             XemuNeuralPluginFrameOutputV1 *output)
{
    g_assert_cmphex(record->frame.color_image, ==, (uintptr_t)&scratch_pixel);
    g_assert_cmpuint(scratch_pixel, ==, 17);
    scratch_pixel = 99;
    output->feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    output->flags = XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN;
    if (reject_output) {
        output->struct_size = 0;
    }
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}
static VKAPI_ATTR void VKAPI_CALL
copy_image(VkCommandBuffer cmd, VkImage source, VkImageLayout source_layout,
           VkImage target, VkImageLayout target_layout, uint32_t count,
           const VkImageCopy *regions)
{
    g_assert_cmpuint(count, ==, 1);
    *(uint32_t *)(uintptr_t)target = *(uint32_t *)(uintptr_t)source;
}
void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer cmd,
                                       VkImage image, VkFormat format,
                                       VkImageLayout before,
                                       VkImageLayout after)
{
    g_assert_true(image == pg->vk_renderer_state->display.image ||
                  image ==
                      pg->vk_renderer_state->neural_present->scratch_image);
}

int main(void)
{
    XemuNeuralPluginApiV1 api = {
        .shutdown = stop_sdk,
        .destroy_bootstrap = destroy_bootstrap_context,
        .release_resources = release_runtime,
        .destroy = release_runtime,
        .create_vulkan_device = reject_device,
        .record_vulkan = write_output,
        .capabilities = XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY,
    };
    for (unsigned int scenario = 0; scenario < 4; scenario++) {
        PGRAPHState *pg = g_new0(PGRAPHState, 1);
        PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
        PGRAPHVkNeuralPresent *neural = g_new0(PGRAPHVkNeuralPresent, 1);
        pg->vk_renderer_state = r;
        r->neural_present = neural;
        r->instance = (VkInstance)(uintptr_t)1;
        instance_alive = sdk_alive = module_alive = true;
        device_alive = scenario == 0;
        r->device = device_alive ? (VkDevice)(uintptr_t)2 : VK_NULL_HANDLE;
        shutdown_count = destroy_count = raw_create_count = 0;
        neural->library.api = &api;
        neural->bootstrapped = neural->bootstrap_owned = true;
        neural->proxy_instance_created = true;
        neural->ready = device_alive;
        neural->plugin_context = device_alive ? neural : NULL;
        vkDestroyInstance = destroy_instance;
        vkDestroyDevice = destroy_device;
        vkCreateDevice = raw_create_device;
        if (scenario == 1) {
            pgraph_vk_neural_present_reject(pg, "requirements rejected");
        }
        if (scenario == 1 || scenario == 2) {
            VkDevice device = VK_NULL_HANDLE;
            g_assert_cmpint(
                pgraph_vk_neural_present_create_device(
                    pg, (VkPhysicalDevice)(uintptr_t)3, NULL, NULL, &device),
                ==, VK_ERROR_INITIALIZATION_FAILED);
            g_assert_null(device);
            g_assert_cmpuint(raw_create_count, ==, 0);
        }
        /* Normal destruction or initialization unwind uses the same owner. */
        pgraph_vk_finalize_instance(pg);
        g_assert_null(r->neural_present);
        g_assert_cmpuint(shutdown_count, ==, 1);
        g_assert_cmpuint(destroy_count, ==, 1);
        g_assert_false(module_alive);
        g_free(r);
        g_free(pg);
    }
    for (unsigned int scenario = 0; scenario < 2; scenario++) {
        PGRAPHState *pg = g_new0(PGRAPHState, 1);
        PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
        PGRAPHVkNeuralPresent neural = { .library.api = &api };
        pg->vk_renderer_state = r;
        r->neural_present = &neural;
        r->display.image = (VkImage)(uintptr_t)&display_pixel;
        r->display.width = r->display.height = 1;
        neural.scratch_image = (VkImage)(uintptr_t)&scratch_pixel;
        display_pixel = 17;
        scratch_pixel = 0;
        reject_output = scenario != 0;
        vkCmdCopyImage = copy_image;
        PGRAPHVkNeuralPresentFrame frame = { .should_record = true };
        frame.plugin_frame.color_image = (uintptr_t)&scratch_pixel;
        pgraph_vk_neural_present_record(pg, VK_NULL_HANDLE, &frame);
        g_assert_cmpuint(display_pixel, ==, reject_output ? 17 : 99);
        g_assert_cmpuint(frame.plugin_result, ==,
                         reject_output ?
                             XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR :
                             XEMU_NEURAL_PLUGIN_RESULT_OK);
        g_free(r);
        g_free(pg);
    }
    for (unsigned int scenario = 0; scenario < 2; scenario++) {
        PGRAPHState *pg = g_new0(PGRAPHState, 1);
        PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
        PGRAPHVkNeuralPresent *neural = g_new0(PGRAPHVkNeuralPresent, 1);
        pg->vk_renderer_state = r;
        r->neural_present = neural;
        r->instance = (VkInstance)(uintptr_t)1;
        r->device = (VkDevice)(uintptr_t)2;
        instance_alive = device_alive = sdk_alive = module_alive = true;
        shutdown_count = destroy_count = release_count = 0;
        scratch_view_destroy_count = scratch_image_destroy_count = 0;
        check_scratch_cleanup = adapter_references_scratch = true;
        frame_completed = false;
        neural->library.api = &api;
        neural->bootstrapped = neural->bootstrap_owned = neural->ready = true;
        neural->plugin_context = neural;
        neural->scratch_image = (VkImage)(uintptr_t)&scratch_pixel;
        neural->scratch_view = (VkImageView)(uintptr_t)4;
        neural->scratch_allocation = (VmaAllocation)(uintptr_t)5;
        neural->scratch_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        vkDestroyImageView = destroy_scratch_view;
        vkDestroyDevice = destroy_device;
        vkDestroyInstance = destroy_instance;
        unsigned int failures =
            scenario == 0 ? 1 : XEMU_NEURAL_MAX_CONSECUTIVE_FAILURES;
        for (unsigned int failure = 0; failure < failures; failure++) {
            PGRAPHVkNeuralPresentFrame frame = {
                .initialized = true,
                .decision.kind = XEMU_NEURAL_DECISION_PROCESS,
                .plugin_result = scenario == 0 ?
                                     XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR :
                                     XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR,
            };
            /*
             * No completed frame means no cleanup. Production calls complete
             * after render_display's synchronous queue completion boundary.
             */
            pgraph_vk_neural_present_complete(pg, &frame);
            g_assert_cmpuint(scratch_image_destroy_count, ==, 0);
            frame.completion_pending = true;
            frame_completed = true;
            pgraph_vk_neural_present_complete(pg, &frame);
            bool disabled = failure + 1 == failures;
            g_assert_cmpint(neural->state.failure_latched, ==, disabled);
            g_assert_cmpuint(scratch_image_destroy_count, ==, disabled);
            g_assert_cmpuint(scratch_view_destroy_count, ==, disabled);
            g_assert_cmpint(neural->ready, ==, !disabled);
            g_assert_true(module_alive);
            g_assert_cmpuint(destroy_count, ==, 0);
            if (disabled) {
                g_assert_null(neural->scratch_image);
                g_assert_null(neural->scratch_view);
                g_assert_null(neural->scratch_allocation);
                g_assert_cmpint(neural->scratch_layout, ==,
                                VK_IMAGE_LAYOUT_UNDEFINED);
                g_assert_cmpuint(release_count, ==, 2);
                g_assert_cmpuint(shutdown_count, ==, 1);
            } else {
                g_assert_true(adapter_references_scratch);
                g_assert_cmpuint(release_count, ==, 0);
            }
            /* A duplicate completion must not release anything twice. */
            pgraph_vk_neural_present_complete(pg, &frame);
            g_assert_cmpuint(scratch_image_destroy_count, ==, disabled);
        }
        pgraph_vk_neural_present_release_display(pg);
        g_assert_cmpuint(scratch_image_destroy_count, ==, 1);
        pgraph_vk_finalize_instance(pg);
        g_assert_null(r->neural_present);
        g_assert_cmpuint(shutdown_count, ==, 1);
        g_assert_cmpuint(destroy_count, ==, 1);
        g_assert_false(module_alive);
        check_scratch_cleanup = false;
        g_free(r);
        g_free(pg);
    }
    puts("4 production lifecycle, 2 recording and 2 disable-cleanup scenarios "
         "passed");
    return 0;
}
