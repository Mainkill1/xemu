/*
 * Canonical Vulkan texture cache identities
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_CACHE_KEY_H
#define HW_XBOX_NV2A_PGRAPH_VK_TEXTURE_CACHE_KEY_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <vulkan/vulkan.h>

/*
 * TextureShape currently also carries sampler LOD clamps.  Keep the storage
 * identity separate so sampler-only state cannot split the image cache.
 * storage_mip_levels is deliberately supplied by the caller: it may be
 * derived from the texture format and dimensions independently of LOD clamps.
 */
typedef struct PGRAPHVkTextureStorageShape {
    bool cubemap;
    uint32_t dimensionality;
    uint32_t color_format;
    uint32_t mip_levels;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    bool border;
    uint32_t pitch;
} PGRAPHVkTextureStorageShape;

typedef struct PGRAPHVkTextureImageKey {
    PGRAPHVkTextureStorageShape shape;
    uint64_t texture_vram_offset;
    uint64_t texture_length;
    uint64_t palette_vram_offset;
    uint64_t palette_length;
    uint32_t scale_bits;
} PGRAPHVkTextureImageKey;

typedef struct PGRAPHVkTextureCustomBorderColor {
    VkFormat format;
    uint32_t value[4];
} PGRAPHVkTextureCustomBorderColor;

/* Canonicalized effective VkSamplerCreateInfo, excluding sType and pNext. */
typedef struct PGRAPHVkTextureSamplerKey {
    VkSamplerCreateFlags flags;
    VkFilter mag_filter;
    VkFilter min_filter;
    VkSamplerMipmapMode mipmap_mode;
    VkSamplerAddressMode address_mode_u;
    VkSamplerAddressMode address_mode_v;
    VkSamplerAddressMode address_mode_w;
    uint32_t mip_lod_bias_bits;
    VkBool32 anisotropy_enable;
    uint32_t max_anisotropy_bits;
    VkBool32 compare_enable;
    VkCompareOp compare_op;
    uint32_t min_lod_bits;
    uint32_t max_lod_bits;
    VkBorderColor border_color;
    VkBool32 unnormalized_coordinates;
    VkFormat custom_border_format;
    uint32_t custom_border_value[4];
} PGRAPHVkTextureSamplerKey;

