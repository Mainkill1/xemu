/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-observation.h"

#include <string.h>

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
