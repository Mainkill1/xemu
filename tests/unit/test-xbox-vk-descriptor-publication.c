/*
 * Exercise the production descriptor writer with recorded Vulkan calls.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shaders.c"

static unsigned int writes, finishes, uploads;

static VKAPI_ATTR void VKAPI_CALL
record_writes(VkDevice device, uint32_t count, const VkWriteDescriptorSet *sets,
              uint32_t copies, const VkCopyDescriptorSet *copy_sets)
{
    g_assert_cmpuint(count, >=, 6);
    g_assert_cmpuint(copies, ==, 0);
    for (uint32_t i = 1; i < count; i++) {
        g_assert_true(sets[i].dstSet == sets[0].dstSet);
    }
    writes++;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index, void **data,
                                        VkDeviceSize *sizes, size_t count,
                                        VkDeviceAddress alignment)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[index];
    g_assert_cmpint(index, ==, BUFFER_UNIFORM_STAGING);
    g_assert_cmpuint(count, ==, 1);
    VkDeviceSize offset = ROUND_UP(buffer->buffer_offset, alignment);
    buffer->buffer_offset = offset + sizes[0];
    g_assert_cmpuint(buffer->buffer_offset, <=, buffer->buffer_size);
    uploads++;
    return offset;
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    finishes++;
    /* Fake only the external submission boundary, including its reset. */
    r->descriptor_set_index = 0;
    r->descriptor_set_selected = 0;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 0;
    pgraph_vk_descriptor_cache_reset(&r->descriptor_cache);
}

static void test_publication(gconstpointer with_controls)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    ShaderModuleInfo modules[2] = { 0 };
    ShaderBinding shader = { 0 };
    TextureBinding textures[NV2A_MAX_TEXTURES] = { 0 };
    uint8_t uniform_bytes[2][64] = { 0 };
    pg->vk_renderer_state = r;
    r->shader_binding = &shader;
    shader.vsh.module_info = &modules[0];
    shader.psh.module_info = &modules[1];
    r->perf.enabled = true;
    r->ubershader_runtime_enabled = with_controls != NULL;
    r->uber_controls_valid = true;
    shader.fragment_route = with_controls ? PGRAPH_VK_FRAGMENT_UBERSHADER :
                                            PGRAPH_VK_FRAGMENT_SPECIALIZED;
    r->device_props.limits.minUniformBufferOffsetAlignment = 256;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_size = 65536;
    r->storage_buffers[BUFFER_UNIFORM].buffer = (VkBuffer)(uintptr_t)1;
    for (int i = 0; i < 2; i++) {
        modules[i].uniforms.total_size = sizeof(uniform_bytes[i]);
        modules[i].uniforms.allocation = uniform_bytes[i];
    }
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        textures[i].image_view = (VkImageView)(uintptr_t)(i + 10);
        textures[i].sampler = (VkSampler)(uintptr_t)(i + 20);
        r->texture_bindings[i] = &textures[i];
    }
    for (int i = 0; i < ARRAY_SIZE(r->descriptor_sets); i++) {
        r->descriptor_sets[i] = (VkDescriptorSet)(uintptr_t)(i + 1);
    }
    vkUpdateDescriptorSets = record_writes;
    writes = finishes = uploads = 0;

    pgraph_vk_update_descriptor_sets(pg); /* A */
    g_assert_cmpuint(writes, ==, 1);
    textures[0].image_view = (VkImageView)(uintptr_t)100;
    r->texture_descriptor_publication_pending = true;
    pgraph_vk_update_descriptor_sets(pg); /* B */
    g_assert_cmpuint(writes, ==, 2);
    textures[0].image_view = (VkImageView)(uintptr_t)10;
    r->texture_descriptor_publication_pending = true;
    pgraph_vk_update_descriptor_sets(pg); /* A again */
    g_assert_cmpuint(writes, ==, 2);
    g_assert_cmpint(r->descriptor_set_selected, ==, 0);
    g_assert_cmpint(r->descriptor_set_index, ==, 2);
    g_assert_false(r->texture_descriptor_publication_pending);
    g_assert_cmpuint(r->perf.descriptor_cache_hits, ==, 1);

    /* Even a full pool may select an existing set without a drain. */
    r->descriptor_set_index = ARRAY_SIZE(r->descriptor_sets);
    textures[0].image_view = (VkImageView)(uintptr_t)100;
    r->texture_descriptor_publication_pending = true;
    pgraph_vk_update_descriptor_sets(pg);
    g_assert_cmpuint(finishes, ==, 0);
    g_assert_cmpint(r->descriptor_set_selected, ==, 1);

    if (with_controls) {
        ((unsigned char *)&r->uber_controls)[0] ^= 1;
        unsigned int old_uploads = uploads;
        pgraph_vk_update_descriptor_sets(pg);
        g_assert_cmpuint(uploads, ==, old_uploads + 1);
        g_assert_cmpuint(writes, ==, 2);
        g_assert_cmpint(r->descriptor_set_selected, ==, 1);
    }

    /* A real miss must finish and reupload BOTH uniforms before publication. */
    textures[0].sampler = (VkSampler)(uintptr_t)999;
    r->texture_descriptor_publication_pending = true;
    unsigned int old_uploads = uploads;
    pgraph_vk_update_descriptor_sets(pg);
    g_assert_cmpuint(finishes, ==, 1);
    g_assert_cmpuint(writes, ==, 3);
    g_assert_cmpuint(uploads, ==, old_uploads + (with_controls ? 3 : 2));
    g_assert_cmpint(r->descriptor_set_selected, ==, 0);
    g_assert_cmpint(r->descriptor_set_index, ==, 1);
    g_assert_cmpuint(r->uniform_buffer_offsets[0], ==, 0);

    /* A uniform change also prevents a hit, even with the same textures. */
    r->uniform_stage_dirty[0] = true;
    pgraph_vk_update_descriptor_sets(pg);
    g_assert_cmpuint(writes, ==, 4);
    g_assert_cmpint(r->descriptor_set_selected, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/descriptor-publication/specialized", NULL,
                         test_publication);
    g_test_add_data_func("/xbox/vk/descriptor-publication/uber", (void *)1,
                         test_publication);
    return g_test_run();
}
