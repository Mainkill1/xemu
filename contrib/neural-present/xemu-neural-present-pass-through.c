/*
 * Reference neural-presentation adapter that deliberately changes no pixels.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

typedef struct PassThroughContext {
    const XemuNeuralPluginHostV1 *host;
    uint64_t frames;
    uint64_t resets;
} PassThroughContext;

static void set_text(char *destination, size_t size, const char *text);

static XemuNeuralPluginResult pass_bootstrap(
    const XemuNeuralPluginHostV1 *host, void **bootstrap_context,
    XemuNeuralPluginVulkanRequirementsV1 *requirements,
    XemuNeuralPluginStatusV1 *status)
{
    (void)host;
    if (bootstrap_context == NULL || requirements == NULL || status == NULL) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    *bootstrap_context = NULL;
    *requirements = (XemuNeuralPluginVulkanRequirementsV1)
        XEMU_NEURAL_PLUGIN_VULKAN_REQUIREMENTS_V1_INIT;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    set_text(status->text, sizeof(status->text),
             "Reference adapter has no pre-device Vulkan requirements");
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void pass_shutdown(void *bootstrap_context)
{
    (void)bootstrap_context;
}

static void set_text(char *destination, size_t size, const char *text)
{
    if (size == 0) {
        return;
    }
    snprintf(destination, size, "%s", text);
}

static void log_message(PassThroughContext *context, const char *message)
{
    if (context->host != NULL && context->host->log != NULL) {
        context->host->log(context->host->opaque,
                           XEMU_NEURAL_PLUGIN_LOG_INFO, message);
    }
}

static XemuNeuralPluginResult pass_create(
    const XemuNeuralPluginCreateInfoV1 *create_info, void **context_out,
    XemuNeuralPluginStatusV1 *status)
{
    if (create_info == NULL || context_out == NULL || status == NULL ||
        create_info->struct_size < sizeof(*create_info) ||
        create_info->vulkan.struct_size < sizeof(create_info->vulkan) ||
        create_info->vulkan.device == 0 || create_info->vulkan.queue == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    PassThroughContext *context = calloc(1, sizeof(*context));
    if (context == NULL) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    context->host = create_info->host;
    *context_out = context;
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    set_text(status->text, sizeof(status->text),
             "Reference adapter loaded; no neural work is recorded");
    log_message(context, status->text);
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static void pass_destroy(void *opaque)
{
    PassThroughContext *context = opaque;
    if (context != NULL) {
        log_message(context, "Reference adapter unloaded");
    }
    free(context);
}

static XemuNeuralPluginResult pass_prepare(
    void *opaque, const XemuNeuralPluginFrameV1 *frame,
    XemuNeuralPluginStatusV1 *status)
{
    PassThroughContext *context = opaque;
    if (context == NULL || frame == NULL || status == NULL ||
        frame->struct_size < sizeof(*frame) || frame->color_image == 0 ||
        frame->display_width == 0 || frame->display_height == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }
    status->state = XEMU_NEURAL_PLUGIN_STATE_READY;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    status->evaluated_frames = context->frames;
    set_text(status->text, sizeof(status->text), "Pass-through frame prepared");
    return XEMU_NEURAL_PLUGIN_RESULT_OK;
}

static XemuNeuralPluginResult pass_record(
    void *opaque, const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output)
{
    PassThroughContext *context = opaque;
    if (context == NULL || record == NULL || output == NULL ||
        record->struct_size < sizeof(*record) ||
        record->frame.struct_size < sizeof(record->frame) ||
        record->command_buffer == 0) {
        return XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
    }

    context->frames++;
    output->feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    output->flags = 0;
    set_text(output->text, sizeof(output->text),
             "No commands recorded; original presentation retained");
    return XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH;
}

static void pass_reset(void *opaque, uint32_t reason)
{
    PassThroughContext *context = opaque;
    (void)reason;
    if (context != NULL) {
        context->resets++;
    }
}

static void pass_release(void *opaque)
{
    PassThroughContext *context = opaque;
    if (context != NULL) {
        log_message(context, "Reference adapter released frame resources");
    }
}

static void pass_status(void *opaque, XemuNeuralPluginStatusV1 *status)
{
    PassThroughContext *context = opaque;
    if (status == NULL) {
        return;
    }
    status->state = context != NULL ? XEMU_NEURAL_PLUGIN_STATE_READY
                                    : XEMU_NEURAL_PLUGIN_STATE_FAILED;
    status->active_feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
    status->evaluated_frames = context != NULL ? context->frames : 0;
    set_text(status->text, sizeof(status->text),
             "Reference pass-through adapter");
}

static const XemuNeuralPluginApiV1 api = {
    .struct_size = sizeof(api),
    .abi_version = XEMU_NEURAL_PLUGIN_ABI_VERSION,
    .capabilities = XEMU_NEURAL_PLUGIN_CAP_VALIDATION_ONLY |
                    XEMU_NEURAL_PLUGIN_CAP_VULKAN_RECORD |
                    XEMU_NEURAL_PLUGIN_CAP_TRANSACTIONAL_IN_PLACE |
                    XEMU_NEURAL_PLUGIN_CAP_COLOR_ONLY,
    .name = "xemu-pass-through",
    .version = "1.0.0",
    .bootstrap = pass_bootstrap,
    .shutdown = pass_shutdown,
    .create = pass_create,
    .destroy = pass_destroy,
    .prepare_frame = pass_prepare,
    .record_vulkan = pass_record,
    .reset_history = pass_reset,
    .release_resources = pass_release,
    .get_status = pass_status,
};

XEMU_NEURAL_PLUGIN_EXPORT const XemuNeuralPluginApiV1 *
xemu_neural_present_get_api(uint32_t requested_abi_version)
{
    return requested_abi_version == XEMU_NEURAL_PLUGIN_ABI_VERSION ? &api : NULL;
}
