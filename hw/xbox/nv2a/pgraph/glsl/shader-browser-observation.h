/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_OBSERVATION_H
#define HW_XBOX_NV2A_PGRAPH_GLSL_SHADER_BROWSER_OBSERVATION_H

#include "shader-browser-publication.h"

#define PGRAPH_SHADER_BROWSER_OBSERVATION_SLOTS 256
#define PGRAPH_SHADER_BROWSER_OBSERVATION_INDEX_SLOTS 512

typedef struct PGRAPHShaderBrowserObservations {
    XemuShaderBrowserObservation slots[PGRAPH_SHADER_BROWSER_OBSERVATION_SLOTS];
    uint16_t indices[PGRAPH_SHADER_BROWSER_OBSERVATION_INDEX_SLOTS];
    uint64_t scope_generation;
    uint32_t used;
    uint32_t draw_poll_count;
    bool collecting;
} PGRAPHShaderBrowserObservations;

void pgraph_shader_browser_record_draw(
    PGRAPHShaderBrowserObservations *batch,
    const PGRAPHShaderBrowserBinding *binding, uint64_t frame,
    uint32_t pixel_route);
void pgraph_shader_browser_flush_observations(
    PGRAPHShaderBrowserObservations *batch, uint64_t frame);

#endif
