/*
 * Vulkan bridge for optional host neural-presentation adapters.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "hw/xbox/nv2a/pgraph/neural-present-loader.h"
#include "neural-present-vk.h"
#include "ui/xemu-dlss.h"

typedef struct PGRAPHVkNeuralPresent {
    XemuNeuralPluginLibrary library;
    XemuNeuralPluginHostV1 host;
    XemuNeuralPluginVulkanRequirementsV1 requirements;
    XemuNeuralPluginStatusV1 status;
    XemuNeuralPresentState state;
    void *bootstrap_context;
    void *plugin_context;
    bool bootstrapped;
    bool ready;
    uint32_t configuration_generation;
    XemuDLSSLaunch launch;
    XemuDLSSRuntime runtime;
    uint64_t status_token;
    int64_t last_status_publish_us;
    XemuDLSSRuntimeState last_published_state;
    XemuNeuralPluginFeature last_published_feature;
    uint64_t next_frame_id;
    XemuNeuralPresentFeature last_logged_feature;
    XemuNeuralPresentBypassReason last_logged_bypass;
} PGRAPHVkNeuralPresent;

static void terminate_status(XemuNeuralPluginStatusV1 *status)
{
    status->text[sizeof(status->text) - 1] = '\0';
}

static void terminate_output(XemuNeuralPluginFrameOutputV1 *output)
{
    output->text[sizeof(output->text) - 1] = '\0';
}

/* Called only on the renderer thread. UI readers receive an owned copy.
 * No SDK callback is executed while the publication mutex is held. */
static void publish_status(PGRAPHVkNeuralPresent *neural, bool force)
{
    int64_t now = g_get_monotonic_time();
    if (force || neural->runtime.state != neural->last_published_state ||
        neural->runtime.feature != neural->last_published_feature ||
        now - neural->last_status_publish_us >= 250000) {
        neural->last_status_publish_us = now;
        neural->last_published_state = neural->runtime.state;
        neural->last_published_feature = neural->runtime.feature;
        xemu_dlss_publish_runtime(neural->status_token, &neural->runtime);
    }
}

static void set_runtime_status(PGRAPHVkNeuralPresent *neural,
                               XemuDLSSRuntimeState state, const char *message)
{
    neural->runtime.state = state;
    if (state != XEMU_DLSS_RUNTIME_ACTIVE) {
        neural->runtime.feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
        neural->runtime.gpu_time_ns = 0;
    }
    g_strlcpy(neural->runtime.message, message ? message : "",
              sizeof(neural->runtime.message));
    publish_status(neural, true);
}

static void host_log(void *opaque, XemuNeuralPluginLogLevel level,
                     const char *message)
{
    const char *kind = "info";
    (void)opaque;
    switch (level) {
    case XEMU_NEURAL_PLUGIN_LOG_DEBUG:
        kind = "debug";
        break;
    case XEMU_NEURAL_PLUGIN_LOG_INFO:
        kind = "info";
        break;
    case XEMU_NEURAL_PLUGIN_LOG_WARNING:
        kind = "warning";
        break;
    case XEMU_NEURAL_PLUGIN_LOG_ERROR:
        kind = "error";
        break;
    }
    fprintf(stderr, "nv2a/vk/neural [%s]: %s\n", kind,
            message != NULL ? message : "(no message)");
}

static void report_bypass(PGRAPHVkNeuralPresent *neural)
{
    if (neural->last_logged_bypass == neural->state.last_bypass_reason) {
        return;
    }
    neural->last_logged_bypass = neural->state.last_bypass_reason;
    if (neural->state.last_bypass_reason != XEMU_NEURAL_BYPASS_NONE) {
        fprintf(stderr, "nv2a/vk/neural: bypass: %s\n",
                xemu_neural_present_bypass_reason_string(
                    neural->state.last_bypass_reason));
    }
}

static uint64_t next_output_generation(uint64_t current)
{
    return current == UINT64_MAX ? 1 : current + 1;
}

