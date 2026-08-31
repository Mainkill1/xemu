/*
 * NV2A texture storage mipmap derivation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_TEXTURE_MIPMAP_H
#define HW_XBOX_NV2A_PGRAPH_TEXTURE_MIPMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PGRAPHTextureMipLevels {
    uint32_t storage_levels;
    uint32_t sampler_min_level;
    uint32_t sampler_max_level;
} PGRAPHTextureMipLevels;

/*
 * Storage allocation follows the format and dimensions only.  LOD clamps are
 * resolved separately and must never truncate the guest mip chain.
 */
static inline PGRAPHTextureMipLevels pgraph_texture_mip_levels_derive(
    bool linear, uint32_t dimensionality, uint32_t declared_levels,
    uint32_t log_width, uint32_t log_height, uint32_t min_lod_clamp,
    uint32_t max_lod_clamp)
{
    PGRAPHTextureMipLevels result;
    uint32_t max_log;
    uint32_t dimension_levels;

    if (linear) {
        return (PGRAPHTextureMipLevels) {
            .storage_levels = 1,
            /* Linear textures do not use mipmapped sampling. */
            .sampler_min_level = min_lod_clamp,
            .sampler_max_level = max_lod_clamp,
        };
    }

    max_log = log_width > log_height ? log_width : log_height;
    dimension_levels = max_log == UINT32_MAX ? UINT32_MAX : max_log + 1;
    result.storage_levels = declared_levels < dimension_levels ?
                                declared_levels : dimension_levels;
    if (result.storage_levels == 0) {
        result.storage_levels = 1;
    }

    if (dimensionality == 3) {
        if (log_width < 2 || log_height < 2) {
            result.storage_levels = 1;
        } else {
            uint32_t dimensional_limit =
                (log_width < log_height ? log_width : log_height) - 1;
            if (result.storage_levels > dimensional_limit) {
                result.storage_levels = dimensional_limit;
            }
        }
    }

    result.sampler_max_level = max_lod_clamp < result.storage_levels ?
                                   max_lod_clamp : result.storage_levels - 1;
    result.sampler_min_level = min_lod_clamp < result.sampler_max_level ?
                                   min_lod_clamp : result.sampler_max_level;
    return result;
}

static inline bool pgraph_texture_size_mul(size_t a, size_t b, size_t *result)
{
    if (a != 0 && b > SIZE_MAX / a) {
        return false;
    }
    *result = a * b;
    return true;
}

static inline bool pgraph_texture_size_add(size_t a, size_t b, size_t *result)
{
    if (b > SIZE_MAX - a) {
        return false;
    }
    *result = a + b;
    return true;
}

/* Exact 2D chain length used by live texture-length calculation and tests. */
static inline bool pgraph_texture_mip_chain_2d_length(
    uint32_t width, uint32_t height, uint32_t levels,
    uint32_t bytes_per_pixel, uint32_t compressed_block_size, size_t *length)
{
    size_t total = 0;

    if (!length || levels == 0 ||
        (bytes_per_pixel == 0 && compressed_block_size == 0)) {
        return false;
    }

    for (uint32_t level = 0; level < levels; level++) {
        size_t level_size;
        width = width > 0 ? width : 1;
        height = height > 0 ? height : 1;

        if (compressed_block_size) {
            if (width > UINT32_MAX - 3 || height > UINT32_MAX - 3) {
                return false;
            }
            size_t blocks_w = ((width + 3) & ~3U) / 4;
            size_t blocks_h = ((height + 3) & ~3U) / 4;
            if (!pgraph_texture_size_mul(blocks_w, blocks_h, &level_size) ||
                !pgraph_texture_size_mul(level_size, compressed_block_size,
                                         &level_size)) {
                return false;
            }
        } else if (!pgraph_texture_size_mul(width, height, &level_size) ||
                   !pgraph_texture_size_mul(level_size, bytes_per_pixel,
                                            &level_size)) {
            return false;
        }

        if (!pgraph_texture_size_add(total, level_size, &total)) {
            return false;
        }
        width /= 2;
        height /= 2;
    }

    *length = total;
    return true;
}

#endif
