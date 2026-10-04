/*
 * Production Vulkan retained surface lookup tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/vk/surface.c"

static void test_retained_lookup(void)
{
    g_autofree NV2AState *d = g_new0(NV2AState, 1);
    g_autofree PGRAPHVkState *r = g_new0(PGRAPHVkState, 1);
    uint8_t guest[4] = { 1, 2, 3, 4 };
    static const uint8_t image[4] = { 1, 2, 3, 4 };
    SurfaceBinding target = { 0 };
    SurfaceBinding retained;
    SurfaceBinding producer = { 0 };

    d->pgraph.vk_renderer_state = r;
    d->vram_ptr = guest;
    QTAILQ_INIT(&r->surfaces);
    QTAILQ_INIT(&r->invalid_surfaces);
    target.size = sizeof(guest);
    target.width = 1;
    target.height = 1;
    target.pitch = 4;
    target.fmt.bytes_per_pixel = 4;
    target.host_fmt.vk_format = VK_FORMAT_D24_UNORM_S8_UINT;
    retained = target;
    QTAILQ_INSERT_TAIL(&r->invalid_surfaces, &retained, entry);
    /* Clean eviction has no image-owned reference and cannot skip upload. */
    g_assert_null(get_retained_surface(d, &target));
    retained.retained_guest_bytes = g_memdup2(image, sizeof(image));

    /* A newer guest generation must reload, even with identical layout. */
    guest[2] = 9;
    g_assert_null(get_retained_surface(d, &target));
    g_assert_null(retained.retained_guest_bytes);
    g_assert_false(QTAILQ_EMPTY(&r->invalid_surfaces));

    memcpy(guest, image, sizeof(image));
    retained.retained_guest_bytes = g_memdup2(image, sizeof(image));
    producer.size = sizeof(guest);
    producer.draw_dirty = true;
    QTAILQ_INSERT_TAIL(&r->surfaces, &producer, entry);
    /* Unpublished GPU writes override apparent guest-byte equality. */
    g_assert_null(get_retained_surface(d, &target));
    g_assert_nonnull(retained.retained_guest_bytes);
    QTAILQ_REMOVE(&r->surfaces, &producer, entry);

    target.shape.clip_width = 1;
    g_assert_null(get_retained_surface(d, &target));
    g_assert_nonnull(retained.retained_guest_bytes);
    target.shape.clip_width = 0;
    target.color = true;
    g_assert_null(get_retained_surface(d, &target));
    target.color = false;

    g_assert_true(get_retained_surface(d, &target) == &retained);
    g_assert_null(retained.retained_guest_bytes);
    g_assert_true(QTAILQ_EMPTY(&r->invalid_surfaces));
}

static void test_padded_publication(void)
{
    SurfaceBinding surface = { 0 };
    uint8_t guest[8] = { 0, 0, 7, 8, 0, 0, 9, 10 };
    static const uint8_t owned[8] = { 1, 2, 91, 92, 3, 4, 93, 94 };
    static const uint8_t expected[8] = { 1, 2, 7, 8, 3, 4, 9, 10 };

    surface.width = 2;
    surface.height = 2;
    surface.pitch = 4;
    surface.fmt.bytes_per_pixel = 1;
    publish_retained_surface(&surface, guest, owned);
    g_assert_cmpmem(guest, sizeof(guest), expected, sizeof(expected));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/xbox/vk/retention/production-lookup",
                    test_retained_lookup);
    g_test_add_func("/xbox/vk/retention/padded-publication",
                    test_padded_publication);
    return g_test_run();
}