static XemuNeuralPresentFeature map_feature(XemuNeuralPluginFeature feature)
{
    switch (feature) {
    case XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH:
        return XEMU_NEURAL_FEATURE_PASSTHROUGH;
    case XEMU_NEURAL_PLUGIN_FEATURE_DLAA:
        return XEMU_NEURAL_FEATURE_DLAA;
    case XEMU_NEURAL_PLUGIN_FEATURE_DLSS_SUPER_RESOLUTION:
        return XEMU_NEURAL_FEATURE_DLSS_SUPER_RESOLUTION;
    case XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR:
        return XEMU_NEURAL_FEATURE_DLSS_NR;
    default:
        return XEMU_NEURAL_FEATURE_NONE;
    }
}

static XemuNeuralPresentResult map_result(
    const PGRAPHVkNeuralPresentFrame *frame)
{
    switch (frame->plugin_result) {
    case XEMU_NEURAL_PLUGIN_RESULT_OK:
        return (frame->output.flags &
                XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN)
                   ? XEMU_NEURAL_RESULT_RECORDED
                   : XEMU_NEURAL_RESULT_PASSTHROUGH;
    case XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH:
    case XEMU_NEURAL_PLUGIN_RESULT_BYPASS:
        return XEMU_NEURAL_RESULT_PASSTHROUGH;
    case XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR:
        return XEMU_NEURAL_RESULT_RETRYABLE_FAILURE;
    case XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR:
    default:
        return XEMU_NEURAL_RESULT_FATAL_FAILURE;
    }
}

static void query_queue_flags(PGRAPHVkState *r, uint32_t queue_family_index,
                              uint32_t *flags)
{
    uint32_t count = 0;
    *flags = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count, NULL);
    if (queue_family_index >= count || count == 0) {
        return;
    }
    VkQueueFamilyProperties *properties =
        g_new(VkQueueFamilyProperties, count);
    vkGetPhysicalDeviceQueueFamilyProperties(r->physical_device, &count,
                                             properties);
    *flags = properties[queue_family_index].queueFlags;
    g_free(properties);
}

static void shutdown_adapter(PGRAPHVkNeuralPresent *neural)
{
    if (neural->ready && neural->plugin_context != NULL) {
        neural->library.api->release_resources(neural->plugin_context);
        neural->library.api->destroy(neural->plugin_context);
    }
    neural->plugin_context = NULL;
    neural->ready = false;

    if (neural->bootstrapped && neural->library.api != NULL) {
        neural->library.api->shutdown(neural->bootstrap_context);
    }
    neural->bootstrap_context = NULL;
    neural->bootstrapped = false;
    xemu_neural_plugin_library_close(&neural->library);
}

void pgraph_vk_neural_present_reject(PGRAPHState *pg, const char *reason)
{
    PGRAPHVkNeuralPresent *neural = pg->vk_renderer_state->neural_present;
    if (neural == NULL) {
        return;
    }
    error_report("nv2a/vk/neural: adapter rejected: %s; "
                 "original presentation will be used",
                 reason != NULL ? reason : "unspecified requirement failure");
    set_runtime_status(neural, XEMU_DLSS_RUNTIME_FAILED, reason);
    shutdown_adapter(neural);
}

const XemuNeuralPluginVulkanRequirementsV1 *
pgraph_vk_neural_present_requirements(PGRAPHState *pg)
{
    PGRAPHVkNeuralPresent *neural = pg->vk_renderer_state->neural_present;
    return neural != NULL && neural->bootstrapped ?
               &neural->requirements : NULL;
}

