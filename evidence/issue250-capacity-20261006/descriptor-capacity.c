/* Production descriptor publication and rollover check. */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/shaders.c"

static unsigned int pool_sets, finishes, writes;
static bool used[16384];

static VKAPI_ATTR VkResult VKAPI_CALL make_pool(VkDevice device,
    const VkDescriptorPoolCreateInfo *info, const VkAllocationCallbacks *alloc,
    VkDescriptorPool *pool)
{
    pool_sets = info->maxSets;
    g_assert_cmpuint(pool_sets, <=, ARRAY_SIZE(used));
    g_assert_cmpuint(info->pPoolSizes[0].descriptorCount, ==, 2 * pool_sets);
    g_assert_cmpuint(info->pPoolSizes[1].descriptorCount, ==,
                    NV2A_MAX_TEXTURES * pool_sets);
    if (info->poolSizeCount == 3) {
        g_assert_cmpuint(info->pPoolSizes[2].descriptorCount, ==, pool_sets);
    }
    *pool = (VkDescriptorPool)(uintptr_t)1;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL make_sets(VkDevice device,
    const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *sets)
{
    g_assert_cmpuint(info->descriptorSetCount, ==, pool_sets);
    for (unsigned int i = 0; i < info->descriptorSetCount; i++) {
        sets[i] = (VkDescriptorSet)(uintptr_t)(i + 1);
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL update_sets(VkDevice device,
    uint32_t count, const VkWriteDescriptorSet *updates, uint32_t copy_count,
    const VkCopyDescriptorSet *copies)
{
    unsigned int index = (uintptr_t)updates[0].dstSet - 1;
    g_assert_cmpuint(index, <, pool_sets);
    g_assert_false(used[index]);
    used[index] = true;
    for (unsigned int i = 0; i < count; i++) {
        g_assert_true(updates[i].dstSet == updates[0].dstSet);
    }
    g_assert_cmpuint(updates[0].pBufferInfo->offset % 256, ==, 0);
    g_assert_cmpuint(updates[0].pBufferInfo->range, ==, 16);
    writes++;
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    g_assert_cmpint(reason, ==, VK_FINISH_REASON_NEED_BUFFER_SPACE);
    g_assert_cmpuint(r->descriptor_set_index, ==, pool_sets);
    finishes++;
    memset(used, 0, sizeof(used));
    r->descriptor_set_index = 0;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_offset = 0;
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index,
    void **data, VkDeviceSize *sizes, size_t count,
    VkDeviceSize alignment)
{
    StorageBuffer *b = &pg->vk_renderer_state->storage_buffers[index];
    VkDeviceSize offset = ROUND_UP(b->buffer_offset, alignment);
    b->buffer_offset = offset;
    for (unsigned int i = 0; i < count; i++) {
        b->buffer_offset += sizes[i];
    }
    g_assert_cmpuint(b->buffer_offset, <=, b->buffer_size);
    return offset;
}

static void test_publication(gconstpointer arg)
{
    g_autofree PGRAPHState *pg = g_new0(PGRAPHState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    ShaderBinding binding = { 0 };
    ShaderModuleInfo modules[2] = { 0 };
    TextureBinding textures[NV2A_MAX_TEXTURES] = { 0 };
    pg->vk_renderer_state = r;
    r->shader_binding = &binding;
    r->ubershader_runtime_enabled = GPOINTER_TO_INT(arg) != 0;
    if (GPOINTER_TO_INT(arg) == 2) {
        binding.fragment_route = PGRAPH_VK_FRAGMENT_UBERSHADER;
        r->uber_controls_valid = true;
    }
    binding.vsh.module_info = &modules[0];
    binding.psh.module_info = &modules[1];
    modules[0].uniforms.total_size = 16;
    modules[1].uniforms.total_size = 16;
    r->storage_buffers[BUFFER_UNIFORM_STAGING].buffer_size = 16 * MiB;
    r->device_props.limits.minUniformBufferOffsetAlignment = 256;
    for (unsigned int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_bindings[i] = &textures[i];
    }
    finishes = writes = 0;
    memset(used, 0, sizeof(used));
    vkCreateDescriptorPool = make_pool;
    vkAllocateDescriptorSets = make_sets;
    vkUpdateDescriptorSets = update_sets;
    create_descriptor_pool(pg);
    create_descriptor_sets(pg);
    /* A representative PGR2-sized batch must publish distinct generations. */
    for (unsigned int i = 0; i < 2000; i++) {
        r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_VSH] = true;
        pgraph_vk_update_descriptor_sets(pg);
    }
    g_assert_cmpuint(writes, ==, 2000);
    g_assert_cmpuint(finishes, ==, 0);
    for (unsigned int i = 2000; i <= pool_sets; i++) {
        r->uniform_stage_dirty[PGRAPH_UNIFORM_STAGE_VSH] = true;
        pgraph_vk_update_descriptor_sets(pg);
    }
    g_assert_cmpuint(finishes, ==, 1);
    g_assert_cmpuint(r->descriptor_set_index, ==, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/xbox/vk/descriptor/specialized", NULL,
                         test_publication);
    g_test_add_data_func("/xbox/vk/descriptor/hybrid-layout",
                         GINT_TO_POINTER(1), test_publication);
    g_test_add_data_func("/xbox/vk/descriptor/uber-controls",
                         GINT_TO_POINTER(2), test_publication);
    return g_test_run();
}
