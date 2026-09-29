/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-observation.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "ui/xui/shader-browser-draw-request.h"

#include <string.h>

PGRAPHShaderBrowserSampleDecision
pgraph_shader_browser_choose_sample(PGRAPHShaderBrowserSampler *sampler,
                                    uint64_t frame, bool gpu_supported)
{
    PGRAPHShaderBrowserSampleDecision decision = { 0 };
    if (!sampler)
        return decision;
    XemuShaderBrowserProfilingConfig config;
    xemu_shader_browser_copy_profiling_config(&config);
    if (config.monitoring_level != XEMU_SHADER_BROWSER_MONITOR_DIAGNOSTIC ||
        (!config.cpu_timing && !config.gpu_timing))
        return decision;
    if (sampler->frame != frame) {
        sampler->frame = frame;
        sampler->gpu_samples_this_frame = 0;
    }
    uint32_t interval =
        config.draw_sample_interval ? config.draw_sample_interval : 1;
    if (++sampler->eligible_draws % interval)
        return decision;
    decision.cpu = config.cpu_timing;
    if (config.gpu_timing && gpu_supported &&
        sampler->gpu_samples_this_frame < config.max_gpu_samples_per_frame) {
        decision.gpu = true;
        ++sampler->gpu_samples_this_frame;
    }
    return decision;
}

static void publish_batch(PGRAPHShaderBrowserObservations *batch)
{
    if (!batch->used) {
        return;
    }
    /* The slots are dense; no allocation or persistence occurs in PGRAPH. */
    xemu_shader_browser_publish_observations(batch->slots, batch->used);
    batch->used = 0;
    memset(batch->indices, 0, sizeof(batch->indices));
}

static uint32_t identity_bucket(const PGRAPHShaderBrowserIdentity *identity,
                                uint32_t route)
{
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < sizeof(identity->hash); ++i) {
        hash = (hash ^ identity->hash[i]) * 16777619U;
    }
    hash = (hash ^ identity->stage) * 16777619U;
    hash = (hash ^ route) * 16777619U;
    return hash & (PGRAPH_SHADER_BROWSER_OBSERVATION_INDEX_SLOTS - 1);
}

uint64_t
pgraph_shader_browser_capture_claim(PGRAPHState *pg,
                                    const PGRAPHShaderBrowserBinding *binding)
{
    if (!xemu_shader_draw_request_is_armed())
        return 0;
    bool valid_binding = binding && binding->count &&
                         binding->count <= ARRAY_SIZE(binding->identities);
    if (!valid_binding && !xemu_shader_capture_session_active())
        return 0;
    XemuShaderDrawIdentity identities[3] = { 0 };
    for (uint32_t i = 0; valid_binding && i < binding->count; ++i) {
        memcpy(identities[i].identity_hash, binding->identities[i].hash,
               sizeof(identities[i].identity_hash));
        identities[i].stage = binding->identities[i].stage;
    }
    uint64_t token =
        valid_binding ?
            xemu_shader_draw_request_claim(
                binding->scope_generation,
                nv2a_profile_preview_renderer_epoch(), identities,
                binding->count, pg->frame_time, pg->draw_time,
                pg->shader_browser_submission + 1) :
            xemu_shader_capture_session_begin_event(
                XEMU_SHADER_CAPTURE_DRAW, pg->frame_time, pg->draw_time, 0,
                xemu_shader_browser_scope_generation(),
                nv2a_profile_preview_renderer_epoch());
    if (token && xemu_shader_draw_request_wants_inputs(token)) {
        const struct {
            const char *name;
            const void *data;
            size_t size;
        } banks[] = {
            { "pgraph.registers", pg->regs_, sizeof(pg->regs_) },
            { "pgraph.vertex_program", pg->program_data,
              sizeof(pg->program_data) },
            { "pgraph.vertex_constants", pg->vsh_constants,
              sizeof(pg->vsh_constants) },
            { "pgraph.lighting_a", pg->ltctxa, sizeof(pg->ltctxa) },
            { "pgraph.lighting_b", pg->ltctxb, sizeof(pg->ltctxb) },
            { "pgraph.lighting_c", pg->ltc1, sizeof(pg->ltc1) },
        };
        for (size_t i = 0; i < ARRAY_SIZE(banks); ++i) {
            XemuShaderDrawBlob blob = { .name = banks[i].name,
                                        .data = banks[i].data,
                                        .byte_count = banks[i].size };
            xemu_shader_draw_request_stage_blob(token, &blob);
        }
        xemu_shader_draw_request_stage_register(token, "capture.generator_abi",
                                                1);
        xemu_shader_draw_request_stage_register(token, "capture.interface_abi",
                                                1);
    }
    return token;
}

