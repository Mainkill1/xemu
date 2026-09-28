/*
 * Vulkan replacement fragment ABI and uniform upload regression tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/override-interface.h"
#include "hw/xbox/nv2a/pgraph/vk/device-inventory.h"

static ShaderModuleInfo compile_fragment(const char *source)
{
    const PGRAPHVkGlslCompileConfig config = {
        .api_version = VK_API_VERSION_1_1,
    };
    ShaderModuleInfo module = { 0 };
    module.spirv = pgraph_vk_compile_glsl_to_spv_config(
        &config, GLSLANG_STAGE_FRAGMENT, source);
    g_assert_nonnull(module.spirv);
    return module;
}

static void clear_fragment(ShaderModuleInfo *module)
{
    pgraph_vk_clear_shader_module_layout(module);
    g_byte_array_unref(module->spirv);
}

static void check_uploads(ShaderModuleInfo *module)
{
    PshUniformValues values;
    memset(&values, 0x42, sizeof(values));
    for (size_t i = 0; i < PshUniform__COUNT; ++i) {
        const UniformInfo *info = &PshUniformInfo[i];
        int loc = uniform_index(&module->uniforms, info->name);
        if (loc == -1) {
            continue;
        }
        ShaderUniform *uniform = &module->uniforms.uniforms[loc - 1];
        const uint8_t *source = (const uint8_t *)&values + info->val_offs;
        const size_t words = info->size * info->count / sizeof(uint32_t);
        g_assert_true(uniform_copy(&module->uniforms, loc, (void *)source,
                                   sizeof(uint32_t), words));
        for (size_t element = 0; element < uniform->dim_a; ++element) {
            const size_t bytes = uniform->dim_v * sizeof(uint32_t);
            const uint8_t *actual =
                (const uint8_t *)module->uniforms.allocation + uniform->offset +
                element * uniform->stride;
            g_assert_cmpmem(actual, bytes, source + element * bytes, bytes);
        }
        g_assert_false(uniform_copy(&module->uniforms, loc, (void *)source,
                                    sizeof(uint32_t), words));
    }
}

static void test_generated_fragment_uploads(void)
{
    PshState state = {
        .combiner_control = 1 | (PS_COMBINERCOUNT_MUX_MSB << 8),
        .final_inputs_0 = 0x20202020,
        .final_inputs_1 = 0x20202000,
        .smooth_shading = true,
        .depth_format = DEPTH_FORMAT_D24,
    };
    GenPshGlslOptions opts = {
        .vulkan = true,
        .ubo_binding = PSH_UBO_BINDING,
        .tex_binding = PSH_TEX_BINDING,
    };
    MString *source = pgraph_glsl_gen_psh(&state, opts);
    ShaderModuleInfo module = compile_fragment(mstring_get_str(source));
    g_assert_true(pgraph_vk_init_shader_module_layout_from_spv(
        &module, VK_SHADER_STAGE_FRAGMENT_BIT));
    g_assert_cmpuint(module.uniforms.num_uniforms, ==, PshUniform__COUNT);
    g_assert_true(pgraph_vk_override_fragment_layout_supported(&module));
    check_uploads(&module);
    clear_fragment(&module);
    mstring_unref(source);
}

static void test_subset_reordered_uploads(void)
{
    ShaderModuleInfo module = compile_fragment(
        "#version 450\n"
        "layout(binding=1,std140) uniform U {vec4 fogColor;mat2 bumpMat[4];"
        "int alphaRef;float texScale[4];uint colorKey[4];};\n"
        "layout(location=0) out vec4 color;void main(){color=fogColor+"
        "vec4(bumpMat[3][1],texScale[0],float(alphaRef+int(colorKey[0])));}");
    g_assert_true(pgraph_vk_init_shader_module_layout_from_spv(
        &module, VK_SHADER_STAGE_FRAGMENT_BIT));
    g_assert_true(pgraph_vk_override_fragment_layout_supported(&module));
    check_uploads(&module);
    clear_fragment(&module);
}

static void test_unsafe_uniform_declarations(void)
{
    static const struct {
        const char *declaration;
        const char *expression;
    } cases[] = {
        { "vec4 unknownValue;", "unknownValue" },
        { "float alphaRef;", "vec4(alphaRef)" },
        { "uint alphaRef;", "vec4(alphaRef)" },
        { "ivec4 fogColor;", "vec4(fogColor)" },
        { "vec2 fogColor;", "vec4(fogColor,0,1)" },
        { "vec4 fogColor[1];", "fogColor[0]" },
        { "vec4 consts[17];", "consts[0]" },
        { "vec4 consts[19];", "consts[0]" },
        { "vec4 consts[3][6];", "consts[0][0]" },
        { "vec2 bumpMat[8];", "vec4(bumpMat[0],0,1)" },
        { "mat3 bumpMat[4];", "vec4(bumpMat[0][0],1)" },
        { "layout(row_major) mat2 bumpMat[4];",
          "vec4(bumpMat[0][0],bumpMat[0][1])" },
        { "ivec4 clipRegion[7];", "vec4(clipRegion[0])" },
        { "int colorKey[4];", "vec4(colorKey[0])" },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); ++i) {
        g_test_message("reject %s", cases[i].declaration);
        g_autofree char *source = g_strdup_printf(
            "#version 450\nlayout(binding=1,std140) uniform U {%s};\n"
            "layout(location=0) out vec4 color;void main(){color=%s;}",
            cases[i].declaration, cases[i].expression);
        ShaderModuleInfo module = compile_fragment(source);
        bool initialized = pgraph_vk_init_shader_module_layout_from_spv(
            &module, VK_SHADER_STAGE_FRAGMENT_BIT);
        g_assert_false(initialized &&
                       pgraph_vk_override_fragment_layout_supported(&module));
        clear_fragment(&module);
    }
}

static void test_resource_boundaries(void)
{
    static const struct {
        const char *source;
        bool supported;
    } cases[] = {
        { "layout(location=0) out vec4 color;void main(){color=vec4(1);}",
          true },
        { "layout(binding=2) uniform sampler2D tex;"
          "layout(location=0) out vec4 color;"
          "void main(){color=texture(tex,vec2(.5));}",
          true },
        { "layout(set=1,binding=1,std140) uniform U {vec4 fogColor;};"
          "layout(location=0) out vec4 color;void main(){color=fogColor;}",
          false },
        { "layout(binding=0,std140) uniform U {vec4 fogColor;};"
          "layout(location=0) out vec4 color;void main(){color=fogColor;}",
          false },
        { "layout(binding=1,std140) uniform U {vec4 fogColor;} u[2];"
          "layout(location=0) out vec4 color;void main(){color=u[0].fogColor;}",
          false },
        { "layout(push_constant) uniform U {vec4 fogColor;};"
          "layout(location=0) out vec4 color;void main(){color=fogColor;}",
          false },
        { "layout(binding=2) uniform sampler2D tex[2];"
          "layout(location=0) out vec4 color;"
          "void main(){color=texture(tex[0],vec2(.5));}",
          false },
        { "layout(binding=6) uniform sampler2D tex;"
          "layout(location=0) out vec4 color;"
          "void main(){color=texture(tex,vec2(.5));}",
          false },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); ++i) {
        g_autofree char *source =
            g_strconcat("#version 450\n", cases[i].source, NULL);
        ShaderModuleInfo module = compile_fragment(source);
        bool initialized = pgraph_vk_init_shader_module_layout_from_spv(
            &module, VK_SHADER_STAGE_FRAGMENT_BIT);
        g_assert_cmpint(
            initialized &&
                pgraph_vk_override_fragment_layout_supported(&module),
            ==, cases[i].supported);
        clear_fragment(&module);
    }
}

static void test_reflection_upload_mismatch(void)
{
    ShaderModuleInfo module = compile_fragment(
        "#version 450\nlayout(binding=1,std140) uniform U {vec4 consts[18];};"
        "layout(location=0) out vec4 color;void main(){color=consts[0];}");
    g_assert_true(pgraph_vk_init_shader_module_layout_from_spv(
        &module, VK_SHADER_STAGE_FRAGMENT_BIT));
    g_assert_true(pgraph_vk_override_fragment_layout_supported(&module));
    ShaderUniform *uniform = &module.uniforms.uniforms[0];
    const ShaderUniform original = *uniform;
    uniform->dim_v = 3;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    *uniform = original;
    uniform->dim_a = 17;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    *uniform = original;
    uniform->stride = 4;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    *uniform = original;
    uniform->offset = module.uniforms.total_size;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    *uniform = original;
    module.uniforms.total_size--;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    clear_fragment(&module);
}

static void test_matrix_stride_decoration(void)
{
    ShaderModuleInfo module = compile_fragment(
        "#version 450\nlayout(binding=1,std140) uniform U {mat2 bumpMat[4];};"
        "layout(location=0) out vec4 color;"
        "void main(){color=vec4(bumpMat[0][0],bumpMat[0][1]);}");
    g_assert_true(pgraph_vk_init_shader_module_layout_from_spv(
        &module, VK_SHADER_STAGE_FRAGMENT_BIT));
    g_assert_true(pgraph_vk_override_fragment_layout_supported(&module));
    const SpvReflectBlockVariable *block =
        &module.descriptor_sets[0]->bindings[0]->block;
    const SpvReflectTypeDescription *type = block->type_description;
    const uint32_t struct_id = type->op == SpvOpTypeStruct ?
                                   type->id :
                                   type->struct_type_description->id;
    uint32_t *words = (uint32_t *)module.spirv->data;
    const size_t count = module.spirv->len / sizeof(uint32_t);
    uint32_t *stride = NULL;
    for (size_t i = 5; i < count;) {
        const uint32_t length = words[i] >> 16;
        g_assert_cmpuint(length, >, 0);
        g_assert_cmpuint(length, <=, count - i);
        if ((words[i] & 0xffff) == SpvOpMemberDecorate && length == 5 &&
            words[i + 1] == struct_id && words[i + 2] == 0 &&
            words[i + 3] == SpvDecorationMatrixStride) {
            g_assert_null(stride);
            stride = &words[i + 4];
        }
        i += length;
    }
    g_assert_nonnull(stride);
    g_assert_cmpuint(*stride, ==, 16);
    // Cached reflection deliberately remains unchanged: admission must prove
    // the actual compiled column stride, which pinned Reflect loses for arrays.
    *stride = 8;
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    *stride = 16;
    g_assert_true(pgraph_vk_override_fragment_layout_supported(&module));
    const guint bytes = module.spirv->len;
    module.spirv->len = 5 * sizeof(uint32_t);
    g_assert_false(pgraph_vk_override_fragment_layout_supported(&module));
    module.spirv->len = bytes;
    clear_fragment(&module);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    pgraph_vk_init_glsl_compiler();
    g_test_add_func("/xbox/vk/override-interface/generated-uploads",
                    test_generated_fragment_uploads);
    g_test_add_func("/xbox/vk/override-interface/subset-reordered-uploads",
                    test_subset_reordered_uploads);
    g_test_add_func("/xbox/vk/override-interface/unsafe-uniforms",
                    test_unsafe_uniform_declarations);
    g_test_add_func("/xbox/vk/override-interface/resource-boundaries",
                    test_resource_boundaries);
    g_test_add_func("/xbox/vk/override-interface/reflection-upload-mismatch",
                    test_reflection_upload_mismatch);
    g_test_add_func("/xbox/vk/override-interface/matrix-stride-decoration",
                    test_matrix_stride_decoration);
    int result = g_test_run();
    pgraph_vk_finalize_glsl_compiler();
    return result;
}
