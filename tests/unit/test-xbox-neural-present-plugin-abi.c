/*
 * Experimental neural-presentation plug-in ABI validation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

static XemuNeuralPluginResult fake_bootstrap(
    const XemuNeuralPluginHostV1 *host, void **bootstrap_context,
    XemuNeuralPluginVulkanRequirementsV1 *requirements,
    XemuNeuralPluginStatusV1 *status)
{
    (void)host;
    *bootstrap_context = (void *)(uintptr_t)2;
    *requirements = (XemuNeuralPluginVulkanRequirementsV1)
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void fake_shutdown(void *bootstrap_context)
{
    (void)bootstrap_context;
}

static int32_t fake_create_instance(
    void *bootstrap_context, const void *create_info,
    const void *allocation_callbacks, uintptr_t *instance_out)
{
    (void)bootstrap_context;
    (void)create_info;
    (void)allocation_callbacks;
    *instance_out = 1;
    return 0;
}

static int32_t fake_create_device(
    void *bootstrap_context, uintptr_t physical_device,
    const void *create_info, const void *allocation_callbacks,
    uintptr_t *device_out)
{
    (void)bootstrap_context;
    (void)physical_device;
    (void)create_info;
    (void)allocation_callbacks;
    *device_out = 1;
    return 0;
}

static XemuNeuralPluginResult fake_create(
    const XemuNeuralPluginCreateInfoV1 *create_info, void **context,
    XemuNeuralPluginStatusV1 *status)
{
    (void)create_info;
    *context = (void *)(uintptr_t)1;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void fake_destroy(void *context)
{
    (void)context;
}

static XemuNeuralPluginResult fake_prepare(
    void *context, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status)
{
    (void)context;
    (void)frame;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static XemuNeuralPluginResult fake_record(
    void *context, const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output)
{
    (void)context;
    (void)record;
    output->feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    return XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH;
}

static void fake_reset(void *context, uint32_t reason)
{
    (void)context;
    (void)reason;
}

static void fake_release(void *context)
{
    (void)context;
}

static void fake_get_status(void *context, XemuNeuralPluginStatusV1 *status)
{
    (void)context;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
}

static XemuNeuralPluginApiV1 valid_api(void)
{
    XemuNeuralPluginApiV1 api = {
        .struct_size = sizeof(api),
        .abi_version = XEMU_NEURAL_PLUGIN_ABI_VERSION,
        .capabilities = XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD |
                        XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE |
                        XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY,
        .name = "test-adapter",
        .version = "1.0",
        .bootstrap = fake_bootstrap,
        .shutdown = fake_shutdown,
        .create = fake_create,
        .destroy = fake_destroy,
        .prepare_frame = fake_prepare,
        .record_vulkan = fake_record,
        .reset_history = fake_reset,
        .release_resources = fake_release,
        .get_status = fake_get_status,
    };
    return api;
}

static bool valid_contract_is_accepted(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_VALID;
}

static bool wrong_version_is_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.abi_version++;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_BAD_VERSION;
}

static bool short_structure_is_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.struct_size = offsetof(XemuNeuralPluginApiV1, get_status);
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_BAD_SIZE;
}

static bool missing_identity_is_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.name = NULL;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_BAD_IDENTITY) {
        return false;
    }
    api = valid_api();
    api.version = "";
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_BAD_IDENTITY;
}

static bool missing_required_capabilities_are_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.capabilities = XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_MISSING_CAPABILITY;
}

static bool missing_callback_is_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.bootstrap = NULL;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK) {
        return false;
    }
    api = valid_api();
    api.shutdown = NULL;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK) {
        return false;
    }
    api = valid_api();
    api.record_vulkan = NULL;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK) {
        return false;
    }
    api = valid_api();
    api.reset_history = NULL;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK) {
        return false;
    }
    api = valid_api();
    api.release_resources = NULL;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_MISSING_CALLBACK;
}

static bool requirements_contract_is_enforced(void)
{
    static const char *const instance_extensions[] = {
        "VK_KHR_get_physical_device_properties2",
    };
    static const char *const features12[] = {
        "timelineSemaphore",
    };
    XemuNeuralPluginVulkanRequirementsV1 requirements =
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    requirements.instance_extension_count = 1;
    requirements.instance_extensions = instance_extensions;
    requirements.feature12_count = 1;
    requirements.feature12_names = features12;
    requirements.additional_graphics_queue_count = 1;
    if (xemu_neural_plugin_requirements_validate(&requirements) !=
        XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID) {
        return false;
    }

    requirements.feature12_names = NULL;
    if (xemu_neural_plugin_requirements_validate(&requirements) !=
        XEMU_NEURAL_PLUGIN_REQUIREMENTS_MISSING_LIST) {
        return false;
    }

    requirements.feature12_names = features12;
    requirements.feature13_count =
        XEMU_NEURAL_PLUGIN_MAX_REQUIREMENT_COUNT + 1;
    if (xemu_neural_plugin_requirements_validate(&requirements) !=
        XEMU_NEURAL_PLUGIN_REQUIREMENTS_TOO_MANY) {
        return false;
    }

    requirements.feature13_count = 0;
    requirements.additional_optical_flow_queue_count = 17;
    return xemu_neural_plugin_requirements_validate(&requirements) ==
           XEMU_NEURAL_PLUGIN_REQUIREMENTS_QUEUE_COUNT;
}

static bool incompatible_depth_capabilities_are_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.capabilities |= XEMU_NEURAL_PLUGIN_CAP_REQUIRES_DEPTH |
                        XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY;
}

static bool create_proxy_contract_is_enforced(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.capabilities |= XEMU_NEURAL_PLUGIN_CAP_VULKAN_CREATE_PROXY;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY) {
        return false;
    }

    api.create_vulkan_instance = fake_create_instance;
    api.create_vulkan_device = fake_create_device;
    if (xemu_neural_plugin_api_validate(&api) !=
        XEMU_NEURAL_PLUGIN_API_VALID) {
        return false;
    }

    api = valid_api();
    api.create_vulkan_instance = fake_create_instance;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_CONFLICTING_CAPABILITY;
}

static bool missing_frame_contract_is_rejected(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.capabilities &= ~XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_MISSING_FRAME_CONTRACT;
}

static bool valid_internal_motion_contract_is_accepted(void)
{
    XemuNeuralPluginApiV1 api = valid_api();
    api.capabilities &= ~XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY;
    api.capabilities |= XEMU_NEURAL_PLUGIN_CAP_INTERNAL_MOTION |
                        XEMU_NEURAL_PLUGIN_CAP_DEPTH_OPTIONAL;
    return xemu_neural_plugin_api_validate(&api) ==
           XEMU_NEURAL_PLUGIN_API_VALID;
}

static bool frame_output_contract_is_enforced(void)
{
    XemuNeuralPluginFrameOutputV1 output =
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    output.feature = XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR;
    output.flags = XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN |
                   XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID;
    if (xemu_neural_plugin_frame_output_validate(
            XEMU_NEURAL_PLUGIN_RESULT_OK, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID) {
        return false;
    }

    output.flags = XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID;
    if (xemu_neural_plugin_frame_output_validate(
            XEMU_NEURAL_PLUGIN_RESULT_OK, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_MISSING_COLOR) {
        return false;
    }

    output = (XemuNeuralPluginFrameOutputV1)
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    output.feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    if (xemu_neural_plugin_frame_output_validate(
            XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID) {
        return false;
    }

    output.flags = XEMU_NEURAL_PLUGIN_OUTPUT_HISTORY_VALID;
    if (xemu_neural_plugin_frame_output_validate(
            XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT) {
        return false;
    }

    output = (XemuNeuralPluginFrameOutputV1)
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    output.feature = XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR;
    output.flags = XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN;
    if (xemu_neural_plugin_frame_output_validate(
            XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT) {
        return false;
    }

    output = (XemuNeuralPluginFrameOutputV1)
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    if (xemu_neural_plugin_frame_output_validate(
            99, &output) !=
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_RESULT) {
        return false;
    }

    output.struct_size = offsetof(XemuNeuralPluginFrameOutputV1, text);
    return xemu_neural_plugin_frame_output_validate(
               XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH, &output) ==
           XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_BAD_SIZE;
}

static bool structures_are_explicitly_versioned(void)
{
    XemuNeuralPluginHostV1 host = XEMU_NEURAL_PLUGIN_HOST_V1_INIT;
    XemuNeuralPluginVulkanDeviceV1 device =
        XEMU_NEURAL_PLUGIN_VULKAN_DEVICE_V1_INIT;
    XemuNeuralPluginVulkanRequirementsV1 requirements =
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    XemuNeuralPluginCreateInfoV1 create_info =
        XEMU_NEURAL_PLUGIN_CREATE_INFO_V1_INIT;
    XemuNeuralPluginFrameV1 frame = XEMU_NEURAL_PLUGIN_FRAME_V1_INIT;
    XemuNeuralPluginVulkanRecordV1 record =
        XEMU_NEURAL_PLUGIN_VULKAN_RECORD_V1_INIT;
    XemuNeuralPluginFrameOutputV1 output =
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    XemuNeuralPluginStatusV1 status = XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;

    return host.struct_size == sizeof(host) &&
           host.abi_version == XEMU_NEURAL_PLUGIN_ABI_VERSION &&
           device.struct_size == sizeof(device) &&
           requirements.struct_size == sizeof(requirements) &&
           create_info.struct_size == sizeof(create_info) &&
           create_info.vulkan.struct_size == sizeof(create_info.vulkan) &&
           frame.struct_size == sizeof(frame) &&
           record.struct_size == sizeof(record) &&
           record.frame.struct_size == sizeof(record.frame) &&
           output.struct_size == sizeof(output) &&
           status.struct_size == sizeof(status);
}

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        { "valid ABI accepted", valid_contract_is_accepted },
        { "wrong ABI version rejected", wrong_version_is_rejected },
        { "short ABI structure rejected", short_structure_is_rejected },
        { "missing identity rejected", missing_identity_is_rejected },
        { "required capabilities enforced",
          missing_required_capabilities_are_rejected },
        { "required callbacks enforced", missing_callback_is_rejected },
        { "pre-device requirements contract enforced",
          requirements_contract_is_enforced },
        { "frame input contract required",
          missing_frame_contract_is_rejected },
        { "internal motion contract accepted",
          valid_internal_motion_contract_is_accepted },
        { "conflicting depth capabilities rejected",
          incompatible_depth_capabilities_are_rejected },
        { "Vulkan create proxy contract enforced",
          create_proxy_contract_is_enforced },
        { "frame output contract enforced",
          frame_output_contract_is_enforced },
        { "all frame structures carry sizes",
          structures_are_explicitly_versioned },
    };
    bool passed = true;

    puts("TAP version 13");
    printf("1..%zu\n", sizeof(tests) / sizeof(tests[0]));
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        bool result = tests[i].run();
        printf("%s %zu - %s\n", result ? "ok" : "not ok", i + 1,
               tests[i].name);
        passed &= result;
    }
    return passed ? 0 : 1;
}
