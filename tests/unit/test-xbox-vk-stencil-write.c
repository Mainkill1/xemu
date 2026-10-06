/*
 * Vulkan stencil write ownership regression tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

/*
 * Exercise the recipe passed to both pipeline creation paths. Section GC
 * discards unrelated renderer code; only driver allocation is substituted.
 */
#include "hw/xbox/nv2a/pgraph/vk/draw.c"

int nv2a_vk_dgroup_indent;
bool nv2a_vk_text_debug_enabled;

void pgraph_vk_text_debug_printf(const char *format, ...)
{
    (void)format;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_create_layout(
    VkDevice device, const VkPipelineLayoutCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkPipelineLayout *layout)
{
    *layout = (VkPipelineLayout)(uintptr_t)1;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL test_create_render_pass(
    VkDevice device, const VkRenderPassCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkRenderPass *render_pass)
{
    *render_pass = (VkRenderPass)(uintptr_t)2;
    return VK_SUCCESS;
}

static void test_stencil_write_mask(void)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    ShaderModuleInfo module = { 0 };
    ShaderBinding binding = { 0 };
    PipelineKey key = { 0 };
    PGRAPHVkGraphicsPipelineRecipe recipe;

    pg->vk_renderer_state = r;
    r->render_passes = g_array_new(false, false, sizeof(RenderPass));
    binding.state.geom.primitive_mode = PRIM_TYPE_TRIANGLES;
    binding.state.geom.polygon_front_mode = POLY_MODE_FILL;
    binding.state.geom.polygon_back_mode = POLY_MODE_FILL;
    binding.vsh.module_info = &module;
    binding.geom.module_info = &module;
    binding.psh.module_info = &module;
    binding.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    key.fragment_route = binding.fragment_route;
    key.shader_state = binding.state;
    key.render_pass_state.zeta_format = VK_FORMAT_D24_UNORM_S8_UINT;
    SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE, 1);
    SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_FUNC, 7);
    SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ, 0xff);
    SET_MASK(key.regs[3], NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL, 3);
    SET_MASK(key.regs[3], NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL, 4);
    SET_MASK(key.regs[3], NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS, 3);

    for (unsigned int enabled = 0; enabled < 2; enabled++) {
        for (unsigned int mask = 0; mask < 256; mask++) {
            SET_MASK(key.regs[1], NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE,
                     enabled);
            SET_MASK(key.regs[2], NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE, mask);
            pg->regs_[NV_PGRAPH_CONTROL_0] = key.regs[1];
            pg->regs_[NV_PGRAPH_CONTROL_1] = key.regs[2];

            g_assert_true(prepare_graphics_pipeline_recipe(pg, &key, &binding,
                                                          &recipe));
            const VkPipelineDepthStencilStateCreateInfo *state =
                recipe.info.pDepthStencilState;
            g_assert_nonnull(state);
            g_assert_true(state->stencilTestEnable);
            g_assert_false(state->depthWriteEnable);
            g_assert_cmpint(state->front.passOp, ==, VK_STENCIL_OP_REPLACE);
            g_assert_cmpint(state->front.depthFailOp, ==,
                            VK_STENCIL_OP_INCREMENT_AND_CLAMP);
            g_assert_cmpuint(state->front.compareMask, ==, 0xff);
            g_assert_cmpuint(state->front.writeMask, ==, enabled ? mask : 0);
            g_assert_cmpuint(state->back.writeMask, ==, enabled ? mask : 0);
            if (!pgraph_zeta_surface_dirty_required(pg, true)) {
                g_assert_cmpuint(state->front.writeMask, ==, 0);
                g_assert_cmpuint(state->back.writeMask, ==, 0);
            }
        }
    }
    g_array_free(r->render_passes, true);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    vkCreatePipelineLayout = test_create_layout;
    vkCreateRenderPass = test_create_render_pass;
    g_test_add_func("/xbox/vk/stencil/recipe-write-mask",
                    test_stencil_write_mask);
    return g_test_run();
}
