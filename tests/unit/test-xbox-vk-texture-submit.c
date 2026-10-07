/*
 * Texture ownership must follow the batch that actually records a draw.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shaders.c"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"

#include "test-xbox-vk-texture-submit.h"
static unsigned int finishes, binds;

static VKAPI_ATTR void VKAPI_CALL
record_writes(VkDevice device, uint32_t count, const VkWriteDescriptorSet *sets,
              uint32_t copies, const VkCopyDescriptorSet *copy_sets)
{
    g_assert_cmpuint(count, >=, 6);
}

static VKAPI_ATTR void VKAPI_CALL record_bind(
    VkCommandBuffer cmd, VkPipelineBindPoint point, VkPipelineLayout layout,
    uint32_t first, uint32_t count, const VkDescriptorSet *sets,
    uint32_t offsets_count, const uint32_t *offsets)
{
    g_assert_cmpint(point, ==, VK_PIPELINE_BIND_POINT_GRAPHICS);
    g_assert_cmpuint(count, ==, 1);
    binds++;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    StorageBuffer *buffer = &pg->vk_renderer_state->storage_buffers[index];
    VkDeviceSize offset = ROUND_UP(buffer->buffer_offset, alignment);
    buffer->buffer_offset = offset + sizes[0];
    g_assert_cmpuint(buffer->buffer_offset, <=, buffer->buffer_size);
    return offset;
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    /* Simulate completed submission, keeping production preparation/binding. */
    r->submit_count++;
    r->in_command_buffer = false;
    r->descriptor_set_index = 0;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 0;
    finishes++;
}

static void test_rollover(gconstpointer uniform_capacity)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    ShaderModuleInfo modules[2] = { 0 };
    ShaderBinding shader = { 0 };
    PipelineBinding pipeline = { 0 };
    TextureBinding old[NV2A_MAX_TEXTURES] = { 0 };
    TextureBinding replacement[NV2A_MAX_TEXTURES] = { 0 };
    uint8_t uniforms[2][64] = { 0 };
    pg->vk_renderer_state = r;
    r->shader_binding = &shader;
    r->pipeline_binding = &pipeline;
    shader.vsh.module_info = &modules[0];
    shader.psh.module_info = &modules[1];
    shader.fragment_route = PGRAPH_VK_FRAGMENT_SPECIALIZED;
    r->device_props.limits.minUniformBufferOffsetAlignment = 256;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_size = 65536;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset =
        uniform_capacity ? 65536 : 512;
    r->storage_buffers[BUFFER_UNIFORM].buffer = (VkBuffer)(uintptr_t)1;
    r->descriptor_set_index =
        uniform_capacity ? 1 : ARRAY_SIZE(r->descriptor_sets);
    r->uniform_stage_dirty[0] = r->uniform_stage_dirty[1] = true;
    r->texture_descriptor_publication_pending = true;
    r->submit_count = 7;
    r->in_command_buffer = true;
    for (int i = 0; i < 2; i++) {
        modules[i].uniforms.total_size = sizeof(uniforms[i]);
        modules[i].uniforms.allocation = uniforms[i];
    }
    lru_init(&r->texture_cache);
    r->texture_cache.pre_node_evict = texture_cache_entry_pre_evict;
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        lru_add_free(&r->texture_cache, &old[i].node);
        g_assert_true(lru_lookup(&r->texture_cache, i + 1, NULL) ==
                      &old[i].node);
        old[i].image_view = (VkImageView)(uintptr_t)(i + 1);
        old[i].sampler = (VkSampler)(uintptr_t)(i + 10);
        r->texture_bindings[i] = &old[i];
    }
    for (int i = 0; i < ARRAY_SIZE(r->descriptor_sets); i++) {
        r->descriptor_sets[i] = (VkDescriptorSet)(uintptr_t)(i + 1);
    }
    vkUpdateDescriptorSets = record_writes;
    vkCmdBindDescriptorSets = record_bind;
    finishes = binds = 0;
    pgraph_vk_pin_bound_textures(
        r); /* The same preparation stamp used by bind_textures. */
    pgraph_vk_update_descriptor_sets(pg); /* Real capacity rollover path. */
    g_assert_cmpuint(finishes, ==, 1);
    g_assert_cmpuint(r->submit_count, ==, 8);
    r->in_command_buffer = true;
    test_texture_submit_bind(pg); /* Real graphics descriptor binding. */
    g_assert_cmpuint(binds, ==, 1);

    /*
     * LRU pressure during subsequent multistage replacement must reject each
     * old node even after it is no longer among the current bindings.
     */
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_bindings[i] = &replacement[i];
        g_assert_false(
            texture_cache_entry_pre_evict(&r->texture_cache, &old[i].node));
        g_assert_null(lru_try_evict_one(&r->texture_cache));
    }
    r->in_command_buffer = false; /* Completion makes unbound nodes eligible. */
    r->submit_count++;
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_true(
            texture_cache_entry_pre_evict(&r->texture_cache, &old[i].node));
    }
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        g_assert_nonnull(lru_try_evict_one(&r->texture_cache));
    }
    g_assert_cmpint(r->texture_cache.num_used, ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/vk/texture-submit/descriptor-rollover", NULL,
                         test_rollover);
    g_test_add_data_func("/vk/texture-submit/uniform-rollover",
                         GINT_TO_POINTER(1), test_rollover);
    return g_test_run();
}
