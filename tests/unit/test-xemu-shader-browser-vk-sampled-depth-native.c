/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../hw/xbox/nv2a/pgraph/vk/draw.c"
void xemu_test_stage_sampler(uint64_t token);
void xemu_test_stage_sampler(uint64_t token)
{
    VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .minFilter = VK_FILTER_LINEAR,
        .magFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .minLod = 1.25f,
        .maxLod = 6.5f,
        .mipLodBias = -0.75f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 4.f,
        .borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT,
        .compareEnable = VK_FALSE,
    };
    const float border[4] = { .1f, .2f, .3f, .4f };
    pgraph_vk_input_stage_sampler(token, 0, &info, border);
}
bool xemu_test_depth_rejected(uint32_t guest, uint32_t host);
bool xemu_test_depth_rejected(uint32_t guest, uint32_t host)
{
    return pgraph_vk_input_depth_texture(guest, host);
}

bool xemu_test_stage_y16(uint64_t token, uint8_t *bytes, size_t count);
bool xemu_test_stage_y16(uint64_t token, uint8_t *bytes, size_t count)
{
    PGRAPHVkInputReadback readback = { .mapped = bytes,
                                       .raw_bytes = count,
                                       .width = 2,
                                       .height = 2,
                                       .format = VK_FORMAT_R16_UNORM,
                                       .before = -1,
                                       .texture = { .slot = 0,
                                                    .face_count = 1 } };
    return pgraph_vk_input_stage_texture_storage(token, &readback);
}
