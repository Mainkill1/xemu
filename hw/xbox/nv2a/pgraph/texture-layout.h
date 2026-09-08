/*
 * QEMU Geforce NV2A encoded texture layout helpers
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_TEXTURE_LAYOUT_H
#define HW_XBOX_NV2A_PGRAPH_TEXTURE_LAYOUT_H

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

typedef struct TextureShape {
    bool cubemap;
    unsigned int dimensionality;
    unsigned int color_format;
    unsigned int levels;
    unsigned int storage_levels;
    unsigned int width, height, depth;
    bool border;

    unsigned int min_mipmap_level, max_mipmap_level;
    unsigned int pitch;
} TextureShape;

typedef struct PGRAPHTextureMipCrop {
    unsigned int width;
    unsigned int height;
    unsigned int skip_pixels;
    unsigned int skip_rows;
} PGRAPHTextureMipCrop;

static inline PGRAPHTextureMipCrop pgraph_bordered_texture_mip_crop(
    unsigned int base_width, unsigned int base_height,
    unsigned int stored_width, unsigned int stored_height,
    unsigned int level)
{
    unsigned int width = base_width;
    unsigned int height = base_height;
    unsigned int mip_level = level;

    while (level--) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }

    width = MIN(width, stored_width);
    height = MIN(height, stored_height);
    unsigned int border = mip_level < 3 ? 4U >> mip_level : 0;
    unsigned int skip_pixels = MIN(border, stored_width - width);
    unsigned int skip_rows = MIN(border, stored_height - height);

    return (PGRAPHTextureMipCrop) {
        .width = width,
        .height = height,
        .skip_pixels = skip_pixels,
        .skip_rows = skip_rows,
    };
}

typedef struct BasicColorFormatInfo {
    unsigned int bytes_per_pixel;
    bool linear;
    bool depth;
} BasicColorFormatInfo;

extern const BasicColorFormatInfo kelvin_color_format_info_map[66];

/*
 * NV097 texture and palette contexts use an in-memory DMA object backed by
 * NV2A memory. Other classes describe directional DMA engines, and the
 * current texture mapping does not implement alternate target apertures.
 */
static inline bool pgraph_texture_dma_object_valid(unsigned int dma_class,
                                                   unsigned int dma_target)
{
    return dma_class == NV_DMA_IN_MEMORY_CLASS &&
           dma_target == (NV_DMA_TARGET_NVM >> 16);
}

/* DMAObject.limit is inclusive; vram_size is an exclusive upper bound. */
static inline bool pgraph_texture_dma_range_valid(uint64_t base,
                                                  uint64_t object_offset,
                                                  size_t required_length,
                                                  uint64_t dma_limit,
                                                  uint64_t vram_size)
{
    if (required_length &&
        (object_offset > dma_limit ||
         required_length - 1 > dma_limit - object_offset)) {
        return false;
    }
    if (base > vram_size || object_offset > vram_size - base ||
        required_length > vram_size - base - object_offset) {
        return false;
    }
    return true;
}

/* Return the physical dimensions occupied by a texture in guest memory. */
static inline bool pgraph_get_texture_storage_extent(TextureShape shape,
                                                     unsigned int *width,
                                                     unsigned int *height,
                                                     unsigned int *depth)
{
    uint64_t w = shape.width;
    uint64_t h = shape.height;
    uint64_t d = shape.depth;

    if (!width || !height ||
        shape.color_format >= ARRAY_SIZE(kelvin_color_format_info_map)) {
        return false;
    }

    if (!shape.border ||
        kelvin_color_format_info_map[shape.color_format].linear) {
        *width = shape.width;
        *height = shape.height;
        if (depth) {
            *depth = shape.depth;
        }
        return true;
    }

    if (w > UINT_MAX / 2 || h > UINT_MAX / 2 ||
        (depth && shape.dimensionality == 3 && d > UINT_MAX / 2)) {
        return false;
    }

    w = MAX(UINT64_C(16), w * 2);
    h = MAX(UINT64_C(16), h * 2);
    if (depth && shape.dimensionality == 3) {
        d = MAX(UINT64_C(16), d * 2);
    }

    *width = (unsigned int)w;
    *height = (unsigned int)h;
    if (depth) {
        *depth = (unsigned int)d;
    }
    return true;
}

static inline bool pgraph_texture_size_add(size_t *total, uint64_t value)
{
    if (value > SIZE_MAX - *total) {
        return false;
    }
    *total += (size_t)value;
    return true;
}

static inline bool pgraph_texture_size_mul(uint64_t a, uint64_t b,
                                           uint64_t *result)
{
    if (b && a > UINT64_MAX / b) {
        return false;
    }
    *result = a * b;
    return true;
}