VkResult pgraph_vk_neural_present_create_instance(
    PGRAPHState *pg, const VkInstanceCreateInfo *create_info,
    const VkAllocationCallbacks *allocation_callbacks,
    VkInstance *instance_out)
{
    PGRAPHVkNeuralPresent *neural = pg->vk_renderer_state->neural_present;
    if (neural == NULL || !neural->bootstrapped ||
        neural->library.api == NULL ||
        !(neural->library.api->capabilities &
          XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY)) {
        return vkCreateInstance(create_info, allocation_callbacks,
                                instance_out);
    }
    if (neural->requirements.minimum_api_version != 0 &&
        (create_info == NULL || create_info->pApplicationInfo == NULL ||
         create_info->pApplicationInfo->apiVersion <
             neural->requirements.minimum_api_version)) {
        pgraph_vk_neural_present_reject(
            pg, "requested Vulkan API version is below adapter minimum");
        return vkCreateInstance(create_info, allocation_callbacks,
                                instance_out);
    }

    uintptr_t instance = 0;
    int32_t result = neural->library.api->create_vulkan_instance(
        neural->bootstrap_context, create_info, allocation_callbacks,
        &instance);
    if (result == VK_SUCCESS && instance != 0) {
        *instance_out = (VkInstance)instance;
        return VK_SUCCESS;
    }

    char message[160];
    snprintf(message, sizeof(message),
             "Vulkan instance proxy failed (%d)", result);
    pgraph_vk_neural_present_reject(pg, message);
    *instance_out = VK_NULL_HANDLE;
    return vkCreateInstance(create_info, allocation_callbacks, instance_out);
}

VkResult pgraph_vk_neural_present_create_device(
    PGRAPHState *pg, VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo *create_info,
    const VkAllocationCallbacks *allocation_callbacks,
    VkDevice *device_out)
{
    PGRAPHVkNeuralPresent *neural = pg->vk_renderer_state->neural_present;
    if (neural == NULL || !neural->bootstrapped ||
        neural->library.api == NULL ||
        !(neural->library.api->capabilities &
          XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY)) {
        return vkCreateDevice(physical_device, create_info,
                              allocation_callbacks, device_out);
    }

    uintptr_t device = 0;
    int32_t result = neural->library.api->create_vulkan_device(
        neural->bootstrap_context, (uintptr_t)physical_device,
        create_info, allocation_callbacks, &device);
    if (result == VK_SUCCESS && device != 0) {
        *device_out = (VkDevice)device;
        return VK_SUCCESS;
    }

    char message[160];
    snprintf(message, sizeof(message),
             "Vulkan device proxy failed (%d)", result);
    pgraph_vk_neural_present_reject(pg, message);
    *device_out = VK_NULL_HANDLE;
    return vkCreateDevice(physical_device, create_info,
                          allocation_callbacks, device_out);
}

