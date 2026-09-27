/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-observation.h"
#include "hw/xbox/nv2a/debug.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "hw/xbox/nv2a/pgraph/shader-browser-geometry-copy.h"
#include "hw/xbox/nv2a/pgraph/vertex-fetch-span.h"
#include "ui/xui/shader-browser-draw-request.h"

#include <string.h>
#include <math.h>

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

static void add_duration(XemuShaderBrowserDurationStats *stats, uint64_t ns)
{
    if (!ns) {
        return;
    }
    if (!stats->sample_count || ns < stats->min_ns) {
        stats->min_ns = ns;
    }
    if (ns > stats->max_ns) {
        stats->max_ns = ns;
    }
    ++stats->sample_count;
    stats->total_ns += ns;
}

static bool capture_float_geometry(NV2AState *d, PGRAPHState *pg,
                                   XemuShaderDrawGeometry *geometry,
                                   float **positions, uint32_t **indices)
{
    if (pg->primitive_mode != NV097_SET_BEGIN_END_OP_TRIANGLES) return false;
    const VertexAttribute *attr = &pg->vertex_attributes[0];
    if (pg->inline_buffer_length) {
        const size_t count = pg->inline_buffer_length;
        if (count > 4096 || count < 3 || count % 3 ||
            !attr->inline_buffer_populated || !attr->inline_buffer)
            return false;
        *positions = g_try_new(float, count * 4);
        *indices = g_try_new(uint32_t, count);
        if (!*positions || !*indices) {
            g_free(*positions);
            g_free(*indices);
            *positions = NULL;
            *indices = NULL;
            return false;
        }
        memcpy(*positions, attr->inline_buffer, count * 4 * sizeof(float));
        for (size_t i = 0; i < count; ++i) (*indices)[i] = i;
        geometry->positions = *positions;
        geometry->position_count = count;
        geometry->indices = *indices;
        geometry->index_count = count;
        return true;
    }
    if (attr->format != NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F ||
        attr->size != sizeof(float) ||
        (attr->count != 3 && attr->count != 4) || !attr->stride) return false;

    uint32_t first = UINT32_MAX;
    uint32_t last = 0;
    size_t emitted = 0;
    if (pg->inline_elements_length) {
        emitted = pg->inline_elements_length;
        if (emitted > 12288) return false;
        for (size_t i = 0; i < emitted; ++i) {
            first = MIN(first, pg->inline_elements[i]);
            last = MAX(last, pg->inline_elements[i]);
        }
    } else if (pg->draw_arrays_length) {
        for (size_t i = 0; i < pg->draw_arrays_length; ++i) {
            int32_t start = pg->draw_arrays_start[i];
            int32_t count = pg->draw_arrays_count[i];
            if (start < 0 || count <= 0 ||
                (uint64_t)start + count > UINT32_MAX) return false;
            emitted += count;
            if (emitted > 12288) return false;
            first = MIN(first, (uint32_t)start);
            last = MAX(last, (uint32_t)(start + count - 1));
        }
    } else {
        return false;
    }
    if (!emitted || emitted % 3 || first > last ||
        (uint64_t)last - first + 1 > 4096)
        return false;
    const size_t position_count = (size_t)last - first + 1;
    hwaddr dma_limit = 0;
    uint8_t *mapped = nv_dma_map(
        d, attr->dma_select ? pg->dma_vertex_b : pg->dma_vertex_a,
        &dma_limit);
    if (!mapped) return false;
    const uint64_t dma_base = (uintptr_t)mapped - (uintptr_t)d->vram_ptr;
    const uint64_t vram_size = memory_region_size(d->vram);
    PGRAPHVertexFetchRange range;
    if (!pgraph_vertex_resolve_fetch_range(
            dma_base, dma_limit, attr->offset, vram_size, first, last,
            attr->stride, attr->count * sizeof(float), &range)) return false;
    *positions = g_try_new(float, position_count * 4);
    *indices = g_try_new(uint32_t, emitted);
    if (!*positions || !*indices ||
        !xemu_shader_draw_copy_float_positions(
            d->vram_ptr + range.attribute_base,
            vram_size - range.attribute_base, first, position_count,
            attr->stride, attr->count, *positions, position_count * 4)) {
        g_free(*positions);
        g_free(*indices);
        *positions = NULL;
        *indices = NULL;
        return false;
    }
    if (pg->inline_elements_length) {
        for (size_t i = 0; i < emitted; ++i)
            (*indices)[i] = pg->inline_elements[i] - first;
    } else {
        size_t output = 0;
        for (size_t i = 0; i < pg->draw_arrays_length; ++i) {
            for (int32_t j = 0; j < pg->draw_arrays_count[i]; ++j)
                (*indices)[output++] = pg->draw_arrays_start[i] + j - first;
        }
    }
    geometry->positions = *positions;
    geometry->position_count = position_count;
    geometry->indices = *indices;
    geometry->index_count = emitted;
    return true;
}

void pgraph_shader_browser_capture_submitted(NV2AState *d,
                                             PGRAPHShaderBrowserBinding *binding)
{
    if (!d || !binding || !binding->count ||
        binding->count > ARRAY_SIZE(binding->identities) ||
        !xemu_shader_draw_request_is_armed()) {
        return;
    }
    PGRAPHState *pg = &d->pgraph;
    XemuShaderDrawIdentity identities[ARRAY_SIZE(binding->identities)] = {0};
    for (uint32_t i = 0; i < binding->count; ++i) {
        memcpy(identities[i].identity_hash, binding->identities[i].hash,
               sizeof(identities[i].identity_hash));
        identities[i].stage = binding->identities[i].stage;
    }
    const uint64_t renderer_epoch = nv2a_profile_preview_renderer_epoch();
    if (!xemu_shader_draw_request_wants(binding->scope_generation,
                                        renderer_epoch, identities,
                                        binding->count)) return;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    if (pg->inline_elements_length) {
        index_count = pg->inline_elements_length;
    } else if (pg->draw_arrays_length) {
        for (uint32_t i = 0; i < pg->draw_arrays_length; ++i) {
            vertex_count += pg->draw_arrays_count[i];
        }
    } else if (pg->inline_buffer_length) {
        vertex_count = pg->inline_buffer_length;
    }
    XemuShaderDrawGeometry geometry = {0};
    float *positions = NULL;
    uint32_t *indices = NULL;
    bool owned = capture_float_geometry(
        d, pg, &geometry, &positions, &indices);
    if (owned) {
        for (size_t i = 0; i < geometry.position_count * 4; ++i) {
            if (!isfinite(positions[i]) || fabsf(positions[i]) > 1.0e9f) {
                owned = false;
                break;
            }
        }
    }
    if (owned) {
        vertex_count = geometry.position_count;
        index_count = geometry.index_count;
    }
    xemu_shader_draw_request_submitted(
        binding->scope_generation, renderer_epoch,
        identities, binding->count, pg->frame_time, pg->draw_time,
        pg->primitive_mode, vertex_count, index_count,
        owned ? &geometry : NULL);
    g_free(positions);
    g_free(indices);
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
    if (!batch || !binding || !binding->count) {
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
        if (binding->timings_pending &&
            identity->stage == XEMU_SHADER_BROWSER_STAGE_PIXEL) {
            add_duration(&observation->compile_cpu, binding->compile_cpu_ns);
            add_duration(&observation->prepare_cpu, binding->prepare_cpu_ns);
            binding->timings_pending = false;
        }
    }
}
