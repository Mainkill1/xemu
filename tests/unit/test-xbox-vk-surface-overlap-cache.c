#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw/xbox/nv2a/pgraph/vk/surface-overlap-cache.h"

int main(void)
{
    PGRAPHVkSurfaceOverlapCache cache = {0};
    bool overlap = true;

    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                   &overlap));
    pgraph_vk_surface_overlap_cache_store(&cache, 0x1000, 64, false);
    assert(pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                  &overlap));
    assert(!overlap);

    /* A surface entering or leaving the active set invalidates old answers. */
    pgraph_vk_surface_overlap_cache_invalidate(&cache);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                   &overlap));
    pgraph_vk_surface_overlap_cache_store(&cache, 0x1000, 64, true);
    assert(pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                  &overlap));
    assert(overlap);
    pgraph_vk_surface_overlap_cache_invalidate(&cache);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                   &overlap));

    /* Exact address and extent identity must survive direct-map collisions. */
    pgraph_vk_surface_overlap_cache_store(&cache, 0x1000, 64, false);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1001, 64,
                                                   &overlap));
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 65,
                                                   &overlap));
    assert(pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                  &overlap));
    assert(!overlap);

    uint64_t collision = 0;
    for (uint64_t start = 0x1001; start < 0x10000; start++) {
        if (pgraph_vk_surface_overlap_cache_index(start, 64) ==
            pgraph_vk_surface_overlap_cache_index(0x1000, 64)) {
            collision = start;
            break;
        }
    }
    assert(collision);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, collision, 64,
                                                   &overlap));
    pgraph_vk_surface_overlap_cache_store(&cache, collision, 64, true);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                   &overlap));

    cache.generation = UINT64_MAX;
    pgraph_vk_surface_overlap_cache_store(&cache, 0x1000, 64, true);
    pgraph_vk_surface_overlap_cache_invalidate(&cache);
    assert(!pgraph_vk_surface_overlap_cache_lookup(&cache, 0x1000, 64,
                                                   &overlap));

    return 0;
}