void pgraph_vk_neural_present_bootstrap(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    XemuDLSSLaunch launch;
    xemu_dlss_get_launch(&launch);
    if (launch.mode == XEMU_DLSS_OFF) {
        return;
    }

    PGRAPHVkNeuralPresent *neural = g_new0(PGRAPHVkNeuralPresent, 1);
    r->neural_present = neural;
    neural->launch = launch;
    neural->configuration_generation = 1;
    neural->status_token = xemu_dlss_begin_runtime();
    if (!launch.valid) {
        set_runtime_status(neural, XEMU_DLSS_RUNTIME_UNAVAILABLE,
                            launch.reason);
        return;
    }
    if (!xemu_neural_plugin_library_open(&neural->library,
                                         launch.adapter_path)) {
        error_report("nv2a/vk/neural: %s", neural->library.error);
        set_runtime_status(neural, XEMU_DLSS_RUNTIME_MISSING_ADAPTER,
                            neural->library.error);
        return;
    }
    g_strlcpy(neural->runtime.adapter_name, neural->library.api->name,
              sizeof(neural->runtime.adapter_name));
    g_strlcpy(neural->runtime.adapter_version, neural->library.api->version,
              sizeof(neural->runtime.adapter_version));
    if (!xemu_dlss_adapter_allowed(launch.mode,
                                   neural->library.api->capabilities)) {
        set_runtime_status(neural, XEMU_DLSS_RUNTIME_UNAVAILABLE,
            "Adapter Validation requires a validation-only adapter. "
            "Select the rebuilt pass-through or Vulkan-copy DLL.");
        xemu_neural_plugin_library_close(&neural->library);
        return;
    }
    set_runtime_status(neural, XEMU_DLSS_RUNTIME_LOADING,
                        "Adapter loaded; initializing before Vulkan creation.");

    neural->host = (XemuNeuralPluginHostV1)
        XEMU_NEURAL_PLUGIN_HOST_V1_INIT;
    neural->host.opaque = neural;
    neural->host.log = host_log;
    neural->requirements = (XemuNeuralPluginVulkanRequirementsV1)
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    neural->status = (XemuNeuralPluginStatusV1)
        XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;

    XemuNeuralPluginResult result = neural->library.api->bootstrap(
        &neural->host, &neural->bootstrap_context,
        &neural->requirements, &neural->status);
    terminate_status(&neural->status);
    XemuNeuralPluginRequirementsValidation validation =
        xemu_neural_plugin_requirements_validate(&neural->requirements);
    if (result != XEMU_NEURAL_PLUGIN_RESULT_OK ||
        validation != XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID) {
        char message[256];
        if (result != XEMU_NEURAL_PLUGIN_RESULT_OK) {
            snprintf(message, sizeof(message),
                     "bootstrap failed: %s",
                     neural->status.text[0] ? neural->status.text :
                                              "no status supplied");
        } else {
            snprintf(message, sizeof(message),
                     "invalid Vulkan requirements: %s",
                     xemu_neural_plugin_requirements_validation_string(
                         validation));
        }
        /* bootstrap may have created global SDK state even on failure. */
        neural->bootstrapped = true;
        pgraph_vk_neural_present_reject(pg, message);
        return;
    }

    const bool needs_create_proxy =
        neural->requirements.instance_extension_count != 0 ||
        neural->requirements.device_extension_count != 0 ||
        neural->requirements.feature12_count != 0 ||
        neural->requirements.feature13_count != 0 ||
        neural->requirements.additional_graphics_queue_count != 0 ||
        neural->requirements.additional_compute_queue_count != 0 ||
        neural->requirements.additional_optical_flow_queue_count != 0;
    const bool has_create_proxy =
        neural->library.api->capabilities &
        XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY;
    if (needs_create_proxy && !has_create_proxy) {
        neural->bootstrapped = true;
        pgraph_vk_neural_present_reject(
            pg, "adapter declares pre-device Vulkan requirements without "
                "a Vulkan create proxy");
        return;
    }

    neural->bootstrapped = true;
    fprintf(stderr,
            "nv2a/vk/neural: bootstrapped %s %s before Vulkan creation\n",
            neural->library.api->name, neural->library.api->version);
}

void pgraph_vk_neural_present_init(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkNeuralPresent *neural = r->neural_present;
    if (neural == NULL) {
        return;
    }
    g_strlcpy(neural->runtime.gpu_name, r->selected_device.name,
              sizeof(neural->runtime.gpu_name));
    publish_status(neural, true);
    if (!neural->bootstrapped || neural->library.api == NULL) {
        return;
    }
    if (neural->requirements.minimum_api_version != 0 &&
        r->vk_api_version < neural->requirements.minimum_api_version) {
        pgraph_vk_neural_present_reject(
            pg, "selected Vulkan API version is below adapter minimum");
        return;
    }

    XemuNeuralPluginCreateInfoV1 create_info =
        XEMU_NEURAL_PLUGIN_CREATE_INFO_V1_INIT;
    create_info.host = &neural->host;
    create_info.bootstrap_context = neural->bootstrap_context;
    create_info.vulkan.api_version = r->vk_api_version;
    create_info.vulkan.instance = (uintptr_t)r->instance;
    create_info.vulkan.physical_device = (uintptr_t)r->physical_device;
    create_info.vulkan.device = (uintptr_t)r->device;
    create_info.vulkan.queue = (uintptr_t)r->queue;
    QueueFamilyIndices indices =
        pgraph_vk_find_queue_families(r->physical_device);
    if (indices.queue_family < 0) {
        pgraph_vk_neural_present_reject(
            pg, "no compatible Vulkan queue family");
        return;
    }
    create_info.vulkan.queue_family_index = indices.queue_family;
    query_queue_flags(r, indices.queue_family,
                      &create_info.vulkan.queue_flags);
    create_info.vulkan.vendor_id = r->selected_device.vendor_id;
    create_info.vulkan.device_id = r->selected_device.device_id;
    create_info.vulkan.driver_version = r->selected_device.driver_version;
    memcpy(create_info.vulkan.device_uuid,
           r->selected_device.device_uuid,
           sizeof(create_info.vulkan.device_uuid));
    memcpy(create_info.vulkan.driver_uuid,
           r->selected_device.driver_uuid,
           sizeof(create_info.vulkan.driver_uuid));

    neural->status = (XemuNeuralPluginStatusV1)
        XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;
    XemuNeuralPluginResult result = neural->library.api->create(
        &create_info, &neural->plugin_context, &neural->status);
    terminate_status(&neural->status);
    if (result != XEMU_NEURAL_PLUGIN_RESULT_OK ||
        neural->plugin_context == NULL) {
        char message[320];
        snprintf(message, sizeof(message),
                 "adapter '%s' failed to initialize: %s",
                 neural->library.api->name,
                 neural->status.text[0] ? neural->status.text :
                                          "no status supplied");
        if (neural->plugin_context != NULL) {
            neural->library.api->destroy(neural->plugin_context);
            neural->plugin_context = NULL;
        }
        pgraph_vk_neural_present_reject(pg, message);
        return;
    }

    neural->ready = true;
    set_runtime_status(neural, XEMU_DLSS_RUNTIME_READY,
        neural->status.text[0] ? neural->status.text :
                               "Ready; no frame has been produced yet.");
    fprintf(stderr, "nv2a/vk/neural: loaded %s %s (experimental)\n",
            neural->library.api->name, neural->library.api->version);
}

