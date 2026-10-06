/*
 * Production cubemap surface copy regression.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/texture.c"

NV2AStats g_nv2a_stats;
static unsigned copies, expected_layer;
static bool guest_write;
static SurfaceBinding sources[6];

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    return (VkCommandBuffer)(uintptr_t)1;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
}

void pgraph_vk_transition_image_layout(PGRAPHState *pg, VkCommandBuffer cmd,
                                       VkImage image, VkFormat format,
                                       VkImageLayout before,
                                       VkImageLayout after)
{
}

static VKAPI_ATTR void VKAPI_CALL
record_copy(VkCommandBuffer cmd, VkImage source, VkImageLayout source_layout,
            VkImage dest, VkImageLayout dest_layout, uint32_t count,
            const VkImageCopy *region)
{
    g_assert_cmpuint(count, ==, 1);
    g_assert_cmpuint(region->srcSubresource.baseArrayLayer, ==, 0);
    g_assert_cmpuint(region->dstSubresource.baseArrayLayer, ==, expected_layer);
    g_assert_cmpuint(region->extent.width, ==, 128);
    g_assert_cmpuint(region->extent.height, ==, 128);
    g_assert_cmpuint(region->extent.depth, ==, 1);
    copies++;
    expected_layer++;
}

static void test_face_destinations(void)
{
    static PGRAPHVkState r;
    static PGRAPHState pg = { .vk_renderer_state = &r,
                              .surface_scale_factor = 1 };
    TextureBinding texture = {
        .key = { .texture_vram_offset = 0x100000,
                 .texture_length = 196608,
                 .state = { .cubemap = true,
                            .dimensionality = 2,
                            .width = 128,
                            .height = 128,
                            .depth = 1,
                            .levels = 1,
                            .storage_levels = 1,
                            .color_format =
                                NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5 } }
    };
    SurfaceBinding surface = { .color = true,
                               .swizzle = true,
                               .width = 128,
                               .height = 128,
                               .host_fmt = {
                                   .vk_format = VK_FORMAT_R5G6B5_UNORM_PACK16,
                                   .aspect = VK_IMAGE_ASPECT_COLOR_BIT } };

    copies = expected_layer = 0;
    vkCmdCopyImage = record_copy;
    for (unsigned i = 0; i < 6; i++) {
        surface.vram_addr = 0x100000 + i * 32768;
        copy_surface_to_texture(&pg, &surface, &texture);
    }
    g_assert_cmpuint(copies, ==, 6);
}


void pgraph_vk_begin_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd,
                                  float color[4], const char *format, ...)
{
}
void pgraph_vk_end_debug_marker(PGRAPHVkState *r, VkCommandBuffer cmd)
{
}
void pgraph_vk_ensure_buffer_capacity(PGRAPHState *pg, int index,
                                      VkDeviceSize size)
{
    g_assert_not_reached();
}
bool pgraph_vk_compute_needs_finish(PGRAPHVkState *r)
{
    g_assert_not_reached();
}
void pgraph_vk_finish(PGRAPHState *pg, FinishReason reason)
{
    g_assert_not_reached();
}
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    g_assert_not_reached();
}

/* The GPU and guest-write observer are external to this command fixture. */
SurfaceBinding *pgraph_vk_surface_get(NV2AState *d, hwaddr address)
{
    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &d->pgraph.vk_renderer_state->surfaces, entry) {
        if (surface->vram_addr == address) {
            return surface;
        }
    }
    return NULL;
}

void pgraph_vk_surface_update_guest_writes(NV2AState *d, hwaddr start,
                                           hwaddr length)
{
    g_assert_cmpuint(start, ==, 0x100000);
    g_assert_cmpuint(length, ==, 196608);
    if (guest_write) {
        sources[3].upload_pending = true;
        sources[3].readback_superseded_by_guest = true;
    }
}

