/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../hw/xbox/nv2a/pgraph/vk/draw.c"
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