void pgraph_vk_neural_present_finalize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkNeuralPresent *neural = r->neural_present;
    if (neural == NULL) {
        return;
    }
    shutdown_adapter(neural);
    xemu_dlss_end_runtime(neural->status_token);
    g_free(neural);
    r->neural_present = NULL;
}

void pgraph_vk_neural_present_release_display(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkNeuralPresent *neural = r->neural_present;
    if (neural == NULL) {
        return;
    }
    if (neural->ready && neural->plugin_context != NULL) {
        neural->library.api->release_resources(neural->plugin_context);
    }
    xemu_neural_present_reset(&neural->state,
                              XEMU_NEURAL_RESET_SOURCE_CHANGED);
    if (neural->ready) {
        set_runtime_status(neural, XEMU_DLSS_RUNTIME_READY,
                           "Display resources changed; waiting for a new frame.");
    }
    r->display.reuse.valid = false;
}

void pgraph_vk_neural_present_reset(PGRAPHState *pg,
                                    XemuNeuralPresentResetReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkNeuralPresent *neural = r->neural_present;
    if (neural == NULL) {
        return;
    }
    if (neural->ready && neural->plugin_context != NULL) {
        neural->library.api->reset_history(neural->plugin_context, reason);
    }
    xemu_neural_present_reset(&neural->state, reason);
    if (neural->ready) {
        set_runtime_status(neural, XEMU_DLSS_RUNTIME_READY,
                           "Temporal history reset; waiting for a new frame.");
    }
    r->display.reuse.valid = false;
}

VkImageUsageFlags pgraph_vk_neural_present_required_image_usage(
    PGRAPHState *pg)
{
    PGRAPHVkNeuralPresent *neural =
        pg->vk_renderer_state->neural_present;
    return neural != NULL && neural->ready
               ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT
               : 0;
}

