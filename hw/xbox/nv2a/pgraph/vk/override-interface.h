/*
 * Vulkan replacement fragment uniform upload compatibility
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_NV2A_PGRAPH_VK_OVERRIDE_INTERFACE_H
#define HW_XBOX_NV2A_PGRAPH_VK_OVERRIDE_INTERFACE_H

#include "renderer.h"

static inline bool
pgraph_vk_override_matrix_stride_supported(const GByteArray *spirv,
                                           const SpvReflectBlockVariable *block,
                                           uint32_t member_index)
{
    const SpvReflectTypeDescription *type = block->type_description;
    if (!spirv || !spirv->data || spirv->len < 5 * sizeof(uint32_t) ||
        spirv->len % sizeof(uint32_t) || !type) {
        return false;
    }
    if (type->op != SpvOpTypeStruct) {
        type = type->struct_type_description;
    }
    if (!type || type->op != SpvOpTypeStruct) {
        return false;
    }
    const uint32_t *words = (const uint32_t *)spirv->data;
    const size_t count = spirv->len / sizeof(uint32_t);
    if (words[0] != SpvMagicNumber) {
        return false;
    }
    bool stride_seen = false, column_major_seen = false;
    for (size_t i = 5; i < count;) {
        const uint32_t length = words[i] >> 16;
        if (!length || length > count - i) {
            return false;
        }
        if ((words[i] & 0xffff) == SpvOpMemberDecorate && length >= 4 &&
            words[i + 1] == type->id && words[i + 2] == member_index) {
            if (words[i + 3] == SpvDecorationMatrixStride) {
                if (length != 5 || stride_seen || words[i + 4] != 16) {
                    return false;
                }
                stride_seen = true;
            } else if (words[i + 3] == SpvDecorationColMajor) {
                if (length != 4 || column_major_seen) {
                    return false;
                }
                column_major_seen = true;
            } else if (words[i + 3] == SpvDecorationRowMajor) {
                return false;
            }
        }
        i += length;
    }
    return stride_seen && column_major_seen;
}

static inline bool
pgraph_vk_override_uniform_block_supported(const SpvReflectBlockVariable *block,
                                           const ShaderUniformLayout *layout,
                                           const GByteArray *spirv)
{
    if (!block->member_count || block->member_count > PshUniform__COUNT ||
        !block->members || block->member_count != layout->num_uniforms ||
        !layout->uniforms || !layout->allocation ||
        block->size != layout->total_size) {
        return false;
    }
    /* Bound padding as well as payload using the complete generated ABI. */
    size_t maximum_size = 0;
    for (size_t i = 0; i < PshUniform__COUNT; ++i) {
        const UniformInfo *info = &PshUniformInfo[i];
        const size_t stride = info->type == UniformElementType_mat2 ?
                                  32 :
                                  ROUND_UP(info->size, 16);
        maximum_size += stride * info->count;
    }
    if (block->size > maximum_size) {
        return false;
    }
    bool seen[PshUniform__COUNT] = { false };
    for (uint32_t i = 0; i < block->member_count; ++i) {
        const SpvReflectBlockVariable *member = &block->members[i];
        const ShaderUniform *uniform = &layout->uniforms[i];
        const SpvReflectTypeDescription *type = member->type_description;
        if (!member->name || !uniform->name || !type || member->member_count) {
            return false;
        }
        size_t index;
        for (index = 0; index < PshUniform__COUNT; ++index) {
            if (!strcmp(member->name, PshUniformInfo[index].name)) {
                break;
            }
        }
        if (index == PshUniform__COUNT || seen[index]) {
            return false;
        }
        seen[index] = true;
        const UniformInfo *info = &PshUniformInfo[index];
        uint32_t components = info->size / sizeof(uint32_t);
        uint32_t columns = 0;
        SpvReflectTypeFlags scalar_type = SPV_REFLECT_TYPE_FLAG_FLOAT;
        uint32_t signedness = 0;
        switch (info->type) {
        case UniformElementType_int:
        case UniformElementType_ivec2:
        case UniformElementType_ivec4:
            scalar_type = SPV_REFLECT_TYPE_FLAG_INT;
            signedness = 1;
            break;
        case UniformElementType_uint:
            scalar_type = SPV_REFLECT_TYPE_FLAG_INT;
            break;
        case UniformElementType_mat2:
            components = columns = 2;
            break;
        case UniformElementType_float:
        case UniformElementType_vec2:
        case UniformElementType_vec3:
        case UniformElementType_vec4:
            break;
        default:
            return false;
        }
        const uint32_t array_dimensions = info->count > 1 ? 1 : 0;
        const size_t elements = info->count * (columns ? columns : 1);
        const size_t stride = elements > 1 ? 16 : 0;
        const size_t array_stride =
            array_dimensions ? 16 * (columns ? columns : 1) : 0;
        const size_t member_size = columns          ? elements * 16 :
                                   array_dimensions ? info->count * 16 :
                                                      info->size;
        const uint32_t alignment =
            columns || array_dimensions || components >= 3 ?
                16 :
                components * sizeof(uint32_t);
        if ((type->type_flags &
             (SPV_REFLECT_TYPE_FLAG_BOOL | SPV_REFLECT_TYPE_FLAG_INT |
              SPV_REFLECT_TYPE_FLAG_FLOAT | SPV_REFLECT_TYPE_FLAG_STRUCT)) !=
                scalar_type ||
            member->numeric.scalar.width != 32 ||
            (scalar_type == SPV_REFLECT_TYPE_FLAG_INT &&
             member->numeric.scalar.signedness != signedness) ||
            member->numeric.vector.component_count !=
                (components == 1 ? 0 : components) ||
            member->numeric.matrix.column_count != columns ||
            member->numeric.matrix.row_count != (columns ? components : 0) ||
            (member->numeric.matrix.stride &&
             member->numeric.matrix.stride != (columns ? 16 : 0)) ||
            (columns &&
             ((member->decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR) ||
              !(member->decoration_flags &
                SPV_REFLECT_DECORATION_COLUMN_MAJOR) ||
              // Pinned Reflect drops MatrixStride through OpTypeArray.
              !pgraph_vk_override_matrix_stride_supported(spirv, block, i))) ||
            member->array.dims_count != array_dimensions ||
            (array_dimensions && member->array.dims[0] != info->count) ||
            member->array.stride != array_stride ||
            member->size != member_size || member->offset % alignment ||
            member->offset > block->size ||
            member_size > block->size - member->offset ||
            strcmp(uniform->name, member->name) ||
            uniform->offset != member->offset || uniform->dim_v != components ||
            uniform->dim_a != elements || uniform->stride != stride) {
            return false;
        }
        for (uint32_t previous = 0; previous < i; ++previous) {
            const SpvReflectBlockVariable *other = &block->members[previous];
            if (member->offset < other->offset + other->size &&
                other->offset < member->offset + member->size) {
                return false;
            }
        }
    }
    return true;
}

