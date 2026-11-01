/*
 * Commit validated adapter output from private storage to the display.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_TRANSACTION_H
#define HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_TRANSACTION_H

#include <stdbool.h>
#include <stdio.h>
#include "neural-present-plugin.h"

typedef void (*XemuNeuralPresentCopyFn)(void *opaque, bool commit);

static inline XemuNeuralPluginResult xemu_neural_present_record_transaction(
    XemuNeuralPluginRecordVulkanFn record_fn, void *context,
    const XemuNeuralPluginVulkanRecordV1 *record,
    XemuNeuralPluginFrameOutputV1 *output, bool validation_only,
    XemuNeuralPresentCopyFn copy, void *opaque)
{
    *output =
        (XemuNeuralPluginFrameOutputV1)XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT;
    copy(opaque, false);
    XemuNeuralPluginResult result = record_fn(context, record, output);
    output->text[sizeof(output->text) - 1] = '\0';
    XemuNeuralPluginFrameOutputValidation validation =
        xemu_neural_plugin_frame_output_validate(result, output);
    if (validation_only &&
        output->feature > XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH) {
        validation = XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_RESULT_CONFLICT;
    }
    if (validation != XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_VALID) {
        snprintf(output->text, sizeof(output->text),
                 "invalid adapter output: %s",
                 xemu_neural_plugin_frame_output_validation_string(validation));
        return XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR;
    }
    if (result == XEMU_NEURAL_PLUGIN_RESULT_OK) {
        copy(opaque, true);
    }
    return result;
}
#endif
