/*
 * Dynamic neural-presentation plug-in loader smoke test.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/neural-present-loader.h"

static bool run_smoke(const char *path)
{
    XemuNeuralPluginLibrary library;
    if (!xemu_neural_plugin_library_open(&library, path)) {
        fprintf(stderr, "loader: %s\n", library.error);
        return false;
    }

    const XemuNeuralPluginApiV1 *api = library.api;
    if (api == NULL || strcmp(api->name, "xemu-pass-through") != 0) {
        xemu_neural_plugin_library_close(&library);
        return false;
    }

    XemuNeuralPluginHostV1 host = XEMU_NEURAL_PLUGIN_HOST_V1_INIT;
    XemuNeuralPluginVulkanRequirementsV1 requirements =
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    XemuNeuralPluginCreateInfoV1 create_info =
        XEMU_NEURAL_PLUGIN_CREATE_INFO_V1_INIT;
    XemuNeuralPluginStatusV1 status = XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;
    void *bootstrap_context = NULL;
    if (api->bootstrap(&host, &bootstrap_context, &requirements, &status) !=
            XEMU_NEURAL_PLUGIN_RESULT_OK ||
        xemu_neural_plugin_requirements_validate(&requirements) !=
            XEMU_NEURAL_PLUGIN_REQUIREMENTS_VALID) {
        xemu_neural_plugin_library_close(&library);
        return false;
    }
    create_info.host = &host;
    create_info.bootstrap_context = bootstrap_context;
    create_info.vulkan.api_version = (1U << 22) | (3U << 12);
    create_info.vulkan.instance = 1;
    create_info.vulkan.physical_device = 2;
    create_info.vulkan.device = 3;
    create_info.vulkan.queue = 4;

    void *context = NULL;
    if (api->create(&create_info, &context, &status) !=
            XEMU_NEURAL_PLUGIN_RESULT_OK ||
        context == NULL) {
        api->shutdown(bootstrap_context);
        xemu_neural_plugin_library_close(&library);
        return false;
    }

    XemuNeuralPluginFrameV1 frame = XEMU_NEURAL_PLUGIN_FRAME_V1_INIT;
    frame.display_width = 1280;
    frame.display_height = 720;
    frame.processing_width = 1280;
    frame.processing_height = 720;
    frame.active_width = 1280;
    frame.active_height = 720;
    frame.color_image = 5;
    frame.color_format = 37;
    frame.color_layout = 2;
    frame.required_return_layout = 2;

    status = (XemuNeuralPluginStatusV1)XEMU_NEURAL_PLUGIN_STATUS_V1_INIT;
    if (api->prepare_frame(context, &frame, &status) !=
        XEMU_NEURAL_PLUGIN_RESULT_OK) {
        api->destroy(context);
        api->shutdown(bootstrap_context);
        xemu_neural_plugin_library_close(&library);
        return false;
    }

    XemuNeuralPluginVulkanRecordV1 record =
        XEMU_NEURAL_PLUGIN_VULKAN_RECORD_V1_INIT;
    XemuNeuralPluginFrameOutputV1 output =
        XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    record.command_buffer = 6;
    record.frame = frame;
    XemuNeuralPluginResult result =
        api->record_vulkan(context, &record, &output);
    bool ok = result == XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH &&
              output.feature == XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH &&
              !(output.flags & XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN);

    api->reset_history(context, 1);
    api->destroy(context);
    api->shutdown(bootstrap_context);
    xemu_neural_plugin_library_close(&library);
    return ok && library.handle == NULL && library.api == NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <plugin>\n", argv[0]);
        return 2;
    }
    puts("TAP version 13");
    puts("1..1");
    bool result = run_smoke(argv[1]);
    printf("%s 1 - load and execute pass-through plug-in\n",
           result ? "ok" : "not ok");
    return result ? 0 : 1;
}