/* Validate every reflected resource before the generic guest uniform writer
 * or graphics pipeline can consume an authored fragment. */
static inline bool
pgraph_vk_override_fragment_layout_supported(const ShaderModuleInfo *module)
{
    if (!module || !module->reflect_module_initialized ||
        module->uses_uber_controls ||
        module->reflect_module.push_constant_block_count) {
        return false;
    }
    uint32_t count = 0;
    if (spvReflectEnumerateDescriptorSets(
            (SpvReflectShaderModule *)&module->reflect_module, &count, NULL) !=
            SPV_REFLECT_RESULT_SUCCESS ||
        (count && !module->descriptor_sets)) {
        return false;
    }
    bool uniform_block_seen = false;
    for (uint32_t i = 0; i < count; ++i) {
        const SpvReflectDescriptorSet *set = module->descriptor_sets[i];
        if (!set || set->set != 0 || (set->binding_count && !set->bindings)) {
            return false;
        }
        for (uint32_t j = 0; j < set->binding_count; ++j) {
            const SpvReflectDescriptorBinding *binding = set->bindings[j];
            if (!binding || binding->count != 1) {
                return false;
            }
            if (binding->binding == PSH_UBO_BINDING) {
                if (uniform_block_seen ||
                    binding->descriptor_type !=
                        SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    !pgraph_vk_override_uniform_block_supported(
                        &binding->block, &module->uniforms, module->spirv)) {
                    return false;
                }
                uniform_block_seen = true;
            } else if (binding->binding < PSH_TEX_BINDING ||
                       binding->binding >=
                           PSH_TEX_BINDING + NV2A_MAX_TEXTURES ||
                       binding->descriptor_type !=
                           SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                return false;
            }
        }
    }
    return uniform_block_seen ||
           (!module->uniforms.num_uniforms && !module->uniforms.total_size);
}

#endif