void pgraph_shader_browser_capture_recipes(
    uint64_t token, const ShaderState *state,
    const PGRAPHShaderBrowserBinding *binding)
{
    if (!token || !state || !binding ||
        !xemu_shader_draw_request_wants_inputs(token))
        return;
    uint8_t palette[256 * 3];
    nv2a_copy_dac_palette(palette);
    XemuShaderDrawBlob display = { .name = "display.dac_palette",
                                   .data = palette,
                                   .byte_count = sizeof(palette) };
    xemu_shader_draw_request_stage_blob(token, &display);
    for (uint32_t i = 0; i < binding->count; ++i) {
        uint8_t bytes[PGRAPH_SHADER_BROWSER_RECIPE_MAX];
        size_t size = 0;
        uint32_t stage = binding->identities[i].stage;
        if (!pgraph_shader_browser_encode_recipe(state, stage, bytes,
                                                 sizeof(bytes), &size))
            continue;
        char name[64];
        snprintf(name, sizeof(name), "recipe.stage%u", stage);
        XemuShaderDrawBlob blob = { .name = name,
                                    .data = bytes,
                                    .byte_count = size };
        xemu_shader_draw_request_stage_blob(token, &blob);
    }
}

void pgraph_shader_browser_capture_finish(PGRAPHState *pg, uint64_t token,
                                          bool emitted, uint32_t vertex_count,
                                          uint32_t index_count)
{
    if (emitted)
        ++pg->shader_browser_submission;
    if (token)
        xemu_shader_draw_request_finish(token, emitted, pg->primitive_mode,
                                        vertex_count, index_count);
}

void pgraph_shader_browser_flush_observations(
    PGRAPHShaderBrowserObservations *batch, uint64_t frame)
{
    if (!batch) {
        return;
    }
    publish_batch(batch);
    xemu_shader_browser_publish_frame(frame);
    batch->collecting = xemu_shader_browser_session_collection_enabled();
    batch->draw_poll_count = 0;
}

void pgraph_shader_browser_record_draw(PGRAPHShaderBrowserObservations *batch,
                                       PGRAPHShaderBrowserBinding *binding,
                                       uint64_t frame, uint32_t pixel_route)
{
    if (!batch || !binding || !binding->count ||
        !xemu_shader_browser_monitoring_enabled()) {
        return;
    }
    /* A title transition must not put the old title's draws in the new
     * performance session. The UI increments this generation on transition. */
    if (batch->scope_generation != binding->scope_generation) {
        publish_batch(batch);
        batch->scope_generation = binding->scope_generation;
        batch->collecting = xemu_shader_browser_session_collection_enabled();
        batch->draw_poll_count = 0;
    }
    if (++batch->draw_poll_count >= 128) {
        batch->collecting = xemu_shader_browser_session_collection_enabled();
        batch->draw_poll_count = 0;
    }
    if (!batch->collecting) {
        return;
    }

    for (uint32_t i = 0; i < binding->count; ++i) {
        const PGRAPHShaderBrowserIdentity *identity = &binding->identities[i];
        uint32_t route =
            identity->stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION ?
                XEMU_SHADER_BROWSER_ROUTE_FIXED_FUNCTION :
            identity->stage == XEMU_SHADER_BROWSER_STAGE_PIXEL ?
                pixel_route :
                XEMU_SHADER_BROWSER_ROUTE_SPECIALIZED;
        uint32_t bucket = identity_bucket(identity, route);
        XemuShaderBrowserObservation *observation;
        while (batch->indices[bucket]) {
            XemuShaderBrowserObservation *candidate =
                &batch->slots[batch->indices[bucket] - 1];
            if (candidate->stage == identity->stage &&
                candidate->route == route &&
                memcmp(candidate->identity_hash, identity->hash,
                       sizeof(identity->hash)) == 0) {
                break;
            }
            bucket = (bucket + 1) &
                     (PGRAPH_SHADER_BROWSER_OBSERVATION_INDEX_SLOTS - 1);
        }
        if (!batch->indices[bucket]) {
            if (batch->used == PGRAPH_SHADER_BROWSER_OBSERVATION_SLOTS) {
                publish_batch(batch);
                bucket = identity_bucket(identity, route);
            }
            observation = &batch->slots[batch->used++];
            batch->indices[bucket] = batch->used;
            memset(observation, 0, sizeof(*observation));
            observation->identity_version = 1;
            memcpy(observation->identity_hash, identity->hash,
                   sizeof(identity->hash));
            observation->stage = identity->stage;
            observation->status = XEMU_SHADER_BROWSER_STATUS_NORMAL;
            observation->route = route;
            observation->readiness = XEMU_SHADER_BROWSER_READINESS_READY;
            observation->flags = XEMU_SHADER_BROWSER_OBSERVED;
            observation->pipeline_variant_count = 1;
            observation->first_frame = frame;
        } else {
            observation = &batch->slots[batch->indices[bucket] - 1];
        }
        observation->last_frame = frame;
        ++observation->draw_count_delta;
        if (route == XEMU_SHADER_BROWSER_ROUTE_UBER) {
            ++observation->uber_draw_delta;
        } else {
            ++observation->specialized_draw_delta;
        }
    }
}
