/*
 * Non-creating Vulkan hybrid execution probes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_HYBRID_READY_H
#define HW_XBOX_NV2A_PGRAPH_VK_HYBRID_READY_H

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

/* A pipeline key can be derived before a shader binding is selected. */
static inline void pgraph_vk_pipeline_key_set_shader(
    PipelineKey *key, const ShaderState *state,
    PGRAPHVkFragmentRoute route)
{
    key->fragment_route = route;
    key->shader_state = *state;
    if (route == PGRAPH_VK_FRAGMENT_UBERSHADER) {
        pgraph_vk_canonicalize_uber_combiner_state(
            &key->shader_state.psh);
    }
}

/* Borrowed exact hits. These functions neither reserve entries nor refresh
 * LRU order. The renderer must keep cache mutation on its owning thread. */
static inline PipelineBinding *pgraph_vk_pipeline_cache_find_ready(
    Lru *cache, uint64_t hash, const PipelineKey *key)
{
    LruNode *node = lru_find_existing(cache, hash, key);
    if (!node) {
        return NULL;
    }
    PipelineBinding *binding = container_of(node, PipelineBinding, node);
    return binding->pipeline != VK_NULL_HANDLE ? binding : NULL;
}

static inline ShaderBinding *pgraph_vk_shader_binding_find_ready(
    Lru *cache, uint64_t hash, const ShaderBindingKey *key,
    bool geometry_required)
{
    LruNode *node = lru_find_existing(cache, hash, key);
    if (!node) {
        return NULL;
    }
    ShaderBinding *binding = container_of(node, ShaderBinding, node);
    return binding->vsh.module_info && binding->psh.module_info &&
           (!geometry_required || binding->geom.module_info) ?
           binding : NULL;
}

typedef enum PGRAPHVkExecutionRoute {
    PGRAPH_VK_EXECUTION_SPECIALIZED,
    PGRAPH_VK_EXECUTION_UBERSHADER,
    PGRAPH_VK_EXECUTION_UBERSHADER_AFTER_ROLLOVER,
    PGRAPH_VK_EXECUTION_UNCOVERED,
} PGRAPHVkExecutionRoute;

/* Module presence or an isolated pipeline hit is insufficient for a draw.
 * A ready fallback remains usable even when the speculative queue is full. */
static inline PGRAPHVkExecutionRoute pgraph_vk_hybrid_choose_execution_route(
    bool specialized_shader_ready, bool specialized_pipeline_ready,
    bool fallback_shader_ready, bool fallback_pipeline_ready,
    PGRAPHVkFallbackResourceState fallback_resources)
{
    if (specialized_shader_ready && specialized_pipeline_ready) {
        return PGRAPH_VK_EXECUTION_SPECIALIZED;
    }
    if (fallback_shader_ready && fallback_pipeline_ready) {
        if (fallback_resources == PGRAPH_VK_FALLBACK_RESOURCES_READY) {
            return PGRAPH_VK_EXECUTION_UBERSHADER;
        }
        if (fallback_resources == PGRAPH_VK_FALLBACK_RESOURCES_NEED_ROLLOVER) {
            return PGRAPH_VK_EXECUTION_UBERSHADER_AFTER_ROLLOVER;
        }
    }
    return PGRAPH_VK_EXECUTION_UNCOVERED;
}

/* A partially prepared specialized route is closer than a cold fallback. */
static inline PGRAPHVkFragmentRoute pgraph_vk_hybrid_choose_uncovered_route(
    bool specialized_shader_ready, bool fallback_shader_ready,
    bool fallback_controls_supported)
{
    return fallback_controls_supported && fallback_shader_ready &&
           !specialized_shader_ready ? PGRAPH_VK_FRAGMENT_UBERSHADER :
                                      PGRAPH_VK_FRAGMENT_SPECIALIZED;
}

static inline bool pgraph_vk_hybrid_promotion_due(int64_t now_us,
                                                  int64_t next_probe_us)
{
    return next_probe_us <= 0 || now_us >= next_probe_us;
}

/* Admission mutates the renderer-owned LRU and pins exact publication
 * capacity before submitting expensive driver work. */
static inline PipelineBinding *pgraph_vk_pipeline_cache_reserve(
    Lru *cache, uint64_t hash, const PipelineKey *key)
{
    LruNode *node = lru_try_lookup(cache, hash, key);
    if (!node) {
        return NULL;
    }
    PipelineBinding *binding = container_of(node, PipelineBinding, node);
    if (binding->pipeline != VK_NULL_HANDLE || binding->hybrid_pending) {
        return NULL;
    }
    binding->key = *key;
    binding->hybrid_pending = true;
    return binding;
}

#endif
