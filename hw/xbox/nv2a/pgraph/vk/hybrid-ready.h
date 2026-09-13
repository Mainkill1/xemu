/*
 * Non-creating Vulkan hybrid execution probes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_XBOX_NV2A_PGRAPH_VK_HYBRID_READY_H
#define HW_XBOX_NV2A_PGRAPH_VK_HYBRID_READY_H

#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

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

#endif
