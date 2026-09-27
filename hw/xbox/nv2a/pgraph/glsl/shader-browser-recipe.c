/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader-browser-recipe.h"

typedef struct RecipeWriter {
    uint8_t *data;
    size_t capacity;
    size_t size;
    bool valid;
} RecipeWriter;

static void put_u8(RecipeWriter *writer, uint8_t value)
{
    if (writer->size >= writer->capacity) {
        writer->valid = false;
        return;
    }
    writer->data[writer->size++] = value;
}

static void put_u32(RecipeWriter *writer, uint32_t value)
{
    put_u8(writer, value);
    put_u8(writer, value >> 8);
    put_u8(writer, value >> 16);
    put_u8(writer, value >> 24);
}

static void put_bool(RecipeWriter *writer, bool value)
{
    put_u8(writer, value ? 1 : 0);
}

static void put_vertex_common(RecipeWriter *writer, const VshState *vsh)
{
    put_u32(writer, vsh->compressed_attrs);
    put_u32(writer, vsh->uniform_attrs);
    put_u32(writer, vsh->swizzle_attrs);
    put_bool(writer, vsh->fog_enable);
    put_u32(writer, vsh->fog_mode);
    put_bool(writer, vsh->specular_enable);
    put_bool(writer, vsh->separate_specular);
    put_bool(writer, vsh->ignore_specular_alpha);
    put_bool(writer, vsh->point_params_enable);
    put_bool(writer, vsh->smooth_shading);
    put_bool(writer, vsh->z_perspective);
    /* NV_PGRAPH_POINTSIZE is converted to an exact multiple of 1/8. */
    put_u32(writer, (uint32_t)(vsh->point_size * 8.0f));
}

static void put_fixed_function(RecipeWriter *writer,
                               const FixedFunctionVshState *fixed)
{
    put_bool(writer, fixed->normalization);
    for (size_t i = 0; i < 4; ++i) {
        put_bool(writer, fixed->texture_matrix_enable[i]);
        for (size_t j = 0; j < 4; ++j) {
            put_u32(writer, fixed->texgen[i][j]);
        }
    }
    put_u32(writer, fixed->foggen);
    put_u32(writer, fixed->skinning);
    put_bool(writer, fixed->lighting);
    for (size_t i = 0; i < NV2A_MAX_LIGHTS; ++i) {
        put_u32(writer, fixed->light[i]);
    }
    put_u32(writer, fixed->emission_src);
    put_u32(writer, fixed->ambient_src);
    put_u32(writer, fixed->diffuse_src);
    put_u32(writer, fixed->specular_src);
    put_bool(writer, fixed->local_eye);
}

static void put_pixel(RecipeWriter *writer, const PshState *psh)
{
    put_u32(writer, psh->combiner_control);
    put_u32(writer, psh->shader_stage_program);
    put_u32(writer, psh->other_stage_input);
    put_u32(writer, psh->final_inputs_0);
    put_u32(writer, psh->final_inputs_1);
    for (size_t i = 0; i < 8; ++i) {
        put_u32(writer, psh->rgb_inputs[i]);
        put_u32(writer, psh->rgb_outputs[i]);
        put_u32(writer, psh->alpha_inputs[i]);
        put_u32(writer, psh->alpha_outputs[i]);
    }
    put_bool(writer, psh->point_sprite);
    for (size_t i = 0; i < 4; ++i) {
        put_bool(writer, psh->rect_tex[i]);
        put_bool(writer, psh->snorm_tex[i]);
        for (size_t j = 0; j < 4; ++j) {
            put_bool(writer, psh->compare_mode[i][j]);
        }
        put_bool(writer, psh->alphakill[i]);
        put_u32(writer, psh->colorkey_mode[i]);
        put_u32(writer, psh->conv_tex[i]);
        put_bool(writer, psh->tex_x8y24[i]);
        put_u32(writer, psh->dim_tex[i]);
        put_bool(writer, psh->tex_cubemap[i]);
        for (size_t j = 0; j < 3; ++j) {
            /* These are exact integer dimensions from guest texture format. */
            put_u32(writer, (uint32_t)psh->border_logical_size[i][j]);
        }
        put_bool(writer, psh->shadow_map[i]);
    }
    put_u32(writer, psh->shadow_depth_func);
    put_bool(writer, psh->alpha_test);
    put_u32(writer, psh->alpha_func);
    put_bool(writer, psh->window_clip_exclusive);
    put_bool(writer, psh->smooth_shading);
    put_bool(writer, psh->depth_clipping);
    put_bool(writer, psh->z_perspective);
    put_u32(writer, psh->surface_zeta_format);
    put_u32(writer, psh->depth_format);
}

static void put_geometry(RecipeWriter *writer, const GeomState *geom)
{
    put_u32(writer, geom->primitive_mode);
    put_u32(writer, geom->polygon_front_mode);
    put_u32(writer, geom->polygon_back_mode);
    put_bool(writer, geom->smooth_shading);
    put_bool(writer, geom->first_vertex_is_provoking);
    put_bool(writer, geom->z_perspective);
    put_u32(writer, (uint32_t)(int32_t)geom->tri_rot0);
    put_u32(writer, (uint32_t)(int32_t)geom->tri_rot1);
}

bool pgraph_shader_browser_encode_recipe(const ShaderState *state,
                                         uint32_t stage, uint8_t *data,
                                         size_t capacity, size_t *size)
{
    if (!state || !data || !size || !capacity ||
        capacity > PGRAPH_SHADER_BROWSER_RECIPE_MAX) {
        return false;
    }
    if ((stage == XEMU_SHADER_BROWSER_STAGE_VERTEX &&
         state->vsh.is_fixed_function) ||
        (stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION &&
         !state->vsh.is_fixed_function)) {
        return false;
    }

    RecipeWriter writer = { data, capacity, 0, true };
    put_u8(&writer, 'N');
    put_u8(&writer, 'V');
    put_u8(&writer, '2');
    put_u8(&writer, 'A');
    put_u8(&writer, stage);
    put_u8(&writer, stage == XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION);

    switch (stage) {
    case XEMU_SHADER_BROWSER_STAGE_VERTEX: {
        int length = state->vsh.programmable.program_length;
        if (length < 0 || length > NV2A_MAX_TRANSFORM_PROGRAM_LENGTH) {
            return false;
        }
        put_vertex_common(&writer, &state->vsh);
        put_u32(&writer, length);
        for (int i = 0; i < length; ++i) {
            for (size_t j = 0; j < VSH_TOKEN_SIZE; ++j) {
                put_u32(&writer, state->vsh.programmable.program_data[i][j]);
            }
        }
        break;
    }
    case XEMU_SHADER_BROWSER_STAGE_FIXED_FUNCTION:
        put_vertex_common(&writer, &state->vsh);
        put_fixed_function(&writer, &state->vsh.fixed_function);
        break;
    case XEMU_SHADER_BROWSER_STAGE_PIXEL:
        put_pixel(&writer, &state->psh);
        break;
    case XEMU_SHADER_BROWSER_STAGE_GEOMETRY:
        put_geometry(&writer, &state->geom);
        break;
    default:
        return false;
    }
    if (!writer.valid) {
        return false;
    }
    *size = writer.size;
    return true;
}