static inline uint32_t pgraph_vk_texture_float_bits(float value)
{
    uint32_t bits;

    /* Vulkan treats positive and negative zero identically. */
    if (value == 0.0f) {
        return 0;
    }
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline uint32_t pgraph_vk_texture_storage_mip_count(
    bool linear, uint32_t dimensionality, uint32_t declared_levels,
    uint32_t log_width, uint32_t log_height)
{
    uint32_t levels;
    uint32_t max_log;
    uint32_t max_dimension_levels;
    uint32_t dimensional_limit;

    if (linear) {
        return 1;
    }

    max_log = log_width > log_height ? log_width : log_height;
    max_dimension_levels = max_log == UINT32_MAX ? UINT32_MAX : max_log + 1;
    levels = declared_levels < max_dimension_levels ? declared_levels :
                                                     max_dimension_levels;
    if (levels == 0) {
        levels = 1;
    }

    if (dimensionality == 3) {
        if (log_width < 2 || log_height < 2) {
            return 1;
        }

        dimensional_limit =
            (log_width < log_height ? log_width : log_height) - 1;
        levels = levels < dimensional_limit ? levels : dimensional_limit;
    }

    return levels;
}

static inline void pgraph_vk_texture_storage_shape_init(
    PGRAPHVkTextureStorageShape *storage, bool cubemap,
    uint32_t dimensionality, uint32_t color_format,
    uint32_t storage_mip_levels, uint32_t width, uint32_t height,
    uint32_t depth, bool border, uint32_t pitch)
{
    memset(storage, 0, sizeof(*storage));
    storage->cubemap = cubemap;
    storage->dimensionality = dimensionality;
    storage->color_format = color_format;
    storage->mip_levels = storage_mip_levels;
    storage->width = width;
    storage->height = height;
    storage->depth = depth;
    storage->border = border;
    storage->pitch = pitch;
}

static inline bool pgraph_vk_texture_storage_shape_equal(
    const PGRAPHVkTextureStorageShape *a,
    const PGRAPHVkTextureStorageShape *b)
{
    return a->cubemap == b->cubemap &&
           a->dimensionality == b->dimensionality &&
           a->color_format == b->color_format &&
           a->mip_levels == b->mip_levels &&
           a->width == b->width &&
           a->height == b->height &&
           a->depth == b->depth &&
           a->border == b->border &&
           a->pitch == b->pitch;
}

static inline void pgraph_vk_texture_image_key_init(
    PGRAPHVkTextureImageKey *key,
    const PGRAPHVkTextureStorageShape *storage,
    uint64_t texture_vram_offset, uint64_t texture_length,
    uint64_t palette_vram_offset, uint64_t palette_length, float scale)
{
    memset(key, 0, sizeof(*key));
    pgraph_vk_texture_storage_shape_init(
        &key->shape, storage->cubemap, storage->dimensionality,
        storage->color_format, storage->mip_levels, storage->width,
        storage->height, storage->depth, storage->border, storage->pitch);
    key->texture_vram_offset = texture_vram_offset;
    key->texture_length = texture_length;
    key->palette_vram_offset = palette_vram_offset;
    key->palette_length = palette_length;
    key->scale_bits = pgraph_vk_texture_float_bits(scale);
}

static inline bool pgraph_vk_texture_image_key_equal(
    const PGRAPHVkTextureImageKey *a, const PGRAPHVkTextureImageKey *b)
{
    return pgraph_vk_texture_storage_shape_equal(&a->shape, &b->shape) &&
           a->texture_vram_offset == b->texture_vram_offset &&
           a->texture_length == b->texture_length &&
           a->palette_vram_offset == b->palette_vram_offset &&
           a->palette_length == b->palette_length &&
           a->scale_bits == b->scale_bits;
}

static inline bool pgraph_vk_texture_sampler_uses_border(
    const VkSamplerCreateInfo *info)
{
    return info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
           info->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
           info->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
}

static inline bool pgraph_vk_texture_sampler_has_custom_border(
    VkBorderColor border_color)
{
    return border_color == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ||
           border_color == VK_BORDER_COLOR_INT_CUSTOM_EXT;
}

/*
 * Build an identity from resolved Vulkan sampler state.  Irrelevant values
 * are canonicalized so semantically equivalent create infos coalesce.  The
 * renderer's sole supported sampler pNext payload, custom border color, is
 * supplied explicitly; pNext pointer identity itself is intentionally ignored.
 */
static inline bool pgraph_vk_texture_sampler_key_init(
    PGRAPHVkTextureSamplerKey *key, const VkSamplerCreateInfo *info,
    const PGRAPHVkTextureCustomBorderColor *custom_border)
{
    bool uses_border;

    if (!key || !info) {
        return false;
    }

    memset(key, 0, sizeof(*key));
    key->flags = info->flags;
    key->mag_filter = info->magFilter;
    key->min_filter = info->minFilter;
    key->mipmap_mode = info->mipmapMode;
    key->address_mode_u = info->addressModeU;
    key->address_mode_v = info->addressModeV;
    key->address_mode_w = info->addressModeW;
    key->mip_lod_bias_bits = pgraph_vk_texture_float_bits(info->mipLodBias);
    key->anisotropy_enable = !!info->anisotropyEnable;
    if (key->anisotropy_enable) {
        key->max_anisotropy_bits =
            pgraph_vk_texture_float_bits(info->maxAnisotropy);
    }
    key->compare_enable = !!info->compareEnable;
    key->compare_op = key->compare_enable ? info->compareOp :
                                           VK_COMPARE_OP_ALWAYS;
    key->min_lod_bits = pgraph_vk_texture_float_bits(info->minLod);
    key->max_lod_bits = pgraph_vk_texture_float_bits(info->maxLod);
    key->unnormalized_coordinates = !!info->unnormalizedCoordinates;

    uses_border = pgraph_vk_texture_sampler_uses_border(info);
    key->border_color = uses_border ? info->borderColor :
                                      VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (uses_border &&
        pgraph_vk_texture_sampler_has_custom_border(info->borderColor)) {
        if (!custom_border) {
            return false;
        }
        key->custom_border_format = custom_border->format;
        memcpy(key->custom_border_value, custom_border->value,
               sizeof(key->custom_border_value));
    }

    return true;
}

static inline bool pgraph_vk_texture_sampler_key_equal(
    const PGRAPHVkTextureSamplerKey *a,
    const PGRAPHVkTextureSamplerKey *b)
{
    return a->flags == b->flags &&
           a->mag_filter == b->mag_filter &&
           a->min_filter == b->min_filter &&
           a->mipmap_mode == b->mipmap_mode &&
           a->address_mode_u == b->address_mode_u &&
           a->address_mode_v == b->address_mode_v &&
           a->address_mode_w == b->address_mode_w &&
           a->mip_lod_bias_bits == b->mip_lod_bias_bits &&
           a->anisotropy_enable == b->anisotropy_enable &&
           a->max_anisotropy_bits == b->max_anisotropy_bits &&
           a->compare_enable == b->compare_enable &&
           a->compare_op == b->compare_op &&
           a->min_lod_bits == b->min_lod_bits &&
           a->max_lod_bits == b->max_lod_bits &&
           a->border_color == b->border_color &&
           a->unnormalized_coordinates == b->unnormalized_coordinates &&
           a->custom_border_format == b->custom_border_format &&
           memcmp(a->custom_border_value, b->custom_border_value,
                  sizeof(a->custom_border_value)) == 0;
}

#endif
