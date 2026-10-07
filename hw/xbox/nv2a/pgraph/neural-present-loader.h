/*
 * Dynamic loader for optional neural-presentation adapters.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_LOADER_H
#define HW_XBOX_NV2A_PGRAPH_NEURAL_PRESENT_LOADER_H

#include <stdbool.h>

#include "hw/xbox/nv2a/pgraph/neural-present-plugin.h"

#define XEMU_NEURAL_PLUGIN_LOADER_ERROR_SIZE 256U

typedef struct XemuNeuralPluginLibrary {
    void *handle;
    const XemuNeuralPluginApiV1 *api;
    char error[XEMU_NEURAL_PLUGIN_LOADER_ERROR_SIZE];
} XemuNeuralPluginLibrary;

bool xemu_neural_plugin_library_open(XemuNeuralPluginLibrary *library,
                                     const char *path);
void xemu_neural_plugin_library_close(XemuNeuralPluginLibrary *library);

#endif