static void test_admission_and_generations(void)
{
    static NV2AState d;
    static PGRAPHVkState r;
    PGRAPHState *pg = &d.pgraph;
    TextureBinding texture = {
        .key = { .texture_vram_offset = 0x100000,
                 .texture_length = 196608,
                 .state = { .cubemap = true,
                            .dimensionality = 2,
                            .width = 128,
                            .height = 128,
                            .depth = 1,
                            .levels = 1,
                            .storage_levels = 1,
                            .color_format =
                                NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5 } }
    };
    TextureShape *shape = &texture.key.state;
    SurfaceBinding *faces[6];
    pg->vk_renderer_state = &r;
    pg->surface_scale_factor = 1;
    QTAILQ_INIT(&r.surfaces);
    for (unsigned i = 0; i < 6; i++) {
        sources[i] = (SurfaceBinding){
            .vram_addr = 0x100000 + i * 32768,
            .size = 32768,
            .width = 128,
            .height = 128,
            .pitch = 256,
            .color = true,
            .swizzle = true,
            .initialized = true,
            .image = (VkImage)(uintptr_t)(i + 1),
            .lifetime_id = i + 1,
            .host_fmt = { .vk_format = VK_FORMAT_R5G6B5_UNORM_PACK16,
                          .aspect = VK_IMAGE_ASPECT_COLOR_BIT }
        };
        QTAILQ_INSERT_TAIL(&r.surfaces, &sources[i], entry);
    }
#define ADMIT() find_cubemap_surfaces(pg, shape, 0x100000, 196608, faces)
    g_assert_true(ADMIT());
    for (unsigned i = 0; i < 6; i++) {
        g_assert_true(faces[i] == &sources[i]);
    }
    copies = expected_layer = 0;
    copy_cubemap_surfaces(pg, faces, &texture);
    g_assert_cmpuint(copies, ==, 6);
    copy_cubemap_surfaces(pg, faces, &texture);
    g_assert_cmpuint(copies, ==, 6);
    sources[4].draw_time++;
    expected_layer = 4;
    copy_cubemap_surfaces(pg, faces, &texture);
    g_assert_cmpuint(copies, ==, 7);
    sources[2].lifetime_id += 100;
    expected_layer = 2;
    copy_cubemap_surfaces(pg, faces, &texture);
    g_assert_cmpuint(copies, ==, 8);
    g_assert_true(texture.possibly_dirty);

    QTAILQ_REMOVE(&r.surfaces, &sources[5], entry);
    g_assert_false(ADMIT());
    QTAILQ_INSERT_TAIL(&r.surfaces, &sources[5], entry);
    sources[2].width = 64;
    g_assert_false(ADMIT());
    sources[2].width = 128;
    sources[2].host_fmt.vk_format = VK_FORMAT_B8G8R8A8_UNORM;
    g_assert_false(ADMIT());
    sources[2].host_fmt.vk_format = VK_FORMAT_R5G6B5_UNORM_PACK16;
    sources[2].swizzle = false;
    g_assert_false(ADMIT());
    sources[2].swizzle = true;
    sources[2].initialized = false;
    g_assert_false(ADMIT());
    sources[2].initialized = true;
    shape->levels = 2;
    g_assert_false(ADMIT());
    shape->levels = 1;
    shape->border = true;
    g_assert_false(ADMIT());
    shape->border = false;
    pg->surface_scale_factor = 2;
    g_assert_false(ADMIT());
    pg->surface_scale_factor = 1;
    SurfaceBinding overlap = { .vram_addr = 0x100004, .size = 4 };
    QTAILQ_INSERT_HEAD(&r.surfaces, &overlap, entry);
    g_assert_false(ADMIT());
    QTAILQ_REMOVE(&r.surfaces, &overlap, entry);
    g_assert_true(ADMIT());
    guest_write = true;
    g_assert_false(ADMIT());
    g_assert_cmpuint(copies, ==, 8);
#undef ADMIT
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/nv2a/vulkan/cubemap/face-destinations",
                    test_face_destinations);
    g_test_add_func("/nv2a/vulkan/cubemap/admission-and-generations",
                    test_admission_and_generations);
    return g_test_run();
}
