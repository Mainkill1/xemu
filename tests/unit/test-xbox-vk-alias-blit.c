/*
 * Vulkan alias ownership at the real RAM blit consumer
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/blit.c"

static uint8_t guest[128];
static uint8_t owned[128];
static SurfaceBinding owners[4];
static unsigned int range_reads;

void pgraph_vk_surface_update(NV2AState *d, bool upload, bool color, bool zeta)
{
    /* TCG draw ownership remains on the GPU until explicit readback. */
}

void *nv_dma_map(NV2AState *d, hwaddr address, hwaddr *length)
{
    *length = sizeof(guest);
    return guest;
}

SurfaceBinding *pgraph_vk_surface_get(NV2AState *d, hwaddr address)
{
    for (unsigned int i = 0; i < G_N_ELEMENTS(owners); i++) {
        if (owners[i].vram_addr == address) {
            return &owners[i];
        }
    }
    return NULL;
}

bool pgraph_vk_surface_download_if_dirty(NV2AState *d, SurfaceBinding *s)
{
    if (s->draw_dirty) {
        memcpy(guest + s->vram_addr, owned + s->vram_addr, s->size);
        s->draw_dirty = false;
    }
    return true;
}

/* Stand in for asynchronous GPU transfer, publishing the owning generation. */
bool pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,
                                                   hwaddr start, hwaddr size)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    range_reads++;
    for (unsigned int i = 0; i < G_N_ELEMENTS(owners); i++) {
        if (start < owners[i].vram_addr + owners[i].size &&
            owners[i].vram_addr < start + size) {
            pgraph_vk_surface_download_if_dirty(d, &owners[i]);
        }
    }
    return true;
}

hwaddr nv_clip_gpu_tile_blit(NV2AState *d, hwaddr address, hwaddr length)
{
    return length;
}

void memory_region_set_client_dirty(MemoryRegion *mr, hwaddr address,
                                    hwaddr length, unsigned int client)
{
}

static void test_alias_blit(void)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    static const uint8_t expected[16] = {
        85, 85, 85, 85, 85, 6, 7, 85, 85, 10, 11, 85, 85, 85, 85, 85,
    };
    d->pgraph.vk_renderer_state = r;
    d->vram_ptr = guest;
    memset(guest, 0, sizeof(guest));
    memset(owned, 85, sizeof(owned));
    range_reads = 0;
    for (unsigned int i = 0; i < 16; i++) {
        owned[16 + i] = i + 1;
    }
    owners[0] = (SurfaceBinding){ .vram_addr = 16,
                                  .size = 8,
                                  .width = 2,
                                  .height = 2,
                                  .pitch = 4,
                                  .swizzle = true };
    owners[1] = (SurfaceBinding){ .vram_addr = 16,
                                  .size = 16,
                                  .width = 4,
                                  .height = 4,
                                  .pitch = 4,
                                  .draw_dirty = true };
    owners[2] = owners[0];
    owners[2].vram_addr = 64;
    owners[3] = owners[1];
    owners[3].vram_addr = 64;
    QTAILQ_INIT(&r->surfaces);
    for (unsigned int i = 0; i < G_N_ELEMENTS(owners); i++) {
        QTAILQ_INSERT_TAIL(&r->surfaces, &owners[i], entry);
    }
    ContextSurfaces2DState *c = &d->pgraph.context_surfaces_2d;
    ImageBlitState *b = &d->pgraph.image_blit;
    c->source_offset = 16;
    c->dest_offset = 64;
    c->source_pitch = c->dest_pitch = 4;
    c->color_format = NV062_SET_COLOR_FORMAT_LE_Y8;
    b->in_x = b->in_y = b->out_x = b->out_y = 1;
    b->width = b->height = 2;
    b->operation = NV09F_SET_OPERATION_SRCCOPY;
    pgraph_vk_image_blit(d);
    g_assert_cmpmem(guest + 64, sizeof(expected), expected, sizeof(expected));
    g_assert_cmpuint(range_reads, ==, 2);
    g_assert_true(owners[2].upload_pending);
    g_assert_true(owners[3].upload_pending);
}

static void test_full_blit(gconstpointer data)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    static const uint8_t source[4] = { 100, 120, 140, 255 };
    static const uint8_t destination[4] = { 20, 40, 60, 77 };
    static const uint8_t expected[4] = { 60, 80, 100, 77 };
    d->pgraph.vk_renderer_state = r;
    d->vram_ptr = guest;
    memset(guest, 0, sizeof(guest));
    memcpy(owned + 16, source, sizeof(source));
    memcpy(owned + 64, destination, sizeof(destination));
    memset(owners, 0, sizeof(owners));
    range_reads = 0;
    owners[0] = (SurfaceBinding){ .vram_addr = 16,
                                  .size = 4,
                                  .width = 1,
                                  .height = 1,
                                  .pitch = 4,
                                  .draw_dirty = true,
                                  .fmt.bytes_per_pixel = 4 };
    owners[1] = owners[0];
    owners[1].vram_addr = 64;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INSERT_TAIL(&r->surfaces, &owners[0], entry);
    QTAILQ_INSERT_TAIL(&r->surfaces, &owners[1], entry);
    ContextSurfaces2DState *c = &d->pgraph.context_surfaces_2d;
    ImageBlitState *b = &d->pgraph.image_blit;
    c->source_offset = 16;
    c->dest_offset = 64;
    c->source_pitch = c->dest_pitch = 4;
    c->color_format = NV062_SET_COLOR_FORMAT_LE_A8R8G8B8;
    b->width = b->height = 1;
    bool blend = GPOINTER_TO_INT(data);
    b->operation =
        blend ? NV09F_SET_OPERATION_BLEND_AND : NV09F_SET_OPERATION_SRCCOPY;
    d->pgraph.beta.beta = 0x3fc00000;
    pgraph_vk_image_blit(d);
    g_assert_cmpmem(guest + 64, 4, blend ? expected : source, 4);
    g_assert_cmpuint(range_reads, ==, blend ? 2 : 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/alias/blit-owned-ram", test_alias_blit);
    g_test_add_data_func("/xbox/vk/alias/full-blend-owned-ram",
                         GINT_TO_POINTER(1), test_full_blit);
    g_test_add_data_func("/xbox/vk/alias/full-copy-no-dest-readback", NULL,
                         test_full_blit);
    return g_test_run();
}