void pgraph_vk_neural_present_prepare(
    PGRAPHState *pg, SurfaceBinding *surface, hwaddr scanout_address,
    bool pvideo_enabled, bool interlaced,
    PGRAPHVkNeuralPresentFrame *frame)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkNeuralPresent *neural = r->neural_present;
    memset(frame, 0, sizeof(*frame));
    if (neural == NULL) {
        return;
    }

    uint32_t processing_width, processing_height;
    xemu_dlss_processing_extent(&neural->launch, r->display.width,
                                 r->display.height, &processing_width,
                                 &processing_height);
    neural->runtime.processing_width = processing_width;
    neural->runtime.processing_height = processing_height;
    frame->initialized = true;
    frame->state_frame = (XemuNeuralPresentFrameInfo){
        .enabled = true,
        .backend_available = neural->ready,
        .color_valid = r->display.image != VK_NULL_HANDLE,
        .pvideo_enabled = pvideo_enabled,
        .interlaced = interlaced,
        .backend_requires_depth =
            neural->ready &&
            (neural->library.api->capabilities &
             XEMU_NEURAL_PLUGIN_CAP_REQUIRES_DEPTH),
        .depth_valid = false,
        .key = {
            .surface_lifetime_id = surface->lifetime_id,
            .output_generation = next_output_generation(
                r->display.completed_output_generation),
            .surface_draw_time = surface->draw_time,
            .guest_frame_time = pg->frame_time,
            .scanout_address = scanout_address,
            .display_width = r->display.width,
            .display_height = r->display.height,
            .processing_width = processing_width,
            .processing_height = processing_height,
            .surface_scale_factor = pg->surface_scale_factor,
            .configuration_generation = neural->configuration_generation,
        },
    };
    xemu_neural_present_begin_frame(&neural->state,
                                    &frame->state_frame,
                                    &frame->decision);
    if (frame->decision.kind != XEMU_NEURAL_DECISION_PROCESS &&
        frame->decision.kind != XEMU_NEURAL_DECISION_PROCESS_RESET) {
        report_bypass(neural);
        neural->runtime.bypassed_frames++;
        /* Retain the actionable startup/failure message instead of replacing
         * it every frame with the less useful "backend unavailable". */
        if (neural->ready) {
            neural->runtime.state = XEMU_DLSS_RUNTIME_BYPASSED;
            neural->runtime.feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
            neural->runtime.gpu_time_ns = 0;
            g_strlcpy(neural->runtime.message,
                xemu_neural_present_bypass_reason_string(frame->decision.reason),
                sizeof(neural->runtime.message));
        }
        publish_status(neural, false);
        return;
    }

    frame->plugin_frame = (XemuNeuralPluginFrameV1)
        XEMU_NEURAL_PLUGIN_FRAME_V1_INIT;
    frame->plugin_frame.frame_id = ++neural->next_frame_id;
    frame->plugin_frame.reset_epoch = frame->decision.reset_epoch;
    frame->plugin_frame.surface_lifetime_id = surface->lifetime_id;
    frame->plugin_frame.output_generation =
        frame->state_frame.key.output_generation;
    frame->plugin_frame.guest_frame_time = pg->frame_time;
    frame->plugin_frame.scanout_address = scanout_address;
    frame->plugin_frame.display_width = r->display.width;
    frame->plugin_frame.display_height = r->display.height;
    frame->plugin_frame.processing_width =
        frame->state_frame.key.processing_width;
    frame->plugin_frame.processing_height =
        frame->state_frame.key.processing_height;
    frame->plugin_frame.active_width = r->display.width;
    frame->plugin_frame.active_height = r->display.height;
    frame->plugin_frame.color_image =
        (uint64_t)(uintptr_t)r->display.image;
    frame->plugin_frame.color_image_view =
        (uint64_t)(uintptr_t)r->display.image_view;
    frame->plugin_frame.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    frame->plugin_frame.color_layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    frame->plugin_frame.required_return_layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    frame->plugin_frame.color_sample_count = VK_SAMPLE_COUNT_1_BIT;
    if (frame->decision.kind == XEMU_NEURAL_DECISION_PROCESS_RESET) {
        frame->plugin_frame.flags |=
            XEMU_NEURAL_PLUGIN_FRAME_RESET_HISTORY;
        neural->library.api->reset_history(
            neural->plugin_context, frame->decision.reset_reason);
    }

    neural->status = (XemuNeuralPluginStatusV1)
        XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;
    frame->plugin_result = neural->library.api->prepare_frame(
        neural->plugin_context, &frame->plugin_frame, &neural->status);
    terminate_status(&neural->status);
    frame->completion_pending = true;
    frame->should_record =
        frame->plugin_result == XEMU_NEURAL_PLUGIN_RESULT_OK;
    if (frame->plugin_result == XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH ||
        frame->plugin_result == XEMU_NEURAL_PLUGIN_RESULT_BYPASS) {
        frame->output = (XemuNeuralPluginFrameOutputV1)
            XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
        frame->output.feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    }
}

