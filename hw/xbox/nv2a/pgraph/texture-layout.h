/*
 * QEMU Geforce NV2A encoded texture layout helpers
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

    return (PGRAPHTextureMipCrop) {
        .width = width,
        .height = height,
        .skip_pixels = MIN(border, stored_width - width),
        .skip_rows = MIN(border, stored_height - height),
    };
}

static inline bool pgraph_cubemap_size_mul(size_t a, size_t b,
                                           size_t *result)
{
    if (b && a > SIZE_MAX / b) {
        return false;
    }
    *result = a * b;
    return true;
}

/* Return the aligned source stride between non-linear cubemap faces. */
static inline bool pgraph_calculate_texture_cubemap_face_stride(
    const TextureShape *shape, bool compressed, unsigned int bytes_per_pixel,
    size_t *stride)
{
    size_t total = 0;
    unsigned int level_count;
    size_t width, height;

    if (!shape || !stride || !shape->cubemap || shape->dimensionality != 2 ||
        !shape->width || !shape->height || !bytes_per_pixel) {
        return false;
    }

    level_count = shape->storage_levels ? shape->storage_levels : shape->levels;
    if (!level_count) {
        return false;
    }

    width = shape->width;
    height = shape->height;
    if (shape->border) {
        if (width > SIZE_MAX / 2 || height > SIZE_MAX / 2) {
            return false;
        }
        width = MAX((size_t)16, width * 2);
        height = MAX((size_t)16, height * 2);
    }

    for (unsigned int level = 0; level < level_count; level++) {
        size_t level_width = MAX(width, (size_t)1);
        size_t level_height = MAX(height, (size_t)1);
        size_t level_size;

        if (compressed) {
            size_t block_size =
                shape->color_format ==
                        NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5 ?
                    8 : 16;
            if (level_width > SIZE_MAX - 3 || level_height > SIZE_MAX - 3) {
                return false;
            }
            level_width = (level_width + 3) & ~(size_t)3;
            level_height = (level_height + 3) & ~(size_t)3;
            if (!pgraph_cubemap_size_mul(level_width / 4, level_height / 4,
                                         &level_size) ||
                !pgraph_cubemap_size_mul(level_size, block_size,
                                         &level_size)) {
                return false;
            }
        } else if (!pgraph_cubemap_size_mul(level_width, level_height,
                                            &level_size) ||
                   !pgraph_cubemap_size_mul(level_size, bytes_per_pixel,
                                            &level_size)) {
            return false;
        }

        if (level_size > SIZE_MAX - total) {
            return false;
        }
        total += level_size;
        width /= 2;
        height /= 2;
    }

    if (total > SIZE_MAX - NV2A_CUBEMAP_FACE_ALIGNMENT + 1) {
        return false;
    }
    total = ROUND_UP(total, NV2A_CUBEMAP_FACE_ALIGNMENT);
    if (total > SIZE_MAX / 6) {
        return false;
    }
    *stride = total;
    return true;
}

typedef struct BasicColorFormatInfo {
    unsigned int bytes_per_pixel;
    bool linear;
    bool depth;
} BasicColorFormatInfo;

extern const BasicColorFormatInfo kelvin_color_format_info_map[66];

#endif
