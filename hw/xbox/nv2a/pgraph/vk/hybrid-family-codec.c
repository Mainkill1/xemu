/*
 * Geforce NV2A PGRAPH Vulkan hybrid family key codec
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hw/xbox/nv2a/pgraph/vk/hybrid-family-codec.h"

#include <stdlib.h>
#include <string.h>

#define FAMILY_KEY_MAGIC 0x4b465358U /* XSFK */
#define FAMILY_KEY_HEADER_SIZE 24U

enum {
    HEADER_MAGIC = 0,
    HEADER_ABI = 4,
    HEADER_SIZE = 8,
    HEADER_RESERVED = 12,
    HEADER_PAYLOAD_HASH = 16,
};

typedef struct FamilyKeyCodec {
    uint8_t *data;
    size_t size;
    size_t offset;
    bool reading;
} FamilyKeyCodec;

static uint32_t load_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] | (uint32_t)src[1] << 8 |
           (uint32_t)src[2] << 16 | (uint32_t)src[3] << 24;
}

static uint64_t load_u64_le(const uint8_t *src)
{
    return (uint64_t)load_u32_le(src) |
           (uint64_t)load_u32_le(src + 4) << 32;
}

static void store_u32_le(uint8_t *dst, uint32_t value)
{
    dst[0] = value;
    dst[1] = value >> 8;
    dst[2] = value >> 16;
    dst[3] = value >> 24;
}

static void store_u64_le(uint8_t *dst, uint64_t value)
{
    store_u32_le(dst, value);
    store_u32_le(dst + 4, value >> 32);
}