/* Return the source span consumed by a supported linear 2D texture. */
static inline bool pgraph_calculate_linear_texture_span(
    unsigned int width, unsigned int height, unsigned int pitch,
    unsigned int bytes_per_pixel, size_t *length)
{
    uint64_t row_bytes, row_prefix;
    size_t total = 0;

    if (!length || !width || !height || !bytes_per_pixel ||
        !pgraph_texture_size_mul(width, bytes_per_pixel, &row_bytes) ||
        row_bytes > SIZE_MAX || pitch < row_bytes ||
        pitch % bytes_per_pixel != 0 ||
        !pgraph_texture_size_mul(height - 1, pitch, &row_prefix) ||
        !pgraph_texture_size_add(&total, row_prefix) ||
        !pgraph_texture_size_add(&total, row_bytes)) {
        return false;
    }

    *length = total;
    return true;
}

/* Packed 4:2:2 formats store chroma for complete two-pixel macropixels. */
static inline bool pgraph_calculate_linear_texture_span_for_format(
    unsigned int color_format, unsigned int width, unsigned int height,
    unsigned int pitch, unsigned int bytes_per_pixel, size_t *length)
{
    uint64_t source_width = width;

    if (color_format ==
            NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 ||
        color_format ==
            NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8) {
        source_width += width & 1;
        if (source_width > UINT_MAX) {
            return false;
        }
    }

    return pgraph_calculate_linear_texture_span(
        source_width, height, pitch, bytes_per_pixel, length);
}

/* Calculate the encoded guest-memory footprint consumed by the decoder. */
static inline bool pgraph_calculate_texture_encoded_size(
    TextureShape shape, bool compressed, unsigned int bytes_per_pixel,
    size_t *length)
{
    unsigned int width, height, depth;
    unsigned int level_count = shape.levels;
    size_t total = 0;
    uint64_t level_size, blocks;
    const uint64_t block_size =
        shape.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5 ?
            8 : 16;

    if (!length ||
        shape.color_format >= ARRAY_SIZE(kelvin_color_format_info_map) ||
        !pgraph_get_texture_storage_extent(shape, &width, &height, &depth)) {
        return false;
    }
    if (!shape.width || !shape.height ||
        (shape.dimensionality == 3 && !shape.depth)) {
        return false;
    }
    if (kelvin_color_format_info_map[shape.color_format].linear) {
        if (shape.cubemap || shape.dimensionality != 2) {
            return false;
        }
        return pgraph_calculate_linear_texture_span_for_format(
            shape.color_format, shape.width, shape.height, shape.pitch,
            bytes_per_pixel, length);
    }
    if (shape.cubemap && shape.storage_levels) {
        level_count = shape.storage_levels;
    }
    if (level_count == 0 || shape.dimensionality < 2 ||
        shape.dimensionality > 3 || !bytes_per_pixel) {
        return false;
    }
    if (shape.cubemap && shape.dimensionality != 2) {
        return false;
    }

    for (unsigned int level = 0; level < level_count; level++) {
        uint64_t w = MAX(width, 1u);
        uint64_t h = MAX(height, 1u);
        uint64_t d = shape.dimensionality >= 3 ? MAX(depth, 1u) : 1;

        if (compressed) {
            if (w > UINT_MAX - 3 || h > UINT_MAX - 3) {
                return false;
            }
            uint64_t physical_width = (w + 3) & ~UINT64_C(3);
            uint64_t physical_height = (h + 3) & ~UINT64_C(3);
            if (!pgraph_texture_size_mul(physical_width / 4,
                                         physical_height / 4, &blocks) ||
                !pgraph_texture_size_mul(blocks, d, &blocks) ||
                !pgraph_texture_size_mul(blocks, block_size, &level_size)) {
                return false;
            }
        } else if (!pgraph_texture_size_mul(w, h, &level_size) ||
                   !pgraph_texture_size_mul(level_size, d, &level_size) ||
                   !pgraph_texture_size_mul(level_size, bytes_per_pixel,
                                            &level_size)) {
            return false;
        }

        if (!pgraph_texture_size_add(&total, level_size)) {
            return false;
        }
        width /= 2;
        height /= 2;
        if (shape.dimensionality >= 3) {
            depth /= 2;
        }
    }

    if (shape.cubemap) {
        const size_t alignment = NV2A_CUBEMAP_FACE_ALIGNMENT;
        if (total > SIZE_MAX - alignment + 1) {
            return false;
        }
        total = (total + alignment - 1) & ~(alignment - 1);
        if (total > SIZE_MAX / 6) {
            return false;
        }
        total *= 6;
    }

    *length = total;
    return true;
}

/* Return the aligned source stride between cubemap faces. */
static inline bool pgraph_calculate_texture_cubemap_face_stride(
    TextureShape shape, bool compressed, unsigned int bytes_per_pixel,
    size_t *stride)
{
    size_t total;

    if (!stride || !shape.cubemap ||
        !pgraph_calculate_texture_encoded_size(
            shape, compressed, bytes_per_pixel, &total) ||
        total % 6) {
        return false;
    }

    *stride = total / 6;
    return true;
}

#endif