void pgraph_vk_neural_present_record(
    PGRAPHState *pg, VkCommandBuffer command_buffer,
    PGRAPHVkNeuralPresentFrame *frame)
{
    PGRAPHVkNeuralPresent *neural =
        pg->vk_renderer_state->neural_present;
    if (neural == NULL || !frame->should_record) {
        return;
    }

    XemuNeuralPluginVulkanRecordV1 record =
        XEMU_NEURAL_PLUGIN_VULKAN_RECORD_V1_INIT;
    record.command_buffer = (uintptr_t)command_buffer;
    record.frame = frame->plugin_frame;
    frame->output = (XemuNeuralPluginFrameOutputV1)
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    frame->plugin_result = neural->library.api->record_vulkan(
        neural->plugin_context, &record, &frame->output);
    terminate_output(&frame->output);
    frame->should_record = false;

    XemuNeuralPluginFrameOutputValidation validation =
        xemu_neural_plugin_frame_output_validate(frame->plugin_result,
                                                 &frame->output);
    if (neural->launch.mode == XEMU_DLSS_VALIDATION &&
        frame->output.feature > XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH) {
        validation = XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT;
    }
    if (validation != XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID) {
        frame->plugin_result = XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR;
        snprintf(frame->output.text, sizeof(frame->output.text),
                 "invalid adapter output: %s",
                 xemu_neural_plugin_frame_output_validation_string(
                     validation));
    }
}

void pgraph_vk_neural_present_complete(
    PGRAPHState *pg, PGRAPHVkNeuralPresentFrame *frame)
{
    PGRAPHVkNeuralPresent *neural =
        pg->vk_renderer_state->neural_present;
    if (neural == NULL || !frame->initialized ||
        !frame->completion_pending) {
        return;
    }
    frame->completion_pending = false;

    XemuNeuralPresentFeature feature = map_feature(frame->output.feature);
    XemuNeuralPresentResult result = map_result(frame);
    bool history_valid = frame->output.flags &
                         XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID;
    xemu_neural_present_complete_frame(
        &neural->state, &frame->state_frame, &frame->decision,
        result, feature, history_valid);

    const bool failed = result == XEMU_NEURAL_RESULT_RETRYABLE_FAILURE ||
                        result == XEMU_NEURAL_RESULT_FATAL_FAILURE;
    const char *message = frame->output.text[0] ? frame->output.text :
                           neural->status.text;
    if (failed) {
        neural->runtime.failed_frames++;
        neural->runtime.state = XEMU_DLSS_RUNTIME_FAILED;
        neural->runtime.feature = XEMU_NEURAL_PLUGIN_FEATURE_NONE;
        neural->runtime.gpu_time_ns = 0;
    } else {
        neural->runtime.processed_frames++;
        neural->runtime.state = XEMU_DLSS_RUNTIME_ACTIVE;
        neural->runtime.feature = (uint32_t)neural->state.active_feature;
        neural->runtime.gpu_time_ns = frame->output.gpu_time_ns;
        if (result == XEMU_NEURAL_RESULT_PASSTHROUGH) {
            neural->runtime.bypassed_frames++;
        }
        if (feature == XEMU_NEURAL_FEATURE_DLSS_NR &&
            result == XEMU_NEURAL_RESULT_RECORDED) {
            neural->runtime.nr_frames++;
        }
    }
    g_strlcpy(neural->runtime.message, message,
              sizeof(neural->runtime.message));
    publish_status(neural, failed);

    if (failed) {
        error_report("nv2a/vk/neural: adapter frame failed: %s",
                     frame->output.text[0] ? frame->output.text :
                                             neural->status.text[0]
                                                 ? neural->status.text
                                                 : "no status supplied");
    }
    if (neural->state.failure_latched) {
        error_report("nv2a/vk/neural: adapter disabled after failure; "
                     "original presentation retained");
        if (neural->ready) {
            shutdown_adapter(neural);
        }
    }
    if (neural->last_logged_feature != neural->state.active_feature) {
        neural->last_logged_feature = neural->state.active_feature;
        fprintf(stderr, "nv2a/vk/neural: active feature: %s\n",
                xemu_neural_present_feature_string(
                    neural->state.active_feature));
    }
    report_bypass(neural);
}
