/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/neural-present-transaction.h"

typedef struct Test {
    unsigned int display, scratch, copies;
    XemuNeuralPluginResult result;
    XemuNeuralPluginFrameOutputV1 output;
} Test;

static void copy(void *opaque, bool commit)
{
    Test *test = opaque;
    if (commit) {
        test->display = test->scratch;
        test->copies++;
    } else {
        test->scratch = test->display;
    }
}

static XemuNeuralPluginResult
record(void *opaque, const XemuNeuralPluginVulkanRecordV1 *frame,
       XemuNeuralPluginFrameOutputV1 *output)
{
    Test *test = opaque;
    assert(frame->frame.color_image == (uintptr_t)&test->scratch);
    assert(test->scratch == 17);
    test->scratch = 99; /* Even a rejected adapter has recorded writes. */
    *output = test->output;
    return test->result;
}

int main(void)
{
    for (unsigned int scenario = 0; scenario < 8; scenario++) {
        Test test = {
            .display = 17,
            .result = XEMU_NEURAL_PLUGIN_RESULT_OK,
            .output = XEMU_NEURAL_PLUGIN_FRAME_OUTPUT_V1_INIT,
        };
        test.output.feature = XEMU_NEURAL_PLUGIN_FEATURE_PASSTHROUGH;
        test.output.flags = XEMU_NEURAL_PLUGIN_OUTPUT_COLOR_WRITTEN;
        bool validation_only = false;
        switch (scenario) {
        case 0:
            break;
        case 1:
            test.output.struct_size = 0;
            break;
        case 2:
            test.output.flags |= 0x80000000U;
            break;
        case 3:
            test.output.flags = 0;
            break;
        case 4:
            test.result = XEMU_NEURAL_PLUGIN_RESULT_PASSTHROUGH;
            test.output.flags = 0;
            break;
        case 5:
            test.result = XEMU_NEURAL_PLUGIN_RESULT_RETRYABLE_ERROR;
            break;
        case 6:
            test.result = XEMU_NEURAL_PLUGIN_RESULT_FATAL_ERROR;
            break;
        case 7:
            validation_only = true;
            test.output.feature = XEMU_NEURAL_PLUGIN_FEATURE_DLSS_NR;
            break;
        }
        XemuNeuralPluginVulkanRecordV1 frame =
            XEMU_NEURAL_PLUGIN_VULKAN_RECORD_V1_INIT;
        frame.frame.color_image = (uintptr_t)&test.scratch;
        XemuNeuralPluginFrameOutputV1 output;
        XemuNeuralPluginResult result = xemu_neural_present_record_transaction(
            record, &test, &frame, &output, validation_only, copy, &test);
        bool commit = scenario == 0;
        if (test.display != (commit ? 99U : 17U) ||
            test.copies != (unsigned int)commit ||
            (commit && result != XEMU_NEURAL_PLUGIN_RESULT_OK)) {
            fprintf(stderr, "transaction scenario %u failed\n", scenario);
            return 1;
        }
    }
    puts("8 transaction scenarios passed");
    return 0;
}
