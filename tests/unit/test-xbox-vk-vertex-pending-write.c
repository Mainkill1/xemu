/*
 * Direct vertex writes must preserve pending submission ownership.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/renderer.h"

/* These branches are outside the direct-write fixture. Trap unexpected use. */
#define memory_region_set_client_dirty(mr, ...) \
    do {                                      \
        (void)(mr);                           \
        g_assert_not_reached();               \
    } while (0)
#define error_report(...) g_assert_not_reached()
#include "hw/xbox/nv2a/pgraph/vk/vertex.c"

NV2AStats g_nv2a_stats;
static unsigned waits;
static VkMemoryPropertyFlags memory_flags;

void vmaGetAllocationMemoryProperties(VmaAllocator allocator,
                                     VmaAllocation allocation,
                                     VkMemoryPropertyFlags *flags)
{
    g_assert_true(allocation == (VmaAllocation)(uintptr_t)7);
    *flags = memory_flags;
}

void pgraph_vk_wait_pending_submission(PGRAPHState *pg, PendingDrainReason why)
{
    g_assert_cmpint(why, ==, VK_PENDING_DRAIN_VERTEX_WRITE);
    waits += pg->vk_renderer_state->submission_pending;
    pg->vk_renderer_state->submission_pending = false;
}

bool pgraph_vk_download_surfaces_in_range_if_dirty(PGRAPHState *pg,
                                                  hwaddr start, hwaddr size)
{
    g_assert_not_reached();
}

bool pgraph_vk_buffer_has_space_for(PGRAPHState *pg, int index,
                                   VkDeviceSize size, VkDeviceSize alignment)
{
    g_assert_not_reached();
}

bool pgraph_vk_grow_vertex_ram_staging_buffer(PGRAPHState *pg,
                                             VkDeviceSize size)
{
    g_assert_not_reached();
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason why)
{
    g_assert_not_reached();
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    g_assert_not_reached();
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    g_assert_not_reached();
}

VkDeviceSize pgraph_vk_append_to_buffer(PGRAPHState *pg, int index,
                                       void **data, VkDeviceSize *sizes,
                                       size_t count, VkDeviceSize alignment)
{
    g_assert_not_reached();
}

VkResult vmaFlushAllocation(VmaAllocator allocator, VmaAllocation allocation,
                           VkDeviceSize offset, VkDeviceSize size)
{
    g_assert_not_reached();
}

void pgraph_vk_perf_record_vertex_direct_copy(PGRAPHVkState *r, uint64_t bytes)
{
}
void pgraph_vk_perf_record_vertex_staging_copy(PGRAPHVkState *r, uint64_t bytes)
{
    g_assert_not_reached();
}
void pgraph_vk_perf_record_vertex_staging_fallback(PGRAPHVkState *r)
{
    g_assert_not_reached();
}
void pgraph_vk_perf_record_vertex_staging_growth(PGRAPHVkState *r)
{
    g_assert_not_reached();
}

static void test_write(gconstpointer opaque)
{
    unsigned mode = GPOINTER_TO_UINT(opaque);
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    d->pgraph.vk_renderer_state = r;
    uint8_t mirror[32] = { 0 }, input[] = { 1, 2, 3, 4 };
    r->storage_buffers[BUFFER_VERTEX_RAM] = (StorageBuffer) {
        .mapped = mirror, .buffer_size = sizeof(mirror),
        .allocation = (VmaAllocation)(uintptr_t)7,
    };
    r->storage_buffers[BUFFER_VERTEX_RAM_STAGING].buffer_offset =
        mode == 3 ? 16 : 0;
    r->submission_pending = true;
    r->submission_retained = true;
    r->in_command_buffer = mode != 1;
    r->descriptor_set_index = 12;
    memory_flags = mode == 2 ? 0 : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    waits = 0;
    update_vertex_ram_buffer(&d->pgraph, 8, input, sizeof(input), true,
                             mode != 1);
    g_assert_cmpmem(mirror + 8, sizeof(input), input, sizeof(input));
    g_assert_cmpuint(mirror[7], ==, 0);
    g_assert_cmpuint(mirror[12], ==, 0);
    g_assert_cmpuint(waits, ==, mode == 0 ? 0 : 1);
    g_assert_cmpint(r->submission_pending, ==, mode == 0);
    g_assert_true(r->submission_retained);
    g_assert_cmpint(r->in_command_buffer, ==, mode != 1);
    g_assert_cmpuint(r->descriptor_set_index, ==, 12);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    const char *names[] = {
        "unread-coherent", "no-read-proof", "noncoherent", "pending-copy",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++) {
        g_autofree char *path = g_strdup_printf("/vk/vertex-write/%s", names[i]);
        g_test_add_data_func(path, GUINT_TO_POINTER(i), test_write);
    }
    return g_test_run();
}