static uint64_t family_key_hash(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool codec_u32(FamilyKeyCodec *codec, uint32_t *value)
{
    if (codec->size - codec->offset < sizeof(*value)) {
        return false;
    }
    if (codec->reading) {
        *value = load_u32_le(codec->data + codec->offset);
    } else {
        store_u32_le(codec->data + codec->offset, *value);
    }
    codec->offset += sizeof(*value);
    return true;
}

#define FIELD_U32(field)                                                   \
    do {                                                                   \
        uint32_t value_ = codec->reading ? 0 : (uint32_t)(field);          \
        if (!codec_u32(codec, &value_)) {                                  \
            return false;                                                  \
        }                                                                  \
        if (codec->reading) {                                              \
            (field) = value_;                                              \
        }                                                                  \
    } while (0)

#define FIELD_I32(field)                                                   \
    do {                                                                   \
        uint32_t value_ = codec->reading ? 0 : (uint32_t)(int32_t)(field); \
        if (!codec_u32(codec, &value_)) {                                  \
            return false;                                                  \
        }                                                                  \
        if (codec->reading) {                                              \
            (field) = (int32_t)value_;                                     \
        }                                                                  \
    } while (0)

#define FIELD_BOOL(field)                                                  \
    do {                                                                   \
        uint32_t value_ = codec->reading ? 0 : !!(field);                  \
        if (!codec_u32(codec, &value_) || value_ > 1) {                    \
            return false;                                                  \
        }                                                                  \
        if (codec->reading) {                                              \
            (field) = value_;                                              \
        }                                                                  \
    } while (0)

#define FIELD_FLOAT(field)                                                 \
    do {                                                                   \
        uint32_t value_ = 0;                                               \
        if (!codec->reading) {                                             \
            memcpy(&value_, &(field), sizeof(value_));                     \
        }                                                                  \
        if (!codec_u32(codec, &value_)) {                                  \
            return false;                                                  \
        }                                                                  \
        if (codec->reading) {                                              \
            memcpy(&(field), &value_, sizeof(value_));                     \
        }                                                                  \
    } while (0)

static bool visit_vsh(FamilyKeyCodec *codec, VshState *state)
{
    FIELD_U32(state->surface_scale_factor);
    FIELD_U32(state->compressed_attrs);
    FIELD_U32(state->uniform_attrs);
    FIELD_U32(state->swizzle_attrs);
    FIELD_BOOL(state->fog_enable);
    FIELD_I32(state->fog_mode);
    FIELD_BOOL(state->specular_enable);
    FIELD_BOOL(state->separate_specular);
    FIELD_BOOL(state->ignore_specular_alpha);
    FIELD_FLOAT(state->specular_power);
    FIELD_FLOAT(state->specular_power_back);
    FIELD_BOOL(state->point_params_enable);
    FIELD_FLOAT(state->point_size);
    for (size_t i = 0; i < ARRAY_SIZE(state->point_params); i++) {
        FIELD_FLOAT(state->point_params[i]);
    }
    FIELD_BOOL(state->smooth_shading);
    FIELD_BOOL(state->z_perspective);
    FIELD_BOOL(state->is_fixed_function);

    FixedFunctionVshState *fixed = &state->fixed_function;
    FIELD_BOOL(fixed->normalization);
    for (size_t i = 0; i < ARRAY_SIZE(fixed->texture_matrix_enable); i++) {
        FIELD_BOOL(fixed->texture_matrix_enable[i]);
    }
    for (size_t i = 0; i < ARRAY_SIZE(fixed->texgen); i++) {
        for (size_t j = 0; j < ARRAY_SIZE(fixed->texgen[i]); j++) {
            FIELD_I32(fixed->texgen[i][j]);
        }
    }
    FIELD_I32(fixed->foggen);
    FIELD_I32(fixed->skinning);
    FIELD_BOOL(fixed->lighting);
    for (size_t i = 0; i < ARRAY_SIZE(fixed->light); i++) {
        FIELD_I32(fixed->light[i]);
    }
    FIELD_I32(fixed->emission_src);
    FIELD_I32(fixed->ambient_src);
    FIELD_I32(fixed->diffuse_src);
    FIELD_I32(fixed->specular_src);
    FIELD_BOOL(fixed->local_eye);

    ProgrammableVshState *programmable = &state->programmable;
    for (size_t i = 0; i < ARRAY_SIZE(programmable->program_data); i++) {
        for (size_t j = 0; j < ARRAY_SIZE(programmable->program_data[i]); j++) {
            FIELD_U32(programmable->program_data[i][j]);
        }
    }
    FIELD_I32(programmable->program_length);
    return true;
}

static bool visit_geom(FamilyKeyCodec *codec, GeomState *state)
{
    FIELD_I32(state->primitive_mode);
    FIELD_I32(state->polygon_front_mode);
    FIELD_I32(state->polygon_back_mode);
    FIELD_BOOL(state->smooth_shading);
    FIELD_BOOL(state->first_vertex_is_provoking);
    FIELD_BOOL(state->z_perspective);
    FIELD_I32(state->tri_rot0);
    FIELD_I32(state->tri_rot1);
    return true;
}

static bool visit_psh(FamilyKeyCodec *codec, PshState *state)
{
    FIELD_U32(state->combiner_control);
    FIELD_U32(state->shader_stage_program);
    FIELD_U32(state->other_stage_input);
    FIELD_U32(state->final_inputs_0);
    FIELD_U32(state->final_inputs_1);
    for (size_t i = 0; i < ARRAY_SIZE(state->rgb_inputs); i++) {
        FIELD_U32(state->rgb_inputs[i]);
        FIELD_U32(state->rgb_outputs[i]);
        FIELD_U32(state->alpha_inputs[i]);
        FIELD_U32(state->alpha_outputs[i]);
    }
    FIELD_BOOL(state->point_sprite);
    for (size_t i = 0; i < ARRAY_SIZE(state->rect_tex); i++) {
        FIELD_BOOL(state->rect_tex[i]);
        FIELD_BOOL(state->snorm_tex[i]);
        for (size_t j = 0; j < ARRAY_SIZE(state->compare_mode[i]); j++) {
            FIELD_BOOL(state->compare_mode[i][j]);
        }
        FIELD_BOOL(state->alphakill[i]);
        FIELD_I32(state->colorkey_mode[i]);
        FIELD_I32(state->conv_tex[i]);
        FIELD_BOOL(state->tex_x8y24[i]);
        FIELD_I32(state->dim_tex[i]);
        FIELD_BOOL(state->tex_cubemap[i]);
        for (size_t j = 0; j < ARRAY_SIZE(state->border_logical_size[i]); j++) {
            FIELD_FLOAT(state->border_logical_size[i][j]);
            FIELD_FLOAT(state->border_inv_real_size[i][j]);
        }
        FIELD_BOOL(state->shadow_map[i]);
    }
    FIELD_I32(state->shadow_depth_func);
    FIELD_BOOL(state->alpha_test);
    FIELD_I32(state->alpha_func);
    FIELD_BOOL(state->window_clip_exclusive);
    FIELD_BOOL(state->smooth_shading);
    FIELD_BOOL(state->depth_clipping);
    FIELD_BOOL(state->z_perspective);
    FIELD_U32(state->surface_zeta_format);
    FIELD_I32(state->depth_format);
    return true;
}

static bool visit_pipeline_key(FamilyKeyCodec *codec, PipelineKey *key)
{
    FIELD_BOOL(key->clear);
    FIELD_I32(key->fragment_route);
    FIELD_I32(key->render_pass_state.color_format);
    FIELD_I32(key->render_pass_state.zeta_format);
    if (!visit_vsh(codec, &key->shader_state.vsh) ||
        !visit_geom(codec, &key->shader_state.geom) ||
        !visit_psh(codec, &key->shader_state.psh)) {
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(key->regs); i++) {
        FIELD_U32(key->regs[i]);
    }
    FIELD_U32(key->binding_description_count);
    FIELD_U32(key->attribute_description_count);
    if (key->binding_description_count > ARRAY_SIZE(key->binding_descriptions) ||
        key->attribute_description_count >
            ARRAY_SIZE(key->attribute_descriptions)) {
        return false;
    }
    for (size_t i = 0; i < key->binding_description_count; i++) {
        FIELD_U32(key->binding_descriptions[i].binding);
        FIELD_U32(key->binding_descriptions[i].stride);
        FIELD_I32(key->binding_descriptions[i].inputRate);
    }
    for (size_t i = 0; i < key->attribute_description_count; i++) {
        FIELD_U32(key->attribute_descriptions[i].location);
        FIELD_U32(key->attribute_descriptions[i].binding);
        FIELD_I32(key->attribute_descriptions[i].format);
        FIELD_U32(key->attribute_descriptions[i].offset);
    }
    return !key->clear &&
           (key->fragment_route == PGRAPH_VK_FRAGMENT_SPECIALIZED ||
            key->fragment_route == PGRAPH_VK_FRAGMENT_UBERSHADER);
}

bool pgraph_vk_family_key_encode(const PipelineKey *key,
                                 PGRAPHVkFamilyKeyBlob *blob)
{
    if (!key || !blob) {
        return false;
    }
    uint8_t *data = calloc(1, PGRAPH_VK_FAMILY_KEY_MAX_SIZE);
    if (!data) {
        return false;
    }
    PipelineKey copy = *key;
    FamilyKeyCodec codec = {
        .data = data,
        .size = PGRAPH_VK_FAMILY_KEY_MAX_SIZE,
        .offset = FAMILY_KEY_HEADER_SIZE,
    };
    if (!visit_pipeline_key(&codec, &copy) || codec.offset > UINT32_MAX) {
        free(data);
        return false;
    }
    store_u32_le(data + HEADER_MAGIC, FAMILY_KEY_MAGIC);
    store_u32_le(data + HEADER_ABI, PGRAPH_VK_FAMILY_KEY_ABI);
    store_u32_le(data + HEADER_SIZE, codec.offset);
    store_u32_le(data + HEADER_RESERVED, 0);
    store_u64_le(data + HEADER_PAYLOAD_HASH,
                 family_key_hash(data + FAMILY_KEY_HEADER_SIZE,
                                 codec.offset - FAMILY_KEY_HEADER_SIZE));
    uint8_t *compact = realloc(data, codec.offset);
    *blob = (PGRAPHVkFamilyKeyBlob) {
        .data = compact ? compact : data,
        .size = codec.offset,
    };
    return true;
}

bool pgraph_vk_family_key_decode(const uint8_t *data, size_t size,
                                 PipelineKey *key)
{
    if (!data || !key || size < FAMILY_KEY_HEADER_SIZE ||
        size > PGRAPH_VK_FAMILY_KEY_MAX_SIZE ||
        load_u32_le(data + HEADER_MAGIC) != FAMILY_KEY_MAGIC ||
        load_u32_le(data + HEADER_ABI) != PGRAPH_VK_FAMILY_KEY_ABI ||
        load_u32_le(data + HEADER_SIZE) != size ||
        load_u32_le(data + HEADER_RESERVED) != 0 ||
        load_u64_le(data + HEADER_PAYLOAD_HASH) !=
            family_key_hash(data + FAMILY_KEY_HEADER_SIZE,
                            size - FAMILY_KEY_HEADER_SIZE)) {
        return false;
    }
    PipelineKey decoded = { 0 };
    FamilyKeyCodec codec = {
        .data = (uint8_t *)data,
        .size = size,
        .offset = FAMILY_KEY_HEADER_SIZE,
        .reading = true,
    };
    if (!visit_pipeline_key(&codec, &decoded) || codec.offset != size) {
        return false;
    }
    *key = decoded;
    return true;
}

void pgraph_vk_family_key_blob_destroy(PGRAPHVkFamilyKeyBlob *blob)
{
    if (!blob) {
        return;
    }
    free(blob->data);
    *blob = (PGRAPHVkFamilyKeyBlob) { 0 };
}
